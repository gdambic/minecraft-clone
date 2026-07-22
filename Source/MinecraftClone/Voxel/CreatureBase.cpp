#include "CreatureBase.h"
#include "GameFramework/CharacterMovementComponent.h"

ACreatureBase::ACreatureBase()
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
