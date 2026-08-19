#include "HeadLookComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"

UHeadLookComponent::UHeadLookComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UHeadLookComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerMesh = GetOwner() ? GetOwner()->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
}

FVector UHeadLookComponent::GetHeadLocation() const
{
	if (OwnerMesh && HeadSocketName != NAME_None && OwnerMesh->DoesSocketExist(HeadSocketName))
	{
		return OwnerMesh->GetSocketLocation(HeadSocketName);
	}

	return GetOwner()->GetActorLocation() + FVector(0.0f, 0.0f, HeadHeightOffset);
}

void UHeadLookComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (LookCooldownRemaining > 0.0f)
	{
		LookCooldownRemaining -= DeltaTime;
	}

	// Target pose for this frame; stays at rest unless tracking is active
	float TargetYaw = 0.0f;
	float TargetPitch = 0.0f;
	float TargetAlpha = 0.0f;

	AActor* Owner = GetOwner();
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);

	if (Owner && Player)
	{
		const FVector HeadLocation = GetHeadLocation();
		const FVector PlayerEyes = Player->GetPawnViewLocation();
		const FVector ToPlayer = PlayerEyes - HeadLocation;

		// Direction to player relative to the owner's body
		const FRotator DeltaRot = (ToPlayer.Rotation() - Owner->GetActorRotation()).GetNormalized();

		const bool bInRange = ToPlayer.Size() <= LookAtRange;
		const bool bInFOV = FMath::Abs(DeltaRot.Yaw) <= LookAtFOVHalfAngle;

		if (bIsHeadTracking)
		{
			LookTimeRemaining -= DeltaTime;

			if (LookTimeRemaining <= 0.0f || !bInRange || bSuppressed)
			{
				bIsHeadTracking = false;
				LookCooldownRemaining = FMath::RandRange(LookAtCooldownMin, LookAtCooldownMax);
			}
		}
		else if (bInRange && bInFOV && !bSuppressed && LookCooldownRemaining <= 0.0f)
		{
			// Don't stare through walls
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(HeadLook), false, Owner);
			const bool bBlocked = GetWorld()->LineTraceSingleByChannel(
				Hit, HeadLocation, PlayerEyes, ECC_Visibility, Params);

			if (!bBlocked || Hit.GetActor() == Player)
			{
				bIsHeadTracking = true;
				LookTimeRemaining = FMath::RandRange(LookAtDurationMin, LookAtDurationMax);
			}
		}

		if (bIsHeadTracking)
		{
			TargetYaw = FMath::ClampAngle(DeltaRot.Yaw, -MaxHeadYaw, MaxHeadYaw);
			TargetPitch = FMath::ClampAngle(DeltaRot.Pitch, -MaxHeadPitch, MaxHeadPitch);
			TargetAlpha = 1.0f;
		}
	}
	else
	{
		bIsHeadTracking = false;
	}

	// Smooth here so the AnimBP can use the values directly
	HeadLookYaw = FMath::FInterpTo(HeadLookYaw, TargetYaw, DeltaTime, HeadTurnInterpSpeed);
	HeadLookPitch = FMath::FInterpTo(HeadLookPitch, TargetPitch, DeltaTime, HeadTurnInterpSpeed);
	HeadLookAlpha = FMath::FInterpTo(HeadLookAlpha, TargetAlpha, DeltaTime, HeadTurnInterpSpeed);

	// Rotating about the neck instead of the bone's real pivot equals
	// rotating about the pivot plus this correction: t = d - R*d
	const FRotator BoneRotation(HeadLookPitch, HeadLookYaw, 0.0f);
	HeadLookTranslation = HeadPivotOffset - BoneRotation.RotateVector(HeadPivotOffset);
}
