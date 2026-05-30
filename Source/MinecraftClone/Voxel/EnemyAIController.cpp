#include "EnemyAIController.h"
#include "EnemyBase.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "TimerManager.h"
#include "GameFramework/CharacterMovementComponent.h"

AEnemyAIController::AEnemyAIController()
{
	// Create Behavior Tree and Blackboard components
	BehaviorTreeComponent = CreateDefaultSubobject<UBehaviorTreeComponent>(TEXT("BehaviorTreeComponent"));
	BlackboardComponent = CreateDefaultSubobject<UBlackboardComponent>(TEXT("BlackboardComponent"));

	// Use blackboard from this controller
	Blackboard = BlackboardComponent;
}

void AEnemyAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	AEnemyBase* Enemy = Cast<AEnemyBase>(InPawn);
	if (!Enemy)
	{
		UE_LOG(LogTemp, Error, TEXT("EnemyAIController: Possessed pawn is not an AEnemyBase!"));
		return;
	}

	// Get behavior tree from enemy
	UBehaviorTree* BehaviorTree = Enemy->GetBehaviorTree();
	if (!BehaviorTree)
	{
		UE_LOG(LogTemp, Warning, TEXT("EnemyAIController: No BehaviorTree set on %s"), *Enemy->GetName());
		return;
	}

	// Initialize blackboard
	if (BehaviorTree->BlackboardAsset)
	{
		BlackboardComponent->InitializeBlackboard(*BehaviorTree->BlackboardAsset);
		UE_LOG(LogTemp, Log, TEXT("EnemyAIController: Blackboard initialized for %s"), *Enemy->GetName());
	}

	// Start behavior tree
	BehaviorTreeComponent->StartTree(*BehaviorTree);
	UE_LOG(LogTemp, Log, TEXT("EnemyAIController: Started BehaviorTree for %s"), *Enemy->GetName());

	// DEBUG: Test direct MoveTo after 2 seconds
	FTimerHandle DebugTimer;
	GetWorld()->GetTimerManager().SetTimer(DebugTimer, [this]()
	{
		APawn* MyPawn = GetPawn();
		ACharacter* Player = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);

		if (!MyPawn || !Player)
		{
			UE_LOG(LogTemp, Error, TEXT("DEBUG: Pawn or Player is null"));
			return;
		}

		UE_LOG(LogTemp, Warning, TEXT("DEBUG: Pawn position: %s"), *MyPawn->GetActorLocation().ToString());
		UE_LOG(LogTemp, Warning, TEXT("DEBUG: Player position: %s"), *Player->GetActorLocation().ToString());

		// Check movement component
		if (ACharacter* CharPawn = Cast<ACharacter>(MyPawn))
		{
			UCharacterMovementComponent* MoveComp = CharPawn->GetCharacterMovement();
			if (MoveComp)
			{
				UE_LOG(LogTemp, Warning, TEXT("DEBUG: MovementMode: %d (0=None,1=Walking,2=NavWalking,3=Falling,4=Swimming,5=Flying)"), (int32)MoveComp->MovementMode);
				UE_LOG(LogTemp, Warning, TEXT("DEBUG: Gravity: %f"), MoveComp->GravityScale);
				UE_LOG(LogTemp, Warning, TEXT("DEBUG: MaxWalkSpeed: %f"), MoveComp->MaxWalkSpeed);
				UE_LOG(LogTemp, Warning, TEXT("DEBUG: IsMovingOnGround: %s"), MoveComp->IsMovingOnGround() ? TEXT("YES") : TEXT("NO"));
			}
		}

		// Check NavMesh
		UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		if (NavSys)
		{
			FNavLocation NavLoc;
			bool bPawnOnNav = NavSys->ProjectPointToNavigation(MyPawn->GetActorLocation(), NavLoc);
			UE_LOG(LogTemp, Warning, TEXT("DEBUG: Pawn on NavMesh: %s (projected Z: %f)"),
				bPawnOnNav ? TEXT("YES") : TEXT("NO"), NavLoc.Location.Z);

			bool bPlayerOnNav = NavSys->ProjectPointToNavigation(Player->GetActorLocation(), NavLoc);
			UE_LOG(LogTemp, Warning, TEXT("DEBUG: Player on NavMesh: %s (projected Z: %f)"),
				bPlayerOnNav ? TEXT("YES") : TEXT("NO"), NavLoc.Location.Z);
		}

		// Try direct MoveToActor
		EPathFollowingRequestResult::Type Result = MoveToActor(Player, 100.0f);

		FString ResultStr;
		switch (Result)
		{
			case EPathFollowingRequestResult::Failed: ResultStr = TEXT("FAILED"); break;
			case EPathFollowingRequestResult::AlreadyAtGoal: ResultStr = TEXT("AlreadyAtGoal"); break;
			case EPathFollowingRequestResult::RequestSuccessful: ResultStr = TEXT("RequestSuccessful"); break;
			default: ResultStr = TEXT("Unknown"); break;
		}
		UE_LOG(LogTemp, Warning, TEXT("DEBUG: MoveToActor result: %s"), *ResultStr);

	}, 2.0f, false);
}

void AEnemyAIController::OnUnPossess()
{
	// Stop behavior tree
	if (BehaviorTreeComponent)
	{
		BehaviorTreeComponent->StopTree();
	}

	Super::OnUnPossess();
}

AEnemyBase* AEnemyAIController::GetControlledEnemy() const
{
	return Cast<AEnemyBase>(GetPawn());
}
