#pragma once

#include "CoreMinimal.h"
#include "BlockType.h"
#include "TreeGenerator.h"
#include "VoxelChunk.h"

/** Polja jednog bioma koja generacija cita - kopija iz FBiomeDefinition (worker ne cita registry). */
struct FBiomeGenData
{
	EBlockType Surface = EBlockType::Grass;
	EBlockType Subsurface = EBlockType::Dirt;
	int32 SubsurfaceDepth = 3;
	EBlockType Stripe = EBlockType::Air;
	float TreeDensity = 1.f;
};

/**
 * Ulazni parametri generacije chunka - snapshot svih postavki AVoxelWorld-a
 * koje generacija koristi. Kopira se po vrijednosti u worker task (faza 3
 * plana): worker NE smije citati actor dok igra tece.
 */
struct FChunkGenParams
{
	int32 WorldSeed = 0;

	// Teren (FTerrainGenerator)
	float NoiseOffsetX = 0.f;
	float NoiseOffsetY = 0.f;
	float NoiseScale = 0.08f;
	int32 Octaves = 3;
	int32 SeaLevel = 0;
	int32 HeightAmplitude = 6;

	// Kontinenti (Docs/PLAN_Oceans.md): noise kontinentalnosti -> visina iznad/ispod
	// mora kroz krivulju (X = kontinentalnost, Y = blokovi od SeaLevel), ocean i obala
	float ContinentOffsetX = 0.f;
	float ContinentOffsetY = 0.f;
	float ContinentNoiseScale = 0.0015f;
	int32 ContinentOctaves = 4;
	float ContinentNoiseGain = 2.3f;
	TArray<FVector2D> ContinentHeightCurve;
	float OceanContinentalness = -0.19f;
	float CoastContinentalness = -0.11f;
	float InlandContinentalness = 0.3f;
	float UnderwaterAmplitudeScale = 0.3f;
	int32 ShallowWaterDepth = 5;

	// Planine (FTerrainGenerator::GetMountainLift) - MaxLift 0 = bez planina
	float MountainOffsetX = 0.f;
	float MountainOffsetY = 0.f;
	float MountainNoiseScale = 0.004f;
	int32 MountainOctaves = 3;
	float MountainNoiseStart = 0.1f;
	float MountainNoiseFull = 0.5f;
	float MountainMaxLift = 0.f;

	// Biomi (FBiomeGenerator). NoiseBiomes = indeksi bioma s placement "noise"
	// (redom iz Biomes.json); PeakBiome = biom s placement "peaks", -1 = nema ga.
	float BiomeOffsetX = 0.f;
	float BiomeOffsetY = 0.f;
	float BiomeNoiseScale = 0.012f;
	TArray<uint8> NoiseBiomes = { 0 };
	int32 PeakBiome = -1;
	int32 OceanBiome = -1;   // biom s placement "ocean", -1 = nema ga
	float PeakBiomeLift = 30.f;

	// Nazubljena granica vrhova: noise pomice PeakBiomeLift za ±PeakBorderJitter po stupcu
	float PeakBorderOffsetX = 0.f;
	float PeakBorderOffsetY = 0.f;
	float PeakBorderNoiseScale = 0.06f;
	float PeakBorderJitter = 8.f;

	// Generacijska polja po biomu (indeks = indeks bioma) + noise pruga (FBiomeGenData::Stripe)
	TArray<FBiomeGenData> BiomeGenData;
	float StripeOffsetX = 0.f;
	float StripeOffsetY = 0.f;
	float StripeNoiseScale = 0.02f;
	int32 StripeOctaves = 2;
	float StripeHalfWidth = 0.025f;

	// Stabla - deterministicki po chunku (plan 3.5)
	int32 TreeAttemptsPerChunk = 1;
	float TreeChancePerChunk = 0.25f;

	/** Showcase red za chunk (0,0): po jedan blok svake registrirane definicije. Prazno = bez showcasea. */
	TArray<EBlockType> ShowcaseBlocks;
};

/** Deterministicki kandidat stabla - izracunljiv za BILO KOJI chunk samo iz seeda i koordinate. */
struct FTreeCandidate
{
	FIntVector Base = FIntVector::ZeroValue;   // grid pozicija prvog bloka debla (povrsina + 1)
	ETreeType Type = ETreeType::Oak;
	int32 TrunkHeight = 4;
};

/**
 * Cista generacija podataka jednog chunka: teren + showcase + stabla.
 * Thread-safe (nista od UObjecta, nista od globalnog RNG-a) - isti ulazi
 * uvijek daju isti izlaz, neovisno o redoslijedu generiranja chunkova.
 *
 * Stabla preko granica (plan 3.5): pri generaciji chunka C izracunaju se
 * kandidati za C i svih 8 susjeda (deterministicki iz njihovih seedova) i
 * upisu se SAMO blokovi koji padaju unutar C. Susjed ce kasnije izracunati
 * ISTE kandidate i upisati svoju polovicu - stablo na granici se sastavi
 * identicno bez obzira tko se generirao prvi. Krosnja je <= 2 bloka radijusa
 * pa je 3x3 susjedstvo dovoljno.
 */
class MINECRAFTCLONE_API FChunkGenerator
{
public:
	/** Blokovi chunka; OutBiomes dobiva biom svakog stupca (VoxelGrid::ColumnIndex). */
	static TArray<EBlockType> GenerateChunkData(const FChunkGenParams& Params, const FIntPoint& Coord,
		TArray<uint8>& OutBiomes);

	/** Samo biomi stupaca chunka - za chunk ciji blokovi dolaze iz memorije (izmijenjeni chunk). */
	static TArray<uint8> GenerateChunkBiomes(const FChunkGenParams& Params, const FIntPoint& Coord);

	/**
	 * Visina povrsine stupca - JEDINA formula visine: koriste je generacija,
	 * spawn i teleport (AVoxelWorld::GetTerrainHeightAt).
	 */
	static int32 GetTerrainHeight(const FChunkGenParams& Params, int32 X, int32 Y);

	/** Indeks bioma stupca - JEDINA formula bioma (generacija i AVoxelWorld::GetBiomeIndexAt). */
	static int32 GetBiomeIndex(const FChunkGenParams& Params, int32 X, int32 Y);

	/** Kontinentalnost stupca (fraktalni noise, ~-1..1): nisko = ocean, visoko = unutrasnjost. */
	static float GetContinentalness(const FChunkGenParams& Params, int32 X, int32 Y);

	/** Visina terena od SeaLevel za danu kontinentalnost - linearno izmedu tocaka ContinentHeightCurve. */
	static float GetContinentHeight(const FChunkGenParams& Params, float Continentalness);

	/**
	 * Je li celija morska voda: prazna celija na otvorenom (iznad generirane
	 * povrsine stupca) do ukljucivo SeaLevel. JEDINO pravilo gdje je more -
	 * na njega se oslanja prikaz vode (Docs/PLAN_Water.md).
	 */
	static bool IsSeaWater(const FChunkGenParams& Params, int32 X, int32 Y, int32 Z);

	/** Koliko planina podize stupac (u blokovima) - ulazi i u visinu i u odabir bioma. */
	static float GetMountainLift(const FChunkGenParams& Params, int32 X, int32 Y);

	/** Generacijska polja bioma; nepoznat indeks dobiva default (Grass/Dirt/3, bez pruga, gustoca 1). */
	static const FBiomeGenData& GetBiomeGenData(const FChunkGenParams& Params, int32 BiomeIndex);

	/**
	 * Povrsinski sloj stupca ciji je vrh pod morem (Height < SeaLevel): plitko
	 * dno je Dirt, duboko Gravel na Stoneu (Minecraft surface pravilo), neovisno o biomu.
	 */
	static const FBiomeGenData& GetUnderwaterGenData(const FChunkGenParams& Params, int32 Height);

	/** Lezi li stupac na pruzi - noise pruga unutar ±StripeHalfWidth oko nule. */
	static bool IsStripeColumn(const FChunkGenParams& Params, int32 X, int32 Y);

	/** Blok na visini Z stupca s povrsinom Height - JEDINO pravilo slaganja stupca. */
	static EBlockType GetColumnBlock(const FBiomeGenData& Surface, bool bStripeColumn, int32 Z, int32 Height);

	/** Deterministicki kandidati stabala jednog chunka (bez upisivanja blokova). */
	static void GetTreeCandidates(const FChunkGenParams& Params, const FIntPoint& Coord,
		TArray<FTreeCandidate>& OutCandidates);

private:
	/** Upisi blok u Blocks array chunka Coord ako pozicija pada u taj chunk i celija je Air. */
	static void WriteBlockIfAir(TArray<EBlockType>& Blocks, const FIntPoint& Coord,
		const FIntVector& GridPos, EBlockType Type);

	/** Upisi blokove jednog stabla (deblo + krosnja) - samo dio koji pada u chunk Coord. */
	static void WriteTree(TArray<EBlockType>& Blocks, const FIntPoint& Coord, const FTreeCandidate& Tree);

	/** Jedan sloj krosnje: kvadrat (2R+1)x(2R+1), za R=2 bez kutova (oblik iz FTreeGenerator::GenerateCanopy). */
	static void WriteCanopyLayer(TArray<EBlockType>& Blocks, const FIntPoint& Coord,
		const FIntVector& Center, EBlockType LeafType, int32 Radius);
};
