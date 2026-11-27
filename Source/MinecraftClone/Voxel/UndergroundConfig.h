#pragma once

#include "CoreMinimal.h"
#include "CaveGenerator.h"
#include "UndergroundGenerator.h"

/**
 * Postavke generacije podzemlja iz Content/Data/WorldGen/Underground.json
 * (Docs/PLAN_Underground.md). Polje koje u datoteci nedostaje zadrzava
 * Minecraftov default; bez datoteke vrijede svi defaulti (+ Warning u logu).
 */
struct MINECRAFTCLONE_API FUndergroundConfig
{
	FCaveConfig Caves;

	/** "layers" - prazno (blokovi Air) ako dijela nema u datoteci */
	FStoneLayers Layers;

	/** "features" - nakupine kamena i rude, redom kojim se postavljaju */
	TArray<FOreFeature> Features;

	static FUndergroundConfig Load();
};
