#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "EnemyAIController.generated.h"

class AEnemyBase;
class UBehaviorTreeComponent;
class UBlackboardComponent;

/**
 * AI Controller shared by all enemy types.
 * Runs the Behavior Tree specified on the possessed enemy.
 */
UCLASS()
class MINECRAFTCLONE_API AEnemyAIController : public AAIController
{
	GENERATED_BODY()

public:
	AEnemyAIController();

	/** Get the enemy this controller is possessing */
	UFUNCTION(BlueprintCallable, Category = "AI")
	AEnemyBase* GetControlledEnemy() const;

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
