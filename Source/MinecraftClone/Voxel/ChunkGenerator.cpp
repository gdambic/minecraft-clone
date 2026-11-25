#include "ChunkGenerator.h"
#include "TerrainGenerator.h"
#include "BiomeGenerator.h"
#include "Math/RandomStream.h"

namespace
{
	/** Salt za stream stabala - buduci potrosaci chunk seeda (mob spawn...) koriste druge saltove. */
	constexpr uint32 TreeStreamSalt = 0x7EE5EED5;
}

float FChunkGenerator::GetMountainLift(const FChunkGenParams& Params, int32 X, int32 Y)
{
	if (Params.MountainMaxLift <= 0.f)
	{
		return 0.f;
	}
	return FTerrainGenerator::GetMountainLift(X, Y, Params.MountainOffsetX, Params.MountainOffsetY,
		Params.MountainNoiseScale, Params.MountainOctaves, Params.MountainNoiseStart,
		Params.MountainNoiseFull, Params.MountainMaxLift)
		* FMath::SmoothStep(Params.CoastContinentalness, Params.InlandContinentalness, GetContinentalness(Params, X, Y));
}

float FChunkGenerator::GetContinentalness(const FChunkGenParams& Params, int32 X, int32 Y)
{
	return FTerrainGenerator::FractalNoise2D(X, Y, Params.ContinentOffsetX, Params.ContinentOffsetY,
		Params.ContinentNoiseScale, Params.ContinentOctaves) * Params.ContinentNoiseGain;
}

float FChunkGenerator::GetContinentHeight(const FChunkGenParams& Params, float Continentalness)
{
	const TArray<FVector2D>& Curve = Params.ContinentHeightCurve;
	if (Curve.IsEmpty())
	{
		return 0.f;
	}
	if (Continentalness <= Curve[0].X)
	{
		return Curve[0].Y;
	}
	for (int32 i = 1; i < Curve.Num(); i++)
	{
		if (Continentalness <= Curve[i].X)
		{
			return FMath::GetMappedRangeValueClamped(FVector2D(Curve[i - 1].X, Curve[i].X),
				FVector2D(Curve[i - 1].Y, Curve[i].Y), Continentalness);
		}
	}
	return Curve.Last().Y;
}

bool FChunkGenerator::IsSeaWater(const FChunkGenParams& Params, int32 X, int32 Y, int32 Z)
{
	return Z <= Params.SeaLevel && Z > GetTerrainHeight(Params, X, Y);
}

const FBiomeGenData& FChunkGenerator::GetUnderwaterGenData(const FChunkGenParams& Params, int32 Height)
{
	static const FBiomeGenData Shallow = { EBlockType::Dirt, EBlockType::Dirt, 3, EBlockType::Air, 0.f };
	static const FBiomeGenData Deep = { EBlockType::Gravel, EBlockType::Stone, 1, EBlockType::Air, 0.f };
	return Params.SeaLevel - Height <= Params.ShallowWaterDepth ? Shallow : Deep;
}

const FBiomeGenData& FChunkGenerator::GetBiomeGenData(const FChunkGenParams& Params, int32 BiomeIndex)
{
	static const FBiomeGenData Default;
	return Params.BiomeGenData.IsValidIndex(BiomeIndex) ? Params.BiomeGenData[BiomeIndex] : Default;
}

bool FChunkGenerator::IsStripeColumn(const FChunkGenParams& Params, int32 X, int32 Y)
{
	const float Noise = FTerrainGenerator::FractalNoise2D(X, Y, Params.StripeOffsetX, Params.StripeOffsetY,
		Params.StripeNoiseScale, Params.StripeOctaves);
	return FMath::Abs(Noise) < Params.StripeHalfWidth;
}

EBlockType FChunkGenerator::GetColumnBlock(const FBiomeGenData& Surface, bool bStripeColumn, int32 Z, int32 Height)
{
	// Povrsinski sloj = najgornji blok + (SubsurfaceDepth - 1) ispod njega
	if (Z <= Height - FMath::Max(Surface.SubsurfaceDepth, 1))
	{
		return EBlockType::Stone;
	}
	if (bStripeColumn && Surface.Stripe != EBlockType::Air)
	{
		return Surface.Stripe;
	}
	return Z == Height ? Surface.Surface : Surface.Subsurface;
}

int32 FChunkGenerator::GetTerrainHeight(const FChunkGenParams& Params, int32 X, int32 Y)
{
	// Kontinentalnost daje grubu visinu prema moru; sitne neravnine su pod morem
	// prigusene (mirnije dno), a puna amplituda vrijedi tek dublje u kopnu
	const float Continentalness = GetContinentalness(Params, X, Y);
	const float AmplitudeScale = FMath::Lerp(Params.UnderwaterAmplitudeScale, 1.f,
		FMath::SmoothStep(Params.OceanContinentalness, Params.InlandContinentalness, Continentalness));
	const float Local = FTerrainGenerator::FractalNoise2D(X, Y, Params.NoiseOffsetX, Params.NoiseOffsetY,
		Params.NoiseScale, Params.Octaves) * Params.HeightAmplitude * AmplitudeScale;
	const int32 Height = FMath::RoundToInt(Params.SeaLevel + GetContinentHeight(Params, Continentalness)
		+ Local + GetMountainLift(Params, X, Y));

	// Donja granica da povrsina nikad ne padne na samo dno svijeta
	// (WorldMinZ mora ostati cvrst sloj ispod najdubljeg terena)
	return FMath::Max(Height, VoxelGrid::WorldMinZ + 1);
}

int32 FChunkGenerator::GetBiomeIndex(const FChunkGenParams& Params, int32 X, int32 Y)
{
	// Ocean bira ista kontinentalnost koja spusta teren - biom i dubina se ne razilaze
	if (Params.OceanBiome >= 0 && GetContinentalness(Params, X, Y) < Params.OceanContinentalness)
	{
		return Params.OceanBiome;
	}

	// Biom vrhova ovisi o istom podizanju koje radi visinu - granica bioma je
	// izohipsa na padini, a ne mrlja neovisna o terenu
	if (Params.PeakBiome >= 0)
	{
		// Prag se po stupcu pomice malim noiseom - inace je granica savrsena
		// izohipsa; ovako kamen mjestimicno silazi niz padinu, a trava se penje
		const float Jitter = FTerrainGenerator::FractalNoise2D(X, Y, Params.PeakBorderOffsetX,
			Params.PeakBorderOffsetY, Params.PeakBorderNoiseScale, 2) * Params.PeakBorderJitter;
		if (GetMountainLift(Params, X, Y) >= Params.PeakBiomeLift + Jitter)
		{
			return Params.PeakBiome;
		}
	}

	const int32 Pick = FBiomeGenerator::GetBiomeIndexAt(X, Y, Params.BiomeOffsetX, Params.BiomeOffsetY,
		Params.BiomeNoiseScale, Params.NoiseBiomes.Num());
	return Params.NoiseBiomes.IsValidIndex(Pick) ? Params.NoiseBiomes[Pick] : 0;
}

TArray<uint8> FChunkGenerator::GenerateChunkBiomes(const FChunkGenParams& Params, const FIntPoint& Coord)
{
	using namespace VoxelGrid;

	TArray<uint8> Biomes;
	Biomes.SetNumUninitialized(ColumnsPerChunk);
	const FIntVector Origin = ChunkOrigin(Coord);
	for (int32 LocalY = 0; LocalY < ChunkSize; LocalY++)
	{
		for (int32 LocalX = 0; LocalX < ChunkSize; LocalX++)
		{
			Biomes[ColumnIndex(LocalX, LocalY)] =
				(uint8)GetBiomeIndex(Params, Origin.X + LocalX, Origin.Y + LocalY);
		}
	}
	return Biomes;
}

TArray<EBlockType> FChunkGenerator::GenerateChunkData(const FChunkGenParams& Params, const FIntPoint& Coord,
	TArray<uint8>& OutBiomes)
{
	using namespace VoxelGrid;

	TArray<EBlockType> Blocks;
	Blocks.SetNumZeroed(BlocksPerChunk);

	// --- 0. Biomi stupaca (prvi, jer o njima ovisi sve ispod) ---
	OutBiomes = GenerateChunkBiomes(Params, Coord);

	// --- 1. Teren: height-map po stupcu, povrsinski sloj iz bioma stupca, stone dublje ---
	const FIntVector Origin = ChunkOrigin(Coord);
	for (int32 LocalX = 0; LocalX < ChunkSize; LocalX++)
	{
		for (int32 LocalY = 0; LocalY < ChunkSize; LocalY++)
		{
			const int32 X = Origin.X + LocalX;
			const int32 Y = Origin.Y + LocalY;
			const int32 Height = FMath::Min(GetTerrainHeight(Params, X, Y), WorldMaxZ);
			const FBiomeGenData& Surface = Height < Params.SeaLevel
				? GetUnderwaterGenData(Params, Height)
				: GetBiomeGenData(Params, OutBiomes[ColumnIndex(LocalX, LocalY)]);
			const bool bStripe = Surface.Stripe != EBlockType::Air && IsStripeColumn(Params, X, Y);
			for (int32 Z = WorldMinZ; Z <= Height; Z++)
			{
				Blocks[LocalIndex(FIntVector(LocalX, LocalY, Z))] = GetColumnBlock(Surface, bStripe, Z, Height);
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

		// Gustoca bioma na bazi stabla skalira istu kocku (Roll), bez novog
		// izvlacenja - gustoca 1 daje tocno stara stabla, 0 nijedno
		const int32 X = Origin.X + LocalX;
		const int32 Y = Origin.Y + LocalY;
		const float Density = GetBiomeGenData(Params, GetBiomeIndex(Params, X, Y)).TreeDensity;
		if (Roll >= Params.TreeChancePerChunk * Density)
		{
			continue;
		}

		const int32 GroundZ = GetTerrainHeight(Params, X, Y);
		if (GroundZ < Params.SeaLevel)
		{
			continue;   // stablo bi stajalo u moru
		}

		FTreeCandidate Tree;
		Tree.Base = FIntVector(X, Y, GroundZ + 1);
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
