#pragma once

#include "CoreMinimal.h"

/**
 * Utility klasa za odabir bioma stupca (X,Y) - niskofrekventni Perlin noise
 * podijeljen na jednake pragove. Cista funkcija kao FTerrainGenerator: isti
 * ulazi uvijek daju isti izlaz, ovisnost o seedu ide kroz BiomeOffsetX/Y
 * (izvodi ih AVoxelWorld::GenerateWorld iz WorldSeed). Biom se zato nigdje
 * ne sprema - uvijek se moze ponovno izracunati iz pozicije.
 */
class MINECRAFTCLONE_API FBiomeGenerator
{
public:
	/**
	 * Indeks bioma (pozicija u listi UBlockRegistry bioma) za stupac (X,Y).
	 * BiomeScale mora biti znatno manji od terenskog NoiseScale da biomi
	 * budu velike regije, a ne sarene tockice.
	 */
	static int32 GetBiomeIndexAt(int32 X, int32 Y, float BiomeOffsetX, float BiomeOffsetY,
		float BiomeScale, int32 BiomeCount);
};
