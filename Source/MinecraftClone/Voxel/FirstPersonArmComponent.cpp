#include "FirstPersonArmComponent.h"
#include "FirstPersonCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMesh.h"
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

	if (!HeldItemMesh)
	{
		return;
	}

	// Check if this is a weapon
	bool bIsWeapon = (ItemType == EItemType::WoodenSword ||
					  ItemType == EItemType::StoneSword ||
					  ItemType == EItemType::IronSword ||
					  ItemType == EItemType::DiamondSword);

	HeldItemMesh->SetVisibility(bIsWeapon);

	if (bIsWeapon)
	{
		UpdateSwordAppearance(ItemType);
	}
}

void UFirstPersonArmComponent::ClearHeldItem()
{
	CurrentHeldItem = EItemType::None;

	if (HeldItemMesh)
	{
		HeldItemMesh->SetVisibility(false);
	}
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
