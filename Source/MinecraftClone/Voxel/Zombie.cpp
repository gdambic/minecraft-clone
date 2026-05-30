#include "Zombie.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"

AZombie::AZombie()
{
	// Set Minecraft zombie default values
	MaxHealth = 20.0f;
	CurrentHealth = 20.0f;
	AttackDamage = 3.0f;
	AttackRange = 150.0f;        // 1.5 blocks
	AttackCooldown = 2.0f;       // 2 seconds between attacks
	DetectionRange = 3500.0f;    // 35 blocks
	LoseTargetRange = 4000.0f;   // 40 blocks
	WalkSpeed = 230.0f;          // Minecraft zombie speed
	WanderRadius = 500.0f;       // 5 blocks

	// Configure capsule (humanoid proportions)
	GetCapsuleComponent()->InitCapsuleSize(34.0f, 88.0f);
	GetCapsuleComponent()->SetCanEverAffectNavigation(false);
	GetCapsuleComponent()->bDynamicObstacle = false;

	// Create cube mesh for visual representation
	CubeMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CubeMesh"));
	CubeMesh->SetupAttachment(RootComponent);
	CubeMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 0.0f));

	// Load default cube mesh
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMeshAsset(
		TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMeshAsset.Succeeded())
	{
		CubeMesh->SetStaticMesh(CubeMeshAsset.Object);
		// Scale to humanoid proportions (slightly smaller than 1 block)
		CubeMesh->SetRelativeScale3D(FVector(0.6f, 0.6f, 1.6f));
		// Center the mesh on the capsule
		CubeMesh->SetRelativeLocation(FVector(0.0f, 0.0f, -8.0f));
	}

	// Set collision to ignore camera
	CubeMesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	// Don't let the cube mesh affect NavMesh generation
	CubeMesh->SetCanEverAffectNavigation(false);
}

void AZombie::BeginPlay()
{
	Super::BeginPlay();

	// Apply walk speed
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = WalkSpeed;
	}
}

void AZombie::PerformAttack()
{
	// Call base to update LastAttackTime
	Super::PerformAttack();

	// Display HIT message on screen
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,                    // Key (-1 = don't overwrite)
			1.0f,                  // Duration
			FColor::Red,           // Color
			TEXT("HIT!")           // Message
		);
	}

	// TODO: In future, apply actual damage to player
	// if (AFirstPersonCharacter* Player = ...)
	// {
	//     Player->TakeDamage(AttackDamage, ...);
	// }
}
