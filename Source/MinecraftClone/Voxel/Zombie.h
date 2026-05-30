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

	/** Visual mesh component (cube placeholder) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UStaticMeshComponent* CubeMesh;

	/** Perform melee attack - displays HIT message */
	virtual void PerformAttack() override;

protected:
	virtual void BeginPlay() override;
};
