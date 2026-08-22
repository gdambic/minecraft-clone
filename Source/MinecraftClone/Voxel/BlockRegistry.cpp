#include "BlockRegistry.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	// Zajednicki defaulti - JSON ih navodi samo kad odstupa
	const TCHAR* GDefaultBlockMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* GDefaultHighlightPath = TEXT("/Game/Blueprints/Materials/M_BlockHighlight.M_BlockHighlight");

	/** Ucitaj Content/Data/<FileName> i deserijaliziraj kao JSON array. */
	bool LoadJsonArrayFromContentData(const FString& FileName, TArray<TSharedPtr<FJsonValue>>& OutEntries)
	{
		const FString FullPath = FPaths::ProjectContentDir() / TEXT("Data") / FileName;

		FString JsonString;
		if (!FFileHelper::LoadFileToString(JsonString, *FullPath))
		{
			UE_LOG(LogTemp, Error, TEXT("BlockRegistry: ne mogu ucitati %s - svi tipovi ce dobiti fallback definicije"), *FullPath);
			return false;
		}

		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
		if (!FJsonSerializer::Deserialize(Reader, OutEntries))
		{
			UE_LOG(LogTemp, Error, TEXT("BlockRegistry: neispravan JSON u %s - svi tipovi ce dobiti fallback definicije"), *FullPath);
			return false;
		}

		return true;
	}
}

void UBlockRegistry::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	LoadBlocksFromJson();
	LoadItemsFromJson();

	// Fallback nakon oba loadera - rupa u JSON-u ne smije znaciti rupu u igri
	RegisterFallbackBlocks();
	RegisterFallbackItems();

	UE_LOG(LogTemp, Log, TEXT("BlockRegistry: Initialized with %d blocks and %d items"),
		BlockDefinitions.Num(), ItemDefinitions.Num());
}

void UBlockRegistry::RegisterBlock(const FBlockDefinition& Definition)
{
	if (Definition.BlockType == EBlockType::Air)
	{
		return;
	}

	BlockDefinitions.Add(Definition.BlockType, Definition);

	// Dodaj u cache mape
	if (Definition.DropItemType != EItemType::None)
	{
		BlockToItemMap.Add(Definition.BlockType, Definition.DropItemType);
	}

	if (Definition.PlaceableFromItem != EItemType::None)
	{
		ItemToBlockMap.Add(Definition.PlaceableFromItem, Definition.BlockType);
	}
}

void UBlockRegistry::RegisterItem(const FItemDefinition& Definition)
{
	if (Definition.ItemType == EItemType::None)
	{
		return;
	}

	ItemDefinitions.Add(Definition.ItemType, Definition);
}

void UBlockRegistry::LoadBlocksFromJson()
{
	TArray<TSharedPtr<FJsonValue>> Entries;
	if (!LoadJsonArrayFromContentData(TEXT("Blocks.json"), Entries))
	{
		return;
	}

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* EntryObject = nullptr;
		FBlockDefinition Def;

		// Neispravan unos preskacemo pojedinacno - jedan tipfeler ne smije
		// srusiti cijelu datoteku (blok dobije fallback u RegisterFallbackBlocks)
		if (!Entries[Index].IsValid() || !Entries[Index]->TryGetObject(EntryObject) ||
			!FJsonObjectConverter::JsonObjectToUStruct(EntryObject->ToSharedRef(), &Def))
		{
			UE_LOG(LogTemp, Error, TEXT("BlockRegistry: Blocks.json unos #%d se ne moze parsirati - preskacem"), Index);
			continue;
		}

		if (Def.BlockType == EBlockType::Air)
		{
			UE_LOG(LogTemp, Warning, TEXT("BlockRegistry: Blocks.json unos #%d nema 'blockType' (ili je Air) - preskacem"), Index);
			continue;
		}

		if (BlockDefinitions.Contains(Def.BlockType))
		{
			UE_LOG(LogTemp, Warning, TEXT("BlockRegistry: Blocks.json unos #%d duplicira blok '%s' - gazim prethodni"),
				Index, *StaticEnum<EBlockType>()->GetNameStringByValue((int64)Def.BlockType));
		}

		if (Def.Mesh.IsNull())
		{
			Def.Mesh = FSoftObjectPath(GDefaultBlockMeshPath);
		}
		if (Def.HighlightMaterial.IsNull())
		{
			Def.HighlightMaterial = FSoftObjectPath(GDefaultHighlightPath);
		}

		RegisterBlock(Def);
	}
}

void UBlockRegistry::LoadItemsFromJson()
{
	TArray<TSharedPtr<FJsonValue>> Entries;
	if (!LoadJsonArrayFromContentData(TEXT("Items.json"), Entries))
	{
		return;
	}

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* EntryObject = nullptr;
		FItemDefinition Def;

		if (!Entries[Index].IsValid() || !Entries[Index]->TryGetObject(EntryObject) ||
			!FJsonObjectConverter::JsonObjectToUStruct(EntryObject->ToSharedRef(), &Def))
		{
			UE_LOG(LogTemp, Error, TEXT("BlockRegistry: Items.json unos #%d se ne moze parsirati - preskacem"), Index);
			continue;
		}

		if (Def.ItemType == EItemType::None)
		{
			UE_LOG(LogTemp, Warning, TEXT("BlockRegistry: Items.json unos #%d nema 'itemType' (ili je None) - preskacem"), Index);
			continue;
		}

		if (ItemDefinitions.Contains(Def.ItemType))
		{
			UE_LOG(LogTemp, Warning, TEXT("BlockRegistry: Items.json unos #%d duplicira item '%s' - gazim prethodni"),
				Index, *StaticEnum<EItemType>()->GetNameStringByValue((int64)Def.ItemType));
		}

		if (Def.Mesh.IsNull())
		{
			Def.Mesh = FSoftObjectPath(GDefaultBlockMeshPath);
		}

		RegisterItem(Def);
	}
}

void UBlockRegistry::RegisterFallbackBlocks()
{
	const UEnum* Enum = StaticEnum<EBlockType>();

	// NumEnums()-1 preskace autogenerirani _MAX unos
	for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
	{
		const EBlockType Type = (EBlockType)Enum->GetValueByIndex(Index);
		if (Type == EBlockType::Air || BlockDefinitions.Contains(Type))
		{
			continue;
		}

		const FString TypeName = Enum->GetNameStringByIndex(Index);
		UE_LOG(LogTemp, Error, TEXT("BlockRegistry: nema JSON definicije za blok '%s' - koristim fallback (siva kocka)"), *TypeName);

		// Bez materijala - ISM/mesh ostaje na default sivom engine materijalu.
		// Bez dropa i placementa: fallback blok postoji u svijetu, ali nije item.
		FBlockDefinition Def;
		Def.BlockType = Type;
		Def.DisplayName = FText::FromString(TypeName);
		Def.Mesh = FSoftObjectPath(GDefaultBlockMeshPath);
		Def.HighlightMaterial = FSoftObjectPath(GDefaultHighlightPath);
		RegisterBlock(Def);
	}
}

void UBlockRegistry::RegisterFallbackItems()
{
	const UEnum* Enum = StaticEnum<EItemType>();

	for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
	{
		const EItemType Type = (EItemType)Enum->GetValueByIndex(Index);
		if (Type == EItemType::None || ItemDefinitions.Contains(Type))
		{
			continue;
		}

		const FString TypeName = Enum->GetNameStringByIndex(Index);
		UE_LOG(LogTemp, Error, TEXT("BlockRegistry: nema JSON definicije za item '%s' - koristim fallback (siva kocka)"), *TypeName);

		FItemDefinition Def;
		Def.ItemType = Type;
		Def.DisplayName = FText::FromString(TypeName);
		Def.Mesh = FSoftObjectPath(GDefaultBlockMeshPath);
		RegisterItem(Def);
	}
}

// === Block API ===

const FBlockDefinition* UBlockRegistry::GetBlockDefinition(EBlockType BlockType) const
{
	return BlockDefinitions.Find(BlockType);
}

FBlockDefinition UBlockRegistry::GetBlockDefinitionCopy(EBlockType BlockType) const
{
	const FBlockDefinition* Found = BlockDefinitions.Find(BlockType);
	return Found ? *Found : FBlockDefinition();
}

const FBlockDefinition* UBlockRegistry::GetBlockForItem(EItemType ItemType) const
{
	const EBlockType* BlockType = ItemToBlockMap.Find(ItemType);
	if (BlockType)
	{
		return GetBlockDefinition(*BlockType);
	}
	return nullptr;
}

EBlockType UBlockRegistry::GetBlockTypeForItem(EItemType ItemType) const
{
	const EBlockType* BlockType = ItemToBlockMap.Find(ItemType);
	return BlockType ? *BlockType : EBlockType::Air;
}

TArray<FBlockDefinition> UBlockRegistry::GetAllBlockDefinitions() const
{
	TArray<FBlockDefinition> Result;
	BlockDefinitions.GenerateValueArray(Result);
	return Result;
}

// === Item API ===

const FItemDefinition* UBlockRegistry::GetItemDefinition(EItemType ItemType) const
{
	return ItemDefinitions.Find(ItemType);
}

FItemDefinition UBlockRegistry::GetItemDefinitionCopy(EItemType ItemType) const
{
	const FItemDefinition* Found = ItemDefinitions.Find(ItemType);
	return Found ? *Found : FItemDefinition();
}

EItemType UBlockRegistry::GetItemTypeForBlock(EBlockType BlockType) const
{
	const EItemType* ItemType = BlockToItemMap.Find(BlockType);
	return ItemType ? *ItemType : EItemType::None;
}

bool UBlockRegistry::CanItemBePlaced(EItemType ItemType) const
{
	return ItemToBlockMap.Contains(ItemType);
}

TArray<FItemDefinition> UBlockRegistry::GetAllItemDefinitions() const
{
	TArray<FItemDefinition> Result;
	ItemDefinitions.GenerateValueArray(Result);
	return Result;
}

// === Static Helper ===

UBlockRegistry* UBlockRegistry::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		return nullptr;
	}

	return GameInstance->GetSubsystem<UBlockRegistry>();
}
