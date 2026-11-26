#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimInstance.h"
#include "ZombieAnimInstance.generated.h"

/**
 * Parent class za ABP_Zombie (Docs/PLAN_Zombie.md). Svaki frame preuzme
 * stanje iz AZombie; AnimGraph ga samo koristi:
 *
 * - WalkAmount: alfa Blend noda Idle -> Walk (Minecraft walkSpeed, 0..1)
 * - WalkCycleTime: Explicit Time Sequence Evaluatora za A_Zombie_Walk
 * - bAggressive: Blend Poses by bool, ruke iz IdleAggressive ili Idle
 * - HeadRotation/HeadAlpha: Transform (Modify) Bone na "head",
 *   Add to Existing, Component Space
 */
UCLASS()
class MINECRAFTCLONE_API UZombieAnimInstance : public UAnimInstance
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly, Category = "Zombie")
	float WalkAmount = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Zombie")
	float WalkCycleTime = 0.0f;

	UPROPERTY(BlueprintReadOnly, Category = "Zombie")
	bool bAggressive = false;

	UPROPERTY(BlueprintReadOnly, Category = "Zombie")
	bool bDead = false;

	/** Rotacija glave u component spaceu (yaw pa pitch, kao Minecraftov ModelPart) */
	UPROPERTY(BlueprintReadOnly, Category = "Zombie")
	FRotator HeadRotation = FRotator::ZeroRotator;

	UPROPERTY(BlueprintReadOnly, Category = "Zombie")
	float HeadAlpha = 0.0f;

protected:
	virtual void NativeUpdateAnimation(float DeltaSeconds) override;
};
