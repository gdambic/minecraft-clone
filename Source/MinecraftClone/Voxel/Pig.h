#pragma once

#include "CoreMinimal.h"
#include "MobBase.h"
#include "Pig.generated.h"

class UHeadLookComponent;

/**
 * Pig - passive mob that wanders, flees when attacked, and drops porkchops on death.
 */
UCLASS()
class MINECRAFTCLONE_API APig : public AMobBase
{
	GENERATED_BODY()

public:
	APig();

	virtual void Tick(float DeltaTime) override;

	// ==================== Components ====================

	/** Glances at the player; the AnimBP reads its HeadLook* outputs */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Pig|HeadLook")
	UHeadLookComponent* HeadLook;

	// ==================== Loot ====================

	/** Minimum porkchops dropped on death */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pig|Loot")
	int32 PorkchopDropMin = 1;

	/** Maximum porkchops dropped on death */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Pig|Loot")
	int32 PorkchopDropMax = 3;

	// ==================== Functions ====================

	/** Called when pig dies - spawns porkchop drops */
	virtual void OnDeath() override;

protected:
	/** Spawn porkchop items at current location */
	void DropPorkchops();
};
