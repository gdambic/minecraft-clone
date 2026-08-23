#pragma once

#include "CoreMinimal.h"
#include "BlockType.h"

class AVoxelWorld;

/** Tip stabla */
UENUM(BlueprintType)
enum class ETreeType : uint8
{
	Oak,
	Birch
};

/**
 * Utility klasa za generiranje stabala u voxel svijetu
 */
class MINECRAFTCLONE_API FTreeGenerator
{
public:
	/** Generiraj stablo na zadanoj grid poziciji */
	static void GenerateTree(AVoxelWorld* World, FIntVector BasePosition, ETreeType TreeType);

	/** Generiraj više stabala na random pozicijama (visina tla po stupcu ide preko World->GetTerrainHeightAt) */
	static void GenerateRandomTrees(AVoxelWorld* World, int32 TreeCount, int32 WorldSizeX, int32 WorldSizeY);

private:
	/** Generiraj hrast (4-6 blokova deblo) */
	static void GenerateOakTree(AVoxelWorld* World, FIntVector BasePosition);

	/** Generiraj brezu (5-7 blokova deblo) */
	static void GenerateBirchTree(AVoxelWorld* World, FIntVector BasePosition);

	/** Generiraj krošnju (5x5 bez kutova) */
	static void GenerateCanopy(AVoxelWorld* World, FIntVector CenterPosition, EBlockType LeafType, int32 Radius);

	/** Provjeri može li se stablo postaviti na poziciju */
	static bool CanPlaceTreeAt(AVoxelWorld* World, FIntVector Position, int32 TrunkHeight);
};
