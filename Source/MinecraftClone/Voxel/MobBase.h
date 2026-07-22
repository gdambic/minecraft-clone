#pragma once

#include "CoreMinimal.h"
#include "CreatureBase.h"
#include "MobBase.generated.h"

/**
 * Abstract base class for passive/friendly mobs.
 * Provides flee behavior properties.
 * Inherits common stats and movement from ACreatureBase.
 */
UCLASS(abstract)
class MINECRAFTCLONE_API AMobBase : public ACreatureBase
{
	GENERATED_BODY()

public:
	AMobBase();

	// ==================== Flee Behavior ====================

	/** How far the mob runs when fleeing (8 blocks = 800 units) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mob|Flee")
	float FleeDistance = 800.0f;

	/** Distance at which player is considered a threat (3 blocks = 300 units) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mob|Flee")
	float ThreatDetectionRange = 300.0f;

	// ==================== Functions ====================

	UFUNCTION(BlueprintCallable, Category = "Mob|Flee")
	float GetFleeDistance() const { return FleeDistance; }

	UFUNCTION(BlueprintCallable, Category = "Mob|Flee")
	float GetThreatDetectionRange() const { return ThreatDetectionRange; }

	/** Called when mob takes damage - triggers flee response */
	UFUNCTION(BlueprintCallable, Category = "Mob|Damage")
	void OnDamageTaken(AActor* DamageSource);

	/** Get the current threat actor (if any) */
	UFUNCTION(BlueprintCallable, Category = "Mob|Flee")
	AActor* GetCurrentThreat() const { return CurrentThreatActor; }

	/** Set the current threat actor */
	UFUNCTION(BlueprintCallable, Category = "Mob|Flee")
	void SetCurrentThreat(AActor* Threat) { CurrentThreatActor = Threat; }

	/** Clear the current threat actor */
	UFUNCTION(BlueprintCallable, Category = "Mob|Flee")
	void ClearCurrentThreat() { CurrentThreatActor = nullptr; }

protected:
	/** Current threat actor for flee behavior */
	UPROPERTY()
	AActor* CurrentThreatActor = nullptr;
};
