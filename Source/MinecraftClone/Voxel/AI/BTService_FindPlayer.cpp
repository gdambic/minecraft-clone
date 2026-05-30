#include "BTService_FindPlayer.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/Character.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "../EnemyBase.h"

UBTService_FindPlayer::UBTService_FindPlayer()
{
	NodeName = "Find Player";

	// How often to tick (check for player)
	Interval = 0.5f;
	RandomDeviation = 0.1f;
}

void UBTService_FindPlayer::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* Blackboard = OwnerComp.GetBlackboardComponent();
	if (!Blackboard) return;

	AAIController* AIController = OwnerComp.GetAIOwner();
	if (!AIController) return;

	AEnemyBase* Enemy = Cast<AEnemyBase>(AIController->GetPawn());
	if (!Enemy)
	{
		UE_LOG(LogTemp, Error, TEXT("BTService_FindPlayer: Pawn is not AEnemyBase! Pawn class: %s"),
			AIController->GetPawn() ? *AIController->GetPawn()->GetClass()->GetName() : TEXT("NULL"));
		return;
	}

	ACharacter* Player = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
	if (!Player)
	{
		Blackboard->ClearValue(TargetActorKey.SelectedKeyName);
		return;
	}

	float Distance = FVector::Dist(Enemy->GetActorLocation(), Player->GetActorLocation());
	UObject* CurrentTarget = Blackboard->GetValueAsObject(TargetActorKey.SelectedKeyName);

	if (CurrentTarget)
	{
		// Already tracking - lose target if too far
		if (Distance > Enemy->LoseTargetRange)
		{
			Blackboard->ClearValue(TargetActorKey.SelectedKeyName);
			UE_LOG(LogTemp, Log, TEXT("BTService_FindPlayer: Player escaped! Distance: %.0f > LoseTargetRange: %.0f"),
				Distance, Enemy->LoseTargetRange);
		}
	}
	else
	{
		// Not tracking - detect if close enough
		if (Distance <= Enemy->DetectionRange)
		{
			Blackboard->SetValueAsObject(TargetActorKey.SelectedKeyName, Player);
			UE_LOG(LogTemp, Log, TEXT("BTService_FindPlayer: Player detected! Distance: %.0f <= DetectionRange: %.0f"),
				Distance, Enemy->DetectionRange);
		}
	}
}
