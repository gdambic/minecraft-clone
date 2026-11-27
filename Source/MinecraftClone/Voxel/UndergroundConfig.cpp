#include "UndergroundConfig.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Class.h"

namespace
{
	void ReadBool(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, bool& Value)
	{
		if (Object.IsValid())
		{
			Object->TryGetBoolField(Field, Value);
		}
	}

	void ReadFloat(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, float& Value)
	{
		double Number = 0.0;
		if (Object.IsValid() && Object->TryGetNumberField(Field, Number))
		{
			Value = (float)Number;
		}
	}

	void ReadInt(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, int32& Value)
	{
		if (Object.IsValid())
		{
			Object->TryGetNumberField(Field, Value);
		}
	}

	TSharedPtr<FJsonObject> Child(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TSharedPtr<FJsonObject>* Found = nullptr;
		return Object.IsValid() && Object->TryGetObjectField(Field, Found) ? *Found : nullptr;
	}

	/** Ime bloka (EBlockType) -> vrijednost; nepoznato ime javlja Error i vraca Air. */
	EBlockType ParseBlock(const FString& Name, const TCHAR* Context)
	{
		const int64 Value = StaticEnum<EBlockType>()->GetValueByNameString(Name);
		if (Value == INDEX_NONE)
		{
			UE_LOG(LogTemp, Error, TEXT("[CAVE] Underground.json: nepoznat blok '%s' (%s)"), *Name, Context);
			return EBlockType::Air;
		}
		return (EBlockType)Value;
	}

	void ReadBlock(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, EBlockType& Value)
	{
		FString Name;
		if (Object.IsValid() && Object->TryGetStringField(Field, Name))
		{
			Value = ParseBlock(Name, Field);
		}
	}

	bool ReadFeature(const TSharedPtr<FJsonObject>& Object, FOreFeature& Feature)
	{
		if (!Object.IsValid() || !Object->TryGetStringField(TEXT("name"), Feature.Name))
		{
			return false;
		}

		int32 Attempts = 1;
		if (Object->TryGetNumberField(TEXT("attempts"), Attempts))
		{
			Feature.AttemptsMin = Feature.AttemptsMax = Attempts;
		}
		ReadInt(Object, TEXT("attemptsMin"), Feature.AttemptsMin);
		ReadInt(Object, TEXT("attemptsMax"), Feature.AttemptsMax);
		ReadInt(Object, TEXT("oneInChunks"), Feature.OneInChunks);
		ReadInt(Object, TEXT("size"), Feature.Size);
		// Nakupina veca od 80 blokova seze dalje od jednog chunka, a generacija gleda samo 3x3 susjedstvo
		if (Feature.Size > 80)
		{
			UE_LOG(LogTemp, Error, TEXT("[CAVE] Underground.json: nakupina '%s' ima size %d, najvise je 80 - smanjujem"),
				*Feature.Name, Feature.Size);
			Feature.Size = 80;
		}
		ReadFloat(Object, TEXT("discardOnAirExposure"), Feature.DiscardOnAirExposure);

		const TSharedPtr<FJsonObject> Height = Child(Object, TEXT("height"));
		FString HeightType;
		if (Height.IsValid() && Height->TryGetStringField(TEXT("type"), HeightType))
		{
			Feature.HeightType = HeightType == TEXT("triangle") ? EOreHeightType::Triangle : EOreHeightType::Uniform;
		}
		ReadInt(Height, TEXT("min"), Feature.MinZ);
		ReadInt(Height, TEXT("max"), Feature.MaxZ);

		const TArray<TSharedPtr<FJsonValue>>* Targets = nullptr;
		if (Object->TryGetArrayField(TEXT("targets"), Targets))
		{
			for (const TSharedPtr<FJsonValue>& TargetValue : *Targets)
			{
				const TSharedPtr<FJsonObject>* TargetObject = nullptr;
				if (!TargetValue.IsValid() || !TargetValue->TryGetObject(TargetObject))
				{
					continue;
				}
				FOreTarget Target;
				ReadBlock(*TargetObject, TEXT("block"), Target.Block);
				TArray<FString> Replace;
				(*TargetObject)->TryGetStringArrayField(TEXT("replace"), Replace);
				for (const FString& Name : Replace)
				{
					const EBlockType Block = ParseBlock(Name, *Feature.Name);
					if (Block != EBlockType::Air)
					{
						Target.Replace.Add(Block);
					}
				}
				if (Target.Block != EBlockType::Air && !Target.Replace.IsEmpty())
				{
					Feature.Targets.Add(MoveTemp(Target));
				}
			}
		}

		TArray<FString> Biomes;
		Object->TryGetStringArrayField(TEXT("biomes"), Biomes);
		for (const FString& Biome : Biomes)
		{
			Feature.BiomeNames.Add(FName(*Biome));
		}

		if (Feature.Targets.IsEmpty())
		{
			UE_LOG(LogTemp, Error, TEXT("[CAVE] Underground.json: nakupina '%s' nema valjan 'targets' - preskacem"), *Feature.Name);
			return false;
		}
		return true;
	}
}

FUndergroundConfig FUndergroundConfig::Load()
{
	FUndergroundConfig Config;

	const FString FullPath = FPaths::ProjectContentDir() / TEXT("Data") / TEXT("WorldGen") / TEXT("Underground.json");
	FString JsonString;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(JsonString, *FullPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CAVE] nema datoteke %s - koristim Minecraftove defaulte"), *FullPath);
		return Config;
	}
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(JsonString), Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("[CAVE] %s nije valjan JSON - koristim Minecraftove defaulte"), *FullPath);
		return Config;
	}

	FCaveConfig& Caves = Config.Caves;
	const TSharedPtr<FJsonObject> CavesObject = Child(Root, TEXT("caves"));
	ReadBool(CavesObject, TEXT("enabled"), Caves.bEnabled);
	ReadFloat(CavesObject, TEXT("sizeScale"), Caves.SizeScale);
	ReadFloat(CavesObject, TEXT("surfaceFactor"), Caves.SurfaceFactor);
	ReadInt(CavesObject, TEXT("seaCrustDepth"), Caves.SeaCrustDepth);
	ReadInt(CavesObject, TEXT("seaGuardRadius"), Caves.SeaGuardRadius);

	const TSharedPtr<FJsonObject> Chambers = Child(CavesObject, TEXT("chambers"));
	ReadBool(Chambers, TEXT("enabled"), Caves.bChambers);
	ReadFloat(Chambers, TEXT("offset"), Caves.ChamberOffset);
	ReadBool(Chambers, TEXT("pillars"), Caves.bPillars);
	ReadBool(Child(CavesObject, TEXT("tunnels")), TEXT("enabled"), Caves.bTunnels);
	ReadBool(Child(CavesObject, TEXT("noodles")), TEXT("enabled"), Caves.bNoodles);
	const TSharedPtr<FJsonObject> Entrances = Child(CavesObject, TEXT("entrances"));
	ReadBool(Entrances, TEXT("enabled"), Caves.bEntrances);
	ReadFloat(Entrances, TEXT("offset"), Caves.EntranceOffset);

	FStoneLayers& Layers = Config.Layers;
	const TSharedPtr<FJsonObject> LayersObject = Child(Root, TEXT("layers"));
	const TSharedPtr<FJsonObject> Bedrock = Child(LayersObject, TEXT("bedrock"));
	ReadBlock(Bedrock, TEXT("block"), Layers.BedrockBlock);
	ReadInt(Bedrock, TEXT("layers"), Layers.BedrockLayers);
	const TSharedPtr<FJsonObject> Deepslate = Child(LayersObject, TEXT("deepslate"));
	ReadBlock(Deepslate, TEXT("block"), Layers.DeepBlock);
	ReadBlock(Deepslate, TEXT("replaces"), Layers.DeepReplaces);
	ReadInt(Deepslate, TEXT("fullAtAndBelow"), Layers.DeepFullAtAndBelow);
	ReadInt(Deepslate, TEXT("noneAtAndAbove"), Layers.DeepNoneAtAndAbove);

	const TArray<TSharedPtr<FJsonValue>>* Features = nullptr;
	if (Root->TryGetArrayField(TEXT("features"), Features))
	{
		for (const TSharedPtr<FJsonValue>& FeatureValue : *Features)
		{
			const TSharedPtr<FJsonObject>* FeatureObject = nullptr;
			FOreFeature Feature;
			if (FeatureValue.IsValid() && FeatureValue->TryGetObject(FeatureObject) && ReadFeature(*FeatureObject, Feature))
			{
				Config.Features.Add(MoveTemp(Feature));
			}
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[CAVE] konfiguracija: %d nakupina, Bedrock=%s (%d slojeva), duboki kamen=%s (pun do Z %d, nestaje na Z %d)"),
		Config.Features.Num(), *StaticEnum<EBlockType>()->GetNameStringByValue((int64)Layers.BedrockBlock), Layers.BedrockLayers,
		*StaticEnum<EBlockType>()->GetNameStringByValue((int64)Layers.DeepBlock), Layers.DeepFullAtAndBelow, Layers.DeepNoneAtAndAbove);

	UE_LOG(LogTemp, Log, TEXT("[CAVE] konfiguracija: spilje=%d dvorane=%d (offset %.2f, stupovi=%d) tuneli=%d prolazi=%d ulazi=%d (offset %.2f) velicina x%.2f, factor %.2f, kora mora %d, radijus %d"),
		Caves.bEnabled, Caves.bChambers, Caves.ChamberOffset, Caves.bPillars, Caves.bTunnels, Caves.bNoodles,
		Caves.bEntrances, Caves.EntranceOffset, Caves.SizeScale, Caves.SurfaceFactor, Caves.SeaCrustDepth, Caves.SeaGuardRadius);
	return Config;
}
