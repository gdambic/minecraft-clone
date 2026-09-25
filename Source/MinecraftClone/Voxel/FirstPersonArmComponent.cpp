#include "FirstPersonArmComponent.h"
#include "FirstPersonCharacter.h"
#include "BlockRegistry.h"
#include "ItemMeshExtruder.h"
#include "WeaponData.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "MinecraftClone.h"

namespace
{
	const FName GSpriteTextureParam(TEXT("SpriteTexture"));
}

UFirstPersonArmComponent::UFirstPersonArmComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UFirstPersonArmComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<AFirstPersonCharacter>(GetOwner());
	if (!OwnerCharacter)
	{
		UE_LOG(LogMinecraftClone, Error, TEXT("FirstPersonArmComponent must be attached to AFirstPersonCharacter!"));
		return;
	}

	OwnerCamera = OwnerCharacter->GetFirstPersonCameraComponent();
	if (!OwnerCamera)
	{
		UE_LOG(LogMinecraftClone, Error, TEXT("FirstPersonArmComponent: No camera found on owner!"));
		return;
	}

	// Native mesh komponente zivi na characteru (prezive PIE izmjene svojstava)
	ArmMesh = OwnerCharacter->GetArmMeshComponent();
	HeldItemMesh = OwnerCharacter->GetHeldItemMeshComponent();
	HeldSpriteMesh = OwnerCharacter->GetHeldSpriteMeshComponent();
	if (!ArmMesh || !HeldItemMesh || !HeldSpriteMesh)
	{
		UE_LOG(LogMinecraftClone, Error, TEXT("FirstPersonArmComponent: character nema mesh komponente ruke!"));
		return;
	}

	// BP hijerarhija zna premjestiti naslijedene komponente (dijagnostika je
	// pokazala FPArmMesh na kapsuli umjesto na kameri - zato je item u ruci
	// pratio tijelo, a ne pogled). Eksplicitni runtime attach na kameru
	// garantira da ruka prati i pitch i yaw pogleda.
	ArmMesh->AttachToComponent(OwnerCamera, FAttachmentTransformRules::KeepRelativeTransform);
	HeldItemMesh->AttachToComponent(ArmMesh, FAttachmentTransformRules::KeepRelativeTransform);
	HeldSpriteMesh->AttachToComponent(ArmMesh, FAttachmentTransformRules::KeepRelativeTransform);
	UE_LOG(LogMinecraftClone, Log, TEXT("FirstPersonArmComponent: ruka attachana na %s"),
		*GetNameSafe(ArmMesh->GetAttachParent()));

	SetupArmMesh();
	SetupHeldItemMesh();
	SetupHeldSpriteMesh();

	// Character moze postaviti item prije nego sto su meshevi konfigurirani
	// (SetHeldItem prije Super::BeginPlay) - primijeni to stanje sada
	SetHeldItem(CurrentHeldItem);

	UE_LOG(LogMinecraftClone, Log, TEXT("FirstPersonArmComponent initialized successfully"));
}

void UFirstPersonArmComponent::SetupArmMesh()
{
	// Load cube mesh for placeholder arm
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh)
	{
		ArmMesh->SetStaticMesh(CubeMesh);
	}

	ArmMesh->SetRelativeLocation(ArmBaseOffset);
	ArmMesh->SetRelativeRotation(ArmBaseRotation);
	ArmMesh->SetRelativeScale3D(ArmScale);

	// Only owner sees first-person arm
	ArmMesh->SetOnlyOwnerSee(true);
	ArmMesh->SetCastShadow(false);
	ArmMesh->bCastDynamicShadow = false;
	ArmMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Set material if available
	if (ArmMaterial)
	{
		ArmMesh->SetMaterial(0, ArmMaterial);
	}

	// Zapamti materijal kocke da se moze vratiti nakon item sprite-a
	DefaultArmMaterial = ArmMesh->GetMaterial(0);
}

void UFirstPersonArmComponent::SetupHeldItemMesh()
{
	// Load cube mesh for sword placeholder
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh)
	{
		HeldItemMesh->SetStaticMesh(CubeMesh);
	}

	// Position sword relative to arm (extending forward and up from hand)
	HeldItemMesh->SetRelativeLocation(FVector(80.0f, 0.0f, 60.0f));
	HeldItemMesh->SetRelativeRotation(FRotator(45.0f, 0.0f, 0.0f));
	HeldItemMesh->SetRelativeScale3D(FVector(0.4f, 3.0f, 0.4f));  // Sword blade shape

	// Only owner sees
	HeldItemMesh->SetOnlyOwnerSee(true);
	HeldItemMesh->SetCastShadow(false);
	HeldItemMesh->bCastDynamicShadow = false;
	HeldItemMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Start hidden (no item equipped)
	HeldItemMesh->SetVisibility(false);
}

void UFirstPersonArmComponent::SetupHeldSpriteMesh()
{
	if (!HeldSpriteMesh)
	{
		return;
	}

	// Apsolutna skala jer bi neuniformna skala ruke izoblicila kvadratni sprite
	HeldSpriteMesh->SetUsingAbsoluteScale(true);

	HeldSpriteMesh->SetOnlyOwnerSee(true);
	HeldSpriteMesh->SetCastShadow(false);
	HeldSpriteMesh->bCastDynamicShadow = false;
	HeldSpriteMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	HeldSpriteMesh->SetVisibility(false);

	// Geometrija se gradi lijeno u BuildHeldSpriteMesh kad se item uzme u ruku
	HeldSpriteMesh->ClearAllMeshSections();
	BuiltSpriteMeshItem = EItemType::None;

	if (UBlockRegistry* Registry = UBlockRegistry::Get(this))
	{
		if (UMaterialInterface* SpriteMaterial = Registry->GetItemSpriteMaterial())
		{
			HeldSpriteMID = UMaterialInstanceDynamic::Create(SpriteMaterial, this);
		}
	}
}

void UFirstPersonArmComponent::BuildHeldSpriteMesh(EItemType ItemType, UTexture2D* SpriteTexture)
{
	if (!HeldSpriteMesh || !HeldSpriteMID)
	{
		return;
	}

	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	const FItemExtrudedMeshData* MeshData = Registry ? Registry->GetItemExtrudedMesh(ItemType) : nullptr;

	// Ekstruzija nije uspjela (kriv format teksture...): flat quad ploca u
	// istoj orijentaciji - Masked materijal i dalje reze prozirne piksele
	FItemExtrudedMeshData FlatQuad;
	if (!MeshData)
	{
		FlatQuad = FItemMeshExtruder::BuildFlatQuad();
		MeshData = &FlatQuad;
	}

	HeldSpriteMesh->ClearAllMeshSections();
	HeldSpriteMesh->CreateMeshSection(0, MeshData->Vertices, MeshData->Triangles,
		MeshData->Normals, MeshData->UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	HeldSpriteMID->SetTextureParameterValue(GSpriteTextureParam, SpriteTexture);
	HeldSpriteMesh->SetMaterial(0, HeldSpriteMID);
	BuiltSpriteMeshItem = ItemType;
}

void UFirstPersonArmComponent::ApplyHeldSpriteTransform()
{
	if (!HeldSpriteMesh || !ArmMesh)
	{
		return;
	}

	// Relativna lokacija se mnozi skalom roditelja - podijeli da offset ostane
	// u stvarnim Unreal jedinicama bez obzira na trenutnu skalu ruke
	const FVector ParentScale = ArmMesh->GetRelativeScale3D();
	FVector RelativeLocation = HeldSpriteOffset;
	RelativeLocation.X /= FMath::Max(ParentScale.X, KINDA_SMALL_NUMBER);
	RelativeLocation.Y /= FMath::Max(ParentScale.Y, KINDA_SMALL_NUMBER);
	RelativeLocation.Z /= FMath::Max(ParentScale.Z, KINDA_SMALL_NUMBER);

	HeldSpriteMesh->SetRelativeLocation(RelativeLocation);
	HeldSpriteMesh->SetRelativeRotation(HeldSpriteRotation);
	HeldSpriteMesh->SetWorldScale3D(HeldSpriteScale);
}

void UFirstPersonArmComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// Sve komponente moraju postojati - nakon hot-reloada koji mijenja tip
	// native komponente BP instanca zna imati null pokazivac (BeginPlay je
	// vec logirao Error; potreban je restart editora)
	if (!OwnerCharacter || !OwnerCamera || !ArmMesh || !HeldItemMesh || !HeldSpriteMesh)
	{
		return;
	}

	// Cuvar hijerarhije: BP construction rerun (npr. izmjena svojstva u
	// Details tijekom PIE) re-aplicira BP-ovu hijerarhiju i premjesti ruku
	// s kamere - vrati je i ponovno konfiguriraj
	if (ArmMesh->GetAttachParent() != OwnerCamera)
	{
		UE_LOG(LogMinecraftClone, Warning,
			TEXT("FirstPersonArmComponent: ruka premjestena na %s - vracam na kameru"),
			*GetNameSafe(ArmMesh->GetAttachParent()));
		ArmMesh->AttachToComponent(OwnerCamera, FAttachmentTransformRules::KeepRelativeTransform);
		HeldItemMesh->AttachToComponent(ArmMesh, FAttachmentTransformRules::KeepRelativeTransform);
		HeldSpriteMesh->AttachToComponent(ArmMesh, FAttachmentTransformRules::KeepRelativeTransform);
		SetupArmMesh();
		SetupHeldItemMesh();
		SetupHeldSpriteMesh();
	}

	// Update swing animation
	if (bIsSwinging)
	{
		UpdateSwing(DeltaTime);
	}
	// Update bobbing only when not swinging
	else if (bEnableBobbing)
	{
		UpdateBobbing(DeltaTime);
	}

	// Idempotentna primjena stanja ruke svaki frame: EditAnywhere parametri
	// (HeldSpriteOffset/Rotation/Scale, HeldBlockScale...) su zivi u
	// runtimeu, a setteri na iste vrijednosti su jeftini early-outi
	SetHeldItem(CurrentHeldItem);
}

void UFirstPersonArmComponent::PlaySwingAnimation(float Duration)
{
	if (bIsSwinging)
	{
		return;  // Already swinging
	}

	bIsSwinging = true;
	SwingProgress = 0.0f;
	SwingDuration = FMath::Max(Duration, 0.1f);

	PlaySwingSound();
}

void UFirstPersonArmComponent::UpdateSwing(float DeltaTime)
{
	SwingProgress += DeltaTime / SwingDuration;

	if (SwingProgress >= 1.0f)
	{
		// Animation complete - return to base position
		bIsSwinging = false;
		SwingProgress = 0.0f;
		ArmMesh->SetRelativeLocation(ArmBaseOffset);
		ArmMesh->SetRelativeRotation(ArmBaseRotation);
		return;
	}

	// Swing phases:
	// 0.0 - 0.3: Wind up (slight back movement)
	// 0.3 - 0.6: Swing down (main attack motion)
	// 0.6 - 1.0: Recovery (return to idle)

	float SwingAngle = 0.0f;
	float ForwardOffset = 0.0f;

	if (SwingProgress < 0.3f)
	{
		// Wind up phase - pull back slightly
		float Phase = SwingProgress / 0.3f;
		float EasedPhase = FMath::InterpEaseOut(0.0f, 1.0f, Phase, 2.0f);
		SwingAngle = -15.0f * EasedPhase;  // Negative = pull back
		ForwardOffset = -5.0f * EasedPhase;
	}
	else if (SwingProgress < 0.6f)
	{
		// Main swing phase - swing down
		float Phase = (SwingProgress - 0.3f) / 0.3f;
		float EasedPhase = FMath::InterpEaseIn(0.0f, 1.0f, Phase, 2.0f);
		SwingAngle = FMath::Lerp(-15.0f, SwingArcAngle, EasedPhase);
		ForwardOffset = FMath::Lerp(-5.0f, SwingForwardDistance, EasedPhase);
	}
	else
	{
		// Recovery phase - return to idle
		float Phase = (SwingProgress - 0.6f) / 0.4f;
		float EasedPhase = FMath::InterpEaseOut(0.0f, 1.0f, Phase, 2.0f);
		SwingAngle = FMath::Lerp(SwingArcAngle, 0.0f, EasedPhase);
		ForwardOffset = FMath::Lerp(SwingForwardDistance, 0.0f, EasedPhase);
	}

	// Apply rotation (pitch = swing down/up)
	FRotator NewRotation = ArmBaseRotation;
	NewRotation.Pitch -= SwingAngle;

	// Add slight roll for more natural motion
	NewRotation.Roll += SwingAngle * 0.2f;

	// Apply position offset (forward movement during swing)
	FVector NewLocation = ArmBaseOffset;
	NewLocation.X += ForwardOffset;

	ArmMesh->SetRelativeRotation(NewRotation);
	ArmMesh->SetRelativeLocation(NewLocation);
}

void UFirstPersonArmComponent::UpdateBobbing(float DeltaTime)
{
	if (!OwnerCharacter)
	{
		return;
	}

	// Get character velocity
	FVector Velocity = OwnerCharacter->GetVelocity();
	float Speed = Velocity.Size2D();  // Horizontal speed only

	const float MovementThreshold = 10.0f;

	if (Speed > MovementThreshold)
	{
		// Update bob time based on speed
		float SpeedFactor = FMath::Clamp(Speed / 400.0f, 0.5f, 1.5f);
		BobTime += DeltaTime * BobSpeed * SpeedFactor;

		FVector BobOffset = CalculateBobOffset();
		FVector NewLocation = ArmBaseOffset + BobOffset;
		ArmMesh->SetRelativeLocation(NewLocation);
	}
	else
	{
		// Smoothly return to base position when stationary
		FVector CurrentLoc = ArmMesh->GetRelativeLocation();
		FVector NewLoc = FMath::VInterpTo(CurrentLoc, ArmBaseOffset, DeltaTime, 8.0f);
		ArmMesh->SetRelativeLocation(NewLoc);

		// Slowly reset bob time
		BobTime = FMath::FInterpTo(BobTime, 0.0f, DeltaTime, 2.0f);
	}
}

FVector UFirstPersonArmComponent::CalculateBobOffset() const
{
	// Minecraft-style bob pattern:
	// - Vertical bob (up/down with each step)
	// - Slight horizontal sway
	// - Uses sine waves for smooth motion

	// Vertical bob - more pronounced
	float BobZ = FMath::Abs(FMath::Sin(BobTime)) * BobAmplitude;

	// Horizontal sway - subtle side-to-side
	float BobY = FMath::Sin(BobTime * 0.5f) * BobAmplitude * 0.4f;

	// Forward/back - very subtle
	float BobX = FMath::Cos(BobTime) * BobAmplitude * 0.2f;

	return FVector(BobX, BobY, BobZ);
}

void UFirstPersonArmComponent::PlaySwingSound()
{
	if (SwingSound && OwnerCharacter)
	{
		UGameplayStatics::PlaySound2D(GetWorld(), SwingSound, 0.8f, 1.0f);
	}
}

void UFirstPersonArmComponent::PlayHitSound()
{
	if (HitSound && OwnerCharacter)
	{
		UGameplayStatics::PlaySound2D(GetWorld(), HitSound, 1.0f, 1.0f);
	}
}

void UFirstPersonArmComponent::SetHeldItem(EItemType ItemType)
{
	CurrentHeldItem = ItemType;

	// Meshevi jos ne postoje (poziv prije BeginPlay komponente) - stanje je
	// zapamceno u CurrentHeldItem, BeginPlay ce ga primijeniti
	if (!ArmMesh || !HeldItemMesh || !HeldSpriteMesh)
	{
		return;
	}

	// Prazan slot: nista u ruci
	if (ItemType == EItemType::None)
	{
		ArmMesh->SetVisibility(false);
		HeldItemMesh->SetVisibility(false);
		HeldSpriteMesh->SetVisibility(false);
		return;
	}

	UBlockRegistry* Registry = UBlockRegistry::Get(this);

	// 1) Placeable blok: kocka s materijalom terena - izgleda tocno kao blok u svijetu
	UMaterialInterface* BlockMaterial = Registry ? Registry->GetBlockMaterialForItem(ItemType) : nullptr;
	if (BlockMaterial)
	{
		ArmMesh->SetVisibility(true);
		ArmMesh->SetMaterial(0, BlockMaterial);
		ArmMesh->SetRelativeScale3D(HeldBlockScale);
		HeldItemMesh->SetVisibility(false);
		HeldSpriteMesh->SetVisibility(false);
		return;
	}

	// 2) Item sa "sprite" prikazom (mac, alat...): ekstrudirani 3D mesh iz
	// teksture (Minecraft stil), flat quad ako ekstruzija nije uspjela
	UTexture2D* SpriteTexture = Registry ? Registry->GetItemIconTexture(ItemType) : nullptr;
	if (SpriteTexture && HeldSpriteMID)
	{
		ArmMesh->SetVisibility(false);
		ArmMesh->SetRelativeScale3D(ArmScale);
		HeldItemMesh->SetVisibility(false);
		if (BuiltSpriteMeshItem != ItemType)
		{
			BuildHeldSpriteMesh(ItemType, SpriteTexture);
		}
		ApplyHeldSpriteTransform();
		HeldSpriteMesh->SetVisibility(true);
		return;
	}

	HeldSpriteMesh->SetVisibility(false);
	ArmMesh->SetVisibility(true);
	ArmMesh->SetMaterial(0, DefaultArmMaterial);
	ArmMesh->SetRelativeScale3D(ArmScale);

	// 3) Oruzje bez vlastitog sprite-a: stari placeholder mac
	if (UWeaponDataLibrary::IsWeapon(ItemType))
	{
		HeldItemMesh->SetVisibility(true);
		UpdateSwordAppearance(ItemType);
		return;
	}

	// 4) Fallback: siva kocka (item bez ikakvog prikaza)
	HeldItemMesh->SetVisibility(false);
}

void UFirstPersonArmComponent::ClearHeldItem()
{
	SetHeldItem(EItemType::None);
}

void UFirstPersonArmComponent::UpdateSwordAppearance(EItemType SwordType)
{
	if (!HeldItemMesh)
	{
		return;
	}

	UMaterialInterface* SwordMaterial = nullptr;

	switch (SwordType)
	{
	case EItemType::WoodenSword:
		SwordMaterial = WoodenSwordMaterial;
		break;
	case EItemType::StoneSword:
		SwordMaterial = StoneSwordMaterial;
		break;
	case EItemType::IronSword:
		SwordMaterial = IronSwordMaterial;
		break;
	case EItemType::DiamondSword:
		SwordMaterial = DiamondSwordMaterial;
		break;
	default:
		break;
	}

	if (SwordMaterial)
	{
		HeldItemMesh->SetMaterial(0, SwordMaterial);
	}
}
