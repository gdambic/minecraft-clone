#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "EnemyBase.generated.h"

class UBehaviorTree;

/**
 * Base class for all enemies in the game.
 * Provides common properties and virtual functions for enemy behavior.
 */
UCLASS(abstract)
class MINECRAFTCLONE_API AEnemyBase : public ACharacter
{
	GENERATED_BODY()

public:
	AEnemyBase();

	// ==================== Stats ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Stats")
	float MaxHealth = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Stats")
	float CurrentHealth = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Stats")
	float AttackDamage = 3.0f;

	/** Attack range in Unreal units (100 = 1 block) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Stats")
	float AttackRange = 150.0f;

	/** Time between attacks in seconds */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Stats")
	float AttackCooldown = 2.0f;

	// ==================== Detection ====================

	/** Detection range in Unreal units (3500 = 35 blocks, Minecraft zombie default) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Detection")
	float DetectionRange = 3500.0f;

	/** Range at which enemy loses sight of target */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Detection")
	float LoseTargetRange = 4000.0f;

	// ==================== Movement ====================

	/** Walk speed in Unreal units per second */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Movement")
	float WalkSpeed = 230.0f;

	/** Wander radius when idle */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Movement")
	float WanderRadius = 500.0f;

	// ==================== AI ====================

	/** Behavior tree asset for this enemy type */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Enemy|AI")
	UBehaviorTree* BehaviorTree;

	// ==================== Functions ====================

	/** Returns the behavior tree for this enemy */
	UFUNCTION(BlueprintCallable, Category = "Enemy|AI")
	UBehaviorTree* GetBehaviorTree() const { return BehaviorTree; }

	/** Check if enemy can attack (cooldown elapsed) */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Combat")
	bool CanAttack() const;

	/** Perform attack - override in child classes */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Combat")
	virtual void PerformAttack();

	/** Called when enemy dies - override in child classes */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Combat")
	virtual void OnDeath();

protected:
	virtual void BeginPlay() override;

	/** Time of last attack (world time seconds) */
	float LastAttackTime = 0.0f;
};
