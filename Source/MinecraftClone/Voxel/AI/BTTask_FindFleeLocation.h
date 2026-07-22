#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_FindFleeLocation.generated.h"

/**
 * BT Task that finds a location to flee to, away from the threat actor.
 * Uses the mob's FleeDistance to determine how far to run.
 */
UCLASS()
class MINECRAFTCLONE_API UBTTask_FindFleeLocation : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_FindFleeLocation();

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

protected:
	/** Blackboard key for the threat actor */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector ThreatActorKey;

	/** Blackboard key for the flee location (output) */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector FleeLocationKey;
};
