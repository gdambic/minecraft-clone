#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_RotateToTarget.generated.h"

/**
 * BT Task that smoothly rotates the enemy to face its target.
 * Returns Succeeded when facing the target within AcceptableAngle.
 */
UCLASS()
class MINECRAFTCLONE_API UBTTask_RotateToTarget : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_RotateToTarget();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

protected:
	/** Blackboard key for the target actor */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetActorKey;

	/** Rotation speed in degrees per second */
	UPROPERTY(EditAnywhere, Category = "Rotation")
	float RotationSpeed = 360.0f;

	/** Angle tolerance in degrees - task succeeds when within this angle */
	UPROPERTY(EditAnywhere, Category = "Rotation")
	float AcceptableAngle = 5.0f;
};
