#pragma once

#include "CoreMinimal.h"
#include "Containers/Queue.h"
#include "GameFramework/Actor.h"
#include "BlockType.h"
#include "BlockDefinition.h"
#include "VoxelChunk.h"
#include "ChunkGenerator.h"
#include "VoxelWorld.generated.h"

class ABlock;
class AZombie;
class AMobBase;
class UInstancedStaticMeshComponent;
class ANavMeshBoundsVolume;
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
 * Voxel svijet organiziran u chunkove (Docs/PLAN_Chunks.md).
 * Teren = ISM instance po chunku; ABlock actor postoji samo za fokusirani blok.
 * Izvor istine je Blocks array pojedinog chunka - jedini javni put do njega
 * je GetBlockTypeAt / SetBlockType / PlaceBlockAt / DestroyBlockAt.
 */
UCLASS()
class MINECRAFTCLONE_API AVoxelWorld : public AActor
{
	GENERATED_BODY()

public:
	AVoxelWorld();

	/** Z nivo oko kojeg se njise povrsina (Grass). Ispod je Stone. Default 0:
	 *  teren -HeightAmplitude..+HeightAmplitude, kopanje do WorldMinZ (-24). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World")
	int32 SurfaceLevel;

	// Veličina jednog bloka u UE units
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World")
	float BlockSize;

	// === STREAMING (plan 3.2/3.3) ===

	/** Radijus (u chunkovima, Chebyshev) unutar kojeg chunkovi imaju vizuale. Podaci idu prsten sire, unload dva. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World|Streaming", meta = (ClampMin = "2", ClampMax = "16"))
	int32 VisualRadius;

	/** Vremenski budzet (ms) izgradnje vizuala chunkova po streaming ticku - sprjecava stucanje pri hodanju. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World|Streaming", meta = (ClampMin = "1.0"))
	float ChunkApplyBudgetMs;

	/** Period streaming provjere u sekundama. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World|Streaming", meta = (ClampMin = "0.05"))
	float StreamingIntervalSeconds;

	/** Radijus sinkrone generacije oko spawna - vizuali gotovi PRIJE prvog framea (igrac ne smije propasti). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World|Streaming", meta = (ClampMin = "1", ClampMax = "4"))
	int32 BootstrapRadius;

	/** Chunkovi s podacima - prsten sire od vizuala da je exposure na rubu vizualnog ringa uvijek tocan. */
	int32 GetDataRadius() const { return VisualRadius + 1; }

	/** Histereza: unload tek dva prstena iza vizuala da setnja po granici chunka ne treperi load/unload. */
	int32 GetUnloadRadius() const { return VisualRadius + 2; }

	// === TERRAIN (noise) ===

	/** Seed generiranja - isti seed uvijek daje isti teren te raspored stabala/mobova. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World|Terrain")
	int32 WorldSeed;

	/** "Zoom" terenskog noisea - manje = razvučenija, blaža brda. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World|Terrain")
	float NoiseScale;

	/** Maksimalno odstupanje visine stupca od SurfaceLevel (u blokovima). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World|Terrain")
	int32 HeightAmplitude;

	/** Broj oktava fraktalnog noisea - više = detaljniji, ali skuplji teren. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World|Terrain")
	int32 Octaves;

	/**
	 * "Zoom" biome noisea - mora biti znatno manji od NoiseScale da biomi budu
	 * regije od desetaka blokova (brda zive UNUTAR bioma, ne obrnuto).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World|Terrain")
	float BiomeNoiseScale;

	// === TREES ===

	/** Broj pokusaja sadnje stabla po chunku (deterministicki iz chunk seeda). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World|Trees")
	int32 TreeAttemptsPerChunk;

	/** Vjerojatnost (0-1) da pojedini pokusaj zaista posadi stablo. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World|Trees", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TreeChancePerChunk;

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

	/** Tip bloka iz podataka chunka; Air ako chunk nije ucitan ili je celija prazna. */
	UFUNCTION(BlueprintPure, Category = "World")
	EBlockType GetBlockTypeAt(FIntVector GridPosition) const;

	/** Visina površine (Z najvišeg čvrstog bloka) na stupcu (X,Y) - isti algoritam kao generacija terena. */
	UFUNCTION(BlueprintPure, Category = "World")
	int32 GetTerrainHeightAt(int32 X, int32 Y) const;

	/** Indeks bioma stupca (X,Y) u registry listi - deterministicki iz seeda, nigdje se ne sprema. */
	UFUNCTION(BlueprintPure, Category = "World")
	int32 GetBiomeIndexAt(int32 X, int32 Y) const;

	/** Definicija bioma stupca (X,Y). Jedina tocka kroz koju ostatak koda pita za biom. */
	UFUNCTION(BlueprintPure, Category = "World")
	FBiomeDefinition GetBiomeAt(int32 X, int32 Y) const;

	/**
	 * Poziva ABlock nakon što je uništen (drop je već spawnan).
	 * Uklanja podatke i actor, lazy-spawna izložene susjede i pokreće leaf decay.
	 */
	void NotifyBlockDestroyed(FIntVector GridPosition, EBlockType DestroyedType);

	UFUNCTION(BlueprintCallable, Category = "World")
	void SetBlockType(int32 X, int32 Y, int32 Z, EBlockType NewType);

	UFUNCTION(BlueprintCallable, Category = "World")
	FVector GridToWorld(int32 X, int32 Y, int32 Z);

	/** Postavi novi blok na zadanu grid poziciju (kao instancu). Vraca false ako je pozicija zauzeta ili chunk nije ucitan. */
	UFUNCTION(BlueprintCallable, Category = "World")
	bool PlaceBlockAt(FIntVector GridPosition, EBlockType Type);

	/** Vraća listu svih tipova blokova koji se mogu postaviti (bez Air) */
	UFUNCTION(BlueprintCallable, Category = "World")
	TArray<EBlockType> GetPlaceableBlockTypes() const;

protected:
	virtual void BeginPlay() override;

private:
	// === CHUNKS (izvor istine) ===

	/**
	 * Svi ucitani chunkovi. TUniquePtr da FVoxelChunk* ostane stabilan kroz
	 * rehash mape (bookkeeping i petlje smiju drzati pointer unutar framea).
	 * ISM komponente unutar chunka od GC-a cuva OwnedComponents actora.
	 */
	TMap<FIntPoint, TUniquePtr<FVoxelChunk>> Chunks;

	/**
	 * "Samo memorija" perzistencija (plan 3.1): unload IZMIJENJENOG chunka
	 * spremi njegov Blocks array ovdje; reload ga vraca umjesto regeneracije.
	 * Neizmijenjeni chunkovi se bacaju u cijelosti (formula ih vraca identicne).
	 */
	TMap<FIntPoint, TArray<EBlockType>> RetainedModifiedChunks;

	/** Hit komponenta -> (chunk, tip) za ResolveHitToGrid. Unosi zive tocno koliko i komponente. */
	TMap<const UPrimitiveComponent*, TPair<FIntPoint, EBlockType>> ComponentToChunk;

	/** Promovirani actori - u praksi najvise 1 (blok koji igrac gleda). */
	UPROPERTY()
	TMap<FIntVector, ABlock*> Blocks;

	FVoxelChunk* FindChunk(const FIntPoint& Coord);
	const FVoxelChunk* FindChunk(const FIntPoint& Coord) const;

	/** Upisi tip u podatke chunka (bez vizuala). Vraca false ako chunk nije ucitan ili je Z izvan svijeta. */
	bool SetBlockData(const FIntVector& GridPos, EBlockType Type, bool bMarkModified);

	// === GENERACIJA ===

	/** Bootstrap: sinkrono generira spawn podrucje, spawna mobove i pokrece streaming timer. */
	void GenerateWorld();

	/** Snapshot parametara generacije - kopira se po vrijednosti u (buduci) worker task. */
	FChunkGenParams MakeChunkGenParams() const;

	/** Izgradi ISM instance za sve izlozene blokove chunka (podaci moraju postojati, i susjedni). */
	void ApplyChunkVisuals(FVoxelChunk& Chunk);

	// === STREAMING ===

	/** Periodicka petlja (plan 3.3): unload dalekih, despawn mobova, load podataka, apply vizuala s budzetom. */
	void UpdateStreaming();

	/** SINKRONO ucitaj podatke chunka: vrati sacuvane izmijenjene (pobjeduju!) ili generiraj iz seeda. Za bootstrap i retained. */
	FVoxelChunk& LoadChunkData(const FIntPoint& Coord);

	/** ASYNC: kreiraj chunk u stanju Generating i posalji generaciju na worker thread (plan 3.4). */
	void RequestChunkDataAsync(const FIntPoint& Coord);

	/** Preuzmi gotove worker rezultate (pocetak streaming ticka). Zastarjeli (unload pa novi zahtjev) se bacaju. */
	void DrainCompletedChunkData();

	/** Imaju li sva 4 bocna susjeda podatke - uvjet za vizuale (exposure na rubu treba prave podatke, ne Air). */
	bool AreNeighborsDataReady(const FIntPoint& Coord) const;

	/** Unisti vizuale i promovirane actore chunka; izmijenjene podatke spremi u RetainedModifiedChunks. */
	void UnloadChunk(const FIntPoint& Coord);

	/** Despawnaj mobove ciji je chunk izvan unload radijusa (MC-ov 128-blok princip preko chunkova). */
	void DespawnFarCreatures(const FIntPoint& PlayerChunk);

	/** Pomakni NavMeshBoundsVolume na igracev chunk - navmesh prostor putuje s igracem (plan 3.7). */
	void UpdateNavBoundsVolume(const FIntPoint& PlayerChunk);

	/** World pozicija -> grid koordinata bloka (floor). */
	FIntVector WorldToGrid(const FVector& WorldPos) const;

	/** Snapshot parametara generacije - gradi se JEDNOM nakon seed offseta (GenerateWorld). */
	FChunkGenParams CachedGenParams;

	FTimerHandle StreamingTimerHandle;

	/** Centar spawn podrucja (chunk igracevog starta) - oko njega se spawnaju mobovi. */
	FIntPoint SpawnCenterChunk = FIntPoint::ZeroValue;

	/** Zadnji igracev chunk - nav volumen i dio posla rade samo na promjeni. */
	FIntPoint LastPlayerChunk = FIntPoint::ZeroValue;
	bool bHasLastPlayerChunk = false;

	/** Nav volumen iz levele (nadje se jednom, mobility se digne na Movable da ga streaming smije micati). */
	TWeakObjectPtr<ANavMeshBoundsVolume> NavBoundsVolume;
	bool bNavBoundsVolumeSearched = false;

	// === SOAK VALIDACIJA (aktivna samo uz -VoxelSoakTest na command lineu) ===

	/** Automatski test: teleportira igraca u hopovima pa natrag i provjerava determinizam regeneracije. */
	bool bSoakTest = false;
	FTimerHandle SoakTimerHandle;
	FTimerHandle SoakSnapTimerHandle;
	int32 SoakHop = 0;

	/** Pozicija bloka iskopanog na pocetku soak testa - na kraju mora ostati Air (retained put). */
	FIntVector SoakDigPos = FIntVector::ZeroValue;

	/** CRC podataka svakog generiranog nemodificiranog chunka - reload MORA dati isti (determinizam). */
	TMap<FIntPoint, uint32> SoakChunkHashes;
	int32 SoakRehashOk = 0;
	int32 SoakRehashFail = 0;

	/** Usporedi/zapamti CRC podataka chunka (no-op bez soak moda ili za izmijenjene chunkove). */
	void SoakVerifyChunkData(const FVoxelChunk& Chunk);

	/** Jedan hop: teleport igraca (3 istocno pa povratak), na kraju [SOAK] sazetak. */
	void SoakTestHop();

	/** 1 s nakon hopa: vrati igraca na povrsinu (teren se async generirao ispod njega dok je padao). */
	void SoakSnapPlayerToGround();

	/**
	 * Red gotovih worker rezultata (MPSC: workeri pisu, game thread cita).
	 * TSharedPtr da task u letu ostane valjan i ako actor umre prije njega.
	 */
	TSharedPtr<TQueue<FCompletedChunkData, EQueueMode::Mpsc>, ESPMode::ThreadSafe> CompletedChunkQueue;

	/** Brojac async zahtjeva - za odbacivanje zastarjelih rezultata. */
	uint64 NextGenRequestId = 1;

	/**
	 * Recastov default AgentMaxStepHeight (35uu) je manji od BlockSize (100uu),
	 * pa svaki rub bloka NavMesh vidi kao litice - mobovi ostanu zarobljeni na
	 * jednoj "polici" terena. Podigni step height na BlockSize prije nego se
	 * doda ijedna instanca (Dynamic generation voxelizira tile-ove kako geometrija
	 * dolazi, pa postavka mora biti gotova prije prve AddInstance).
	 */
	void EnsureNavMeshStepHeight() const;

	/** Pomak uzorkovanja terenskog noisea izveden iz WorldSeed - postavlja ga GenerateWorld() prije generacije terena. */
	float NoiseOffsetX = 0.f;
	float NoiseOffsetY = 0.f;

	/** Pomak uzorkovanja biome noisea - takodjer iz WorldSeed (GenerateWorld). */
	float BiomeOffsetX = 0.f;
	float BiomeOffsetY = 0.f;

	/**
	 * Tint bioma na poziciji za zadani tip tinta (Grass/Foliage); bijelo za
	 * None. Racuna se iz (X,Y) na zahtjev - nula stanja po bloku.
	 */
	FLinearColor GetBiomeTintAt(FIntVector Pos, EBiomeTintType TintType) const;

	/** Ima li blok barem jednu izloženu (vidljivu) stranu. Dno svijeta (Z<WorldMinZ) se tretira kao čvrsto. */
	bool IsBlockExposed(FIntVector Pos) const;

	// === ISM INSTANCE (vizuali) ===

	/**
	 * Dohvati/kreiraj instance set tipa u chunku (lazy: ISM komponenta nastaje
	 * kad chunk prvi put dobije blok tog tipa). nullptr ako tip nema mesh.
	 */
	FChunkInstanceSet* EnsureChunkInstanceSet(FVoxelChunk& Chunk, EBlockType Type);

	/** Dodaj instancu za poziciju (no-op ako vec postoji, chunk nije ucitan ili tip nema mesh). */
	void AddBlockInstance(FIntVector Pos, EBlockType Type);

	/** Batch dodavanje instanci jednog tipa u jedan chunk (jedan AddInstances poziv) - za generaciju. */
	void AddChunkInstancesBatch(FVoxelChunk& Chunk, EBlockType Type, const TArray<FIntVector>& Positions);

	/** Ukloni instancu za poziciju (no-op ako ne postoji). Odrzava swap bookkeeping. */
	void RemoveBlockInstance(FIntVector Pos);

	/** Osiguraj vizual (instancu) za blok iz podataka ako nema ni instance ni actora. */
	void EnsureBlockVisual(FIntVector Pos);

	// === [PERF] DIJAGNOSTIKA ===

	/**
	 * Razriješi sve FSoftObjectPath iz registry-a i izmjeri koliko to traje.
	 * Poziva se PRIJE generiranja terena da izolira fiksni trošak (sinkroni load
	 * Megascans materijala) od skalirajućeg (spawn po bloku).
	 * Rezultate sprema u BlockAssetCache pa se TryLoad NE poziva po bloku.
	 * Idempotentno (bAssetCacheReady). ISM komponente se NE kreiraju ovdje
	 * nego lazy po chunku (EnsureChunkInstanceSet).
	 */
	void BuildBlockAssetCache();

	/** Je li BuildBlockAssetCache vec uspjesno odradjen */
	bool bAssetCacheReady = false;

	/** Dohvati razrijesene assete za tip bloka (lazy fallback ako cache jos nije izgraden) */
	const FBlockAssets& GetBlockAssets(EBlockType Type, const FBlockDefinition& BlockDefinition);

	/** Cache razrijesenih asseta po tipu bloka - puni ga BuildBlockAssetCache() */
	UPROPERTY()
	TMap<EBlockType, FBlockAssets> BlockAssetCache;

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
