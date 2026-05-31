#pragma once

#include "CoreMinimal.h"
#include "EnemyBase.h"
#include "Zombie.generated.h"

/**
 * Zombie enemy - basic melee attacker.
 * Follows Minecraft zombie behavior:
 * - Detection range: 35 blocks (3500 units)
 * - Attack cooldown: 2 seconds
 * - Random wandering when idle
 */
UCLASS()
class MINECRAFTCLONE_API AZombie : public AEnemyBase
{
	GENERATED_BODY()

public:
	AZombie();

	/** Is zombie currently playing attack animation */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	bool bIsAttacking = false;

	/** Perform melee attack - displays HIT message */
	virtual void PerformAttack() override;

	/** Called by Animation Notify to reset attack state */
	UFUNCTION(BlueprintCallable, Category = "Animation")
	void OnAttackAnimationEnd();

protected:
	virtual void BeginPlay() override;
};
