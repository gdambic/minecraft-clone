#include "TreeGenerator.h"
#include "VoxelWorld.h"
#include "Block.h"

void FTreeGenerator::GenerateTree(AVoxelWorld* World, FIntVector BasePosition, ETreeType TreeType)
{
	if (!World)
	{
		return;
	}

	switch (TreeType)
	{
	case ETreeType::Oak:
		GenerateOakTree(World, BasePosition);
		break;
	case ETreeType::Birch:
		GenerateBirchTree(World, BasePosition);
		break;
	}
}

void FTreeGenerator::GenerateOakTree(AVoxelWorld* World, FIntVector BasePosition)
{
	// Oak: 4-6 blokova deblo
	int32 TrunkHeight = FMath::RandRange(4, 6);

	// Provjeri može li se postaviti
	if (!CanPlaceTreeAt(World, BasePosition, TrunkHeight))
	{
		return;
	}

	// Generiraj deblo
	for (int32 Z = 0; Z < TrunkHeight; Z++)
	{
		FIntVector LogPos = BasePosition + FIntVector(0, 0, Z);
		ABlock* PlacedBlock = World->PlaceBlockAt(LogPos, EBlockType::OakLog);
		if (!PlacedBlock)
		{
			UE_LOG(LogTemp, Warning, TEXT("TreeGenerator: Failed to place OakLog at (%d, %d, %d)"),
				LogPos.X, LogPos.Y, LogPos.Z);
		}
	}

	// Generiraj krošnju
	// Donja 2 sloja krošnje (5x5 bez kutova)
	FIntVector CanopyCenter = BasePosition + FIntVector(0, 0, TrunkHeight - 2);
	GenerateCanopy(World, CanopyCenter, EBlockType::OakLeaves, 2);
	GenerateCanopy(World, CanopyCenter + FIntVector(0, 0, 1), EBlockType::OakLeaves, 2);

	// Gornja 2 sloja (manji radius)
	GenerateCanopy(World, CanopyCenter + FIntVector(0, 0, 2), EBlockType::OakLeaves, 1);
	GenerateCanopy(World, CanopyCenter + FIntVector(0, 0, 3), EBlockType::OakLeaves, 1);
}

void FTreeGenerator::GenerateBirchTree(AVoxelWorld* World, FIntVector BasePosition)
{
	// Birch: 5-7 blokova deblo
	int32 TrunkHeight = FMath::RandRange(5, 7);

	if (!CanPlaceTreeAt(World, BasePosition, TrunkHeight))
	{
		return;
	}

	// Generiraj deblo
	for (int32 Z = 0; Z < TrunkHeight; Z++)
	{
		FIntVector LogPos = BasePosition + FIntVector(0, 0, Z);
		World->PlaceBlockAt(LogPos, EBlockType::BirchLog);
	}

	// Generiraj krošnju (slično kao oak)
	FIntVector CanopyCenter = BasePosition + FIntVector(0, 0, TrunkHeight - 2);
	GenerateCanopy(World, CanopyCenter, EBlockType::BirchLeaves, 2);
	GenerateCanopy(World, CanopyCenter + FIntVector(0, 0, 1), EBlockType::BirchLeaves, 2);
	GenerateCanopy(World, CanopyCenter + FIntVector(0, 0, 2), EBlockType::BirchLeaves, 1);
	GenerateCanopy(World, CanopyCenter + FIntVector(0, 0, 3), EBlockType::BirchLeaves, 1);
}

void FTreeGenerator::GenerateCanopy(AVoxelWorld* World, FIntVector CenterPosition, EBlockType LeafType, int32 Radius)
{
	for (int32 X = -Radius; X <= Radius; X++)
	{
		for (int32 Y = -Radius; Y <= Radius; Y++)
		{
			// Preskoči kutove za 5x5 krošnju (kad je Radius=2)
			if (Radius == 2 && FMath::Abs(X) == 2 && FMath::Abs(Y) == 2)
			{
				continue;
			}

			FIntVector LeafPos = CenterPosition + FIntVector(X, Y, 0);

			// Ne prepiši postojeće logove (provjera u podacima)
			if (World->GetBlockTypeAt(LeafPos) != EBlockType::Air)
			{
				continue;
			}

			World->PlaceBlockAt(LeafPos, LeafType);
		}
	}
}

void FTreeGenerator::GenerateRandomTrees(AVoxelWorld* World, int32 TreeCount, int32 WorldSizeX, int32 WorldSizeY, int32 SurfaceLevel)
{
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("TreeGenerator: World is null!"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("TreeGenerator: Starting generation. TreeCount=%d, WorldSize=%dx%d, SurfaceLevel=%d"),
		TreeCount, WorldSizeX, WorldSizeY, SurfaceLevel);

	int32 PlacedTrees = 0;
	int32 FailedAttempts = 0;
	int32 NoGroundCount = 0;
	int32 CantPlaceCount = 0;
	int32 MaxAttempts = TreeCount * 10;

	for (int32 Attempt = 0; Attempt < MaxAttempts && PlacedTrees < TreeCount; Attempt++)
	{
		// Random pozicija (drži se dalje od rubova)
		int32 RandX = FMath::RandRange(3, WorldSizeX - 4);
		int32 RandY = FMath::RandRange(3, WorldSizeY - 4);
		FIntVector TreeBase(RandX, RandY, SurfaceLevel + 1);

		// Provjeri ima li tla ispod (u podacima - radi i za zakopane blokove)
		if (World->GetBlockTypeAt(FIntVector(RandX, RandY, SurfaceLevel)) == EBlockType::Air)
		{
			NoGroundCount++;
			continue;
		}

		// Random tip stabla (50/50)
		ETreeType TreeType = FMath::RandBool() ? ETreeType::Oak : ETreeType::Birch;

		// Pokušaj generirati stablo
		int32 TrunkHeight = (TreeType == ETreeType::Oak) ? FMath::RandRange(4, 6) : FMath::RandRange(5, 7);
		if (CanPlaceTreeAt(World, TreeBase, TrunkHeight))
		{
			GenerateTree(World, TreeBase, TreeType);
			PlacedTrees++;
			UE_LOG(LogTemp, Log, TEXT("TreeGenerator: Placed tree at (%d, %d, %d)"), RandX, RandY, SurfaceLevel + 1);
		}
		else
		{
			CantPlaceCount++;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("TreeGenerator: Finished. Placed=%d, NoGround=%d, CantPlace=%d"),
		PlacedTrees, NoGroundCount, CantPlaceCount);
}

bool FTreeGenerator::CanPlaceTreeAt(AVoxelWorld* World, FIntVector Position, int32 TrunkHeight)
{
	if (!World)
	{
		return false;
	}

	// Provjeri ima li tla ispod (u podacima - radi i za zakopane blokove)
	if (World->GetBlockTypeAt(Position - FIntVector(0, 0, 1)) == EBlockType::Air)
	{
		return false;
	}

	// Prostor iznad površine nije spawnan kao blok, pa je to OK
	// Ne trebamo provjeravati je li prostor slobodan jer iznad površine nema blokova

	return true;
}
