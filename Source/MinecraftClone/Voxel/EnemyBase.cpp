#include "EnemyBase.h"
#include "GameFramework/CharacterMovementComponent.h"

AEnemyBase::AEnemyBase()
{
	PrimaryActorTick.bCanEverTick = true;

	// Configure character movement
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = WalkSpeed;
		MoveComp->bOrientRotationToMovement = true;
		MoveComp->RotationRate = FRotator(0.0f, 500.0f, 0.0f);
	}

	// Don't rotate character to controller rotation (AI will handle this)
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
}

void AEnemyBase::BeginPlay()
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

bool AEnemyBase::CanAttack() const
{
	const float CurrentTime = GetWorld()->GetTimeSeconds();
	return (CurrentTime - LastAttackTime) >= AttackCooldown;
}

void AEnemyBase::PerformAttack()
{
	// Base implementation - override in child classes
	LastAttackTime = GetWorld()->GetTimeSeconds();
}

void AEnemyBase::OnDeath()
{
	// Base implementation - override in child classes
	Destroy();
}
