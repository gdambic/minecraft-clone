#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "MobAIController.generated.h"

class AMobBase;
class UBehaviorTreeComponent;
class UBlackboardComponent;

/**
 * AI Controller for passive mobs (sheep, cows, etc.).
 * Runs the Behavior Tree specified on the possessed mob.
 */
UCLASS()
class MINECRAFTCLONE_API AMobAIController : public AAIController
{
	GENERATED_BODY()

public:
	AMobAIController();

	/** Get the mob this controller is possessing */
	UFUNCTION(BlueprintCallable, Category = "AI")
	AMobBase* GetControlledMob() const;

	/** Set the threat actor in the blackboard */
	UFUNCTION(BlueprintCallable, Category = "AI")
	void SetThreatActor(AActor* Threat);

	/** Clear the threat actor from the blackboard */
	UFUNCTION(BlueprintCallable, Category = "AI")
	void ClearThreatActor();

	/** Blackboard key names */
	static const FName ThreatActorKey;
	static const FName FleeLocationKey;
	static const FName WanderLocationKey;

protected:
	virtual void OnPossess(APawn* InPawn) override;
	virtual void OnUnPossess() override;

private:
	/** Behavior Tree component */
	UPROPERTY()
	UBehaviorTreeComponent* BehaviorTreeComponent;

	/** Blackboard component */
	UPROPERTY()
	UBlackboardComponent* BlackboardComponent;
};
