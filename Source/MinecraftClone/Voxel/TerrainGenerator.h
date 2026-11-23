#pragma once

#include "CoreMinimal.h"

/**
 * Utility klasa za proceduralno racunanje visine terena (fraktalni Perlin noise,
 * kao Minecraftov height-map pristup - bez bioma i spilja).
 */
class MINECRAFTCLONE_API FTerrainGenerator
{
public:
	/**
	 * Visina stupca (Z povrsine) na (X,Y). Cista funkcija - isti ulazi uvijek
	 * daju isti izlaz; ovisnost o seedu ide kroz NoiseOffsetX/Y (vidi
	 * AVoxelWorld::GenerateWorld, koji ih izvodi iz WorldSeed).
	 */
	static int32 GetColumnHeight(int32 X, int32 Y, float NoiseOffsetX, float NoiseOffsetY,
		float NoiseScale, int32 Octaves, int32 SurfaceLevel, int32 HeightAmplitude);

	/**
	 * Podizanje terena planinom na (X,Y), u blokovima (0..MaxLift). Noise
	 * planina ispod NoiseStart ne dize nista; od NoiseStart do NoiseFull
	 * podizanje raste smoothstepom, pa granica planine nema stepenicu.
	 */
	static float GetMountainLift(int32 X, int32 Y, float NoiseOffsetX, float NoiseOffsetY,
		float NoiseScale, int32 Octaves, float NoiseStart, float NoiseFull, float MaxLift);

	/** Fraktalni Perlin (persistence 0.5, lacunarity 2.0), normaliziran na [-1,1]. */
	static float FractalNoise2D(int32 X, int32 Y, float NoiseOffsetX, float NoiseOffsetY,
		float NoiseScale, int32 Octaves);
};
