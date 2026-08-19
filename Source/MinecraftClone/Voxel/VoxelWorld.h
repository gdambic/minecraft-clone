#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BlockType.h"
#include "BlockDefinition.h"
#include "TreeGenerator.h"
#include "VoxelWorld.generated.h"

class ABlock;
class AZombie;
class AMobBase;
class UInstancedStaticMeshComponent;
struct FHitResult;

/** Jedan unos u spawn listi pasivnih mobova: koja klasa i koliko komada. */
USTRUCT(BlueprintType)
struct FMobSpawnEntry
{
	GENERATED_BODY()

	/** Blueprint klasa moba (BP_Sheep, BP_Pig...) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mob")
	TSubclassOf<AMobBase> MobClass;

	/** Broj primjeraka za spawnati */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mob")
	int32 Count = 3;
};

/**
 * Instance set za jedan tip bloka: ISM komponenta + dvosmjerni bookkeeping
 * grid pozicija <-> indeks instance. Komponenta radi RemoveAtSwap pri uklanjanju
 * (SetRemoveSwap) pa se indeksi mijenjaju - NIKAD ih ne cuvati izvan ove
 * strukture, uvijek ici kroz GridToInstance u trenutku koristenja.
 */
USTRUCT()
struct FBlockInstanceSet
{
	GENERATED_BODY()

	UPROPERTY()
	UInstancedStaticMeshComponent* Component = nullptr;

	/** Grid pozicija -> indeks instance u komponenti */
	TMap<FIntVector, int32> GridToInstance;

	/** Indeks instance -> grid pozicija (paralelan s nizom instanci u komponenti) */
	TArray<FIntVector> InstanceToGrid;
};

UCLASS()
class MINECRAFTCLONE_API AVoxelWorld : public AActor
{
	GENERATED_BODY()

public:
	AVoxelWorld();

	/** Z nivo površine (Grass). Ispod je Stone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World")
	int32 SurfaceLevel;

	// Dimenzije svijeta
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World")
	int32 WorldSizeX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World")
	int32 WorldSizeY;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World")
	int32 WorldSizeZ;

	// Veličina jednog bloka u UE units
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World")
	float BlockSize;

	// Broj random blokova na vrhu
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World")
	int32 RandomBlockCount;

	// === TREES ===

	/** Broj stabala za generirati */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World|Trees")
	int32 TreeCount;

	// === ENEMIES ===

	/** Blueprint klasa za zombija - postaviti BP_Zombie */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World|Enemies")
	TSubclassOf<AZombie> ZombieClass;

	// === MOBS (Passive) ===

	/** Pasivni mobovi koji se spawnaju pri generiranju svijeta */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World|Mobs")
	TArray<FMobSpawnEntry> MobSpawns;

	// === LEAF DECAY ===

	/** Poziva se kad se log uništi - pokreće provjeru decay-a obližnjeg lišća */
	UFUNCTION(BlueprintCallable, Category = "World")
	void OnLogDestroyed(FIntVector LogPosition);

	/** Poziva se kad list propadne - dodaje susjede u listu za provjeru */
	UFUNCTION(BlueprintCallable, Category = "World")
	void OnLeafDecayed(FIntVector LeafPosition);

	/**
	 * Vraca ABlock actor na poziciji - postoji SAMO za trenutno promovirani
	 * (fokusirani) blok. Za tip bloka na proizvoljnoj poziciji koristiti
	 * GetBlockTypeAt.
	 */
	UFUNCTION(BlueprintCallable, Category = "World")
	ABlock* GetBlock(int32 X, int32 Y, int32 Z);

	/**
	 * Prevedi hit (ISM instanca ili promovirani ABlock) u grid poziciju i tip.
	 * Vraca false ako hit nije blok ovog svijeta.
	 */
	bool ResolveHitToGrid(const FHitResult& Hit, FIntVector& OutPos, EBlockType& OutType) const;

	/**
	 * Ukloni instancu i spawnaj ABlock actor koji preuzima highlight, progress
	 * kopanja i drop. Vraca postojeci actor ako je pozicija vec promovirana.
	 * Invarijanta: za jednu poziciju postoji ILI instanca ILI actor, nikad oboje.
	 */
	ABlock* PromoteToActor(FIntVector Pos);

	/** Unisti promovirani actor (bez dropa) i vrati instancu. Poziva se kad fokus ode s bloka. */
	void DemoteToInstance(FIntVector Pos);

	/**
	 * Unisti blok na poziciji s drop sansom iz registry definicije - bez
	 * promoviranja u actor. Koristi leaf decay.
	 */
	void DestroyBlockAt(FIntVector Pos);

	/** Tip bloka iz mape podataka; Air ako nema unosa. Radi i za zakopane blokove bez actora. */
	UFUNCTION(BlueprintPure, Category = "World")
	EBlockType GetBlockTypeAt(FIntVector GridPosition) const;

	/**
	 * Poziva ABlock nakon što je uništen (drop je već spawnan).
	 * Uklanja podatke i actor, lazy-spawna izložene susjede i pokreće leaf decay.
	 */
	void NotifyBlockDestroyed(FIntVector GridPosition, EBlockType DestroyedType);

	UFUNCTION(BlueprintCallable, Category = "World")
	void SetBlockType(int32 X, int32 Y, int32 Z, EBlockType NewType);

	UFUNCTION(BlueprintCallable, Category = "World")
	FVector GridToWorld(int32 X, int32 Y, int32 Z);

	/** Postavi novi blok na zadanu grid poziciju (kao instancu). Vraca false ako je pozicija zauzeta. */
	UFUNCTION(BlueprintCallable, Category = "World")
	bool PlaceBlockAt(FIntVector GridPosition, EBlockType Type);

	/** Vraća listu svih tipova blokova koji se mogu postaviti (bez Air) */
	UFUNCTION(BlueprintCallable, Category = "World")
	TArray<EBlockType> GetPlaceableBlockTypes() const;

protected:
	virtual void BeginPlay() override;

private:
	/** Izvor istine: tip bloka po poziciji. NEMA unosa = Air. */
	TMap<FIntVector, EBlockType> BlockData;

	/** Promovirani actori - u praksi najvise 1 (blok koji igrac gleda). */
	UPROPERTY()
	TMap<FIntVector, ABlock*> Blocks;

	/** ISM komponente i bookkeeping po tipu bloka - vizual svih neizlozenih blokova. */
	UPROPERTY()
	TMap<EBlockType, FBlockInstanceSet> InstanceSets;

	void GenerateWorld();

	/** Ima li blok barem jednu izloženu (vidljivu) stranu. Dno svijeta (Z<0) se tretira kao čvrsto. */
	bool IsBlockExposed(FIntVector Pos) const;

	/** Dodaj instancu za poziciju (no-op ako vec postoji ili tip nema ISM). */
	void AddBlockInstance(FIntVector Pos, EBlockType Type);

	/** Batch dodavanje instanci jednog tipa (jedan AddInstances poziv) - za generaciju. */
	void AddBlockInstancesBatch(EBlockType Type, const TArray<FIntVector>& Positions);

	/** Ukloni instancu za poziciju (no-op ako ne postoji). Odrzava swap bookkeeping. */
	void RemoveBlockInstance(FIntVector Pos);

	/** Osiguraj vizual (instancu) za blok iz BlockData ako nema ni instance ni actora. */
	void EnsureBlockVisual(FIntVector Pos);

	// === [PERF] DIJAGNOSTIKA ===

	/**
	 * Razriješi sve FSoftObjectPath iz registry-a i izmjeri koliko to traje.
	 * Poziva se PRIJE generiranja terena da izolira fiksni trošak (sinkroni load
	 * Megascans materijala) od skalirajućeg (spawn po bloku).
	 * Rezultate sprema u BlockAssetCache pa se TryLoad NE poziva po bloku.
	 * Uz to kreira ISM komponentu po tipu bloka (InstanceSets).
	 * Idempotentno (bAssetCacheReady). Vidi Docs/PLAN_InstancedStaticMesh.md.
	 */
	void BuildBlockAssetCache();

	/** Je li BuildBlockAssetCache vec uspjesno odradjen */
	bool bAssetCacheReady = false;

	/** Dohvati razrijesene assete za tip bloka (lazy fallback ako cache jos nije izgraden) */
	const FBlockAssets& GetBlockAssets(EBlockType Type, const FBlockDefinition& BlockDefinition);

	/** Cache razrijesenih asseta po tipu bloka - puni ga BuildBlockAssetCache() */
	UPROPERTY()
	TMap<EBlockType, FBlockAssets> BlockAssetCache;

	// === TREES ===
	void GenerateTrees();

	// === ENEMIES ===
	void SpawnEnemies();

	// === MOBS ===
	void SpawnMobs();

	// === LEAF DECAY ===

	/** Lista lišća koje čeka provjeru decay-a */
	TArray<FIntVector> LeavesToCheck;

	/** Timer handle za procesiranje decay-a */
	FTimerHandle LeafDecayTimerHandle;

	/** Procesiraj pending decay provjere */
	void ProcessLeafDecay();

	/** Provjeri ima li lišće na poziciji vezu s logom */
	bool HasLogConnection(FIntVector LeafPosition) const;
};
