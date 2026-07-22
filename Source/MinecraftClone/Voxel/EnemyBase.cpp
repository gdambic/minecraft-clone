#include "EnemyBase.h"
#include "GameFramework/CharacterMovementComponent.h"

AEnemyBase::AEnemyBase()
{
	// Set enemy-specific defaults (inherited from CreatureBase)
	WalkSpeed = 230.0f;
	WanderRadius = 500.0f;
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
