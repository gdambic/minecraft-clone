#include "CreatureBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationInvokerComponent.h"

ACreatureBase::ACreatureBase()
{
	PrimaryActorTick.bCanEverTick = true;

	// Generira navmesh u radijusu oko moba (3000 gen / 3500 uklanjanje).
	// Pokriva WanderRadius (500) s velikom rezervom; balon prati moba u chase-u.
	NavInvoker = CreateDefaultSubobject<UNavigationInvokerComponent>(TEXT("NavInvoker"));
	NavInvoker->SetGenerationRadii(3000.0f, 3500.0f);

	// Configure character movement
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = WalkSpeed;
		MoveComp->bOrientRotationToMovement = true;
		MoveComp->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

		// Default MaxStepHeight (~45uu) je manji od BlockSize (100uu) - kapsula bi
		// se fizicki sudarala s rubom bloka i ne mogla se popeti, iako je NavMesh
		// (AVoxelWorld::EnsureNavMeshStepHeight) vec dopustio taj korak u pathu.
		// Drzati u sinku s BlockSize + margina koristenom tamo.
		MoveComp->MaxStepHeight = 110.0f;
	}

	// Don't rotate character to controller rotation (AI will handle this)
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
}

void ACreatureBase::BeginPlay()
{
	Super::BeginPlay();

	// Apply walk speed to movement component
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = WalkSpeed;
	}

	// Initialize health
	CurrentHealth = MaxHealth;
}

bool ACreatureBase::TakeDamage(float DamageAmount)
{
	CurrentHealth -= DamageAmount;

	if (CurrentHealth <= 0.0f)
	{
		CurrentHealth = 0.0f;
		OnDeath();
		return true;
	}

	return false;
}

void ACreatureBase::OnDeath()
{
	// Base implementation - override in child classes
	Destroy();
}
