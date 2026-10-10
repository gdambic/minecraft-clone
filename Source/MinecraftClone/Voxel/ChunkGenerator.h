#pragma once

#include "CoreMinimal.h"
#include "BlockType.h"
#include "TreeGenerator.h"
#include "VoxelChunk.h"

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
	int32 SurfaceLevel = 24;
	int32 HeightAmplitude = 6;
	int32 DirtLayers = 3;

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
	static TArray<EBlockType> GenerateChunkData(const FChunkGenParams& Params, const FIntPoint& Coord);

	/** Visina povrsine stupca - prosljeduje FTerrainGeneratoru parametre iz Params. */
	static int32 GetTerrainHeight(const FChunkGenParams& Params, int32 X, int32 Y);

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
