#include "BTTask_FindWanderLocation.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "NavigationSystem.h"
#include "../EnemyBase.h"

UBTTask_FindWanderLocation::UBTTask_FindWanderLocation()
{
	NodeName = "Find Wander Location";
}

EBTNodeResult::Type UBTTask_FindWanderLocation::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController) return EBTNodeResult::Failed;

	AEnemyBase* Enemy = Cast<AEnemyBase>(AIController->GetPawn());
	if (!Enemy) return EBTNodeResult::Failed;

	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard) return EBTNodeResult::Failed;

	// Get navigation system
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSys) return EBTNodeResult::Failed;

	// Get random point within wander radius
	FVector Origin = Enemy->GetActorLocation();
	FNavLocation RandomLocation;

	bool bFound = NavSys->GetRandomReachablePointInRadius(Origin, Enemy->WanderRadius, RandomLocation);

	if (bFound)
	{
		Blackboard->SetValueAsVector(WanderLocationKey.SelectedKeyName, RandomLocation.Location);

		UE_LOG(LogTemp, Log, TEXT("BTTask_FindWanderLocation: Found location at (%.0f, %.0f, %.0f)"),
			RandomLocation.Location.X, RandomLocation.Location.Y, RandomLocation.Location.Z);

		return EBTNodeResult::Succeeded;
	}

	UE_LOG(LogTemp, Warning, TEXT("BTTask_FindWanderLocation: Could not find valid wander location"));
	return EBTNodeResult::Failed;
}
