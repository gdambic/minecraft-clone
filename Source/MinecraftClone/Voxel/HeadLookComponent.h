#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HeadLookComponent.generated.h"

class USkeletalMeshComponent;

/**
 * Makes the owning creature glance at the player: notice -> track for a
 * while -> cooldown. Computes head yaw/pitch/translation each frame; the
 * owner's AnimBP applies them via a Transform (Modify) Bone node. The
 * component never touches the skeleton itself, so it works on any mob
 * regardless of bone naming.
 */
UCLASS(ClassGroup = (AI), meta = (BlueprintSpawnableComponent))
class MINECRAFTCLONE_API UHeadLookComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHeadLookComponent();

	// ==================== Tuning ====================

	/** Max distance at which the mob notices the player (8 blocks) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeadLook")
	float LookAtRange = 800.0f;

	/** Half-angle of the mob's field of view, degrees from body forward */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeadLook")
	float LookAtFOVHalfAngle = 60.0f;

	/** How long the mob keeps looking at the player (random in range) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeadLook")
	float LookAtDurationMin = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeadLook")
	float LookAtDurationMax = 4.0f;

	/** Pause before the mob will look at the player again (random in range) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeadLook")
	float LookAtCooldownMin = 5.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeadLook")
	float LookAtCooldownMax = 10.0f;

	/** How far the head can turn sideways (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeadLook")
	float MaxHeadYaw = 75.0f;

	/** How far the head can tilt up/down (degrees) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeadLook")
	float MaxHeadPitch = 35.0f;

	/** Interp speed for head rotation (higher = snappier) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeadLook")
	float HeadTurnInterpSpeed = 5.0f;

	/**
	 * Socket/bone on the owner's skeletal mesh used as the trace/aim origin.
	 * If None, falls back to actor center + HeadHeightOffset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeadLook")
	FName HeadSocketName = NAME_None;

	/** Approximate head height above actor center; fallback when HeadSocketName is None */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeadLook")
	float HeadHeightOffset = 40.0f;

	/**
	 * Vector from the head bone's actual pivot to the desired visual pivot
	 * (base of the neck), in bone space. Tweak in editor until the head
	 * appears to rotate from the neck instead of the bone's real pivot.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HeadLook")
	FVector HeadPivotOffset = FVector(20.0f, 0.0f, 10.0f);

	// ==================== Outputs (read by the AnimBP) ====================

	/** Head yaw offset for the AnimBP, degrees relative to body forward */
	UPROPERTY(BlueprintReadOnly, Category = "HeadLook")
	float HeadLookYaw = 0.0f;

	/** Head pitch offset for the AnimBP, degrees relative to body */
	UPROPERTY(BlueprintReadOnly, Category = "HeadLook")
	float HeadLookPitch = 0.0f;

	/** 0..1 blend weight for the head-look pose in the AnimBP */
	UPROPERTY(BlueprintReadOnly, Category = "HeadLook")
	float HeadLookAlpha = 0.0f;

	/**
	 * Bone-space translation that shifts the rotation so it visually pivots
	 * around HeadPivotOffset (t = d - R*d). Feed to the Transform (Modify)
	 * Bone Translation pin (Add to Existing, Bone Space).
	 */
	UPROPERTY(BlueprintReadOnly, Category = "HeadLook")
	FVector HeadLookTranslation = FVector::ZeroVector;

	// ==================== Control ====================

	/**
	 * Pause/resume the behavior (e.g. while fleeing). While suppressed the
	 * head blends back to rest and new tracking never starts.
	 */
	UFUNCTION(BlueprintCallable, Category = "HeadLook")
	void SetSuppressed(bool bInSuppressed) { bSuppressed = bInSuppressed; }

	UFUNCTION(BlueprintCallable, Category = "HeadLook")
	bool IsSuppressed() const { return bSuppressed; }

protected:
	virtual void BeginPlay() override;

public:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Trace/aim origin: head socket if set, otherwise actor center + offset */
	FVector GetHeadLocation() const;

	/** Owner's mesh, cached for socket lookups */
	UPROPERTY()
	USkeletalMeshComponent* OwnerMesh = nullptr;

	/** While true, tracking is disabled and the head returns to rest */
	bool bSuppressed = false;

	/** True while the head is actively tracking the player */
	bool bIsHeadTracking = false;

	/** Seconds left in the current tracking window */
	float LookTimeRemaining = 0.0f;

	/** Seconds until the mob may start tracking again */
	float LookCooldownRemaining = 0.0f;
};
