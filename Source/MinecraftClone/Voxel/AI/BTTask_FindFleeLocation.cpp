#include "BTTask_FindFleeLocation.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "NavigationSystem.h"
#include "../MobBase.h"

UBTTask_FindFleeLocation::UBTTask_FindFleeLocation()
{
	NodeName = "Find Flee Location";
}

EBTNodeResult::Type UBTTask_FindFleeLocation::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController) return EBTNodeResult::Failed;

	AMobBase* Mob = Cast<AMobBase>(AIController->GetPawn());
	if (!Mob) return EBTNodeResult::Failed;

	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard) return EBTNodeResult::Failed;

	// Get threat actor from blackboard
	AActor* ThreatActor = Cast<AActor>(Blackboard->GetValueAsObject(ThreatActorKey.SelectedKeyName));
	if (!ThreatActor)
	{
		UE_LOG(LogTemp, Warning, TEXT("BTTask_FindFleeLocation: No threat actor set"));
		return EBTNodeResult::Failed;
	}

	// Get navigation system
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSys) return EBTNodeResult::Failed;

	// Calculate flee direction (away from threat)
	FVector MobLocation = Mob->GetActorLocation();
	FVector ThreatLocation = ThreatActor->GetActorLocation();
	FVector FleeDirection = (MobLocation - ThreatLocation).GetSafeNormal();

	// Calculate target flee location
	float FleeDistance = Mob->GetFleeDistance();
	FVector TargetLocation = MobLocation + (FleeDirection * FleeDistance);

	// Try to find a valid point on the NavMesh
	FNavLocation NavLocation;
	bool bFound = NavSys->ProjectPointToNavigation(TargetLocation, NavLocation, FVector(FleeDistance, FleeDistance, 500.0f));

	if (!bFound)
	{
		// Try to find any reachable point in the flee direction
		// Use a smaller radius search if direct projection fails
		bFound = NavSys->GetRandomReachablePointInRadius(
			MobLocation + (FleeDirection * FleeDistance * 0.5f),
			FleeDistance * 0.5f,
			NavLocation
		);
	}

	if (bFound)
	{
		Blackboard->SetValueAsVector(FleeLocationKey.SelectedKeyName, NavLocation.Location);
		UE_LOG(LogTemp, Log, TEXT("BTTask_FindFleeLocation: Found flee location at (%.0f, %.0f, %.0f)"),
			NavLocation.Location.X, NavLocation.Location.Y, NavLocation.Location.Z);
		return EBTNodeResult::Succeeded;
	}

	UE_LOG(LogTemp, Warning, TEXT("BTTask_FindFleeLocation: Could not find valid flee location"));
	return EBTNodeResult::Failed;
}
