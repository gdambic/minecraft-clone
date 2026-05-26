#include "VoxelWorld.h"
#include "Block.h"
#include "TreeGenerator.h"
#include "TimerManager.h"

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
	// Provjeri jesu li BlockClasses postavljeni
	if (BlockClasses.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelWorld: BlockClasses map is empty! Please configure it in BP_VoxelWorld."));
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

	// Dohvati odgovarajući BlockClass za ovaj tip
	TSubclassOf<ABlock>* FoundClass = BlockClasses.Find(Type);
	if (!FoundClass || !*FoundClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("BlockClass not set for type %d"), (int32)Type);
		return;
	}

	FVector WorldPos = GridToWorld(X, Y, Z);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;

	// Koristi odgovarajući BlockClass za tip bloka
	ABlock* NewBlock = GetWorld()->SpawnActor<ABlock>(*FoundClass, WorldPos, FRotator::ZeroRotator, SpawnParams);

	if (NewBlock)
	{
		NewBlock->SetGridPosition(GridPos);
		NewBlock->SetBlockType(Type);
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
	// Dohvati odgovarajući BlockClass za ovaj tip
	TSubclassOf<ABlock>* FoundClass = BlockClasses.Find(Type);
	if (!FoundClass || !*FoundClass)
	{
		UE_LOG(LogTemp, Error, TEXT("PlaceBlockAt: BlockClass NOT FOUND for type %d at (%d,%d,%d)"),
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

	// Spawnaj novi blok s odgovarajućim Blueprintom
	FVector WorldPos = GridToWorld(GridPosition.X, GridPosition.Y, GridPosition.Z);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;

	ABlock* NewBlock = GetWorld()->SpawnActor<ABlock>(*FoundClass, WorldPos, FRotator::ZeroRotator, SpawnParams);

	if (NewBlock)
	{
		NewBlock->SetGridPosition(GridPosition);
		NewBlock->SetBlockType(Type);
		Blocks.Add(GridPosition, NewBlock);
	}

	return NewBlock;
}

TArray<EBlockType> AVoxelWorld::GetPlaceableBlockTypes() const
{
	TArray<EBlockType> Result;
	for (const auto& Pair : BlockClasses)
	{
		if (Pair.Key != EBlockType::Air && Pair.Value)
		{
			Result.Add(Pair.Key);
		}
	}
	return Result;
}

// === TREES ===

void AVoxelWorld::GenerateTrees()
{
	FTreeGenerator::GenerateRandomTrees(this, TreeCount, WorldSizeX, WorldSizeY, SurfaceLevel);
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
