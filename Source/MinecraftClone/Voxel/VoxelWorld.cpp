#include "VoxelWorld.h"
#include "Block.h"
#include "TreeGenerator.h"
#include "TimerManager.h"
#include "Zombie.h"
#include "BlockRegistry.h"

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

	// Generiraj teren od Z=0 do SurfaceLevel
	for (int32 Z = 0; Z <= SurfaceLevel; Z++)
	{
		EBlockType Type = (Z == SurfaceLevel) ? EBlockType::Grass : EBlockType::Stone;
		for (int32 X = 0; X < WorldSizeX; X++)
		{
			for (int32 Y = 0; Y < WorldSizeY; Y++)
			{
				SpawnBlock(X, Y, Z, Type);
			}
		}
	}

	// Random Dirt blokovi iznad površine
	for (int32 i = 0; i < RandomBlockCount; i++)
	{
		int32 RandX = FMath::RandRange(0, WorldSizeX - 1);
		int32 RandY = FMath::RandRange(0, WorldSizeY - 1);
		SpawnBlock(RandX, RandY, SurfaceLevel + 1, EBlockType::Dirt);
	}

	UE_LOG(LogTemp, Log, TEXT("VoxelWorld: Generated %d layers (0-%d) + %d random blocks"),
		SurfaceLevel + 1, SurfaceLevel, RandomBlockCount);

	// Generiraj stabla
	GenerateTrees();

	// Spawn enemies
	SpawnEnemies();

	// Pokreni timer za leaf decay (svakih 2.5 sekundi)
	GetWorld()->GetTimerManager().SetTimer(
		LeafDecayTimerHandle,
		this,
		&AVoxelWorld::ProcessLeafDecay,
		2.5f,
		true // Looping
	);
}

void AVoxelWorld::SpawnBlock(int32 X, int32 Y, int32 Z, EBlockType Type)
{
	FIntVector GridPos(X, Y, Z);

	// Provjeri postoji li već blok na toj poziciji
	if (Blocks.Contains(GridPos))
	{
		return;
	}

	// Provjeri ima li registry definiciju za ovaj tip
	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	if (!Registry || !Registry->GetBlockDefinition(Type))
	{
		UE_LOG(LogTemp, Warning, TEXT("BlockRegistry: No definition for block type %d"), (int32)Type);
		return;
	}

	FVector WorldPos = GridToWorld(X, Y, Z);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;

	// Spawn generički ABlock i inicijaliziraj iz registry-a
	ABlock* NewBlock = GetWorld()->SpawnActor<ABlock>(ABlock::StaticClass(), WorldPos, FRotator::ZeroRotator, SpawnParams);

	if (NewBlock)
	{
		NewBlock->SetGridPosition(GridPos);
		NewBlock->InitializeFromRegistry(Type);
		Blocks.Add(GridPos, NewBlock);
	}
}

ABlock* AVoxelWorld::GetBlock(int32 X, int32 Y, int32 Z)
{
	FIntVector GridPos(X, Y, Z);
	ABlock** FoundBlock = Blocks.Find(GridPos);
	return FoundBlock ? *FoundBlock : nullptr;
}

void AVoxelWorld::SetBlockType(int32 X, int32 Y, int32 Z, EBlockType NewType)
{
	ABlock* Block = GetBlock(X, Y, Z);
	if (Block)
	{
		Block->SetBlockType(NewType);
	}
}

FVector AVoxelWorld::GridToWorld(int32 X, int32 Y, int32 Z)
{
	// X i Y su širina/duljina, Z je visina
	return GetActorLocation() + FVector(X * BlockSize, Y * BlockSize, Z * BlockSize);
}

ABlock* AVoxelWorld::PlaceBlockAt(FIntVector GridPosition, EBlockType Type)
{
	// Provjeri ima li registry definiciju za ovaj tip
	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	if (!Registry || !Registry->GetBlockDefinition(Type))
	{
		UE_LOG(LogTemp, Error, TEXT("PlaceBlockAt: No registry definition for type %d at (%d,%d,%d)"),
			(int32)Type, GridPosition.X, GridPosition.Y, GridPosition.Z);
		return nullptr;
	}

	// Provjeri postoji li već blok na toj poziciji
	if (Blocks.Contains(GridPosition))
	{
		// Ako postoji Air blok, uništi ga i spawnaj novi s pravim tipom
		ABlock* ExistingBlock = Blocks[GridPosition];
		if (ExistingBlock && ExistingBlock->BlockType == EBlockType::Air)
		{
			Blocks.Remove(GridPosition);
			ExistingBlock->Destroy();
		}
		else
		{
			return nullptr;
		}
	}

	// Spawnaj generički ABlock i inicijaliziraj iz registry-a
	FVector WorldPos = GridToWorld(GridPosition.X, GridPosition.Y, GridPosition.Z);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;

	ABlock* NewBlock = GetWorld()->SpawnActor<ABlock>(ABlock::StaticClass(), WorldPos, FRotator::ZeroRotator, SpawnParams);

	if (NewBlock)
	{
		NewBlock->SetGridPosition(GridPosition);
		NewBlock->InitializeFromRegistry(Type);
		Blocks.Add(GridPosition, NewBlock);
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
				ABlock* Block = GetBlock(CheckPos.X, CheckPos.Y, CheckPos.Z);

				if (Block && (Block->BlockType == EBlockType::OakLeaves ||
				              Block->BlockType == EBlockType::BirchLeaves))
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
				ABlock* Block = GetBlock(CheckPos.X, CheckPos.Y, CheckPos.Z);

				if (Block && (Block->BlockType == EBlockType::OakLeaves ||
				              Block->BlockType == EBlockType::BirchLeaves))
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

		ABlock* LeafBlock = GetBlock(LeafPos.X, LeafPos.Y, LeafPos.Z);
		if (!LeafBlock)
		{
			continue;
		}

		// Provjeri je li još uvijek lišće
		if (LeafBlock->BlockType != EBlockType::OakLeaves &&
		    LeafBlock->BlockType != EBlockType::BirchLeaves)
		{
			continue;
		}

		// Provjeri ima li vezu s logom
		if (!HasLogConnection(LeafPos))
		{
			// Nema veze - decay (instant uništenje koje može dropati sapling)
			LeafBlock->AddDestroyProgress(LeafBlock->TimeToDestroy);
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

		ABlock* Block = const_cast<AVoxelWorld*>(this)->GetBlock(Current.X, Current.Y, Current.Z);
		if (!Block || Block->BlockType == EBlockType::Air)
		{
			continue;
		}

		// Pronašli smo log - lišće je podržano
		if (Block->BlockType == EBlockType::OakLog || Block->BlockType == EBlockType::BirchLog)
		{
			return true;
		}

		// Nastavi BFS kroz lišće
		if (Block->BlockType == EBlockType::OakLeaves || Block->BlockType == EBlockType::BirchLeaves)
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
