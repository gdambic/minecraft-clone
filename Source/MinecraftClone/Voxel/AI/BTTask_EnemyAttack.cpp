#include "BTTask_EnemyAttack.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "../EnemyBase.h"

UBTTask_EnemyAttack::UBTTask_EnemyAttack()
{
	NodeName = "Enemy Attack";
}

EBTNodeResult::Type UBTTask_EnemyAttack::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController) return EBTNodeResult::Failed;

	AEnemyBase* Enemy = Cast<AEnemyBase>(AIController->GetPawn());
	if (!Enemy) return EBTNodeResult::Failed;

	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard) return EBTNodeResult::Failed;

	AActor* Target = Cast<AActor>(Blackboard->GetValueAsObject(TargetActorKey.SelectedKeyName));
	if (!Target) return EBTNodeResult::Failed;

	// Check distance to target - return Succeeded to stay in Chase loop
	float Distance = FVector::Dist(Enemy->GetActorLocation(), Target->GetActorLocation());
	if (Distance > Enemy->AttackRange)
	{
		return EBTNodeResult::Succeeded;
	}

	// Check cooldown - return Succeeded to stay in Chase loop (don't trigger Wander)
	if (!Enemy->CanAttack())
	{
		return EBTNodeResult::Succeeded;
	}

	// Perform the attack
	Enemy->PerformAttack();

	UE_LOG(LogTemp, Log, TEXT("BTTask_EnemyAttack: Attack performed! Distance: %.0f"), Distance);

	return EBTNodeResult::Succeeded;
}
