#include "BTService_DetectThreat.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "../MobBase.h"

UBTService_DetectThreat::UBTService_DetectThreat()
{
	NodeName = "Detect Threat";

	// Check frequently for responsive flee behavior
	Interval = 0.25f;
	RandomDeviation = 0.05f;
}

void UBTService_DetectThreat::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard) return;

	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController) return;

	AMobBase* Mob = Cast<AMobBase>(AIController->GetPawn());
	if (!Mob)
	{
		UE_LOG(LogTemp, Error, TEXT("BTService_DetectThreat: Pawn is not AMobBase!"));
		return;
	}

	// Check if mob has damage-based threat (from OnDamageTaken)
	AActor* DamageThreat = Mob->GetCurrentThreat();
	if (DamageThreat)
	{
		// Mob was attacked - set threat in blackboard
		Blackboard->SetValueAsObject(ThreatActorKey.SelectedKeyName, DamageThreat);
		UE_LOG(LogTemp, Log, TEXT("BTService_DetectThreat: Threat from damage: %s"), *DamageThreat->GetName());
	}

	// Get player for proximity check
	ACharacter* Player = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
	if (!Player) return;

	float Distance = FVector::Dist(Mob->GetActorLocation(), Player->GetActorLocation());
	UObject* CurrentThreat = Blackboard->GetValueAsObject(ThreatActorKey.SelectedKeyName);

	if (CurrentThreat)
	{
		// Currently fleeing - clear threat if we've escaped far enough
		if (Distance > Mob->GetFleeDistance())
		{
			Blackboard->ClearValue(ThreatActorKey.SelectedKeyName);
			Mob->ClearCurrentThreat();
			UE_LOG(LogTemp, Log, TEXT("BTService_DetectThreat: Escaped threat! Distance: %.0f > FleeDistance: %.0f"),
				Distance, Mob->GetFleeDistance());
		}
	}
	else
	{
		// Not fleeing - detect if player is too close
		if (Distance <= Mob->GetThreatDetectionRange())
		{
			Blackboard->SetValueAsObject(ThreatActorKey.SelectedKeyName, Player);
			Mob->SetCurrentThreat(Player);
			UE_LOG(LogTemp, Log, TEXT("BTService_DetectThreat: Player too close! Distance: %.0f <= ThreatDetectionRange: %.0f"),
				Distance, Mob->GetThreatDetectionRange());
		}
	}
}
