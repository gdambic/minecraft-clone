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
