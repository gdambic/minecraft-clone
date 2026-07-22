#include "MobBase.h"

AMobBase::AMobBase()
{
	// Set passive mob defaults (slower than enemies)
	MaxHealth = 10.0f;
	WalkSpeed = 150.0f;
	WanderRadius = 400.0f;
}

void AMobBase::OnDamageTaken(AActor* DamageSource)
{
	// Set damage source as threat to trigger flee behavior
	if (DamageSource)
	{
		CurrentThreatActor = DamageSource;
	}
}
