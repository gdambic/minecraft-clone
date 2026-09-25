#include "BiomeGenerator.h"

int32 FBiomeGenerator::GetBiomeIndexAt(int32 X, int32 Y, float BiomeOffsetX, float BiomeOffsetY,
	float BiomeScale, int32 BiomeCount)
{
	if (BiomeCount <= 1)
	{
		return 0;
	}

	const float SampleX = (X + BiomeOffsetX) * BiomeScale;
	const float SampleY = (Y + BiomeOffsetY) * BiomeScale;

	// PerlinNoise2D vraca [-1,1]; jedna oktava je dovoljna - granice bioma
	// ionako ne trebaju fraktalne detalje
	const float Normalized = (FMath::PerlinNoise2D(FVector2D(SampleX, SampleY)) + 1.f) * 0.5f;

	// Jednaki pragovi po biomu; clamp jer PerlinNoise2D zna vratiti tocno 1.0
	return FMath::Clamp(FMath::FloorToInt32(Normalized * BiomeCount), 0, BiomeCount - 1);
}
