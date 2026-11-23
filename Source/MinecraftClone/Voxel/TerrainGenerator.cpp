#include "TerrainGenerator.h"
#include "VoxelChunk.h"

float FTerrainGenerator::FractalNoise2D(int32 X, int32 Y, float NoiseOffsetX, float NoiseOffsetY,
	float NoiseScale, int32 Octaves)
{
	// Fraktalni zbroj oktava: svaka sljedeca duplo vecu frekvenciju i upola manju
	// amplitudu (persistence=0.5, lacunarity=2.0) - niska frekvencija/velika
	// amplituda daje grubi oblik terena, visoke frekvencije dodaju sitne neravnine.
	float Total = 0.f;
	float Frequency = 1.f;
	float Amplitude = 1.f;
	float MaxAmplitude = 0.f;

	for (int32 i = 0; i < FMath::Max(Octaves, 1); i++)
	{
		const float SampleX = (X + NoiseOffsetX) * NoiseScale * Frequency;
		const float SampleY = (Y + NoiseOffsetY) * NoiseScale * Frequency;
		Total += FMath::PerlinNoise2D(FVector2D(SampleX, SampleY)) * Amplitude;

		MaxAmplitude += Amplitude;
		Amplitude *= 0.5f;
		Frequency *= 2.0f;
	}

	return MaxAmplitude > 0.f ? Total / MaxAmplitude : 0.f;
}

int32 FTerrainGenerator::GetColumnHeight(int32 X, int32 Y, float NoiseOffsetX, float NoiseOffsetY,
	float NoiseScale, int32 Octaves, int32 SurfaceLevel, int32 HeightAmplitude)
{
	const float Normalized = FractalNoise2D(X, Y, NoiseOffsetX, NoiseOffsetY, NoiseScale, Octaves);
	const int32 Height = SurfaceLevel + FMath::RoundToInt(Normalized * HeightAmplitude);

	// Donja granica da povrsina nikad ne padne na samo dno svijeta
	// (WorldMinZ mora ostati cvrst sloj ispod najdubljeg terena)
	return FMath::Max(Height, VoxelGrid::WorldMinZ + 1);
}

float FTerrainGenerator::GetMountainLift(int32 X, int32 Y, float NoiseOffsetX, float NoiseOffsetY,
	float NoiseScale, int32 Octaves, float NoiseStart, float NoiseFull, float MaxLift)
{
	const float Noise = FractalNoise2D(X, Y, NoiseOffsetX, NoiseOffsetY, NoiseScale, Octaves);
	return FMath::SmoothStep(NoiseStart, NoiseFull, Noise) * MaxLift;
}
