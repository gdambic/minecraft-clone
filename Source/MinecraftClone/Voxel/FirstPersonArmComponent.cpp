#include "FirstPersonArmComponent.h"
#include "FirstPersonCharacter.h"
#include "BlockRegistry.h"
#include "WeaponData.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "MinecraftClone.h"

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

	CreateArmMesh();
	CreateHeldItemMesh();

	// Character moze postaviti item prije nego sto meshevi postoje
	// (SetHeldItem prije Super::BeginPlay) - primijeni to stanje sada
	SetHeldItem(CurrentHeldItem);

	UE_LOG(LogMinecraftClone, Log, TEXT("FirstPersonArmComponent initialized successfully"));
}

void UFirstPersonArmComponent::CreateArmMesh()
{
	if (!OwnerCharacter || !OwnerCamera)
	{
		return;
	}

	// Create arm mesh component
	ArmMesh = NewObject<UStaticMeshComponent>(OwnerCharacter, TEXT("FPArmMesh"));
	if (!ArmMesh)
	{
		return;
	}

	// Load cube mesh for placeholder arm
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh)
	{
		ArmMesh->SetStaticMesh(CubeMesh);
	}

	// Setup attachment and transform
	ArmMesh->SetupAttachment(OwnerCamera);
	ArmMesh->SetRelativeLocation(ArmBaseOffset);
	ArmMesh->SetRelativeRotation(ArmBaseRotation);
	ArmMesh->SetRelativeScale3D(ArmScale);

	// Only owner sees first-person arm
	ArmMesh->SetOnlyOwnerSee(true);
	ArmMesh->SetCastShadow(false);
	ArmMesh->bCastDynamicShadow = false;

	// Set material if available
	if (ArmMaterial)
	{
		ArmMesh->SetMaterial(0, ArmMaterial);
	}

	// Zapamti materijal kocke da se moze vratiti nakon item sprite-a
	DefaultArmMaterial = ArmMesh->GetMaterial(0);

	// Register the component
	ArmMesh->RegisterComponent();
}

void UFirstPersonArmComponent::CreateHeldItemMesh()
{
	if (!OwnerCharacter || !ArmMesh)
	{
		return;
	}

	// Create held item mesh component
	HeldItemMesh = NewObject<UStaticMeshComponent>(OwnerCharacter, TEXT("FPHeldItemMesh"));
	if (!HeldItemMesh)
	{
		return;
	}

	// Load cube mesh for sword placeholder
	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh)
	{
		HeldItemMesh->SetStaticMesh(CubeMesh);
	}

	// Attach to arm mesh
	HeldItemMesh->SetupAttachment(ArmMesh);

	// Position sword relative to arm (extending forward and up from hand)
	HeldItemMesh->SetRelativeLocation(FVector(80.0f, 0.0f, 60.0f));
	HeldItemMesh->SetRelativeRotation(FRotator(45.0f, 0.0f, 0.0f));
	HeldItemMesh->SetRelativeScale3D(FVector(0.4f, 3.0f, 0.4f));  // Sword blade shape

	// Only owner sees
	HeldItemMesh->SetOnlyOwnerSee(true);
	HeldItemMesh->SetCastShadow(false);
	HeldItemMesh->bCastDynamicShadow = false;

	// Start hidden (no item equipped)
	HeldItemMesh->SetVisibility(false);

	// Register the component
	HeldItemMesh->RegisterComponent();
}

void UFirstPersonArmComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!ArmMesh)
	{
		return;
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
	if (!ArmMesh || !HeldItemMesh)
	{
		return;
	}

	// Prazan slot: nista u ruci
	if (ItemType == EItemType::None)
	{
		ArmMesh->SetVisibility(false);
		HeldItemMesh->SetVisibility(false);
		return;
	}

	ArmMesh->SetVisibility(true);

	// Oruzje: postojeca kocka ruke + placeholder mac; pravi prikaz je buduci posao
	if (UWeaponDataLibrary::IsWeapon(ItemType))
	{
		ArmMesh->SetMaterial(0, DefaultArmMaterial);
		ArmMesh->SetRelativeScale3D(ArmScale);
		HeldItemMesh->SetVisibility(true);
		UpdateSwordAppearance(ItemType);
		return;
	}

	// Obican item: kocka u ruci s materijalom bloka - isti MI kao teren,
	// pa izgleda tocno kao blok u svijetu
	HeldItemMesh->SetVisibility(false);

	UMaterialInterface* BlockMaterial = nullptr;
	if (UBlockRegistry* Registry = UBlockRegistry::Get(this))
	{
		BlockMaterial = Registry->GetBlockMaterialForItem(ItemType);
	}

	if (BlockMaterial)
	{
		ArmMesh->SetMaterial(0, BlockMaterial);
		ArmMesh->SetRelativeScale3D(HeldBlockScale);
	}
	else
	{
		// Item bez placeable bloka (alat, hrana...) ili blok bez materijala -
		// siva kocka, ista fallback konvencija kao za blokove u svijetu
		ArmMesh->SetMaterial(0, DefaultArmMaterial);
		ArmMesh->SetRelativeScale3D(ArmScale);
	}
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
