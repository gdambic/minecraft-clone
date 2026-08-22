#include "VoxelWorld.h"
#include "Block.h"
#include "ItemDrop.h"
#include "TreeGenerator.h"
#include "TimerManager.h"
#include "Zombie.h"
#include "MobBase.h"
#include "BlockRegistry.h"
#include "AI/NavigationSystemBase.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/HitResult.h"
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

	// Showcase red: po jedan blok svake definicije uz rub svijeta (prije
	// prolaza 2 da instance dobiju kroz standardni exposure put)
	PlaceShowcaseBlocks(Registry);

	// PROLAZ 2: ISM instance SAMO za izlozene blokove (zakopani ostaju samo
	// podaci). Batch AddInstances po tipu - jedan poziv umjesto tisuca.
	TMap<EBlockType, TArray<FIntVector>> ExposedByType;
	for (const TPair<FIntVector, EBlockType>& Pair : BlockData)
	{
		if (IsBlockExposed(Pair.Key))
		{
			ExposedByType.FindOrAdd(Pair.Value).Add(Pair.Key);
		}
	}

	int32 TerrainInstances = 0;
	for (const TPair<EBlockType, TArray<FIntVector>>& Pair : ExposedByType)
	{
		AddBlockInstancesBatch(Pair.Key, Pair.Value);
		TerrainInstances += Pair.Value.Num();
	}

	const double T2 = FPlatformTime::Seconds();
	const int32 TerrainBlocks = BlockData.Num();
	const double TerrainMs = (T2 - T1) * 1000.0;
	UE_LOG(LogTemp, Warning, TEXT("[PERF] 2/4 Teren      %8.1f ms   %6d blokova (%d instanci, %d lazy)   %.4f ms/blok"),
		TerrainMs, TerrainBlocks, TerrainInstances, TerrainBlocks - TerrainInstances,
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
	int32 TotalInstances = 0;
	for (const TPair<EBlockType, FBlockInstanceSet>& Pair : InstanceSets)
	{
		TotalInstances += Pair.Value.InstanceToGrid.Num();
	}
	UE_LOG(LogTemp, Warning, TEXT("[PERF] ------------------------------------------------"));
	UE_LOG(LogTemp, Warning, TEXT("[PERF] UKUPNO        %8.1f ms   %6d blokova (%d instanci, %d actora)"),
		TotalMs, BlockData.Num(), TotalInstances, Blocks.Num());
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

ABlock* AVoxelWorld::PromoteToActor(FIntVector Pos)
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
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Spawn generički ABlock i inicijaliziraj iz registry-a
	ABlock* NewBlock = GetWorld()->SpawnActor<ABlock>(ABlock::StaticClass(), WorldPos, FRotator::ZeroRotator, SpawnParams);

	if (NewBlock)
	{
		// Invarijanta: instanca i actor nikad istovremeno - ukloni instancu
		// tek nakon uspjesnog spawna (neuspjeh ne smije "izbrisati" vizual)
		RemoveBlockInstance(Pos);

		NewBlock->SetGridPosition(Pos);
		NewBlock->InitializeFromRegistry(*Type, *BlockDefinition,
			BlockAssets.Mesh, BlockAssets.Material, BlockAssets.HighlightMaterial);
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
	if (const EBlockType* Type = BlockData.Find(Pos))
	{
		AddBlockInstance(Pos, *Type);
	}
}

void AVoxelWorld::AddBlockInstance(FIntVector Pos, EBlockType Type)
{
	FBlockInstanceSet* Set = InstanceSets.Find(Type);
	if (!Set || !Set->Component || Set->GridToInstance.Contains(Pos))
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

	Set->GridToInstance.Add(Pos, Index);
	Set->InstanceToGrid.Add(Pos);
}

void AVoxelWorld::AddBlockInstancesBatch(EBlockType Type, const TArray<FIntVector>& Positions)
{
	FBlockInstanceSet* Set = InstanceSets.Find(Type);
	if (!Set || !Set->Component || Positions.Num() == 0)
	{
		return;
	}

	TArray<FTransform> Transforms;
	Transforms.Reserve(Positions.Num());
	for (const FIntVector& Pos : Positions)
	{
		Transforms.Add(FTransform(GridToWorld(Pos.X, Pos.Y, Pos.Z)));
	}

	Set->Component->AddInstances(Transforms, /*bShouldReturnIndices=*/false, /*bWorldSpace=*/true);

	for (const FIntVector& Pos : Positions)
	{
		Set->GridToInstance.Add(Pos, Set->InstanceToGrid.Num());
		Set->InstanceToGrid.Add(Pos);
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
	for (TPair<EBlockType, FBlockInstanceSet>& Pair : InstanceSets)
	{
		FBlockInstanceSet& Set = Pair.Value;
		const int32* Found = Set.GridToInstance.Find(Pos);
		if (!Found)
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
	const EBlockType* Type = BlockData.Find(Pos);
	if (!Type || Blocks.Contains(Pos))
	{
		return;
	}

	// No-op ako instanca vec postoji
	AddBlockInstance(Pos, *Type);
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
	const UPrimitiveComponent* HitComponent = Hit.GetComponent();
	for (const TPair<EBlockType, FBlockInstanceSet>& Pair : InstanceSets)
	{
		if (Pair.Value.Component != HitComponent)
		{
			continue;
		}
		if (!Pair.Value.InstanceToGrid.IsValidIndex(Hit.Item))
		{
			return false;
		}
		OutPos = Pair.Value.InstanceToGrid[Hit.Item];
		OutType = Pair.Key;
		return true;
	}

	return false;
}

void AVoxelWorld::DestroyBlockAt(FIntVector Pos)
{
	const EBlockType* Found = BlockData.Find(Pos);
	if (!Found)
	{
		return;
	}
	const EBlockType Type = *Found;

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
	BlockData.Remove(GridPosition);

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
		// Air = odsutnost unosa - ukloni podatke i vizual
		BlockData.Remove(GridPos);
		RemoveBlockInstance(GridPos);
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
	// Zauzetost se provjerava u podacima (Air actori vise ne postoje)
	if (BlockData.Contains(GridPosition))
	{
		return false;
	}

	// Pokriva pozive prije GenerateWorld (no-op nakon prvog uspjesnog builda)
	BuildBlockAssetCache();

	// Bez ISM seta (nema registry definicije/mesha) ne ostavljaj "duh" podatke
	const FBlockInstanceSet* Set = InstanceSets.Find(Type);
	if (!Set || !Set->Component)
	{
		UE_LOG(LogTemp, Error, TEXT("PlaceBlockAt: No instance set for block type %d at (%d,%d,%d)"),
			(int32)Type, GridPosition.X, GridPosition.Y, GridPosition.Z);
		return false;
	}

	BlockData.Add(GridPosition, Type);

	// Postavljeni blok je po definiciji izlozen - odmah dodaj instancu
	AddBlockInstance(GridPosition, Type);
	return true;
}

void AVoxelWorld::PlaceShowcaseBlocks(UBlockRegistry* Registry)
{
	TArray<FBlockDefinition> Definitions = Registry->GetAllBlockDefinitions();

	// TMap redoslijed nije garantiran - sortiraj po enum vrijednosti
	Definitions.Sort([](const FBlockDefinition& A, const FBlockDefinition& B)
	{
		return (uint8)A.BlockType < (uint8)B.BlockType;
	});

	const int32 Z = SurfaceLevel + 1;
	int32 X = 0;
	for (const FBlockDefinition& Def : Definitions)
	{
		if (X >= WorldSizeX)
		{
			UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: showcase red ne stane u WorldSizeX=%d - preskacem ostatak"), WorldSizeX);
			break;
		}
		// Add gazi eventualni random Dirt na istoj poziciji
		BlockData.Add(FIntVector(X, 0, Z), Def.BlockType);
		++X;
	}

	UE_LOG(LogTemp, Log, TEXT("VoxelWorld: showcase red - %d blokova uz rub Y=0 na Z=%d"), X, Z);
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

	// ISM komponente se attachaju na root - osiguraj da postoji
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

		// Jedna ISM komponenta po tipu bloka - vizual svih instanci tog tipa
		if (Assets.Mesh)
		{
			const FString CompName = FString::Printf(TEXT("ISM_%s"),
				*StaticEnum<EBlockType>()->GetNameStringByValue((int64)Def.BlockType));
			UInstancedStaticMeshComponent* ISM =
				NewObject<UInstancedStaticMeshComponent>(this, FName(*CompName));
			ISM->SetStaticMesh(Assets.Mesh);
			if (Assets.Material)
			{
				ISM->SetMaterial(0, Assets.Material);
			}
			ISM->SetMobility(GetRootComponent()->Mobility);
			ISM->SetCollisionProfileName(TEXT("BlockAll"));
			ISM->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			ISM->SetCanEverAffectNavigation(true);
			// RemoveInstance u 5.6 po defaultu radi order-preserving RemoveAt;
			// bookkeeping u RemoveBlockInstance racuna na RemoveAtSwap semantiku
			ISM->SetRemoveSwap();
			ISM->SetupAttachment(GetRootComponent());
			ISM->RegisterComponent();

			InstanceSets.Add(Def.BlockType).Component = ISM;
		}

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
	UE_LOG(LogTemp, Warning, TEXT("[PERF] 1/4 Assets     %8.1f ms   %6d ucitano, %d neuspjelo (%d definicija, %d ISM komponenti)"),
		ElapsedMs, Resolved, Failed, AllBlocks.Num(), InstanceSets.Num());
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
	if (MobSpawns.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: MobSpawns list is empty, skipping mob spawn"));
		return;
	}

	int32 SpawnZ = SurfaceLevel + 1;

	for (const FMobSpawnEntry& Entry : MobSpawns)
	{
		if (!Entry.MobClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: MobSpawns entry has no class set, skipping"));
			continue;
		}

		int32 SpawnedCount = 0;

		for (int32 i = 0; i < Entry.Count; i++)
		{
			// Random pozicija na površini (izbjegavaj rubove)
			int32 RandX = FMath::RandRange(10, WorldSizeX - 10);
			int32 RandY = FMath::RandRange(10, WorldSizeY - 10);

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
