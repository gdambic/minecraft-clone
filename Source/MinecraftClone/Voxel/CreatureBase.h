#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CreatureBase.generated.h"

class UBehaviorTree;

/**
 * Abstract base class for all creatures (enemies and passive mobs).
 * Provides shared stats, movement, and AI properties.
 */
UCLASS(abstract)
class MINECRAFTCLONE_API ACreatureBase : public ACharacter
{
	GENERATED_BODY()

public:
	ACreatureBase();

	// ==================== Stats ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature|Stats")
	float MaxHealth = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature|Stats")
	float CurrentHealth = 20.0f;

	// ==================== Movement ====================

	/** Walk speed in Unreal units per second */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature|Movement")
	float WalkSpeed = 200.0f;

	/** Wander radius when idle */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature|Movement")
	float WanderRadius = 500.0f;

	// ==================== AI ====================

	/** Behavior tree asset for this creature type */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Creature|AI")
	UBehaviorTree* BehaviorTree;

	// ==================== Functions ====================

	/** Returns the behavior tree for this creature */
	UFUNCTION(BlueprintCallable, Category = "Creature|AI")
	UBehaviorTree* GetBehaviorTree() const { return BehaviorTree; }

	/** Returns wander radius */
	UFUNCTION(BlueprintCallable, Category = "Creature|Movement")
	float GetWanderRadius() const { return WanderRadius; }

	/** Called when creature dies - override in child classes */
	UFUNCTION(BlueprintCallable, Category = "Creature|Stats")
	virtual void OnDeath();

	/** Apply damage to creature, returns true if creature died */
	UFUNCTION(BlueprintCallable, Category = "Creature|Stats")
	virtual bool TakeDamage(float DamageAmount);

protected:
	virtual void BeginPlay() override;
};
