#include "ChunkGenerator.h"
#include "TerrainGenerator.h"
#include "Math/RandomStream.h"

namespace
{
	/** Salt za stream stabala - buduci potrosaci chunk seeda (mob spawn...) koriste druge saltove. */
	constexpr uint32 TreeStreamSalt = 0x7EE5EED5;
}

int32 FChunkGenerator::GetTerrainHeight(const FChunkGenParams& Params, int32 X, int32 Y)
{
	return FTerrainGenerator::GetColumnHeight(X, Y, Params.NoiseOffsetX, Params.NoiseOffsetY,
		Params.NoiseScale, Params.Octaves, Params.SurfaceLevel, Params.HeightAmplitude);
}

TArray<EBlockType> FChunkGenerator::GenerateChunkData(const FChunkGenParams& Params, const FIntPoint& Coord)
{
	using namespace VoxelGrid;

	TArray<EBlockType> Blocks;
	Blocks.SetNumZeroed(BlocksPerChunk);

	// --- 1. Teren: height-map po stupcu (grass na vrhu, dirt ispod, stone dublje) ---
	const FIntVector Origin = ChunkOrigin(Coord);
	for (int32 LocalX = 0; LocalX < ChunkSize; LocalX++)
	{
		for (int32 LocalY = 0; LocalY < ChunkSize; LocalY++)
		{
			const int32 Height = FMath::Min(
				GetTerrainHeight(Params, Origin.X + LocalX, Origin.Y + LocalY), WorldMaxZ);
			for (int32 Z = WorldMinZ; Z <= Height; Z++)
			{
				EBlockType Type;
				if (Z == Height)
				{
					Type = EBlockType::Grass;
				}
				else if (Z > Height - Params.DirtLayers)
				{
					Type = EBlockType::Dirt;
				}
				else
				{
					Type = EBlockType::Stone;
				}
				Blocks[LocalIndex(FIntVector(LocalX, LocalY, Z))] = Type;
			}
		}
	}

	// --- 2. Showcase red (samo chunk (0,0)): po jedan blok svake definicije na (i, 0, h+1) ---
	if (Coord == FIntPoint::ZeroValue)
	{
		const int32 Count = FMath::Min(Params.ShowcaseBlocks.Num(), ChunkSize);
		for (int32 i = 0; i < Count; i++)
		{
			const int32 Z = GetTerrainHeight(Params, i, 0) + 1;
			WriteBlockIfAir(Blocks, Coord, FIntVector(i, 0, Z), Params.ShowcaseBlocks[i]);
		}
		if (Params.ShowcaseBlocks.Num() > ChunkSize)
		{
			UE_LOG(LogTemp, Warning, TEXT("ChunkGenerator: showcase red ima %d definicija, stane %d - preskacem ostatak"),
				Params.ShowcaseBlocks.Num(), ChunkSize);
		}
	}

	// --- 3. Stabla: kandidati ovog chunka + svih 8 susjeda, upis samo blokova unutar ovog chunka ---
	// Redoslijed obilaska je fiksan (Y pa X) pa se preklapanja stabala rjesavaju
	// identicno u svakom chunku koji racuna iste kandidate.
	TArray<FTreeCandidate> Candidates;
	for (int32 DY = -1; DY <= 1; DY++)
	{
		for (int32 DX = -1; DX <= 1; DX++)
		{
			GetTreeCandidates(Params, Coord + FIntPoint(DX, DY), Candidates);
		}
	}
	for (const FTreeCandidate& Tree : Candidates)
	{
		WriteTree(Blocks, Coord, Tree);
	}

	return Blocks;
}

void FChunkGenerator::GetTreeCandidates(const FChunkGenParams& Params, const FIntPoint& Coord,
	TArray<FTreeCandidate>& OutCandidates)
{
	using namespace VoxelGrid;

	const FRandomStream Stream(ChunkSeed(Params.WorldSeed, Coord, TreeStreamSalt));
	const FIntVector Origin = ChunkOrigin(Coord);

	for (int32 Attempt = 0; Attempt < Params.TreeAttemptsPerChunk; Attempt++)
	{
		// Sve odluke se vuku iz streama BEZUVJETNO istim redoslijedom - da
		// preskoceni pokusaj ne pomakne niz sljedecem pokusaju.
		const float Roll = Stream.FRand();
		const int32 LocalX = Stream.RandRange(0, ChunkSize - 1);
		const int32 LocalY = Stream.RandRange(0, ChunkSize - 1);
		const bool bOak = Stream.FRand() < 0.5f;
		const int32 TrunkHeight = bOak ? Stream.RandRange(4, 6) : Stream.RandRange(5, 7);

		if (Roll >= Params.TreeChancePerChunk)
		{
			continue;
		}

		const int32 GroundZ = GetTerrainHeight(Params, Origin.X + LocalX, Origin.Y + LocalY);

		FTreeCandidate Tree;
		Tree.Base = FIntVector(Origin.X + LocalX, Origin.Y + LocalY, GroundZ + 1);
		Tree.Type = bOak ? ETreeType::Oak : ETreeType::Birch;
		Tree.TrunkHeight = TrunkHeight;
		OutCandidates.Add(Tree);
	}
}

void FChunkGenerator::WriteBlockIfAir(TArray<EBlockType>& Blocks, const FIntPoint& Coord,
	const FIntVector& GridPos, EBlockType Type)
{
	using namespace VoxelGrid;

	if (GridToChunk(GridPos) != Coord || !IsValidZ(GridPos.Z))
	{
		return;
	}

	EBlockType& Cell = Blocks[LocalIndex(GridToLocal(GridPos))];
	if (Cell == EBlockType::Air)
	{
		Cell = Type;
	}
}

void FChunkGenerator::WriteTree(TArray<EBlockType>& Blocks, const FIntPoint& Coord, const FTreeCandidate& Tree)
{
	// Oblik identican FTreeGenerator::GenerateOakTree/GenerateBirchTree:
	// deblo TrunkHeight blokova, krosnja 2 sloja radijusa 2 (od vrha debla - 2)
	// pa 2 sloja radijusa 1.
	const EBlockType TrunkType = (Tree.Type == ETreeType::Oak) ? EBlockType::OakLog : EBlockType::BirchLog;
	const EBlockType LeafType = (Tree.Type == ETreeType::Oak) ? EBlockType::OakLeaves : EBlockType::BirchLeaves;

	// Deblo se upisuje PRIJE krosnje (i vlastite i tudje) pa logovi ne gube
	// celije od tudeg lisca: unutar jednog kandidata deblo ide prvo, a medu
	// kandidatima fiksan redoslijed daje isti rezultat u svim chunkovima.
	for (int32 Z = 0; Z < Tree.TrunkHeight; Z++)
	{
		WriteBlockIfAir(Blocks, Coord, Tree.Base + FIntVector(0, 0, Z), TrunkType);
	}

	const FIntVector CanopyCenter = Tree.Base + FIntVector(0, 0, Tree.TrunkHeight - 2);
	WriteCanopyLayer(Blocks, Coord, CanopyCenter, LeafType, 2);
	WriteCanopyLayer(Blocks, Coord, CanopyCenter + FIntVector(0, 0, 1), LeafType, 2);
	WriteCanopyLayer(Blocks, Coord, CanopyCenter + FIntVector(0, 0, 2), LeafType, 1);
	WriteCanopyLayer(Blocks, Coord, CanopyCenter + FIntVector(0, 0, 3), LeafType, 1);
}

void FChunkGenerator::WriteCanopyLayer(TArray<EBlockType>& Blocks, const FIntPoint& Coord,
	const FIntVector& Center, EBlockType LeafType, int32 Radius)
{
	for (int32 X = -Radius; X <= Radius; X++)
	{
		for (int32 Y = -Radius; Y <= Radius; Y++)
		{
			// 5x5 sloj (Radius=2) je bez kutova - isto kao FTreeGenerator::GenerateCanopy
			if (Radius == 2 && FMath::Abs(X) == 2 && FMath::Abs(Y) == 2)
			{
				continue;
			}
			WriteBlockIfAir(Blocks, Coord, Center + FIntVector(X, Y, 0), LeafType);
		}
	}
}
