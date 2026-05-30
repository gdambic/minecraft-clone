#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EnemySpawnPoint.generated.h"

class AEnemyBase;
class UBillboardComponent;

/**
 * Spawn point for enemies. Place in level to spawn enemies at game start.
 * Only visible in editor, invisible during gameplay.
 */
UCLASS()
class MINECRAFTCLONE_API AEnemySpawnPoint : public AActor
{
	GENERATED_BODY()

public:
	AEnemySpawnPoint();

	/** The enemy class to spawn */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	TSubclassOf<AEnemyBase> EnemyClass;

	/** If true, spawns the enemy when the game starts */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn")
	bool bSpawnOnBeginPlay = true;

	/** Billboard sprite for editor visualization */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	UBillboardComponent* BillboardSprite;

	/** Spawns the enemy at this location */
	UFUNCTION(BlueprintCallable, Category = "Spawn")
	AEnemyBase* SpawnEnemy();

protected:
	virtual void BeginPlay() override;
};
