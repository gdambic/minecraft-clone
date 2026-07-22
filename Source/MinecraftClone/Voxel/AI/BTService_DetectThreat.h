#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BTService_DetectThreat.generated.h"

/**
 * BT Service that detects threats (player too close) for passive mobs.
 * Sets ThreatActor when player is within ThreatDetectionRange.
 * Clears ThreatActor when player is beyond FleeDistance.
 */
UCLASS()
class MINECRAFTCLONE_API UBTService_DetectThreat : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_DetectThreat();

	/** Blackboard key for the threat actor */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector ThreatActorKey;

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
};
