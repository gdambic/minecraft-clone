#pragma once

#include "CoreMinimal.h"
#include "CreatureBase.h"
#include "EnemyBase.generated.h"

/**
 * Base class for all enemies in the game.
 * Provides combat and detection specific properties.
 * Inherits common stats and movement from ACreatureBase.
 */
UCLASS(abstract)
class MINECRAFTCLONE_API AEnemyBase : public ACreatureBase
{
	GENERATED_BODY()

public:
	AEnemyBase();

	// ==================== Combat ====================

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

	// ==================== Functions ====================

	/** Check if enemy can attack (cooldown elapsed) */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Combat")
	bool CanAttack() const;

	/** Perform attack - override in child classes */
	UFUNCTION(BlueprintCallable, Category = "Enemy|Combat")
	virtual void PerformAttack();

protected:
	/** Time of last attack (world time seconds) */
	float LastAttackTime = 0.0f;
};
