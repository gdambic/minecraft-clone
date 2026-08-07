#include "VoxelWorld.h"
#include "Block.h"
#include "TreeGenerator.h"
#include "TimerManager.h"
#include "Zombie.h"
#include "Sheep.h"
#include "BlockRegistry.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

AVoxelWorld::AVoxelWorld()
{
	PrimaryActorTick.bCanEverTick = false;

	// Default vrijednosti - mogu se override-ati u BP_VoxelWorld
	WorldSizeX = 100;
	WorldSizeY = 100;
	WorldSizeZ = 10;
	BlockSize = 100.0f;
	RandomBlockCount = 20;
	SurfaceLevel = 3;
	TreeCount = 10;
}

void AVoxelWorld::BeginPlay()
{
	Super::BeginPlay();
	GenerateWorld();
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

	// === [PERF] Početak mjerenja - vidi Docs/PLAN_UbrzanjePokretanja.md, sekcija 2 ===
	UE_LOG(LogTemp, Warning, TEXT("[PERF] ================================================"));
	UE_LOG(LogTemp, Warning, TEXT("[PERF] GenerateWorld  %dx%d, %d slojeva (SurfaceLevel=%d)"),
		WorldSizeX, WorldSizeY, SurfaceLevel + 1, SurfaceLevel);

	const double T0 = FPlatformTime::Seconds();

	// --- Faza 1: assets (fiksni trosak, NE skalira s velicinom svijeta) ---
	// Razrijesi mesh/materijale JEDNOM i spremi u cache - petlje ispod koriste gotove pointere
	BuildBlockAssetCache();

	const double T1 = FPlatformTime::Seconds();

	// --- Faza 2: teren (skalirajuci trosak) ---
	// PROLAZ 1: samo podaci - izvor istine, bez actora (par ms)
	for (int32 Z = 0; Z <= SurfaceLevel; Z++)
	{
		EBlockType Type = (Z == SurfaceLevel) ? EBlockType::Grass : EBlockType::Stone;
		for (int32 X = 0; X < WorldSizeX; X++)
		{
			for (int32 Y = 0; Y < WorldSizeY; Y++)
			{
				BlockData.Add(FIntVector(X, Y, Z), Type);
			}
		}
	}

	// Random Dirt blokovi iznad površine
	for (int32 i = 0; i < RandomBlockCount; i++)
	{
		int32 RandX = FMath::RandRange(0, WorldSizeX - 1);
		int32 RandY = FMath::RandRange(0, WorldSizeY - 1);
		BlockData.Add(FIntVector(RandX, RandY, SurfaceLevel + 1), EBlockType::Dirt);
	}

	// PROLAZ 2: actori SAMO za izlozene blokove (zakopani ostaju samo podaci)
	for (const TPair<FIntVector, EBlockType>& Pair : BlockData)
	{
		if (IsBlockExposed(Pair.Key))
		{
			EnsureBlockActor(Pair.Key);
		}
	}

	const double T2 = FPlatformTime::Seconds();
	const int32 TerrainBlocks = BlockData.Num();
	const int32 TerrainActors = Blocks.Num();
	const double TerrainMs = (T2 - T1) * 1000.0;
	UE_LOG(LogTemp, Warning, TEXT("[PERF] 2/4 Teren      %8.1f ms   %6d blokova (%d actora, %d lazy)   %.4f ms/blok"),
		TerrainMs, TerrainBlocks, TerrainActors, TerrainBlocks - TerrainActors,
		TerrainBlocks > 0 ? TerrainMs / TerrainBlocks : 0.0);

	UE_LOG(LogTemp, Log, TEXT("VoxelWorld: Generated %d layers (0-%d) + %d random blocks"),
		SurfaceLevel + 1, SurfaceLevel, RandomBlockCount);

	// --- Faza 3: stabla ---
	GenerateTrees();

	const double T3 = FPlatformTime::Seconds();
	const int32 TreeBlocks = BlockData.Num() - TerrainBlocks;
	const double TreesMs = (T3 - T2) * 1000.0;
	UE_LOG(LogTemp, Warning, TEXT("[PERF] 3/4 Stabla     %8.1f ms   %6d blokova   %.4f ms/blok"),
		TreesMs, TreeBlocks, TreeBlocks > 0 ? TreesMs / TreeBlocks : 0.0);

	// --- Faza 4: mobovi ---
	// Spawn enemies
	SpawnEnemies();

	// Spawn passive mobs
	SpawnMobs();

	const double T4 = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Warning, TEXT("[PERF] 4/4 Mobovi     %8.1f ms"), (T4 - T3) * 1000.0);

	// --- Sazetak ---
	const double AssetMs = (T1 - T0) * 1000.0;
	const double SpawnMs = (T4 - T1) * 1000.0;
	const double TotalMs = (T4 - T0) * 1000.0;
	UE_LOG(LogTemp, Warning, TEXT("[PERF] ------------------------------------------------"));
	UE_LOG(LogTemp, Warning, TEXT("[PERF] UKUPNO        %8.1f ms   %6d blokova (%d actora)"),
		TotalMs, BlockData.Num(), Blocks.Num());
	UE_LOG(LogTemp, Warning, TEXT("[PERF]   fiksni (assets)      %8.1f ms  (%.0f%%)"),
		AssetMs, TotalMs > 0.0 ? AssetMs / TotalMs * 100.0 : 0.0);
	UE_LOG(LogTemp, Warning, TEXT("[PERF]   skalirajuci (spawn)  %8.1f ms  (%.0f%%)"),
		SpawnMs, TotalMs > 0.0 ? SpawnMs / TotalMs * 100.0 : 0.0);
	UE_LOG(LogTemp, Warning, TEXT("[PERF] ================================================"));

	// Pokreni timer za leaf decay (svakih 2.5 sekundi)
	GetWorld()->GetTimerManager().SetTimer(
		LeafDecayTimerHandle,
		this,
		&AVoxelWorld::ProcessLeafDecay,
		2.5f,
		true // Looping
	);
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

		// Donju stranu svijeta nitko ne vidi - tretiraj kao cvrsto.
		// Bocne strane (X/Y izvan granica) namjerno prolaze: nema unosa
		// u BlockData pa se racunaju kao izlozene (rub svijeta se vidi izvana).
		if (N.Z < 0)
		{
			continue;
		}

		if (!BlockData.Contains(N))
		{
			return true;
		}
	}

	return false;
}

ABlock* AVoxelWorld::EnsureBlockActor(FIntVector Pos)
{
	if (ABlock* Existing = Blocks.FindRef(Pos))
	{
		return Existing;
	}

	const EBlockType* Type = BlockData.Find(Pos);
	if (!Type)
	{
		return nullptr;
	}

	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	if (!Registry)
	{
		return nullptr;
	}

	const FBlockDefinition* BlockDefinition = Registry->GetBlockDefinition(*Type);
	if (!BlockDefinition)
	{
		UE_LOG(LogTemp, Warning, TEXT("BlockRegistry: No definition for block type %d"), (int32)*Type);
		return nullptr;
	}

	// Gotovi pointeri iz cachea - bez TryLoad-a po bloku
	const FBlockAssets& BlockAssets = GetBlockAssets(*Type, *BlockDefinition);

	FVector WorldPos = GridToWorld(Pos.X, Pos.Y, Pos.Z);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;

	// Spawn generički ABlock i inicijaliziraj iz registry-a
	ABlock* NewBlock = GetWorld()->SpawnActor<ABlock>(ABlock::StaticClass(), WorldPos, FRotator::ZeroRotator, SpawnParams);

	if (NewBlock)
	{
		NewBlock->SetGridPosition(Pos);
		NewBlock->InitializeFromRegistry(*Type, *BlockDefinition,
			BlockAssets.Mesh, BlockAssets.Material, BlockAssets.HighlightMaterial);
		Blocks.Add(Pos, NewBlock);
	}

	return NewBlock;
}

void AVoxelWorld::NotifyBlockDestroyed(FIntVector GridPosition, EBlockType DestroyedType)
{
	BlockData.Remove(GridPosition);

	if (ABlock* Actor = Blocks.FindRef(GridPosition))
	{
		Blocks.Remove(GridPosition);
		Actor->Destroy();
	}

	// Susjedi su mozda upravo postali izlozeni -> lazy spawn
	// (no-op za pozicije bez podataka ili s vec spawnanim actorom)
	static const FIntVector Neighbors[6] = {
		FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
		FIntVector(0, 1, 0), FIntVector(0, -1, 0),
		FIntVector(0, 0, 1), FIntVector(0, 0, -1)
	};
	for (const FIntVector& Offset : Neighbors)
	{
		EnsureBlockActor(GridPosition + Offset);
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

EBlockType AVoxelWorld::GetBlockTypeAt(FIntVector GridPosition) const
{
	const EBlockType* Found = BlockData.Find(GridPosition);
	return Found ? *Found : EBlockType::Air;
}

void AVoxelWorld::SetBlockType(int32 X, int32 Y, int32 Z, EBlockType NewType)
{
	FIntVector GridPos(X, Y, Z);

	if (NewType == EBlockType::Air)
	{
		// Air = odsutnost unosa - ukloni podatke i actor
		BlockData.Remove(GridPos);
		if (ABlock* Actor = Blocks.FindRef(GridPos))
		{
			Blocks.Remove(GridPos);
			Actor->Destroy();
		}
		return;
	}

	BlockData.Add(GridPos, NewType);

	if (ABlock* Actor = Blocks.FindRef(GridPos))
	{
		Actor->SetBlockType(NewType);
	}
	else if (IsBlockExposed(GridPos))
	{
		EnsureBlockActor(GridPos);
	}
}

FVector AVoxelWorld::GridToWorld(int32 X, int32 Y, int32 Z)
{
	// X i Y su širina/duljina, Z je visina
	return GetActorLocation() + FVector(X * BlockSize, Y * BlockSize, Z * BlockSize);
}

ABlock* AVoxelWorld::PlaceBlockAt(FIntVector GridPosition, EBlockType Type)
{
	// Zauzetost se provjerava u podacima (Air actori vise ne postoje)
	if (BlockData.Contains(GridPosition))
	{
		return nullptr;
	}

	BlockData.Add(GridPosition, Type);

	// Postavljeni blok je po definiciji izlozen - odmah spawnaj actor
	ABlock* NewBlock = EnsureBlockActor(GridPosition);
	if (!NewBlock)
	{
		// Npr. nema registry definicije - ne ostavljaj "duh" podatke bez actora
		BlockData.Remove(GridPosition);
		UE_LOG(LogTemp, Error, TEXT("PlaceBlockAt: Failed to spawn block type %d at (%d,%d,%d)"),
			(int32)Type, GridPosition.X, GridPosition.Y, GridPosition.Z);
	}

	return NewBlock;
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
	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	if (!Registry)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PERF] 1/4 Assets     -- registry nedostupan --"));
		return;
	}

	const TArray<FBlockDefinition> AllBlocks = Registry->GetAllBlockDefinitions();

	const double Start = FPlatformTime::Seconds();

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

// === TREES ===

void AVoxelWorld::GenerateTrees()
{
	FTreeGenerator::GenerateRandomTrees(this, TreeCount, WorldSizeX, WorldSizeY, SurfaceLevel);
}

// === ENEMIES ===

void AVoxelWorld::SpawnEnemies()
{
	if (!ZombieClass)
	{
		return;
	}

	// Spawn zombie na nasumičnoj poziciji na površini
	int32 RandX = FMath::RandRange(10, WorldSizeX - 10);
	int32 RandY = FMath::RandRange(10, WorldSizeY - 10);
	int32 SpawnZ = SurfaceLevel + 1;
	UE_LOG(LogTemp, Warning, TEXT("DEBUG SpawnEnemies: SpawnZ = %d"), SpawnZ);

	FVector WorldPos = GridToWorld(RandX, RandY, SpawnZ);
	UE_LOG(LogTemp, Warning, TEXT("DEBUG SpawnEnemies: WorldPos after GridToWorld = %s"), *WorldPos.ToString());
	// Podignuti malo iznad površine da izbjegnemo collision
	WorldPos.Z += 88.0f;
	UE_LOG(LogTemp, Warning, TEXT("DEBUG SpawnEnemies: WorldPos after +50 = %s"), *WorldPos.ToString());

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
	if (!SheepClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: SheepClass not set, skipping mob spawn"));
		return;
	}

	int32 SpawnZ = SurfaceLevel + 1;

	for (int32 i = 0; i < SheepCount; i++)
	{
		// Random pozicija na površini (izbjegavaj rubove)
		int32 RandX = FMath::RandRange(10, WorldSizeX - 10);
		int32 RandY = FMath::RandRange(10, WorldSizeY - 10);

		FVector WorldPos = GridToWorld(RandX, RandY, SpawnZ);
		// Podignuti iznad površine da izbjegnemo collision
		WorldPos.Z += 50.0f;

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

		ASheep* Sheep = GetWorld()->SpawnActor<ASheep>(SheepClass, WorldPos, FRotator::ZeroRotator, SpawnParams);

		if (Sheep)
		{
			UE_LOG(LogTemp, Log, TEXT("VoxelWorld: Spawned sheep %d at (%d, %d, %d)"), i + 1, RandX, RandY, SpawnZ);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("VoxelWorld: Spawned %d sheep"), SheepCount);
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
			// Nema veze - decay (instant uništenje koje može dropati sapling).
			// Lišće je praktički uvijek izloženo pa actor već postoji;
			// EnsureBlockActor pokriva i teoretski zakopan list.
			ABlock* LeafBlock = EnsureBlockActor(LeafPos);
			if (LeafBlock)
			{
				LeafBlock->AddDestroyProgress(LeafBlock->TimeToDestroy);
			}
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
