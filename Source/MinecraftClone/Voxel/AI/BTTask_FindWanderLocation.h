#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_FindWanderLocation.generated.h"

/**
 * BT Task that finds a random wander location within the enemy's wander radius.
 * The location is validated against the NavMesh.
 */
UCLASS()
class MINECRAFTCLONE_API UBTTask_FindWanderLocation : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_FindWanderLocation();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

protected:
	/** Blackboard key for the wander location (Vector) */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector WanderLocationKey;
};
