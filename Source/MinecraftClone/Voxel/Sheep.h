#pragma once

#include "CoreMinimal.h"
#include "MobBase.h"
#include "Sheep.generated.h"

/**
 * Sheep - passive mob that wanders, flees when attacked, and drops wool on death.
 */
UCLASS()
class MINECRAFTCLONE_API ASheep : public AMobBase
{
	GENERATED_BODY()

public:
	ASheep();

	// ==================== Loot ====================

	/** Minimum wool dropped on death */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sheep|Loot")
	int32 WoolDropMin = 1;

	/** Maximum wool dropped on death */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sheep|Loot")
	int32 WoolDropMax = 3;

	// ==================== Functions ====================

	/** Called when sheep dies - spawns wool drops */
	virtual void OnDeath() override;

protected:
	/** Spawn wool items at current location */
	void DropWool();
};
