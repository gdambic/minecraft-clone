#include "UndergroundGenerator.h"
#include "ChunkGenerator.h"
#include "VoxelChunk.h"
#include "Math/RandomStream.h"

namespace
{
	/** Saltovi neovisnih potrosaca slucajnosti (stabla imaju svoj u ChunkGenerator.cpp). */
	constexpr uint32 OreStreamSalt = 0x0BE5EED5;
	constexpr uint32 BedrockSalt = 0xBED20C00;
	constexpr uint32 DeepslateSalt = 0xDEE95A7E;

	/**
	 * Slucajan broj [0,1) vezan uz celiju - zamjena za Minecraftov positional
	 * random. Ne ovisi o redoslijedu obrade, pa chunk i njegov susjed za istu
	 * celiju dobiju isti broj.
	 */
	FORCEINLINE float CellRandom(uint32 Seed, int32 X, int32 Y, int32 Z)
	{
		uint32 Hash = HashCombine(HashCombine(HashCombine(Seed, GetTypeHash(X)), GetTypeHash(Y)), GetTypeHash(Z));
		Hash ^= Hash >> 16;
		Hash *= 0x85EBCA6Bu;
		Hash ^= Hash >> 13;
		Hash *= 0xC2B2AE35u;
		Hash ^= Hash >> 16;
		return (Hash >> 8) * (1.f / 16777216.f);
	}

	int32 SampleHeight(const FOreFeature& Feature, FRandomStream& Random)
	{
		if (Feature.MinZ >= Feature.MaxZ)
		{
			return Feature.MinZ;
		}
		if (Feature.HeightType == EOreHeightType::Uniform)
		{
			return Random.RandRange(Feature.MinZ, Feature.MaxZ);
		}
		// TrapezoidHeight.sample s platoom 0: zbroj dva ravnomjerna broja
		const int32 Range = Feature.MaxZ - Feature.MinZ;
		const int32 Half = Range / 2;
		const int32 Rest = Range - Half;
		return Feature.MinZ + Random.RandRange(0, Rest) + Random.RandRange(0, Half);
	}
}

void FUndergroundGenerator::ApplyLayers(const FChunkGenParams& Params, const FIntPoint& Coord, TArray<EBlockType>& Blocks)
{
	using namespace VoxelGrid;

	if (!Params.Underground)
	{
		return;
	}
	const FStoneLayers& Layers = Params.Underground->Layers;
	const FIntVector Origin = ChunkOrigin(Coord);
	const uint32 BedrockSeed = HashCombine(GetTypeHash(Params.WorldSeed), BedrockSalt);
	const uint32 DeepSeed = HashCombine(GetTypeHash(Params.WorldSeed), DeepslateSalt);

	const bool bDeep = Layers.DeepBlock != EBlockType::Air && Layers.DeepNoneAtAndAbove > Layers.DeepFullAtAndBelow;
	const int32 DeepTop = FMath::Min(Layers.DeepNoneAtAndAbove - 1, WorldMaxZ);
	const bool bBedrock = Layers.BedrockBlock != EBlockType::Air && Layers.BedrockLayers > 0;
	const int32 BedrockTop = FMath::Min(WorldMinZ + Layers.BedrockLayers - 1, WorldMaxZ);

	for (int32 LocalY = 0; LocalY < ChunkSize; LocalY++)
	{
		for (int32 LocalX = 0; LocalX < ChunkSize; LocalX++)
		{
			const int32 X = Origin.X + LocalX;
			const int32 Y = Origin.Y + LocalY;

			if (bDeep)
			{
				for (int32 Z = WorldMinZ; Z <= DeepTop; Z++)
				{
					EBlockType& Cell = Blocks[LocalIndex(FIntVector(LocalX, LocalY, Z))];
					if (Cell != Layers.DeepReplaces)
					{
						continue;
					}
					// vertical_gradient: sigurno do DeepFullAtAndBelow, iznad toga vjerojatnost pada linearno do 0
					const float Chance = Z <= Layers.DeepFullAtAndBelow ? 1.f
						: (float)(Layers.DeepNoneAtAndAbove - Z) / (Layers.DeepNoneAtAndAbove - Layers.DeepFullAtAndBelow);
					if (Chance >= 1.f || CellRandom(DeepSeed, X, Y, Z) < Chance)
					{
						Cell = Layers.DeepBlock;
					}
				}
			}

			if (bBedrock)
			{
				for (int32 Z = WorldMinZ; Z <= BedrockTop; Z++)
				{
					EBlockType& Cell = Blocks[LocalIndex(FIntVector(LocalX, LocalY, Z))];
					if (Cell == EBlockType::Air)
					{
						continue;
					}
					const float Chance = 1.f - (float)(Z - WorldMinZ) / Layers.BedrockLayers;
					if (Z == WorldMinZ || CellRandom(BedrockSeed, X, Y, Z) < Chance)
					{
						Cell = Layers.BedrockBlock;
					}
				}
			}
		}
	}
}

void FUndergroundGenerator::ApplyFeatures(const FChunkGenParams& Params, const FIntPoint& Coord, TArray<EBlockType>& Blocks,
	int32 MaxHeight)
{
	using namespace VoxelGrid;

	if (!Params.Underground)
	{
		return;
	}

	// Redoslijed: nakupina po nakupina (kao u JSON-u), unutar nje izvorni
	// chunkovi fiksnim redom (Y pa X). Isti redoslijed vrijedi u svakom chunku
	// koji racuna iste nakupine, pa se preklapanja rjesavaju jednako.
	const TArray<FOreFeature>& Features = Params.Underground->Features;
	for (int32 FeatureIndex = 0; FeatureIndex < Features.Num(); FeatureIndex++)
	{
		const FOreFeature& Feature = Features[FeatureIndex];
		if (Feature.Targets.IsEmpty() || Feature.Size <= 0)
		{
			continue;
		}

		for (int32 DY = -1; DY <= 1; DY++)
		{
			for (int32 DX = -1; DX <= 1; DX++)
			{
				const FIntPoint Source = Coord + FIntPoint(DX, DY);
				const FIntVector SourceOrigin = ChunkOrigin(Source);
				const uint32 FeatureSeed = HashCombine((uint32)ChunkSeed(Params.WorldSeed, Source, OreStreamSalt),
					GetTypeHash(FeatureIndex));

				FRandomStream CountStream((int32)FeatureSeed);
				if (Feature.OneInChunks > 1 && CountStream.RandRange(0, Feature.OneInChunks - 1) != 0)
				{
					continue;
				}
				const int32 Attempts = CountStream.RandRange(Feature.AttemptsMin, FMath::Max(Feature.AttemptsMin, Feature.AttemptsMax));

				for (int32 Attempt = 0; Attempt < Attempts; Attempt++)
				{
					// Svaki pokusaj ima vlastiti stream: odbaceni pokusaj ne pomice niz sljedecem
					FRandomStream Random((int32)HashCombine(FeatureSeed, GetTypeHash(Attempt + 1)));
					FIntVector Origin;
					Origin.X = SourceOrigin.X + Random.RandRange(0, ChunkSize - 1);
					Origin.Y = SourceOrigin.Y + Random.RandRange(0, ChunkSize - 1);
					Origin.Z = SampleHeight(Feature, Random);

					if (!Feature.Biomes.IsEmpty()
						&& !Feature.Biomes.Contains(FChunkGenerator::GetBiomeIndex(Params, Origin.X, Origin.Y)))
					{
						continue;
					}
					PlaceBlob(Params, Feature, FeatureSeed, Origin, Random, Coord, Blocks, MaxHeight);
				}
			}
		}
	}
}

void FUndergroundGenerator::PlaceBlob(const FChunkGenParams& Params, const FOreFeature& Feature, uint32 FeatureSeed,
	const FIntVector& Origin, FRandomStream& Random, const FIntPoint& Coord, TArray<EBlockType>& Blocks, int32 MaxHeight)
{
	using namespace VoxelGrid;

	// OreFeature.place: nakupina je niz kugli duz duzine izmedu dvije tocke.
	// Minecraftove osi (x, y gore, z) ovdje su (X, Z gore, Y).
	const int32 Size = Feature.Size;
	const float Angle = Random.FRand() * UE_PI;
	const float HalfLength = Size / 8.f;
	const int32 Pad = FMath::CeilToInt((Size / 16.f * 2.f + 1.f) / 2.f);
	const double StartX = Origin.X + FMath::Sin(Angle) * HalfLength;
	const double EndX = Origin.X - FMath::Sin(Angle) * HalfLength;
	const double StartY = Origin.Y + FMath::Cos(Angle) * HalfLength;
	const double EndY = Origin.Y - FMath::Cos(Angle) * HalfLength;
	const double StartZ = Origin.Z + Random.RandRange(0, 2) - 2;
	const double EndZ = Origin.Z + Random.RandRange(0, 2) - 2;

	const int32 Reach = FMath::CeilToInt(HalfLength) + Pad;
	const int32 BoxMinX = Origin.X - Reach;
	const int32 BoxMinY = Origin.Y - Reach;
	const int32 BoxMinZ = Origin.Z - 2 - Pad;
	const int32 BoxSpanXY = 2 * Reach;
	const int32 BoxSpanZ = 2 * (2 + Pad);

	// Samo dio koji pada u ovaj chunk i u teren
	const FIntVector ChunkMin = ChunkOrigin(Coord);
	const int32 ClipMinX = ChunkMin.X;
	const int32 ClipMaxX = ChunkMin.X + ChunkSize - 1;
	const int32 ClipMinY = ChunkMin.Y;
	const int32 ClipMaxY = ChunkMin.Y + ChunkSize - 1;
	const int32 ClipMinZ = WorldMinZ;
	const int32 ClipMaxZ = FMath::Min(MaxHeight, WorldMaxZ);
	if (BoxMinX > ClipMaxX || BoxMinX + BoxSpanXY < ClipMinX || BoxMinY > ClipMaxY || BoxMinY + BoxSpanXY < ClipMinY
		|| BoxMinZ > ClipMaxZ || BoxMinZ + BoxSpanZ < ClipMinZ)
	{
		return;
	}

	// OreFeature.doPlace: kugle (x, y, z, polumjer)
	struct FSphere
	{
		double X, Y, Z, Radius;
	};
	TArray<FSphere, TInlineAllocator<64>> Spheres;
	Spheres.SetNumUninitialized(Size);
	for (int32 Index = 0; Index < Size; Index++)
	{
		const float Alpha = (float)Index / Size;
		const double Thickness = Random.FRand() * Size / 16.0;
		FSphere& Sphere = Spheres[Index];
		Sphere.X = FMath::Lerp(StartX, EndX, (double)Alpha);
		Sphere.Y = FMath::Lerp(StartY, EndY, (double)Alpha);
		Sphere.Z = FMath::Lerp(StartZ, EndZ, (double)Alpha);
		Sphere.Radius = ((FMath::Sin(UE_PI * Alpha) + 1.f) * Thickness + 1.0) / 2.0;
	}

	// Kugla koja je cijela unutar druge se izbacuje
	for (int32 A = 0; A < Size - 1; A++)
	{
		if (Spheres[A].Radius <= 0.0)
		{
			continue;
		}
		for (int32 B = A + 1; B < Size; B++)
		{
			if (Spheres[B].Radius <= 0.0)
			{
				continue;
			}
			const double DX = Spheres[A].X - Spheres[B].X;
			const double DY = Spheres[A].Y - Spheres[B].Y;
			const double DZ = Spheres[A].Z - Spheres[B].Z;
			const double DRadius = Spheres[A].Radius - Spheres[B].Radius;
			if (DRadius * DRadius > DX * DX + DY * DY + DZ * DZ)
			{
				if (DRadius > 0.0)
				{
					Spheres[B].Radius = -1.0;
				}
				else
				{
					Spheres[A].Radius = -1.0;
				}
			}
		}
	}

	// Zrak za pravilo "odbaci ako dodiruje zrak": prazna celija koja nije more.
	// Unutar chunka cita podatke, preko ruba pita formulu (ista pravila).
	const auto IsAir = [&](int32 X, int32 Y, int32 Z)
	{
		if (!IsValidZ(Z))
		{
			return false;
		}
		if (X >= ClipMinX && X <= ClipMaxX && Y >= ClipMinY && Y <= ClipMaxY)
		{
			return Blocks[LocalIndex(FIntVector(X - ChunkMin.X, Y - ChunkMin.Y, Z))] == EBlockType::Air
				&& !(Z <= Params.SeaLevel && FChunkGenerator::IsSeaWater(Params, X, Y, Z));
		}
		if (Z > FChunkGenerator::GetTerrainHeight(Params, X, Y))
		{
			return Z > Params.SeaLevel;
		}
		return FChunkGenerator::IsCaveCell(Params, X, Y, Z);
	};

	for (const FSphere& Sphere : Spheres)
	{
		const double Radius = Sphere.Radius;
		if (Radius < 0.0)
		{
			continue;
		}
		const int32 MinX = FMath::Max(FMath::FloorToInt32(Sphere.X - Radius), BoxMinX);
		const int32 MinY = FMath::Max(FMath::FloorToInt32(Sphere.Y - Radius), BoxMinY);
		const int32 MinZ = FMath::Max(FMath::FloorToInt32(Sphere.Z - Radius), BoxMinZ);
		const int32 MaxX = FMath::Max(FMath::FloorToInt32(Sphere.X + Radius), MinX);
		const int32 MaxY = FMath::Max(FMath::FloorToInt32(Sphere.Y + Radius), MinY);
		const int32 MaxZ = FMath::Max(FMath::FloorToInt32(Sphere.Z + Radius), MinZ);

		for (int32 X = FMath::Max(MinX, ClipMinX); X <= FMath::Min(MaxX, ClipMaxX); X++)
		{
			const double NX = (X + 0.5 - Sphere.X) / Radius;
			if (NX * NX >= 1.0)
			{
				continue;
			}
			for (int32 Z = FMath::Max(MinZ, ClipMinZ); Z <= FMath::Min(MaxZ, ClipMaxZ); Z++)
			{
				const double NZ = (Z + 0.5 - Sphere.Z) / Radius;
				if (NX * NX + NZ * NZ >= 1.0)
				{
					continue;
				}
				for (int32 Y = FMath::Max(MinY, ClipMinY); Y <= FMath::Min(MaxY, ClipMaxY); Y++)
				{
					const double NY = (Y + 0.5 - Sphere.Y) / Radius;
					if (NX * NX + NZ * NZ + NY * NY >= 1.0)
					{
						continue;
					}

					EBlockType& Cell = Blocks[LocalIndex(FIntVector(X - ChunkMin.X, Y - ChunkMin.Y, Z))];
					for (const FOreTarget& Target : Feature.Targets)
					{
						if (!Target.Replace.Contains(Cell))
						{
							continue;
						}
						// OreFeature.canPlaceOre + shouldSkipAirCheck
						const float Discard = Feature.DiscardOnAirExposure;
						const bool bSkipAirCheck = Discard <= 0.f
							|| (Discard < 1.f && CellRandom(FeatureSeed, X, Y, Z) >= Discard);
						if (bSkipAirCheck || !(IsAir(X + 1, Y, Z) || IsAir(X - 1, Y, Z) || IsAir(X, Y + 1, Z)
							|| IsAir(X, Y - 1, Z) || IsAir(X, Y, Z + 1) || IsAir(X, Y, Z - 1)))
						{
							Cell = Target.Block;
						}
						break;
					}
				}
			}
		}
	}
}
