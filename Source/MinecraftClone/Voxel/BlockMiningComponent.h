#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ItemType.h"
#include "BlockMiningComponent.generated.h"

class AVoxelWorld;
class UAudioComponent;

/**
 * Kopanje bloka: progres, alat, zvuk udarca i faza pukotine.
 * Docs/PLAN_BlockDamage.md.
 *
 * Komponenta odlucuje (koliko je blok iskopan i kad puca), a pukotine crta
 * UBlockCrackComponent na svijetu. Vrijednosti bloka cita iz UBlockRegistry
 * preko tipa na grid poziciji, pa kopanje ne treba promovirani ABlock actor.
 * Kad dode multiplayer, ova komponenta se izvodi na serveru, a klijenti
 * samo dobivaju fazu pukotine.
 */
UCLASS(ClassGroup = (Voxel), meta = (BlueprintSpawnableComponent))
class MINECRAFTCLONE_API UBlockMiningComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBlockMiningComponent();

	void SetVoxelWorld(AVoxelWorld* InVoxelWorld) { VoxelWorld = InVoxelWorld; }

	/** Blok u koji kopac gleda. Promjena pozicije resetira progres. */
	void SetTarget(FIntVector GridPos);
	void ClearTarget();

	/** Drzanje tipke za kopanje. StopMining resetira progres. */
	void StartMining();
	void StopMining();

	/**
	 * Napreduj kopanje za DeltaTime s itemom u ruci. Promjena itema resetira
	 * progres (kao u Minecraftu). Vraca true ako je blok upravo puknuo.
	 */
	bool TickMining(float DeltaTime, EItemType HeldItem);

	/** Trajanje kopanja bloka itemom u sekundama, zaokruzeno na tick (negativno = ne moze se kopati) */
	float GetBreakTime(FIntVector GridPos, EItemType HeldItem, bool* bOutCanHarvest = nullptr) const;

	/** Faza pukotine iz progresa: -1 bez progresa, inace 0..9 */
	static int32 ProgressToStage(float Progress);

	/** Ispisuj [CRACK] liniju pri svakoj promjeni faze (voxel.cracktest) */
	bool bLogStages = false;

private:
	void ResetProgress();

	/** Posalji fazu svijetu kad se promijeni (i prisilnu fazu iz voxel.crack.stage) */
	void UpdateCrack();

	void PlayHitSound(float DeltaTime);

	uint32 GetBreakerId() const;

	UPROPERTY(Transient)
	TObjectPtr<AVoxelWorld> VoxelWorld;

	bool bHasTarget = false;
	FIntVector TargetPos = FIntVector::ZeroValue;

	bool bMining = false;
	float Progress = 0.0f;

	/** Vrijeme od pocetka kopanja ovog bloka (za [CRACK] log) */
	float ElapsedTime = 0.0f;

	/** Item kojim se kopalo u proslom ticku */
	EItemType MiningItem = EItemType::None;

	/** Faza koja je zadnja poslana svijetu (-1 = nema pukotine) */
	int32 SentStage = -1;
	FIntVector SentPos = FIntVector::ZeroValue;

	/** Odbrojava do sljedeceg zvuka kopanja; 0 = sviraj odmah */
	float HitSoundTimer = 0.0f;

	TWeakObjectPtr<UAudioComponent> HitSoundComponent;
};
