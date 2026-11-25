#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PhysicsVolume.h"
#include "VoxelWaterVolume.generated.h"

class UBoxComponent;

/**
 * Kutija vode koja prati igraca (Docs/PLAN_Water.md, faza 4).
 *
 * CharacterMovementComponent prebaci lika u plivanje kad mu je sredina unutar
 * physics volumena s bWaterVolume. Nase more nije takav volumen nego pravilo u
 * generatoru (FChunkGenerator::IsSeaWater), pa AVoxelWorld drzi jednu ovakvu
 * kutiju oko igraca i ukljuci je samo kad je ispod njega more; vrh kutije je
 * povrsina vode.
 *
 * Unrealov volumen inace ima oblik iz brusha koji se gradi u editoru. Ovdje je
 * oblik UBoxComponent kao root: kapsula lika ga nade u svom popisu preklapanja
 * (UPrimitiveComponent::UpdatePhysicsVolume), a APhysicsVolume::IsOverlapInVolume
 * pita root za udaljenost do sudara. Brush ostaje prazan i bez sudara.
 */
UCLASS(NotPlaceable)
class MINECRAFTCLONE_API AVoxelWaterVolume : public APhysicsVolume
{
	GENERATED_BODY()

public:
	AVoxelWaterVolume(const FObjectInitializer& ObjectInitializer);

	/**
	 * Postavi kutiju: sredina vrha na TopCenter, HalfWidth u X i Y, Height prema
	 * dolje. bActive = false iskljuci sudar, pa upit preklapanja kutiju ne nade.
	 */
	void SetWaterBox(const FVector& TopCenter, float HalfWidth, float Height, bool bActive);

	bool IsWaterActive() const { return bWaterActive; }

private:
	UPROPERTY(VisibleAnywhere, Category = "Water")
	TObjectPtr<UBoxComponent> Box;

	bool bWaterActive = false;
};
