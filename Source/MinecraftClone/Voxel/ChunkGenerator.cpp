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

int32 FChunkGenerator::GetMinNearbyHeight(const FChunkGenParams& Params, int32 X, int32 Y)
{
	const int32 Radius = Params.Caves ? Params.Caves->Config.SeaGuardRadius : 0;
	int32 MinHeight = MAX_int32;
	for (int32 DY = -Radius; DY <= Radius; DY++)
	{
		for (int32 DX = -Radius; DX <= Radius; DX++)
		{
			MinHeight = FMath::Min(MinHeight, GetTerrainHeight(Params, X + DX, Y + DY));
		}
	}
	return MinHeight;
}

bool FChunkGenerator::IsCaveCell(const FChunkGenParams& Params, int32 X, int32 Y, int32 Z)
{
	if (!Params.Caves || !Params.Caves->Config.bEnabled)
	{
		return false;
	}
	const int32 Height = GetTerrainHeight(Params, X, Y);
	if (Z > Height || FCaveGenerator::IsSeaProtected(*Params.Caves, Z, GetMinNearbyHeight(Params, X, Y)))
	{
		return false;
	}
	return FCaveGenerator::IsCarvedAt(*Params.Caves, X, Y, Z, Height - Z);
}

int32 FChunkGenerator::GetGroundHeight(const FChunkGenParams& Params, int32 X, int32 Y)
{
	const int32 Height = GetTerrainHeight(Params, X, Y);
	if (!Params.Caves || !Params.Caves->Config.bEnabled)
	{
		return Height;
	}

	const int32 MinNearby = GetMinNearbyHeight(Params, X, Y);
	int32 Z = FMath::Min(Height, VoxelGrid::WorldMaxZ);
	while (Z > VoxelGrid::WorldMinZ && !FCaveGenerator::IsSeaProtected(*Params.Caves, Z, MinNearby)
		&& FCaveGenerator::IsCarvedAt(*Params.Caves, X, Y, Z, Height - Z))
	{
		Z--;
	}
	return Z;
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
	TArray<uint8>& OutBiomes, TArray<uint8>* OutStates)
{
	using namespace VoxelGrid;

	TArray<EBlockType> Blocks;
	Blocks.SetNumZeroed(BlocksPerChunk);

	// --- 0. Biomi stupaca (prvi, jer o njima ovisi sve ispod) ---
	OutBiomes = GenerateChunkBiomes(Params, Coord);

	// Visine stupaca chunka i ruba sirine SeaGuardRadius oko njega - rub treba
	// zastita mora (spilja ne smije dotaknuti more u susjednom stupcu)
	const FIntVector Origin = ChunkOrigin(Coord);
	const bool bCaves = Params.Caves && Params.Caves->Config.bEnabled;
	const int32 Margin = bCaves ? Params.Caves->Config.SeaGuardRadius : 0;
	const int32 HeightsSize = ChunkSize + 2 * Margin;
	TArray<int32> Heights;
	Heights.SetNumUninitialized(HeightsSize * HeightsSize);
	for (int32 HY = 0; HY < HeightsSize; HY++)
	{
		for (int32 HX = 0; HX < HeightsSize; HX++)
		{
			Heights[HY * HeightsSize + HX] = GetTerrainHeight(Params, Origin.X + HX - Margin, Origin.Y + HY - Margin);
		}
	}
	const auto HeightAt = [&Heights, HeightsSize, Margin](int32 LocalX, int32 LocalY)
	{
		return Heights[(LocalY + Margin) * HeightsSize + LocalX + Margin];
	};

	// --- 1. Teren: height-map po stupcu, povrsinski sloj iz bioma stupca, stone dublje ---
	int32 MaxHeight = WorldMinZ;
	for (int32 LocalX = 0; LocalX < ChunkSize; LocalX++)
	{
		for (int32 LocalY = 0; LocalY < ChunkSize; LocalY++)
		{
			const int32 X = Origin.X + LocalX;
			const int32 Y = Origin.Y + LocalY;
			const int32 Height = FMath::Min(HeightAt(LocalX, LocalY), WorldMaxZ);
			MaxHeight = FMath::Max(MaxHeight, Height);
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

	// --- 1b. Spilje: isprazni izdubljene celije (isto pravilo kao IsCaveCell) ---
	if (bCaves)
	{
		const FCaveGenData& Caves = *Params.Caves;
		const FCaveLattice Lattice(Caves, Origin, MaxHeight);
		for (int32 LocalY = 0; LocalY < ChunkSize; LocalY++)
		{
			for (int32 LocalX = 0; LocalX < ChunkSize; LocalX++)
			{
				const int32 RawHeight = HeightAt(LocalX, LocalY);
				int32 MinNearby = RawHeight;
				for (int32 DY = -Margin; DY <= Margin; DY++)
				{
					for (int32 DX = -Margin; DX <= Margin; DX++)
					{
						MinNearby = FMath::Min(MinNearby, HeightAt(LocalX + DX, LocalY + DY));
					}
				}

				for (int32 Z = FMath::Min(RawHeight, WorldMaxZ); Z > WorldMinZ; Z--)
				{
					if (!FCaveGenerator::IsSeaProtected(Caves, Z, MinNearby)
						&& Lattice.IsCarved(LocalX, LocalY, Z, RawHeight - Z))
					{
						Blocks[LocalIndex(FIntVector(LocalX, LocalY, Z))] = EBlockType::Air;
					}
				}
			}
		}
	}

	// --- 1c. Slojevi kamena (Bedrock, Deepslate) pa nakupine kamena i rude ---
	if (Params.Underground)
	{
		FUndergroundGenerator::ApplyLayers(Params, Coord, Blocks);
		FUndergroundGenerator::ApplyFeatures(Params, Coord, Blocks, MaxHeight);
	}

	// --- 2. Showcase (samo chunk (0,0)): po jedan blok svake definicije, 16 u
	// redu, redovi na svakom drugom stupcu po Y da se blokovi vide sa strane ---
	if (Coord == FIntPoint::ZeroValue)
	{
		constexpr int32 MaxShowcase = ChunkSize * (ChunkSize / 2);
		const int32 Count = FMath::Min(Params.ShowcaseBlocks.Num(), MaxShowcase);
		for (int32 i = 0; i < Count; i++)
		{
			const int32 X = i % ChunkSize;
			const int32 Y = (i / ChunkSize) * 2;
			const int32 Z = GetTerrainHeight(Params, X, Y) + 1;
			WriteBlockIfAir(Blocks, Coord, FIntVector(X, Y, Z), Params.ShowcaseBlocks[i]);
		}
		if (Params.ShowcaseBlocks.Num() > MaxShowcase)
		{
			UE_LOG(LogTemp, Warning, TEXT("ChunkGenerator: showcase ima %d definicija, stane %d - preskacem ostatak"),
				Params.ShowcaseBlocks.Num(), MaxShowcase);
		}

		// Zidni blok (zidna baklja) treba oslonac: iza redova stoji stup od
		// ShowcaseWallSupport, a na svakoj bocnoj stranici njegovog vrha po jedan
		// zidni blok koji gleda od stupa
		const int32 PillarX = ChunkSize / 2;
		const int32 PillarY = ((Count + ChunkSize - 1) / ChunkSize) * 2 + 2;
		if (Params.ShowcaseWallBlock != EBlockType::Air && PillarY + 1 < ChunkSize)
		{
			static const FIntVector Sides[4] = {
				FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0)
			};

			// Vrh stupa je iznad tla sva cetiri susjedna stupca, da nijedna baklja ne zavrsi u zemlji
			const int32 PillarGround = GetTerrainHeight(Params, PillarX, PillarY);
			int32 Top = PillarGround + 1;
			for (const FIntVector& Side : Sides)
			{
				Top = FMath::Max(Top, GetTerrainHeight(Params, PillarX + Side.X, PillarY + Side.Y) + 1);
			}
			for (int32 Z = PillarGround + 1; Z <= Top; Z++)
			{
				WriteBlockIfAir(Blocks, Coord, FIntVector(PillarX, PillarY, Z), Params.ShowcaseWallSupport);
			}

			for (int32 Side = 0; Side < 4; Side++)
			{
				const FIntVector Pos = FIntVector(PillarX, PillarY, Top) + Sides[Side];
				if (!IsValidZ(Pos.Z) || Blocks[LocalIndex(Pos)] != EBlockType::Air)
				{
					continue;
				}
				Blocks[LocalIndex(Pos)] = Params.ShowcaseWallBlock;
				if (OutStates)
				{
					if (OutStates->Num() != BlocksPerChunk)
					{
						OutStates->SetNumZeroed(BlocksPerChunk);
					}
					(*OutStates)[LocalIndex(Pos)] = Params.ShowcaseWallStates[Side];
				}
			}
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
		if (IsCaveCell(Params, X, Y, GroundZ))
		{
			continue;   // tlo je izdubljeno - ulaz u spilju
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
