#pragma once

#include "CoreMinimal.h"
#include "Math/RandomStream.h"
#include "UObject/ObjectMacros.h"
#include "BlockType.h"

struct FChunkGenParams;

/** Raspodjela visine nakupine: ravnomjerna ili trokutasta (Minecraftov trapezoid bez platoa - najgusce u sredini raspona). */
enum class EOreHeightType : uint8
{
	Uniform,
	Triangle
};

/** Koje blokove nakupina smije zamijeniti i cime ("targets" u Underground.json). */
struct FOreTarget
{
	TArray<EBlockType> Replace;
	EBlockType Block = EBlockType::Air;
};

/**
 * Jedna nakupina kamena ili rude iz Underground.json ("features"). Vrijednosti
 * za Minecraftove nakupine upisuje Scripts/build_underground_config.py iz jara.
 */
struct FOreFeature
{
	FString Name;

	/** Broj pokusaja po chunku: nasumicno izmedu Min i Max, ukljucivo ("attempts" ili "attemptsMin"/"attemptsMax"). */
	int32 AttemptsMin = 1;
	int32 AttemptsMax = 1;

	/** Nakupina se pojavljuje u prosjeku u jednom od ovoliko chunkova ("oneInChunks"); 1 = u svakom. */
	int32 OneInChunks = 1;

	/** Najveci broj blokova nakupine (Minecraftov "size"). */
	int32 Size = 8;

	/** Vjerojatnost (0..1) da se blok nakupine koji dodiruje zrak odbaci. */
	float DiscardOnAirExposure = 0.f;

	EOreHeightType HeightType = EOreHeightType::Uniform;
	int32 MinZ = 0;
	int32 MaxZ = 0;

	TArray<FOreTarget> Targets;

	/** Imena bioma iz JSON-a i njihovi indeksi u registryju (upisuje ih AVoxelWorld). Prazno = svi biomi. */
	TArray<FName> BiomeNames;
	TArray<int32> Biomes;
};

/** Slojevi kamena iz Underground.json ("layers"). Air kao blok = sloj je iskljucen. */
struct FStoneLayers
{
	/** Neprobojno dno: najdonji sloj je pun, sljedeci sve rjedi, kroz BedrockLayers slojeva. */
	EBlockType BedrockBlock = EBlockType::Air;
	int32 BedrockLayers = 5;

	/** Duboki kamen: zamjenjuje DeepReplaces na i ispod DeepFullAtAndBelow, iznad toga sve rjede do DeepNoneAtAndAbove. */
	EBlockType DeepBlock = EBlockType::Air;
	EBlockType DeepReplaces = EBlockType::Stone;
	int32 DeepFullAtAndBelow = -63;
	int32 DeepNoneAtAndAbove = -55;
};

/** Nepromjenjivi podaci podzemlja koje dijele worker threadovi (FChunkGenParams::Underground). */
struct FUndergroundGenData
{
	FStoneLayers Layers;
	TArray<FOreFeature> Features;
};

/**
 * Slojevi kamena i nakupine (Docs/PLAN_Underground.md). Kao i ostatak
 * generacije, cista funkcija seeda i koordinata.
 *
 * Nakupine preko granica chunka rade kao stabla: chunk izracuna nakupine sebe
 * i svojih 8 susjeda, a upise samo blokove koji padaju u njega. Nakupina seze
 * najvise 13 blokova od svog ishodista, pa je 3x3 susjedstvo dovoljno.
 */
class MINECRAFTCLONE_API FUndergroundGenerator
{
public:
	/** Bedrock na dnu i Deepslate u dubini - Minecraftov vertical_gradient (nasumican prijelaz po celiji). */
	static void ApplyLayers(const FChunkGenParams& Params, const FIntPoint& Coord, TArray<EBlockType>& Blocks);

	/** Sve nakupine redom iz FUndergroundGenData::Features; MaxHeight = najvisa povrsina chunka (rano odbacivanje). */
	static void ApplyFeatures(const FChunkGenParams& Params, const FIntPoint& Coord, TArray<EBlockType>& Blocks, int32 MaxHeight);

private:
	/** Jedna nakupina s ishodistem Origin - port Minecraftovog OreFeature.place/doPlace; pise samo u chunk Coord. */
	static void PlaceBlob(const FChunkGenParams& Params, const FOreFeature& Feature, uint32 FeatureSeed,
		const FIntVector& Origin, FRandomStream& Random, const FIntPoint& Coord, TArray<EBlockType>& Blocks, int32 MaxHeight);
};
