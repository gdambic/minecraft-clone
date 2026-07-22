#include "MobAIController.h"
#include "MobBase.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"

// Static key names
const FName AMobAIController::ThreatActorKey = FName("ThreatActor");
const FName AMobAIController::FleeLocationKey = FName("FleeLocation");
const FName AMobAIController::WanderLocationKey = FName("WanderLocation");

AMobAIController::AMobAIController()
{
	// Create Behavior Tree and Blackboard components
	BehaviorTreeComponent = CreateDefaultSubobject<UBehaviorTreeComponent>(TEXT("BehaviorTreeComponent"));
	BlackboardComponent = CreateDefaultSubobject<UBlackboardComponent>(TEXT("BlackboardComponent"));

	// Use blackboard from this controller
	Blackboard = BlackboardComponent;
}

void AMobAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	AMobBase* Mob = Cast<AMobBase>(InPawn);
	if (!Mob)
	{
		UE_LOG(LogTemp, Error, TEXT("MobAIController: Possessed pawn is not an AMobBase!"));
		return;
	}

	// Get behavior tree from mob
	UBehaviorTree* BehaviorTree = Mob->GetBehaviorTree();
	if (!BehaviorTree)
	{
		UE_LOG(LogTemp, Warning, TEXT("MobAIController: No BehaviorTree set on %s"), *Mob->GetName());
		return;
	}

	// Initialize blackboard
	if (BehaviorTree->BlackboardAsset)
	{
		BlackboardComponent->InitializeBlackboard(*BehaviorTree->BlackboardAsset);
		UE_LOG(LogTemp, Log, TEXT("MobAIController: Blackboard initialized for %s"), *Mob->GetName());
	}

	// Start behavior tree
	BehaviorTreeComponent->StartTree(*BehaviorTree);
	UE_LOG(LogTemp, Log, TEXT("MobAIController: Started BehaviorTree for %s"), *Mob->GetName());
}

void AMobAIController::OnUnPossess()
{
	// Stop behavior tree
	if (BehaviorTreeComponent)
	{
		BehaviorTreeComponent->StopTree();
	}

	Super::OnUnPossess();
}

AMobBase* AMobAIController::GetControlledMob() const
{
	return Cast<AMobBase>(GetPawn());
}

void AMobAIController::SetThreatActor(AActor* Threat)
{
	if (BlackboardComponent)
	{
		BlackboardComponent->SetValueAsObject(ThreatActorKey, Threat);

		// Also update the mob's internal state
		if (AMobBase* Mob = GetControlledMob())
		{
			Mob->SetCurrentThreat(Threat);
		}
	}
}

void AMobAIController::ClearThreatActor()
{
	if (BlackboardComponent)
	{
		BlackboardComponent->ClearValue(ThreatActorKey);

		// Also clear the mob's internal state
		if (AMobBase* Mob = GetControlledMob())
		{
			Mob->ClearCurrentThreat();
		}
	}
}
