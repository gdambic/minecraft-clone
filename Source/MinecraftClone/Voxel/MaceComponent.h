#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ItemType.h"
#include "MaceComponent.generated.h"

class ACreatureBase;
class AFirstPersonCharacter;
class AVoxelWorld;
class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;

/** Stanja Terraria thrown flaila (aiStyle 15, redoslijed kao u vanilli) */
UENUM()
enum class EMaceState : uint8
{
	Idle,              // kugla visi na dršci (u Terrariji projektil tada ne postoji)
	Spinning,          // drzi se tipka: kugla krui oko igraca
	LaunchingForward,  // tipka pustena: kugla leti prema nisanu
	Retracting,        // isteklo vrijeme leta ili duljina lanca: vraca se
	ForcedRetracting,  // pusten drop ili zapela kugla: vraca se kroz blokove
	Ricochet,          // kugla je u letu udarila blok
	Dropping           // tipka ponovno drzana nakon bacanja: kugla pada i lezi
};

/**
 * Mace - kugla sa siljcima na lancu (Docs/PLAN_Mace.md).
 *
 * Ponasanje je doslovna preslika Terraria thrown flaila (Projectile.AI_015_Flails,
 * prema tModLoader ExampleAdvancedFlailProjectile). Simulacija ide fiksnim
 * Terraria tickom (60 Hz) i Terraria jedinicama pretvorenim 1 tile = 1 blok
 * (16 px = 100 UU); prikaz interpolira izmedu dva ticka.
 *
 * Drška je obican sprite item u ruci (UFirstPersonArmComponent), a kugla i lanac
 * su komponente s apsolutnim transformom koje ova komponenta stvori na vlasniku.
 */
UCLASS(ClassGroup = (Voxel), meta = (BlueprintSpawnableComponent))
class MINECRAFTCLONE_API UMaceComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMaceComponent();

	void SetVoxelWorld(AVoxelWorld* InVoxelWorld) { VoxelWorld = InVoxelWorld; }

	/** Je li item Mace (character tada LMB ne salje kopanju ni melee udarcu) */
	static bool IsMace(EItemType ItemType) { return ItemType == EItemType::Mace; }

	/** Lijeva tipka pritisnuta / pustena (Terraria channel + controlUseItem) */
	void PressAttack();
	void ReleaseAttack();

	/** Napreduj simulaciju i prikaz; HeldItem != Mace sakrije i ponisti kuglu */
	void TickMace(float DeltaTime, EItemType HeldItem);

	EMaceState GetState() const { return State; }
	FVector GetBallLocation() const { return BallPos; }

	/**
	 * Vrh drške u lokalnom prostoru ekstrudiranog sprite mesha (ploca 100x100 u
	 * X-Z, Z gore = red 0). Default je sredina piksela (13, 2) teksture
	 * T_Item_Mace, gdje skripta crta ušicu lanca.
	 */
	UPROPERTY(EditAnywhere, Category = "Mace")
	FVector ChainAnchorLocal = FVector(34.375f, 0.0f, 34.375f);

	/** Pokreni voxel.macetest skriptu (simulirani pritisci, [MACE] u logu) */
	void StartSelfTest();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

private:
	void TickInternal(float DeltaTime, EItemType HeldItem);

	/** Jedan Terraria tick (1/60 s) */
	void SimulateTick();

	/** Pomak za Velocity uz koliziju s blokovima po osima (Collision.TileCollision + OnTileCollide) */
	void MoveWithTileCollision();

	/** Je li kutija kugle oko Center u nekom cvrstom bloku */
	bool OverlapsSolid(const FVector& Center) const;

	/** Pogoci stvorenja u ovom ticku (lokalni hit cooldown po meti) */
	void CheckHits();
	void HitCreature(ACreatureBase* Target);

	/** "player.MountedCenter": sredina kapsule */
	FVector GetMountedCenter() const;

	/** Tocka u svijetu u koju igrac nisani ("Main.MouseWorld") */
	FVector GetAimPoint() const;

	/** Vrh drške u svijetu */
	FVector GetChainAnchorWorld() const;

	void Kill();
	void SetState(EMaceState NewState);

	void CreateVisuals();
	void BuildBallMesh();
	void UpdateVisuals(bool bVisible);

	void TickSelfTest();

	UPROPERTY()
	TObjectPtr<AFirstPersonCharacter> OwnerCharacter;

	UPROPERTY()
	TObjectPtr<AVoxelWorld> VoxelWorld;

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> BallMesh;

	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> ChainMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> BallMID;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> ChainMID;

	EMaceState State = EMaceState::Idle;

	/** Polozaj i brzina kugle (UU, UU/tick) u trenutnom i proslom ticku */
	FVector BallPos = FVector::ZeroVector;
	FVector PrevBallPos = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;

	/** ai[1], localAI[0], localAI[1] iz vanille */
	float StateTimer = 0.0f;
	float CollisionCounter = 0.0f;
	float SpinningStateTimer = 0.0f;

	/** localNPCHitCooldown za ovaj tick */
	int32 HitCooldownTicks = 10;

	/** Tick zadnjeg pogotka po meti (usesLocalNPCImmunity) */
	TMap<TWeakObjectPtr<ACreatureBase>, int64> LastHitTick;

	/** player.channel / controlUseItem */
	bool bAttackHeld = false;

	/** Pritisak koji jos ceka da ga tick pretvori u vrtnju */
	bool bPressPending = false;

	int64 TickCount = 0;
	int64 LastUseTick = -1000;
	int64 KilledTick = -1000;
	float TickAccumulator = 0.0f;

	bool bHeld = false;

	// voxel.macetest
	bool bSelfTest = false;
	float SelfTestTime = 0.0f;
	int32 SelfTestStep = 0;
	float SelfTestMaxDist = 0.0f;
	int32 SelfTestHits = 0;
	int64 SelfTestLoggedTick = -1;
};
