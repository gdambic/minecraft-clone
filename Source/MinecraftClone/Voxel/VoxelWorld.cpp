#include "VoxelWorld.h"
#include "Block.h"
#include "ItemDrop.h"
#include "ChunkGenerator.h"
#include "TerrainGenerator.h"
#include "BiomeGenerator.h"
#include "TimerManager.h"
#include "Zombie.h"
#include "MobBase.h"
#include "BlockRegistry.h"
#include "AI/NavigationSystemBase.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "CreatureBase.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/RandomStream.h"
#include "Tasks/Task.h"

using namespace VoxelGrid;

/**
 * voxel.tp <X> <Y> - teleportiraj igraca na grid stupac (X,Y), na visinu
 * terena. Za rucni test streaminga (20 chunkova hoda je sporo).
 */
static FAutoConsoleCommandWithWorldAndArgs GVoxelTeleportCmd(
	TEXT("voxel.tp"),
	TEXT("Teleportiraj igraca na grid (X Y), na visinu terena."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 2 || !World)
		{
			UE_LOG(LogTemp, Warning, TEXT("voxel.tp: ocekujem 2 argumenta (grid X Y)"));
			return;
		}
		APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0);
		AVoxelWorld* VoxelWorld = nullptr;
		for (TActorIterator<AVoxelWorld> It(World); It; ++It)
		{
			VoxelWorld = *It;
			break;
		}
		if (!Player || !VoxelWorld)
		{
			return;
		}
		const int32 GX = FCString::Atoi(*Args[0]);
		const int32 GY = FCString::Atoi(*Args[1]);
		const int32 GZ = VoxelWorld->GetTerrainHeightAt(GX, GY) + 3;
		Player->SetActorLocation(
			VoxelWorld->GridToWorld(GX, GY, GZ) + FVector(50.f, 50.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogTemp, Log, TEXT("voxel.tp: igrac na grid (%d,%d,%d)"), GX, GY, GZ);
	}));

AVoxelWorld::AVoxelWorld()
{
	PrimaryActorTick.bCanEverTick = false;

	// Default vrijednosti - mogu se override-ati u BP_VoxelWorld
	BlockSize = 100.0f;
	VisualRadius = 6;
	ChunkApplyBudgetMs = 8.0f;
	StreamingIntervalSeconds = 0.25f;
	BootstrapRadius = 2;
	// Povrsina se njise oko Z=0 (teren -6..6); kopanje ide u minus do
	// WorldMinZ (-24), zrak za stabla/gradnju u plus do WorldMaxZ (23).
	SurfaceLevel = 0;
	WorldSeed = 1337;
	NoiseScale = 0.08f;
	HeightAmplitude = 6;
	Octaves = 3;
	BiomeNoiseScale = 0.012f;
	// ~0.25 stabla po chunku = gustoca starog svijeta (10 stabala na 100x100)
	TreeAttemptsPerChunk = 1;
	TreeChancePerChunk = 0.25f;

	CompletedChunkQueue = MakeShared<TQueue<FCompletedChunkData, EQueueMode::Mpsc>, ESPMode::ThreadSafe>();
}

void AVoxelWorld::BeginPlay()
{
	Super::BeginPlay();
	GenerateWorld();
}

// === CHUNKS ===

FVoxelChunk* AVoxelWorld::FindChunk(const FIntPoint& Coord)
{
	const TUniquePtr<FVoxelChunk>* Found = Chunks.Find(Coord);
	return Found ? Found->Get() : nullptr;
}

const FVoxelChunk* AVoxelWorld::FindChunk(const FIntPoint& Coord) const
{
	const TUniquePtr<FVoxelChunk>* Found = Chunks.Find(Coord);
	return Found ? Found->Get() : nullptr;
}

EBlockType AVoxelWorld::GetBlockTypeAt(FIntVector GridPosition) const
{
	if (!IsValidZ(GridPosition.Z))
	{
		return EBlockType::Air;
	}

	const FVoxelChunk* Chunk = FindChunk(GridToChunk(GridPosition));
	if (!Chunk || Chunk->State == EChunkState::Generating)
	{
		return EBlockType::Air;
	}

	return Chunk->GetBlock(GridToLocal(GridPosition));
}

bool AVoxelWorld::SetBlockData(const FIntVector& GridPos, EBlockType Type, bool bMarkModified)
{
	if (!IsValidZ(GridPos.Z))
	{
		UE_LOG(LogTemp, Warning, TEXT("SetBlockData: Z=%d izvan svijeta (%d..%d)"), GridPos.Z, WorldMinZ, WorldMaxZ);
		return false;
	}

	FVoxelChunk* Chunk = FindChunk(GridToChunk(GridPos));
	if (!Chunk || Chunk->State != EChunkState::Applied)
	{
		UE_LOG(LogTemp, Warning, TEXT("SetBlockData: chunk za (%d,%d,%d) nije ucitan/primijenjen - mutacija odbijena"),
			GridPos.X, GridPos.Y, GridPos.Z);
		return false;
	}

	Chunk->SetBlock(GridToLocal(GridPos), Type);
	if (bMarkModified)
	{
		Chunk->bModified = true;
	}
	return true;
}

// === GENERACIJA ===

FChunkGenParams AVoxelWorld::MakeChunkGenParams() const
{
	FChunkGenParams Params;
	Params.WorldSeed = WorldSeed;
	Params.NoiseOffsetX = NoiseOffsetX;
	Params.NoiseOffsetY = NoiseOffsetY;
	Params.NoiseScale = NoiseScale;
	Params.Octaves = Octaves;
	Params.SurfaceLevel = SurfaceLevel;
	Params.HeightAmplitude = HeightAmplitude;
	Params.TreeAttemptsPerChunk = TreeAttemptsPerChunk;
	Params.TreeChancePerChunk = TreeChancePerChunk;

	// Showcase red za chunk (0,0) - po jedan blok svake definicije,
	// sortirano po enum vrijednosti (TMap redoslijed nije garantiran)
	if (UBlockRegistry* Registry = UBlockRegistry::Get(this))
	{
		TArray<FBlockDefinition> Definitions = Registry->GetAllBlockDefinitions();
		Definitions.Sort([](const FBlockDefinition& A, const FBlockDefinition& B)
		{
			return (uint8)A.BlockType < (uint8)B.BlockType;
		});
		for (const FBlockDefinition& Def : Definitions)
		{
			Params.ShowcaseBlocks.Add(Def.BlockType);
		}
	}

	return Params;
}

void AVoxelWorld::GenerateWorld()
{
	// Provjeri je li registry dostupan
	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	if (!Registry)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelWorld: BlockRegistry not found!"));
		return;
	}

	// Vrijednosti iz BP_VoxelWorld mogu biti stare (SurfaceLevel=24 iz svijeta
	// s 0-baziranim Z) - upozori ako profil visine ne stane u WorldMinZ..WorldMaxZ
	if (SurfaceLevel + HeightAmplitude + 8 > WorldMaxZ + 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: SurfaceLevel=%d + amplituda %d ostavlja premalo zraka do stropa %d (stabla trebaju ~8)"),
			SurfaceLevel, HeightAmplitude, WorldMaxZ + 1);
	}
	if (SurfaceLevel - HeightAmplitude < WorldMinZ)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: SurfaceLevel=%d - amplituda %d pada ispod dna svijeta %d"),
			SurfaceLevel, HeightAmplitude, WorldMinZ);
	}

	// === [PERF] Početak mjerenja - vidi Docs/PLAN_UbrzanjePokretanja.md, sekcija 2 ===
	UE_LOG(LogTemp, Warning, TEXT("[PERF] ================================================"));
	UE_LOG(LogTemp, Warning, TEXT("[PERF] GenerateWorld  seed=%d, SurfaceLevel=%d +/-%d, WorldHeight=%d, VisualRadius=%d (data %d, unload %d)"),
		WorldSeed, SurfaceLevel, HeightAmplitude, WorldHeight, VisualRadius, GetDataRadius(), GetUnloadRadius());

	const double T0 = FPlatformTime::Seconds();

	EnsureNavMeshStepHeight();

	// --- Faza 1: assets (fiksni trosak, NE skalira s velicinom svijeta) ---
	// Razrijesi mesh/materijale JEDNOM i spremi u cache - petlje ispod koriste gotove pointere
	BuildBlockAssetCache();

	const double T1 = FPlatformTime::Seconds();

	// Seed odreduje pomak uzorkovanja noisea; globalni RandInit ostaje SAMO za
	// spawn mobova (stabla su presla na deterministicki chunk stream).
	FMath::RandInit(WorldSeed);
	const FRandomStream NoiseSeedStream(WorldSeed);
	NoiseOffsetX = NoiseSeedStream.FRandRange(-100000.f, 100000.f);
	NoiseOffsetY = NoiseSeedStream.FRandRange(-100000.f, 100000.f);
	BiomeOffsetX = NoiseSeedStream.FRandRange(-100000.f, 100000.f);
	BiomeOffsetY = NoiseSeedStream.FRandRange(-100000.f, 100000.f);

	// Snapshot parametara NAKON postavljanja offseta - streaming ga koristi do kraja igre
	CachedGenParams = MakeChunkGenParams();

	// --- Faza 2: bootstrap spawn podrucja (sinkrono - igrac ne smije propasti) ---
	// Centar je PlayerStart iz levele; podaci se generiraju prsten sire od
	// vizuala da je exposure na rubu bootstrapa tocan (data ring princip).
	FIntVector SpawnGrid(ChunkSize / 2, ChunkSize / 2, 0);
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		SpawnGrid = WorldToGrid(It->GetActorLocation());
		break;
	}
	SpawnCenterChunk = GridToChunk(SpawnGrid);

	for (int32 DY = -BootstrapRadius - 1; DY <= BootstrapRadius + 1; DY++)
	{
		for (int32 DX = -BootstrapRadius - 1; DX <= BootstrapRadius + 1; DX++)
		{
			LoadChunkData(SpawnCenterChunk + FIntPoint(DX, DY));
		}
	}

	const double T2 = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Warning, TEXT("[PERF] 2/4 Podaci     %8.1f ms   %d chunkova (bootstrap oko chunka (%d,%d))"),
		(T2 - T1) * 1000.0, Chunks.Num(), SpawnCenterChunk.X, SpawnCenterChunk.Y);

	// --- Faza 3: vizuali bootstrap podrucja ---
	for (int32 DY = -BootstrapRadius; DY <= BootstrapRadius; DY++)
	{
		for (int32 DX = -BootstrapRadius; DX <= BootstrapRadius; DX++)
		{
			if (FVoxelChunk* Chunk = FindChunk(SpawnCenterChunk + FIntPoint(DX, DY)))
			{
				ApplyChunkVisuals(*Chunk);
			}
		}
	}

	const double T3 = FPlatformTime::Seconds();
	int32 TotalInstances = 0;
	for (const TPair<FIntPoint, TUniquePtr<FVoxelChunk>>& Pair : Chunks)
	{
		for (const TPair<EBlockType, FChunkInstanceSet>& SetPair : Pair.Value->InstanceSets)
		{
			TotalInstances += SetPair.Value.InstanceToGrid.Num();
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("[PERF] 3/4 Vizuali    %8.1f ms   %6d instanci"),
		(T3 - T2) * 1000.0, TotalInstances);

	// --- Faza 4: mobovi (oko spawna - dalje od toga zive buduci per-chunk spawn, vidi BACKLOG) ---
	SpawnEnemies();
	SpawnMobs();

	const double T4 = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Warning, TEXT("[PERF] 4/4 Mobovi     %8.1f ms"), (T4 - T3) * 1000.0);

	// --- Sazetak ---
	const double AssetMs = (T1 - T0) * 1000.0;
	const double TotalMs = (T4 - T0) * 1000.0;
	UE_LOG(LogTemp, Warning, TEXT("[PERF] ------------------------------------------------"));
	UE_LOG(LogTemp, Warning, TEXT("[PERF] UKUPNO        %8.1f ms   (%d chunkova, %d instanci; ostatak puni streaming)"),
		TotalMs, Chunks.Num(), TotalInstances);
	UE_LOG(LogTemp, Warning, TEXT("[PERF]   fiksni (assets)      %8.1f ms  (%.0f%%)"),
		AssetMs, TotalMs > 0.0 ? AssetMs / TotalMs * 100.0 : 0.0);
	UE_LOG(LogTemp, Warning, TEXT("[PERF] ================================================"));

	// Pokreni timer za leaf decay (svakih 2.5 sekundi)
	GetWorld()->GetTimerManager().SetTimer(
		LeafDecayTimerHandle,
		this,
		&AVoxelWorld::ProcessLeafDecay,
		2.5f,
		true // Looping
	);

	// Streaming petlja - puni svijet oko igraca do punih radijusa i dalje ga odrzava
	GetWorld()->GetTimerManager().SetTimer(
		StreamingTimerHandle,
		this,
		&AVoxelWorld::UpdateStreaming,
		StreamingIntervalSeconds,
		true // Looping
	);

	// Soak validacija (headless): -VoxelSoakTest teleportira igraca u hopovima
	// pa natrag i provjerava da regeneracija daje bit-identicne chunkove
	bSoakTest = FParse::Param(FCommandLine::Get(), TEXT("VoxelSoakTest"));
	if (bSoakTest)
	{
		// Iskopaj jedan blok na spawnu - na kraju testa (nakon unload + reload
		// spawn podrucja) mora i dalje biti Air (retained-modified put)
		const FIntVector Base = ChunkOrigin(SpawnCenterChunk);
		SoakDigPos = FIntVector(Base.X + 3, Base.Y + 3, GetTerrainHeightAt(Base.X + 3, Base.Y + 3));
		DestroyBlockAt(SoakDigPos);
		UE_LOG(LogTemp, Warning, TEXT("[SOAK] pokrecem soak test (hop svakih 12 s); iskopan blok na (%d,%d,%d)"),
			SoakDigPos.X, SoakDigPos.Y, SoakDigPos.Z);
		GetWorld()->GetTimerManager().SetTimer(
			SoakTimerHandle, this, &AVoxelWorld::SoakTestHop, 12.0f, true);
	}
}

// === SOAK VALIDACIJA ===

void AVoxelWorld::SoakVerifyChunkData(const FVoxelChunk& Chunk)
{
	if (!bSoakTest || Chunk.bModified)
	{
		return;
	}

	const uint32 Crc = FCrc::MemCrc32(Chunk.Blocks.GetData(), Chunk.Blocks.Num() * sizeof(EBlockType));
	if (const uint32* Known = SoakChunkHashes.Find(Chunk.Coord))
	{
		if (*Known == Crc)
		{
			SoakRehashOk++;
		}
		else
		{
			SoakRehashFail++;
			UE_LOG(LogTemp, Error, TEXT("[SOAK] DETERMINIZAM FAIL: chunk (%d,%d) regeneriran s drugim sadrzajem (crc %08x != %08x)"),
				Chunk.Coord.X, Chunk.Coord.Y, Crc, *Known);
		}
	}
	else
	{
		SoakChunkHashes.Add(Chunk.Coord, Crc);
	}
}

void AVoxelWorld::SoakTestHop()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!Player)
	{
		return;
	}

	SoakHop++;

	if (SoakHop > 4)
	{
		// Iskopani blok je preizivio unload spawn chunka i regeneraciju?
		const bool bRetainedOk = GetBlockTypeAt(SoakDigPos) == EBlockType::Air;
		UE_LOG(LogTemp, Warning, TEXT("[SOAK] RETAINED %s: iskopani blok (%d,%d,%d) je %s"),
			bRetainedOk ? TEXT("OK") : TEXT("FAIL"),
			SoakDigPos.X, SoakDigPos.Y, SoakDigPos.Z, bRetainedOk ? TEXT("Air") : TEXT("regeneriran!"));
		UE_LOG(LogTemp, Warning, TEXT("[SOAK] GOTOVO: rehash OK=%d, FAIL=%d, hashirano %d chunkova, ucitano %d, zadrzano %d"),
			SoakRehashOk, SoakRehashFail, SoakChunkHashes.Num(), Chunks.Num(), RetainedModifiedChunks.Num());
		GetWorld()->GetTimerManager().ClearTimer(SoakTimerHandle);
		return;
	}

	// Hop 1: istok; hop 2: NEGATIVNE koordinate (floor-division test!); hop 3:
	// daleki istok; hop 4: povratak na start (retained + rehash provjera)
	const FIntVector Base = ChunkOrigin(SpawnCenterChunk) + FIntVector(ChunkSize / 2, ChunkSize / 2, 0);
	int32 GX = Base.X;
	int32 GY = Base.Y;
	switch (SoakHop)
	{
	case 1: GX += 10 * ChunkSize; break;
	case 2: GX -= 12 * ChunkSize; GY -= 12 * ChunkSize; break;
	case 3: GX += 24 * ChunkSize; break;
	default: break;
	}
	const int32 GZ = GetTerrainHeightAt(GX, GY) + 3;

	Player->SetActorLocation(GridToWorld(GX, GY, GZ) + FVector(50.f, 50.f, 0.f),
		false, nullptr, ETeleportType::TeleportPhysics);
	UE_LOG(LogTemp, Warning, TEXT("[SOAK] hop %d -> grid (%d,%d,%d), ucitano %d chunkova"),
		SoakHop, GX, GY, GZ, Chunks.Num());

	// Teren se async generira ispod igraca koji vec pada - za 1 s ga vrati na povrsinu
	GetWorld()->GetTimerManager().SetTimer(
		SoakSnapTimerHandle, this, &AVoxelWorld::SoakSnapPlayerToGround, 1.0f, false);
}

void AVoxelWorld::SoakSnapPlayerToGround()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!Player)
	{
		return;
	}

	const FIntVector Grid = WorldToGrid(Player->GetActorLocation());
	const int32 GZ = GetTerrainHeightAt(Grid.X, Grid.Y) + 2;
	Player->SetActorLocation(GridToWorld(Grid.X, Grid.Y, GZ) + FVector(50.f, 50.f, 0.f),
		false, nullptr, ETeleportType::TeleportPhysics);
	if (UPawnMovementComponent* Movement = Player->GetMovementComponent())
	{
		Movement->Velocity = FVector::ZeroVector;
	}
}

// === STREAMING ===

FIntVector AVoxelWorld::WorldToGrid(const FVector& WorldPos) const
{
	const FVector Rel = (WorldPos - GetActorLocation()) / BlockSize;
	return FIntVector(FMath::FloorToInt(Rel.X), FMath::FloorToInt(Rel.Y), FMath::FloorToInt(Rel.Z));
}

FVoxelChunk& AVoxelWorld::LoadChunkData(const FIntPoint& Coord)
{
	if (FVoxelChunk* Existing = FindChunk(Coord))
	{
		return *Existing;
	}

	TUniquePtr<FVoxelChunk> Chunk = MakeUnique<FVoxelChunk>(Coord);

	// Sacuvani izmijenjeni podaci POBJEDUJU regeneraciju (plan, rizik 6)
	if (TArray<EBlockType>* Retained = RetainedModifiedChunks.Find(Coord))
	{
		Chunk->Blocks = MoveTemp(*Retained);
		Chunk->bModified = true;
		RetainedModifiedChunks.Remove(Coord);
	}
	else
	{
		Chunk->Blocks = FChunkGenerator::GenerateChunkData(CachedGenParams, Coord);
	}

	Chunk->State = EChunkState::DataReady;
	FVoxelChunk& Added = *Chunks.Add(Coord, MoveTemp(Chunk));
	SoakVerifyChunkData(Added);
	return Added;
}

void AVoxelWorld::RequestChunkDataAsync(const FIntPoint& Coord)
{
	if (Chunks.Contains(Coord))
	{
		return;
	}

	TUniquePtr<FVoxelChunk> NewChunk = MakeUnique<FVoxelChunk>(Coord);
	NewChunk->State = EChunkState::Generating;
	NewChunk->GenRequestId = NextGenRequestId++;
	const uint64 RequestId = NewChunk->GenRequestId;
	Chunks.Add(Coord, MoveTemp(NewChunk));

	// Worker dobiva KOPIJU parametara i shared pointer na queue - ne dira
	// actor ni UObjecte, pa mu ni smrt svijeta usred rada ne smeta (plan 3.4)
	const FChunkGenParams Params = CachedGenParams;
	TSharedPtr<TQueue<FCompletedChunkData, EQueueMode::Mpsc>, ESPMode::ThreadSafe> Queue = CompletedChunkQueue;

	UE::Tasks::Launch(UE_SOURCE_LOCATION, [Params, Coord, RequestId, Queue]()
	{
		FCompletedChunkData Done;
		Done.Coord = Coord;
		Done.RequestId = RequestId;

		const double Start = FPlatformTime::Seconds();
		Done.Blocks = FChunkGenerator::GenerateChunkData(Params, Coord);
		Done.GenMs = (FPlatformTime::Seconds() - Start) * 1000.0;

		Queue->Enqueue(MoveTemp(Done));
	});
}

void AVoxelWorld::DrainCompletedChunkData()
{
	FCompletedChunkData Done;
	while (CompletedChunkQueue->Dequeue(Done))
	{
		FVoxelChunk* Chunk = FindChunk(Done.Coord);
		if (!Chunk || Chunk->State != EChunkState::Generating || Chunk->GenRequestId != Done.RequestId)
		{
			// Zastarjeli rezultat: chunk unloadan (i mozda ponovno zatrazen) dok
			// je worker radio - id se ne poklapa, rezultat se baca (plan, rizik 6)
			continue;
		}

		Chunk->Blocks = MoveTemp(Done.Blocks);
		Chunk->State = EChunkState::DataReady;
		SoakVerifyChunkData(*Chunk);
		UE_LOG(LogTemp, Verbose, TEXT("[PERF] Chunk (%d,%d) gen=%.2fms (worker)"),
			Done.Coord.X, Done.Coord.Y, Done.GenMs);
	}
}

bool AVoxelWorld::AreNeighborsDataReady(const FIntPoint& Coord) const
{
	static const FIntPoint Sides[4] = {
		FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1)
	};
	for (const FIntPoint& Offset : Sides)
	{
		const FVoxelChunk* Neighbor = FindChunk(Coord + Offset);
		if (!Neighbor || Neighbor->State == EChunkState::Generating)
		{
			return false;
		}
	}
	return true;
}

void AVoxelWorld::UnloadChunk(const FIntPoint& Coord)
{
	FVoxelChunk* Chunk = FindChunk(Coord);
	if (!Chunk)
	{
		return;
	}

	// Promovirani actor u chunku koji nestaje - unisti bez dropa (fokus je ionako daleko)
	for (auto It = Blocks.CreateIterator(); It; ++It)
	{
		if (GridToChunk(It.Key()) == Coord)
		{
			if (It.Value())
			{
				It.Value()->Destroy();
			}
			It.RemoveCurrent();
		}
	}

	// ISM komponente + ComponentToChunk unosi zive tocno koliko i chunk
	for (TPair<EBlockType, FChunkInstanceSet>& Pair : Chunk->InstanceSets)
	{
		if (Pair.Value.Component)
		{
			ComponentToChunk.Remove(Pair.Value.Component);
			Pair.Value.Component->DestroyComponent();
		}
	}

	// Decay provjere za pozicije koje vise nisu ucitane su bespredmetne
	LeavesToCheck.RemoveAll([&Coord](const FIntVector& P) { return GridToChunk(P) == Coord; });

	// "Samo memorija" perzistencija: izmijenjeni chunk ostavlja podatke,
	// neizmijenjeni se baca u cijelosti (formula ga vraca identicnog)
	if (Chunk->bModified)
	{
		RetainedModifiedChunks.Add(Coord, MoveTemp(Chunk->Blocks));
	}

	Chunks.Remove(Coord);
}

void AVoxelWorld::UpdateStreaming()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!Player)
	{
		return;
	}

	const FIntPoint PlayerChunk = GridToChunk(WorldToGrid(Player->GetActorLocation()));
	const bool bChunkChanged = !bHasLastPlayerChunk || PlayerChunk != LastPlayerChunk;

	// 0. Preuzmi gotove worker rezultate (Generating -> DataReady)
	DrainCompletedChunkData();

	// 1. Unload svega iza histereze
	int32 Unloaded = 0;
	TArray<FIntPoint> ToUnload;
	for (const TPair<FIntPoint, TUniquePtr<FVoxelChunk>>& Pair : Chunks)
	{
		if (ChunkDistance(Pair.Key, PlayerChunk) > GetUnloadRadius())
		{
			ToUnload.Add(Pair.Key);
		}
	}
	for (const FIntPoint& Coord : ToUnload)
	{
		UnloadChunk(Coord);
		Unloaded++;
	}

	// 2. Mobovi bez tla ispod sebe bi propali kroz svijet
	DespawnFarCreatures(PlayerChunk);

	// 3. Nav prostor prati igraca (samo na promjeni chunka)
	if (bChunkChanged)
	{
		UpdateNavBoundsVolume(PlayerChunk);
	}

	// 4. Data ring: zahtjevi za sve chunkove unutar DataRadius. Generacija ide
	// na worker (plan 3.4); iznimka su sacuvani izmijenjeni chunkovi - njihov
	// povratak je samo premjestanje arraya pa ide sinkrono.
	const int32 DataRadius = GetDataRadius();
	for (int32 DY = -DataRadius; DY <= DataRadius; DY++)
	{
		for (int32 DX = -DataRadius; DX <= DataRadius; DX++)
		{
			const FIntPoint Coord = PlayerChunk + FIntPoint(DX, DY);
			if (Chunks.Contains(Coord))
			{
				continue;
			}
			if (RetainedModifiedChunks.Contains(Coord))
			{
				LoadChunkData(Coord);
			}
			else
			{
				RequestChunkDataAsync(Coord);
			}
		}
	}

	// 5. Visual ring: izgradnja instanci je skupa (~4 ms/chunk) pa ide s
	// vremenskim budzetom, najblizi chunkovi prvi. Uvjet su podaci sva 4
	// bocna susjeda - inace bi exposure na rubu vidio Air i ostavio trajno
	// visak instanci na granici.
	TArray<FVoxelChunk*> PendingVisuals;
	for (int32 DY = -VisualRadius; DY <= VisualRadius; DY++)
	{
		for (int32 DX = -VisualRadius; DX <= VisualRadius; DX++)
		{
			const FIntPoint Coord = PlayerChunk + FIntPoint(DX, DY);
			FVoxelChunk* Chunk = FindChunk(Coord);
			if (Chunk && !Chunk->bVisualsBuilt && Chunk->State == EChunkState::DataReady
				&& AreNeighborsDataReady(Coord))
			{
				PendingVisuals.Add(Chunk);
			}
		}
	}
	PendingVisuals.Sort([&PlayerChunk](const FVoxelChunk& A, const FVoxelChunk& B)
	{
		return ChunkDistance(A.Coord, PlayerChunk) < ChunkDistance(B.Coord, PlayerChunk);
	});

	int32 Applied = 0;
	const double BudgetEnd = FPlatformTime::Seconds() + ChunkApplyBudgetMs / 1000.0;
	for (FVoxelChunk* Chunk : PendingVisuals)
	{
		ApplyChunkVisuals(*Chunk);
		Applied++;
		if (FPlatformTime::Seconds() >= BudgetEnd)
		{
			break;
		}
	}

	if (Unloaded > 0 || Applied > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("VoxelWorld streaming: igrac u chunku (%d,%d), +%d vizuala, -%d unload, %d ucitano (%d zadrzano)"),
			PlayerChunk.X, PlayerChunk.Y, Applied, Unloaded, Chunks.Num(), RetainedModifiedChunks.Num());
	}

	LastPlayerChunk = PlayerChunk;
	bHasLastPlayerChunk = true;
}

void AVoxelWorld::DespawnFarCreatures(const FIntPoint& PlayerChunk)
{
	int32 Despawned = 0;
	for (TActorIterator<ACreatureBase> It(GetWorld()); It; ++It)
	{
		// Igracev pawn nije ACreatureBase pa ovdje prolaze samo mobovi
		const FIntPoint CreatureChunk = GridToChunk(WorldToGrid(It->GetActorLocation()));
		if (ChunkDistance(CreatureChunk, PlayerChunk) > GetUnloadRadius())
		{
			It->Destroy();
			Despawned++;
		}
	}

	if (Despawned > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("VoxelWorld streaming: despawnano %d mobova izvan radijusa"), Despawned);
	}
}

void AVoxelWorld::UpdateNavBoundsVolume(const FIntPoint& PlayerChunk)
{
	if (!bNavBoundsVolumeSearched)
	{
		bNavBoundsVolumeSearched = true;
		for (TActorIterator<ANavMeshBoundsVolume> It(GetWorld()); It; ++It)
		{
			NavBoundsVolume = *It;
			// Brush actor je Static - bez ovoga SetActorLocation tiho ne radi
			if (USceneComponent* Root = It->GetRootComponent())
			{
				Root->SetMobility(EComponentMobility::Movable);
			}
			break;
		}
		if (!NavBoundsVolume.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: NavMeshBoundsVolume nije naden - navmesh nece pratiti igraca"));
		}
	}

	ANavMeshBoundsVolume* Volume = NavBoundsVolume.Get();
	if (!Volume)
	{
		return;
	}

	const FIntVector Center = ChunkOrigin(PlayerChunk) + FIntVector(ChunkSize / 2, ChunkSize / 2, 0);
	FVector NewLocation = GridToWorld(Center.X, Center.Y, 0);
	NewLocation.Z = Volume->GetActorLocation().Z;   // visinu odreduje levela
	Volume->SetActorLocation(NewLocation);

	// Sam SetActorLocation ne obavjestava nav sustav da se adresabilni prostor pomaknuo
	if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		NavSys->OnNavigationBoundsUpdated(Volume);
	}
}

void AVoxelWorld::ApplyChunkVisuals(FVoxelChunk& Chunk)
{
	if (Chunk.State == EChunkState::Generating)
	{
		UE_LOG(LogTemp, Error, TEXT("ApplyChunkVisuals: chunk (%d,%d) jos nema podatke"), Chunk.Coord.X, Chunk.Coord.Y);
		return;
	}
	if (Chunk.bVisualsBuilt)
	{
		Chunk.State = EChunkState::Applied;
		return;
	}

	// ISM instance SAMO za izlozene blokove (zakopani ostaju samo podaci).
	// Batch AddInstances po tipu - jedan poziv umjesto tisuca.
	const FIntVector Origin = ChunkOrigin(Chunk.Coord);
	TMap<EBlockType, TArray<FIntVector>> ExposedByType;

	for (int32 Z = WorldMinZ; Z <= WorldMaxZ; Z++)
	{
		for (int32 LocalY = 0; LocalY < ChunkSize; LocalY++)
		{
			for (int32 LocalX = 0; LocalX < ChunkSize; LocalX++)
			{
				const EBlockType Type = Chunk.Blocks[LocalIndex(FIntVector(LocalX, LocalY, Z))];
				if (Type == EBlockType::Air)
				{
					continue;
				}
				const FIntVector GridPos = Origin + FIntVector(LocalX, LocalY, Z);
				if (IsBlockExposed(GridPos))
				{
					ExposedByType.FindOrAdd(Type).Add(GridPos);
				}
			}
		}
	}

	for (const TPair<EBlockType, TArray<FIntVector>>& Pair : ExposedByType)
	{
		AddChunkInstancesBatch(Chunk, Pair.Key, Pair.Value);
	}

	Chunk.bVisualsBuilt = true;
	Chunk.State = EChunkState::Applied;
}

void AVoxelWorld::EnsureNavMeshStepHeight() const
{
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSys)
	{
		return;
	}

	ARecastNavMesh* RecastNavMesh = Cast<ARecastNavMesh>(NavSys->GetDefaultNavDataInstance());
	if (!RecastNavMesh)
	{
		return;
	}

	const float RequiredStepHeight = BlockSize + 10.0f;
	if (RecastNavMesh->GetAgentMaxStepHeight(ENavigationDataResolution::Default) < RequiredStepHeight)
	{
		RecastNavMesh->SetAgentMaxStepHeight(ENavigationDataResolution::Default, RequiredStepHeight);

		// SetAgentMaxStepHeight sam po sebi NE utjece na vec izgradjeni generator -
		// FRecastNavMeshGenerator::Init() svoju kopiju (walkableClimb) snimi JEDNOM
		// pri konstrukciji, prije nego ovaj kod uopce stigne promijeniti postavku.
		// Rekonstrukcija generatora tjera ga da ponovno procita novu vrijednost.
		RecastNavMesh->ConditionalConstructGenerator();

		UE_LOG(LogTemp, Log, TEXT("VoxelWorld: NavMesh AgentMaxStepHeight -> %.1f (BlockSize=%.1f), generator rekonstruiran"), RequiredStepHeight, BlockSize);
	}
}

bool AVoxelWorld::IsBlockExposed(FIntVector Pos) const
{
	static const FIntVector Neighbors[6] = {
		FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
		FIntVector(0, 1, 0), FIntVector(0, -1, 0),
		FIntVector(0, 0, 1), FIntVector(0, 0, -1)
	};

	for (const FIntVector& Offset : Neighbors)
	{
		const FIntVector N = Pos + Offset;

		// Donju stranu svijeta (ispod WorldMinZ) nitko ne vidi - tretiraj kao
		// cvrsto. Bocne strane prema neucitanom chunku se racunaju kao izlozene
		// (GetBlockTypeAt vraca Air) - data ring u fazi 2 garantira da
		// vizualni chunkovi uvijek imaju susjede s pravim podacima.
		if (N.Z < WorldMinZ)
		{
			continue;
		}

		if (GetBlockTypeAt(N) == EBlockType::Air)
		{
			return true;
		}
	}

	return false;
}

ABlock* AVoxelWorld::PromoteToActor(FIntVector Pos)
{
	if (ABlock* Existing = Blocks.FindRef(Pos))
	{
		return Existing;
	}

	const EBlockType Type = GetBlockTypeAt(Pos);
	if (Type == EBlockType::Air)
	{
		return nullptr;
	}

	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	if (!Registry)
	{
		return nullptr;
	}

	const FBlockDefinition* BlockDefinition = Registry->GetBlockDefinition(Type);
	if (!BlockDefinition)
	{
		UE_LOG(LogTemp, Warning, TEXT("BlockRegistry: No definition for block type %d"), (int32)Type);
		return nullptr;
	}

	// Gotovi pointeri iz cachea - bez TryLoad-a po bloku
	const FBlockAssets& BlockAssets = GetBlockAssets(Type, *BlockDefinition);

	FVector WorldPos = GridToWorld(Pos.X, Pos.Y, Pos.Z);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Spawn generički ABlock i inicijaliziraj iz registry-a
	ABlock* NewBlock = GetWorld()->SpawnActor<ABlock>(ABlock::StaticClass(), WorldPos, FRotator::ZeroRotator, SpawnParams);

	if (NewBlock)
	{
		// Invarijanta: instanca i actor nikad istovremeno - ukloni instancu
		// tek nakon uspjesnog spawna (neuspjeh ne smije "izbrisati" vizual)
		RemoveBlockInstance(Pos);

		// Actor nije ISM instanca pa custom data tint za njega ne postoji -
		// isti tint ide kroz TintFallback parametar (na ISM putu je bijel,
		// ovdje custom data pada na bijeli default: uvijek mnozi tocno jedna boja)
		UMaterialInterface* Material = BlockAssets.Material;
		if (Material && BlockDefinition->BiomeTint != EBiomeTintType::None)
		{
			UMaterialInstanceDynamic* TintedMID = UMaterialInstanceDynamic::Create(Material, NewBlock);
			TintedMID->SetVectorParameterValue(TEXT("TintFallback"),
				GetBiomeTintAt(Pos, BlockDefinition->BiomeTint));
			Material = TintedMID;
		}

		NewBlock->SetGridPosition(Pos);
		NewBlock->InitializeFromRegistry(Type, *BlockDefinition,
			BlockAssets.Mesh, Material, BlockAssets.HighlightMaterial);
		Blocks.Add(Pos, NewBlock);
	}

	return NewBlock;
}

void AVoxelWorld::DemoteToInstance(FIntVector Pos)
{
	ABlock* Actor = Blocks.FindRef(Pos);
	if (!Actor)
	{
		return;
	}

	Blocks.Remove(Pos);
	Actor->Destroy();

	// Blok jos postoji u podacima -> vrati instancu (fokus je samo otisao dalje)
	const EBlockType Type = GetBlockTypeAt(Pos);
	if (Type != EBlockType::Air)
	{
		AddBlockInstance(Pos, Type);
	}
}

// === ISM INSTANCE ===

FChunkInstanceSet* AVoxelWorld::EnsureChunkInstanceSet(FVoxelChunk& Chunk, EBlockType Type)
{
	if (FChunkInstanceSet* Existing = Chunk.InstanceSets.Find(Type))
	{
		// Component == nullptr je cache-irani negativni rezultat (tip bez mesha)
		return Existing->Component ? Existing : nullptr;
	}

	BuildBlockAssetCache();

	const FBlockAssets* Assets = BlockAssetCache.Find(Type);
	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	const FBlockDefinition* Def = Registry ? Registry->GetBlockDefinition(Type) : nullptr;
	if (!Assets || !Assets->Mesh || !Def)
	{
		UE_LOG(LogTemp, Error, TEXT("EnsureChunkInstanceSet: tip %d nema mesh/definiciju - bez vizuala"), (int32)Type);
		Chunk.InstanceSets.Add(Type);   // marker da ne pokusavamo ponovno
		return nullptr;
	}

	// Lazy kreiranje: ISM komponenta nastaje kad chunk prvi put dobije blok
	// ovog tipa. MakeUniqueObjectName jer ime unistene komponente ostaje
	// zauzeto do GC-a (unload pa brzi reload istog chunka).
	const FString BaseName = FString::Printf(TEXT("ISM_%s_%d_%d"),
		*StaticEnum<EBlockType>()->GetNameStringByValue((int64)Type), Chunk.Coord.X, Chunk.Coord.Y);
	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(
		this, MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), FName(*BaseName)));
	ISM->SetStaticMesh(Assets->Mesh);
	if (Assets->Material)
	{
		ISM->SetMaterial(0, Assets->Material);
	}
	ISM->SetMobility(GetRootComponent()->Mobility);
	ISM->SetCollisionProfileName(TEXT("BlockAll"));
	ISM->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ISM->SetCanEverAffectNavigation(true);
	// RemoveInstance u 5.6 po defaultu radi order-preserving RemoveAt;
	// bookkeeping u RemoveBlockInstance racuna na RemoveAtSwap semantiku
	ISM->SetRemoveSwap();

	// Blokovi s biome tintom nose RGB tint kao 3 custom data floata po
	// instanci - mora se postaviti PRIJE prve AddInstance. Engine pri
	// RemoveAtSwap sam premjesta i custom data (InstancedStaticMesh.cpp:3827).
	if (Def->BiomeTint != EBiomeTintType::None)
	{
		ISM->SetNumCustomDataFloats(3);
	}

	ISM->SetupAttachment(GetRootComponent());
	ISM->RegisterComponent();

	FChunkInstanceSet& NewSet = Chunk.InstanceSets.Add(Type);
	NewSet.Component = ISM;
	NewSet.BiomeTint = Def->BiomeTint;
	ComponentToChunk.Add(ISM, TPair<FIntPoint, EBlockType>(Chunk.Coord, Type));
	return &NewSet;
}

void AVoxelWorld::AddBlockInstance(FIntVector Pos, EBlockType Type)
{
	FVoxelChunk* Chunk = FindChunk(GridToChunk(Pos));
	if (!Chunk)
	{
		return;
	}

	FChunkInstanceSet* Set = EnsureChunkInstanceSet(*Chunk, Type);
	if (!Set || Set->GridToInstance.Contains(Pos))
	{
		return;
	}

	const int32 Index = Set->Component->AddInstance(
		FTransform(GridToWorld(Pos.X, Pos.Y, Pos.Z)), /*bWorldSpace=*/true);

	// AddInstance appenda na kraj pa indeks mora pratiti nas paralelni niz
	if (Index != Set->InstanceToGrid.Num())
	{
		UE_LOG(LogTemp, Error, TEXT("AddBlockInstance: index desync (%d != %d) za tip %d"),
			Index, Set->InstanceToGrid.Num(), (int32)Type);
	}

	// RGB tint bioma - bez njega bi custom data ostao 0 (crna trava)
	if (Set->BiomeTint != EBiomeTintType::None)
	{
		const FLinearColor Tint = GetBiomeTintAt(Pos, Set->BiomeTint);
		const float TintFloats[3] = { Tint.R, Tint.G, Tint.B };
		Set->Component->SetCustomData(Index, MakeArrayView(TintFloats, 3));
	}

	Set->GridToInstance.Add(Pos, Index);
	Set->InstanceToGrid.Add(Pos);
}

void AVoxelWorld::AddChunkInstancesBatch(FVoxelChunk& Chunk, EBlockType Type, const TArray<FIntVector>& Positions)
{
	if (Positions.Num() == 0)
	{
		return;
	}

	FChunkInstanceSet* Set = EnsureChunkInstanceSet(Chunk, Type);
	if (!Set)
	{
		return;
	}

	TArray<FTransform> Transforms;
	Transforms.Reserve(Positions.Num());
	for (const FIntVector& Pos : Positions)
	{
		Transforms.Add(FTransform(GridToWorld(Pos.X, Pos.Y, Pos.Z)));
	}

	// Indeksi batcha su kontinuirani: AddInstances appenda na kraj
	const int32 StartIndex = Set->InstanceToGrid.Num();

	Set->Component->AddInstances(Transforms, /*bShouldReturnIndices=*/false, /*bWorldSpace=*/true);

	for (const FIntVector& Pos : Positions)
	{
		Set->GridToInstance.Add(Pos, Set->InstanceToGrid.Num());
		Set->InstanceToGrid.Add(Pos);
	}

	// RGB tint bioma za cijeli batch - range overload je jedan memcpy, a
	// delta tracker (CustomDataChanged) sam gura promjenu na GPU krajem framea
	if (Set->BiomeTint != EBiomeTintType::None)
	{
		TArray<float> TintFloats;
		TintFloats.Reserve(Positions.Num() * 3);
		for (const FIntVector& Pos : Positions)
		{
			const FLinearColor Tint = GetBiomeTintAt(Pos, Set->BiomeTint);
			TintFloats.Add(Tint.R);
			TintFloats.Add(Tint.G);
			TintFloats.Add(Tint.B);
		}
		Set->Component->SetCustomData(StartIndex, StartIndex + Positions.Num() - 1, TintFloats);
	}

	// Engine registrira komponentu u nav octree kod PRVE instance, s bounds
	// izracunatim od te jedne instance - batch nakon toga NE re-registrira
	// element, pa bi navmesh builder svugdje osim oko prve instance "vidio"
	// prazninu. Osvjezi bounds pa ponovno registriraj element s punim opsegom.
	Set->Component->UpdateBounds();
	FNavigationSystem::UpdateComponentData(*Set->Component);
}

void AVoxelWorld::RemoveBlockInstance(FIntVector Pos)
{
	FVoxelChunk* Chunk = FindChunk(GridToChunk(Pos));
	if (!Chunk)
	{
		return;
	}

	for (TPair<EBlockType, FChunkInstanceSet>& Pair : Chunk->InstanceSets)
	{
		FChunkInstanceSet& Set = Pair.Value;
		const int32* Found = Set.GridToInstance.Find(Pos);
		if (!Found || !Set.Component)
		{
			continue;
		}

		const int32 Removed = *Found;
		const int32 Last = Set.InstanceToGrid.Num() - 1;

		// Komponenta ima SetRemoveSwap(): zadnja instanca preuzima indeks Removed
		Set.Component->RemoveInstance(Removed);

		if (Removed != Last)
		{
			const FIntVector MovedPos = Set.InstanceToGrid[Last];
			Set.InstanceToGrid[Removed] = MovedPos;
			Set.GridToInstance[MovedPos] = Removed;
		}
		Set.InstanceToGrid.RemoveAt(Last);
		Set.GridToInstance.Remove(Pos);
		return;
	}
}

void AVoxelWorld::EnsureBlockVisual(FIntVector Pos)
{
	const EBlockType Type = GetBlockTypeAt(Pos);
	if (Type == EBlockType::Air || Blocks.Contains(Pos))
	{
		return;
	}

	// No-op ako instanca vec postoji
	AddBlockInstance(Pos, Type);
}

bool AVoxelWorld::ResolveHitToGrid(const FHitResult& Hit, FIntVector& OutPos, EBlockType& OutType) const
{
	// Promovirani actor
	if (const ABlock* Block = Cast<ABlock>(Hit.GetActor()))
	{
		OutPos = Block->GridPosition;
		OutType = Block->BlockType;
		return true;
	}

	// ISM instanca: Hit.Item je indeks instance u pogodenoj komponenti
	const TPair<FIntPoint, EBlockType>* Key = ComponentToChunk.Find(Hit.GetComponent());
	if (!Key)
	{
		return false;
	}

	const FVoxelChunk* Chunk = FindChunk(Key->Key);
	if (!Chunk)
	{
		return false;
	}

	const FChunkInstanceSet* Set = Chunk->InstanceSets.Find(Key->Value);
	if (!Set || !Set->InstanceToGrid.IsValidIndex(Hit.Item))
	{
		return false;
	}

	OutPos = Set->InstanceToGrid[Hit.Item];
	OutType = Key->Value;
	return true;
}

void AVoxelWorld::DestroyBlockAt(FIntVector Pos)
{
	const EBlockType Type = GetBlockTypeAt(Pos);
	if (Type == EBlockType::Air)
	{
		return;
	}

	// Drop sansa iz registry definicije (isto ponasanje kao ABlock::AddDestroyProgress)
	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	const FBlockDefinition* Def = Registry ? Registry->GetBlockDefinition(Type) : nullptr;
	if (Def && Def->DropItemType != EItemType::None && FMath::FRand() < Def->DropChance)
	{
		const float HalfBlock = BlockSize * 0.5f;
		const FVector DropLocation = GridToWorld(Pos.X, Pos.Y, Pos.Z)
			+ FVector(HalfBlock, HalfBlock, HalfBlock);
		AItemDrop::SpawnItemDrop(this, Def->DropItemType, DropLocation);
	}

	NotifyBlockDestroyed(Pos, Type);
}

void AVoxelWorld::NotifyBlockDestroyed(FIntVector GridPosition, EBlockType DestroyedType)
{
	SetBlockData(GridPosition, EBlockType::Air, /*bMarkModified=*/true);

	// Vizual je bio ili instanca (npr. leaf decay) ili promovirani actor (kopanje)
	RemoveBlockInstance(GridPosition);
	if (ABlock* Actor = Blocks.FindRef(GridPosition))
	{
		Blocks.Remove(GridPosition);
		Actor->Destroy();
	}

	// Susjedi su mozda upravo postali izlozeni -> lazy instanca
	// (no-op za pozicije bez podataka ili s vec postojecim vizualom)
	static const FIntVector Neighbors[6] = {
		FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
		FIntVector(0, 1, 0), FIntVector(0, -1, 0),
		FIntVector(0, 0, 1), FIntVector(0, 0, -1)
	};
	for (const FIntVector& Offset : Neighbors)
	{
		EnsureBlockVisual(GridPosition + Offset);
	}

	// Leaf decay notifikacije
	if (DestroyedType == EBlockType::OakLog || DestroyedType == EBlockType::BirchLog)
	{
		OnLogDestroyed(GridPosition);
	}
	else if (DestroyedType == EBlockType::OakLeaves || DestroyedType == EBlockType::BirchLeaves)
	{
		OnLeafDecayed(GridPosition);
	}
}

ABlock* AVoxelWorld::GetBlock(int32 X, int32 Y, int32 Z)
{
	FIntVector GridPos(X, Y, Z);
	ABlock** FoundBlock = Blocks.Find(GridPos);
	return FoundBlock ? *FoundBlock : nullptr;
}

int32 AVoxelWorld::GetTerrainHeightAt(int32 X, int32 Y) const
{
	return FTerrainGenerator::GetColumnHeight(X, Y, NoiseOffsetX, NoiseOffsetY,
		NoiseScale, Octaves, SurfaceLevel, HeightAmplitude);
}

int32 AVoxelWorld::GetBiomeIndexAt(int32 X, int32 Y) const
{
	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	const int32 BiomeCount = Registry ? Registry->GetBiomeCount() : 1;
	return FBiomeGenerator::GetBiomeIndexAt(X, Y, BiomeOffsetX, BiomeOffsetY,
		BiomeNoiseScale, BiomeCount);
}

FBiomeDefinition AVoxelWorld::GetBiomeAt(int32 X, int32 Y) const
{
	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	if (!Registry)
	{
		return FBiomeDefinition();
	}
	return Registry->GetBiome(GetBiomeIndexAt(X, Y));
}

FLinearColor AVoxelWorld::GetBiomeTintAt(FIntVector Pos, EBiomeTintType TintType) const
{
	if (TintType == EBiomeTintType::None)
	{
		return FLinearColor::White;
	}
	const FBiomeDefinition Biome = GetBiomeAt(Pos.X, Pos.Y);
	return TintType == EBiomeTintType::Foliage ? Biome.FoliageTint : Biome.GrassTint;
}

void AVoxelWorld::SetBlockType(int32 X, int32 Y, int32 Z, EBlockType NewType)
{
	FIntVector GridPos(X, Y, Z);

	if (NewType == EBlockType::Air)
	{
		// Air = prazna celija - ukloni podatke i vizual
		SetBlockData(GridPos, EBlockType::Air, /*bMarkModified=*/true);
		RemoveBlockInstance(GridPos);
		if (ABlock* Actor = Blocks.FindRef(GridPos))
		{
			Blocks.Remove(GridPos);
			Actor->Destroy();
		}
		return;
	}

	if (!SetBlockData(GridPos, NewType, /*bMarkModified=*/true))
	{
		return;
	}

	if (ABlock* Actor = Blocks.FindRef(GridPos))
	{
		Actor->SetBlockType(NewType);
		return;
	}

	// Tip se mozda promijenio -> instanca mora u drugi set
	RemoveBlockInstance(GridPos);
	if (IsBlockExposed(GridPos))
	{
		AddBlockInstance(GridPos, NewType);
	}
}

FVector AVoxelWorld::GridToWorld(int32 X, int32 Y, int32 Z)
{
	// X i Y su širina/duljina, Z je visina
	return GetActorLocation() + FVector(X * BlockSize, Y * BlockSize, Z * BlockSize);
}

bool AVoxelWorld::PlaceBlockAt(FIntVector GridPosition, EBlockType Type)
{
	if (!IsValidZ(GridPosition.Z))
	{
		return false;
	}

	// Zauzetost se provjerava u podacima (Air actori vise ne postoje)
	if (GetBlockTypeAt(GridPosition) != EBlockType::Air)
	{
		return false;
	}

	FVoxelChunk* Chunk = FindChunk(GridToChunk(GridPosition));
	if (!Chunk || Chunk->State != EChunkState::Applied)
	{
		UE_LOG(LogTemp, Warning, TEXT("PlaceBlockAt: chunk za (%d,%d,%d) nije ucitan - blok odbijen"),
			GridPosition.X, GridPosition.Y, GridPosition.Z);
		return false;
	}

	// Pokriva pozive prije GenerateWorld (no-op nakon prvog uspjesnog builda)
	BuildBlockAssetCache();

	// Bez mesha (nema registry definicije) ne ostavljaj "duh" podatke
	const FBlockAssets* Assets = BlockAssetCache.Find(Type);
	if (!Assets || !Assets->Mesh)
	{
		UE_LOG(LogTemp, Error, TEXT("PlaceBlockAt: No mesh for block type %d at (%d,%d,%d)"),
			(int32)Type, GridPosition.X, GridPosition.Y, GridPosition.Z);
		return false;
	}

	Chunk->SetBlock(GridToLocal(GridPosition), Type);
	Chunk->bModified = true;

	// Postavljeni blok je po definiciji izlozen - odmah dodaj instancu
	AddBlockInstance(GridPosition, Type);
	return true;
}

TArray<EBlockType> AVoxelWorld::GetPlaceableBlockTypes() const
{
	TArray<EBlockType> Result;

	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	if (Registry)
	{
		TArray<FBlockDefinition> AllBlocks = Registry->GetAllBlockDefinitions();
		for (const FBlockDefinition& BlockDef : AllBlocks)
		{
			// Blok se može postaviti ako ima PlaceableFromItem definiran
			if (BlockDef.PlaceableFromItem != EItemType::None)
			{
				Result.Add(BlockDef.BlockType);
			}
		}
	}

	return Result;
}

// === [PERF] DIJAGNOSTIKA ===

void AVoxelWorld::BuildBlockAssetCache()
{
	if (bAssetCacheReady)
	{
		return;
	}

	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	if (!Registry)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PERF] 1/4 Assets     -- registry nedostupan --"));
		return;
	}

	const TArray<FBlockDefinition> AllBlocks = Registry->GetAllBlockDefinitions();

	const double Start = FPlatformTime::Seconds();

	// ISM komponente chunkova se attachaju na root - osiguraj da postoji
	// (BP_VoxelWorld ima DefaultSceneRoot, ali cisti C++ spawn nema)
	if (!GetRootComponent())
	{
		USceneComponent* Root = NewObject<USceneComponent>(this, TEXT("VoxelWorldRoot"));
		SetRootComponent(Root);
		Root->RegisterComponent();
	}

	int32 Resolved = 0;
	int32 Failed = 0;
	for (const FBlockDefinition& Def : AllBlocks)
	{
		FBlockAssets Assets;
		Assets.Mesh = Cast<UStaticMesh>(Def.Mesh.TryLoad());
		Assets.Material = Cast<UMaterialInterface>(Def.Material.TryLoad());
		Assets.HighlightMaterial = Cast<UMaterialInterface>(Def.HighlightMaterial.TryLoad());
		BlockAssetCache.Add(Def.BlockType, Assets);

		// Statistika za [PERF] log
		const FSoftObjectPath* Paths[3] = { &Def.Mesh, &Def.Material, &Def.HighlightMaterial };
		const UObject* Loaded[3] = { Assets.Mesh, Assets.Material, Assets.HighlightMaterial };
		for (int32 i = 0; i < 3; i++)
		{
			if (Paths[i]->IsNull())
			{
				continue;
			}
			if (Loaded[i])
			{
				Resolved++;
			}
			else
			{
				Failed++;
				UE_LOG(LogTemp, Warning, TEXT("[PERF]     NEUSPJEH: %s"), *Paths[i]->ToString());
			}
		}
	}

	bAssetCacheReady = true;

	const double ElapsedMs = (FPlatformTime::Seconds() - Start) * 1000.0;
	UE_LOG(LogTemp, Warning, TEXT("[PERF] 1/4 Assets     %8.1f ms   %6d ucitano, %d neuspjelo (%d definicija)"),
		ElapsedMs, Resolved, Failed, AllBlocks.Num());
}

const FBlockAssets& AVoxelWorld::GetBlockAssets(EBlockType Type, const FBlockDefinition& BlockDefinition)
{
	if (const FBlockAssets* Cached = BlockAssetCache.Find(Type))
	{
		return *Cached;
	}

	// Lazy fallback - npr. ako se blok postavlja prije nego je GenerateWorld izgradio cache
	FBlockAssets Assets;
	Assets.Mesh = Cast<UStaticMesh>(BlockDefinition.Mesh.TryLoad());
	Assets.Material = Cast<UMaterialInterface>(BlockDefinition.Material.TryLoad());
	Assets.HighlightMaterial = Cast<UMaterialInterface>(BlockDefinition.HighlightMaterial.TryLoad());
	return BlockAssetCache.Add(Type, Assets);
}

// === ENEMIES ===

void AVoxelWorld::SpawnEnemies()
{
	if (!ZombieClass)
	{
		return;
	}

	// Spawn zombie na nasumičnoj poziciji na površini spawn podrucja
	// (uvijek unutar bootstrap chunkova - teren garantirano postoji)
	const FIntVector SpawnCenter = ChunkOrigin(SpawnCenterChunk) + FIntVector(ChunkSize / 2, ChunkSize / 2, 0);
	int32 RandX = SpawnCenter.X + FMath::RandRange(-20, 20);
	int32 RandY = SpawnCenter.Y + FMath::RandRange(-20, 20);
	int32 SpawnZ = GetTerrainHeightAt(RandX, RandY) + 1;

	FVector WorldPos = GridToWorld(RandX, RandY, SpawnZ);
	// Podignuti malo iznad površine da izbjegnemo collision
	WorldPos.Z += 88.0f;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AZombie* Zombie = GetWorld()->SpawnActor<AZombie>(ZombieClass, WorldPos, FRotator::ZeroRotator, SpawnParams);

	if (Zombie)
	{
		UE_LOG(LogTemp, Log, TEXT("VoxelWorld: Spawned zombie at (%d, %d, %d)"), RandX, RandY, SpawnZ);
	}
}

// === MOBS ===

void AVoxelWorld::SpawnMobs()
{
	if (MobSpawns.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: MobSpawns list is empty, skipping mob spawn"));
		return;
	}

	for (const FMobSpawnEntry& Entry : MobSpawns)
	{
		if (!Entry.MobClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: MobSpawns entry has no class set, skipping"));
			continue;
		}

		int32 SpawnedCount = 0;

		const FIntVector SpawnCenter = ChunkOrigin(SpawnCenterChunk) + FIntVector(ChunkSize / 2, ChunkSize / 2, 0);

		for (int32 i = 0; i < Entry.Count; i++)
		{
			// Random pozicija na površini spawn podrucja (unutar bootstrap chunkova)
			int32 RandX = SpawnCenter.X + FMath::RandRange(-20, 20);
			int32 RandY = SpawnCenter.Y + FMath::RandRange(-20, 20);
			int32 SpawnZ = GetTerrainHeightAt(RandX, RandY) + 1;

			FVector WorldPos = GridToWorld(RandX, RandY, SpawnZ);
			// Podignuti iznad površine da izbjegnemo collision
			WorldPos.Z += 50.0f;

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

			AMobBase* Mob = GetWorld()->SpawnActor<AMobBase>(Entry.MobClass, WorldPos, FRotator::ZeroRotator, SpawnParams);

			if (Mob)
			{
				SpawnedCount++;
			}
		}

		UE_LOG(LogTemp, Log, TEXT("VoxelWorld: Spawned %d/%d %s"),
			SpawnedCount, Entry.Count, *Entry.MobClass->GetName());
	}
}

// === LEAF DECAY ===

void AVoxelWorld::OnLogDestroyed(FIntVector LogPosition)
{
	// Dodaj svo lišće unutar 6 blokova (Manhattan distance) u listu za provjeru
	for (int32 X = -6; X <= 6; X++)
	{
		for (int32 Y = -6; Y <= 6; Y++)
		{
			for (int32 Z = -6; Z <= 6; Z++)
			{
				// Manhattan distance provjera
				if (FMath::Abs(X) + FMath::Abs(Y) + FMath::Abs(Z) > 6)
				{
					continue;
				}

				FIntVector CheckPos = LogPosition + FIntVector(X, Y, Z);
				EBlockType Type = GetBlockTypeAt(CheckPos);

				if (Type == EBlockType::OakLeaves || Type == EBlockType::BirchLeaves)
				{
					LeavesToCheck.AddUnique(CheckPos);
				}
			}
		}
	}
}

void AVoxelWorld::OnLeafDecayed(FIntVector LeafPosition)
{
	// Kad list propadne, dodaj sve listove unutar Manhattan 6 u listu za provjeru
	// Ovo hvata listove koji su možda već bili provjereni ali su izgubili vezu
	for (int32 X = -6; X <= 6; X++)
	{
		for (int32 Y = -6; Y <= 6; Y++)
		{
			for (int32 Z = -6; Z <= 6; Z++)
			{
				if (FMath::Abs(X) + FMath::Abs(Y) + FMath::Abs(Z) > 6)
				{
					continue;
				}

				FIntVector CheckPos = LeafPosition + FIntVector(X, Y, Z);
				EBlockType Type = GetBlockTypeAt(CheckPos);

				if (Type == EBlockType::OakLeaves || Type == EBlockType::BirchLeaves)
				{
					LeavesToCheck.AddUnique(CheckPos);
				}
			}
		}
	}
}

void AVoxelWorld::ProcessLeafDecay()
{
	if (LeavesToCheck.Num() == 0)
	{
		return;
	}

	// Procesiraj batch listova po tikcu (performance)
	int32 BatchSize = FMath::Min(10, LeavesToCheck.Num());

	for (int32 i = 0; i < BatchSize; i++)
	{
		FIntVector LeafPos = LeavesToCheck[0];
		LeavesToCheck.RemoveAt(0);

		// Provjeri je li još uvijek lišće (u podacima)
		EBlockType Type = GetBlockTypeAt(LeafPos);
		if (Type != EBlockType::OakLeaves && Type != EBlockType::BirchLeaves)
		{
			continue;
		}

		// Provjeri ima li vezu s logom
		if (!HasLogConnection(LeafPos))
		{
			// Nema veze - decay: instant unistenje s drop sansom (sapling)
			// iz registry definicije, bez promoviranja u actor
			DestroyBlockAt(LeafPos);
		}
	}
}

bool AVoxelWorld::HasLogConnection(FIntVector LeafPosition) const
{
	// BFS pretraga za log unutar 6 blokova Manhattan distance
	TSet<FIntVector> Visited;
	TArray<FIntVector> Queue;
	Queue.Add(LeafPosition);

	while (Queue.Num() > 0)
	{
		FIntVector Current = Queue[0];
		Queue.RemoveAt(0);

		if (Visited.Contains(Current))
		{
			continue;
		}
		Visited.Add(Current);

		// Provjeri Manhattan distance od početne pozicije
		int32 Distance = FMath::Abs(Current.X - LeafPosition.X) +
		                 FMath::Abs(Current.Y - LeafPosition.Y) +
		                 FMath::Abs(Current.Z - LeafPosition.Z);
		if (Distance > 6)
		{
			continue;
		}

		// BFS je dotakao neucitan chunk - prekini konzervativno kao "ima vezu"
		// (plan 3.8): decay je kozmetika, bolje lazno "povezano" nego rupa u
		// krosnji cije je deblo iza granice ucitanog svijeta.
		const FVoxelChunk* Chunk = FindChunk(GridToChunk(Current));
		if (!Chunk || Chunk->State == EChunkState::Generating)
		{
			return true;
		}

		EBlockType Type = GetBlockTypeAt(Current);
		if (Type == EBlockType::Air)
		{
			continue;
		}

		// Pronašli smo log - lišće je podržano
		if (Type == EBlockType::OakLog || Type == EBlockType::BirchLog)
		{
			return true;
		}

		// Nastavi BFS kroz lišće
		if (Type == EBlockType::OakLeaves || Type == EBlockType::BirchLeaves)
		{
			// Dodaj 6 susjeda
			Queue.Add(Current + FIntVector(1, 0, 0));
			Queue.Add(Current + FIntVector(-1, 0, 0));
			Queue.Add(Current + FIntVector(0, 1, 0));
			Queue.Add(Current + FIntVector(0, -1, 0));
			Queue.Add(Current + FIntVector(0, 0, 1));
			Queue.Add(Current + FIntVector(0, 0, -1));
		}
	}

	return false;
}
