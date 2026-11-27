#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"

/**
 * Postavke spilja iz Content/Data/WorldGen/Underground.json ("caves").
 * Defaulti su Minecraftove vrijednosti (1.21.11 jar, worldgen/noise_settings/
 * overworld.json i density_function/overworld/caves) - vidi Docs/PLAN_Underground.md.
 */
struct FCaveConfig
{
	bool bEnabled = true;

	/** Dvorane ("cheese"): supljina gdje je noise ispod praga. Veci ChamberOffset = manje dvorana. */
	bool bChambers = true;
	float ChamberOffset = 0.27f;

	/** Kameni stupovi u dvoranama. */
	bool bPillars = true;

	/** Tuneli ("spaghetti", 2D i 3D). */
	bool bTunnels = true;

	/** Uski prolazi ("noodle"). */
	bool bNoodles = true;

	/** Ulazi s povrsine. Veci EntranceOffset = manje ulaza. */
	bool bEntrances = true;
	float EntranceOffset = 0.37f;

	/**
	 * Minecraftov "factor" terena (medijan splinea factor.json). Odreduje debljinu
	 * sloja ispod povrsine u kojem nastaju samo ulazi: 50 / SurfaceFactor blokova.
	 */
	float SurfaceFactor = 5.47f;

	/** Mnozitelj velicine svih spilja (1 = Minecraft, 2 = dvostruko vece i rjede). */
	float SizeScale = 1.f;

	/** Koliko blokova ispod najnizeg obliznjeg morskog dna ostaje netaknuto. */
	int32 SeaCrustDepth = 6;

	/** Koliko stupaca od mora vrijedi zastita (0..4). */
	int32 SeaGuardRadius = 2;
};

/** Perlinov "improved noise" s vlastitom permutacijom - isti algoritam kao Minecraftov ImprovedNoise. */
struct FImprovedNoise
{
	uint8 Permutation[256];
	double OffsetX = 0.0;
	double OffsetY = 0.0;
	double OffsetZ = 0.0;

	void Init(FRandomStream& Stream);
	double Sample(double X, double Y, double Z) const;
};

/**
 * Minecraftov NormalNoise: zbroj dva fraktalna Perlina (drugi na koordinatama
 * x1.0181268882175227), s oktavama zadanim kao firstOctave + amplitude. Formule
 * faktora procitane su iz bytecodea klasa NormalNoise i PerlinNoise.
 */
struct FVoxelNormalNoise
{
	static constexpr int32 MaxOctaves = 9;

	FImprovedNoise Octaves[2][MaxOctaves];
	double Amplitudes[MaxOctaves] = {};
	double InputFactor = 1.0;    // 2^firstOctave
	double OctaveValueFactor = 1.0;   // 2^(n-1) / (2^n - 1)
	double ValueFactor = 1.0;
	int32 NumOctaves = 0;

	void Init(int32 Seed, int32 FirstOctave, TConstArrayView<double> InAmplitudes);
	double Sample(double X, double Y, double Z) const;
};

/** Vrijednosti u jednom cvoru resetke; izmedu cvorova se interpoliraju (Minecraft: celija 4x8x4). */
struct FCaveFields
{
	float Entrances = 1.f;
	float Cheese = 1.f;
	float Spaghetti = 1.f;
	float Pillars = -1.f;
	float NoodleToggle = -1.f;
	float NoodleThickness = 0.f;
	float NoodleRidgeA = 1.f;
	float NoodleRidgeB = 1.f;
};

/**
 * Sve sto generacija spilja treba: postavke + noiseovi izvedeni iz seeda.
 * Gradi se jednom (AVoxelWorld::MakeChunkGenParams), dalje je nepromjenjivo, pa
 * ga worker threadovi dijele kroz TSharedPtr u FChunkGenParams.
 */
struct FCaveGenData
{
	FCaveConfig Config;
	int32 SeaLevel = 0;

	FVoxelNormalNoise CaveEntrance;
	FVoxelNormalNoise Spaghetti3DRarity;
	FVoxelNormalNoise Spaghetti3D1;
	FVoxelNormalNoise Spaghetti3D2;
	FVoxelNormalNoise Spaghetti3DThickness;
	FVoxelNormalNoise SpaghettiRoughness;
	FVoxelNormalNoise SpaghettiRoughnessModulator;
	FVoxelNormalNoise CaveLayer;
	FVoxelNormalNoise CaveCheese;
	FVoxelNormalNoise Spaghetti2D;
	FVoxelNormalNoise Spaghetti2DModulator;
	FVoxelNormalNoise Spaghetti2DThickness;
	FVoxelNormalNoise Spaghetti2DElevation;
	FVoxelNormalNoise Pillar;
	FVoxelNormalNoise PillarRareness;
	FVoxelNormalNoise PillarThickness;
	FVoxelNormalNoise Noodle;
	FVoxelNormalNoise NoodleThickness;
	FVoxelNormalNoise NoodleRidgeA;
	FVoxelNormalNoise NoodleRidgeB;
};

/**
 * Spilje po Minecraftovim formulama (Docs/PLAN_Underground.md). Cista funkcija
 * seeda i koordinata: celija je izdubljena ili nije, neovisno o tome koji se
 * chunk generira. Nasa visina Z odgovara Minecraftovom Y = Z + 63 - SeaLevel.
 */
class MINECRAFTCLONE_API FCaveGenerator
{
public:
	/** Stranica celije resetke vodoravno i okomito (Minecraft: size_horizontal 1, size_vertical 2). */
	static constexpr int32 CellShiftXY = 2;   // 4 bloka
	static constexpr int32 CellShiftZ = 3;    // 8 blokova

	static TSharedPtr<const FCaveGenData, ESPMode::ThreadSafe> Build(int32 WorldSeed, int32 SeaLevel, const FCaveConfig& Config);

	/** Vrijednosti u cvoru resetke na grid poziciji (X,Y,Z). */
	static void SampleNode(const FCaveGenData& Data, int32 X, int32 Y, int32 Z, FCaveFields& Out);

	/** Trilinearna interpolacija 8 cvorova celije (redoslijed: indeks = dx + 2*dy + 4*dz). */
	static FCaveFields Interpolate(const FCaveFields Nodes[8], float FX, float FY, float FZ);

	/**
	 * Je li celija na visini Z izdubljena. Depth = koliko je blokova ispod
	 * povrsine svog stupca (0 = najgornji blok).
	 */
	static bool IsCarved(const FCaveGenData& Data, const FCaveFields& Fields, int32 Z, int32 Depth);

	/** Isto, za jednu tocku: sam uzorkuje 8 cvorova celije. Daje isti rezultat kao FCaveLattice. */
	static bool IsCarvedAt(const FCaveGenData& Data, int32 X, int32 Y, int32 Z, int32 Depth);

	/**
	 * Zastita mora: celija se ne smije izdubiti ako je u blizini stupac pod
	 * morem, a ona je iznad kore ispod njegova dna. MinNearbyHeight = najniza
	 * povrsina u krugu SeaGuardRadius stupaca.
	 */
	static bool IsSeaProtected(const FCaveGenData& Data, int32 Z, int32 MinNearbyHeight)
	{
		return MinNearbyHeight < Data.SeaLevel && Z >= MinNearbyHeight - Data.Config.SeaCrustDepth;
	}
};

/** Resetka cvorova jednog chunka: uzorkuje se jednom, pa se iz nje interpolira svaka celija. */
class MINECRAFTCLONE_API FCaveLattice
{
public:
	/** Origin = grid pozicija prvog stupca chunka; resetka pokriva Z od dna svijeta do MaxZ. */
	FCaveLattice(const FCaveGenData& InData, const FIntVector& Origin, int32 MaxZ);

	bool IsCarved(int32 LocalX, int32 LocalY, int32 Z, int32 Depth) const;

private:
	const FCaveGenData& Data;
	TArray<FCaveFields> Nodes;
	int32 NodesXY = 0;
	int32 NodesZ = 0;
};
