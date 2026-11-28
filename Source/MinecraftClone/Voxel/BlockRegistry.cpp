#include "BlockRegistry.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Engine/Texture2D.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
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

	/** Attenuation za zvukove blokova (radi ga Scripts/import_block_sounds.py) */
	const TCHAR* GBlockSoundAttenuationPath = TEXT("/Game/Audio/SA_BlockSounds.SA_BlockSounds");

	/**
	 * JSON pise citljivu putanju paketa ("/Game/Audio/Blocks/Stone/Hit_1"), a
	 * FSoftObjectPath treba i ime objekta - dopisi ga ako fali.
	 */
	FSoftObjectPath MakeAssetPath(const FString& PackagePath)
	{
		if (PackagePath.Contains(TEXT(".")))
		{
			return FSoftObjectPath(PackagePath);
		}

		FString AssetName;
		PackagePath.Split(TEXT("/"), nullptr, &AssetName, ESearchCase::CaseSensitive, ESearchDir::FromEnd);
		return FSoftObjectPath(FString::Printf(TEXT("%s.%s"), *PackagePath, *AssetName));
	}

	/** Procitaj polje s listom putanja do zvukova iz jednog unosa BlockSounds.json */
	void ReadSoundPaths(const TSharedPtr<FJsonObject>& Entry, const TCHAR* Field,
		TArray<FSoftObjectPath>& OutPaths)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Entry->TryGetArrayField(Field, Values))
		{
			return;
		}

		for (const TSharedPtr<FJsonValue>& Value : *Values)
		{
			FString PackagePath;
			if (Value.IsValid() && Value->TryGetString(PackagePath) && !PackagePath.IsEmpty())
			{
				OutPaths.Add(MakeAssetPath(PackagePath));
			}
		}
	}

	/**
	 * Polja bloka koja JsonObjectToUStruct ne zna procitati: boolovi s citljivim
	 * imenom, svojstva stanja, varijante, obris i model (Docs/PLAN_Torches.md).
	 */
	void ReadBlockExtras(const TSharedPtr<FJsonObject>& Entry, FBlockDefinition& Def)
	{
		const FString BlockName = StaticEnum<EBlockType>()->GetNameStringByValue((int64)Def.BlockType);

		Entry->TryGetBoolField(TEXT("requiresTool"), Def.bRequiresTool);
		Entry->TryGetBoolField(TEXT("collision"), Def.bHasCollision);
		Entry->TryGetBoolField(TEXT("blocksLight"), Def.bBlocksLight);
		Entry->TryGetBoolField(TEXT("sturdy"), Def.bSturdy);

		// "properties": { "facing": ["north", "south", "west", "east"] }
		const TSharedPtr<FJsonObject>* Properties = nullptr;
		if (Entry->TryGetObjectField(TEXT("properties"), Properties))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Properties)->Values)
			{
				FBlockStateProperty Property;
				Property.Name = Pair.Key;
				const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
				if (Pair.Value.IsValid() && Pair.Value->TryGetArray(Values))
				{
					for (const TSharedPtr<FJsonValue>& Value : *Values)
					{
						Property.Values.Add(Value->AsString());
					}
				}
				if (Property.Values.Num() > 0)
				{
					Def.StateProperties.Add(Property);
				}
			}
			if (Def.GetNumStates() > 256)
			{
				UE_LOG(LogTemp, Error, TEXT("BlockRegistry: blok '%s' ima vise od 256 stanja - svojstva odbacena"), *BlockName);
				Def.StateProperties.Reset();
			}
		}

		// "variants": { "facing=east": { "yaw": 0 }, ... } - kao Minecraftov blockstates JSON
		const TSharedPtr<FJsonObject>* Variants = nullptr;
		if (Entry->TryGetObjectField(TEXT("variants"), Variants))
		{
			Def.StateYaw.Init(0.f, Def.GetNumStates());
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Variants)->Values)
			{
				const TSharedPtr<FJsonObject>* Variant = nullptr;
				if (!Pair.Value.IsValid() || !Pair.Value->TryGetObject(Variant))
				{
					continue;
				}

				// Kljuc "svojstvo=vrijednost[,svojstvo=vrijednost]" -> indeks stanja
				uint8 State = 0;
				bool bKeyOk = true;
				TArray<FString> Parts;
				Pair.Key.ParseIntoArray(Parts, TEXT(","));
				for (const FString& Part : Parts)
				{
					FString Name, Value;
					if (!Part.Split(TEXT("="), &Name, &Value) || Def.GetStatePropertyValue(0, Name).IsEmpty())
					{
						bKeyOk = false;
						break;
					}
					const uint8 Before = State;
					State = Def.WithStatePropertyValue(State, Name, Value);
					bKeyOk &= State != Before || Def.GetStatePropertyValue(State, Name) == Value;
				}
				if (!bKeyOk)
				{
					UE_LOG(LogTemp, Error, TEXT("BlockRegistry: blok '%s' ima nepoznatu varijantu '%s'"), *BlockName, *Pair.Key);
					continue;
				}

				double Yaw = 0.0;
				(*Variant)->TryGetNumberField(TEXT("yaw"), Yaw);
				Def.StateYaw[State] = (float)Yaw;
			}
		}

		// "shape": [x1, y1, z1, x2, y2, z2] u Minecraftovim pikselima celije
		const TArray<TSharedPtr<FJsonValue>>* Shape = nullptr;
		if (Entry->TryGetArrayField(TEXT("shape"), Shape))
		{
			if (Shape->Num() == 6)
			{
				const FVector A = BlockModel::McToLocal(FVector3f((float)(*Shape)[0]->AsNumber(), (float)(*Shape)[1]->AsNumber(), (float)(*Shape)[2]->AsNumber()), 1.f);
				const FVector B = BlockModel::McToLocal(FVector3f((float)(*Shape)[3]->AsNumber(), (float)(*Shape)[4]->AsNumber(), (float)(*Shape)[5]->AsNumber()), 1.f);
				Def.Shape = FBox(A.ComponentMin(B), A.ComponentMax(B));
				Def.bHasShape = true;
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("BlockRegistry: blok '%s' - 'shape' treba 6 brojeva"), *BlockName);
			}
		}

		const TSharedPtr<FJsonObject>* ModelJson = nullptr;
		if (Entry->TryGetObjectField(TEXT("model"), ModelJson))
		{
			TSharedPtr<FBlockModel> Model = MakeShared<FBlockModel>();
			FString Error;
			if (BlockModel::Parse(*ModelJson, *Model, Error))
			{
				Def.Model = Model;
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("BlockRegistry: blok '%s' - %s"), *BlockName, *Error);
			}
		}
	}

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
	LoadBiomesFromJson();
	LoadBlockSoundsFromJson();

	// Fallback nakon oba loadera - rupa u JSON-u ne smije znaciti rupu u igri
	RegisterFallbackBlocks();
	RegisterFallbackItems();

	UE_LOG(LogTemp, Log, TEXT("BlockRegistry: Initialized with %d blocks, %d items, %d biomes and %d sound grupa"),
		BlockDefinitions.Num(), ItemDefinitions.Num(), BiomeDefinitions.Num(), BlockSoundSets.Num());
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

		// JsonObjectToUStruct trazi polje imena "bRequiresTool" (authored name
		// bool propertyja); JSON koristi citljiviji "requiresTool" pa se boolovi
		// i slozena polja ucitavaju rucno.
		ReadBlockExtras(*EntryObject, Def);

		if (Def.Mesh.IsNull())
		{
			Def.Mesh = FSoftObjectPath(GDefaultBlockMeshPath);
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

void UBlockRegistry::LoadBlockSoundsFromJson()
{
	TArray<TSharedPtr<FJsonValue>> Entries;
	if (!LoadJsonArrayFromContentData(TEXT("BlockSounds.json"), Entries))
	{
		UE_LOG(LogTemp, Error, TEXT("BlockRegistry: bez BlockSounds.json blokovi nemaju zvuk"));
		return;
	}

	const UEnum* GroupEnum = StaticEnum<EBlockSoundGroup>();

	for (int32 Index = 0; Index < Entries.Num(); ++Index)
	{
		const TSharedPtr<FJsonObject>* EntryObject = nullptr;
		if (!Entries[Index].IsValid() || !Entries[Index]->TryGetObject(EntryObject))
		{
			UE_LOG(LogTemp, Error, TEXT("BlockRegistry: BlockSounds.json unos #%d se ne moze parsirati - preskacem"), Index);
			continue;
		}

		FString GroupName;
		const int64 GroupValue = (*EntryObject)->TryGetStringField(TEXT("group"), GroupName)
			? GroupEnum->GetValueByNameString(GroupName)
			: INDEX_NONE;

		if (GroupValue == INDEX_NONE || (EBlockSoundGroup)GroupValue == EBlockSoundGroup::None)
		{
			UE_LOG(LogTemp, Error, TEXT("BlockRegistry: BlockSounds.json unos #%d ima nepoznatu grupu '%s' - preskacem"),
				Index, *GroupName);
			continue;
		}

		FBlockSoundSet Set;
		ReadSoundPaths(*EntryObject, TEXT("hit"), Set.HitSounds);
		ReadSoundPaths(*EntryObject, TEXT("break"), Set.BreakSounds);
		ReadSoundPaths(*EntryObject, TEXT("place"), Set.PlaceSounds);

		BlockSoundSets.Add((EBlockSoundGroup)GroupValue, Set);
	}
}

EBlockSoundGroup UBlockRegistry::GetBlockSoundGroup(EBlockType BlockType) const
{
	const FBlockDefinition* Def = GetBlockDefinition(BlockType);
	const EBlockSoundGroup Group = Def ? Def->SoundGroup : EBlockSoundGroup::None;

	// Blok bez grupe (ili s grupom koje nema u JSON-u) zvuci kao kamen -
	// radije kriv zvuk nego tisina, ista logika kao sivi fallback blok.
	return BlockSoundSets.Contains(Group) ? Group : EBlockSoundGroup::Stone;
}

USoundBase* UBlockRegistry::GetRandomBlockSound(EBlockType BlockType, EBlockSoundEvent Event)
{
	const EBlockSoundGroup Group = GetBlockSoundGroup(BlockType);

	const FBlockDefinition* Def = GetBlockDefinition(BlockType);
	if ((!Def || Def->SoundGroup != Group) && !ReportedSoundFallbacks.Contains(BlockType))
	{
		ReportedSoundFallbacks.Add(BlockType);
		UE_LOG(LogTemp, Error, TEXT("BlockRegistry: blok '%s' nema sound grupu - koristim Stone"),
			*StaticEnum<EBlockType>()->GetNameStringByValue((int64)BlockType));
	}

	const FBlockSoundSet* Set = BlockSoundSets.Find(Group);
	if (!Set)
	{
		return nullptr;
	}

	const TArray<FSoftObjectPath>& Paths = Set->GetSounds(Event);
	if (Paths.Num() == 0)
	{
		return nullptr;
	}

	const FSoftObjectPath& Path = Paths[FMath::RandRange(0, Paths.Num() - 1)];
	const FName CacheKey(*Path.ToString());
	if (const TObjectPtr<USoundBase>* Cached = BlockSoundCache.Find(CacheKey))
	{
		return *Cached;
	}

	USoundBase* Sound = Cast<USoundBase>(Path.TryLoad());
	if (!Sound)
	{
		UE_LOG(LogTemp, Error, TEXT("BlockRegistry: zvuk ne postoji na %s"), *Path.ToString());
	}

	BlockSoundCache.Add(CacheKey, Sound);
	return Sound;
}

USoundAttenuation* UBlockRegistry::GetBlockSoundAttenuation()
{
	if (!bBlockSoundAttenuationLoaded)
	{
		bBlockSoundAttenuationLoaded = true;
		BlockSoundAttenuation = Cast<USoundAttenuation>(
			FSoftObjectPath(GBlockSoundAttenuationPath).TryLoad());

		if (!BlockSoundAttenuation)
		{
			UE_LOG(LogTemp, Warning, TEXT("BlockRegistry: nema %s - zvukovi blokova bit ce bez prostornog pada"),
				GBlockSoundAttenuationPath);
		}
	}

	return BlockSoundAttenuation;
}

void UBlockRegistry::LoadBiomesFromJson()
{
	TArray<TSharedPtr<FJsonValue>> Entries;
	if (LoadJsonArrayFromContentData(TEXT("Biomes.json"), Entries))
	{
		for (int32 Index = 0; Index < Entries.Num(); ++Index)
		{
			const TSharedPtr<FJsonObject>* EntryObject = nullptr;
			FBiomeDefinition Def;

			if (!Entries[Index].IsValid() || !Entries[Index]->TryGetObject(EntryObject) ||
				!FJsonObjectConverter::JsonObjectToUStruct(EntryObject->ToSharedRef(), &Def))
			{
				UE_LOG(LogTemp, Error, TEXT("BlockRegistry: Biomes.json unos #%d se ne moze parsirati - preskacem"), Index);
				continue;
			}

			if (!Def.IsValid())
			{
				UE_LOG(LogTemp, Warning, TEXT("BlockRegistry: Biomes.json unos #%d nema 'name' - preskacem"), Index);
				continue;
			}

			BiomeDefinitions.Add(Def);
		}
	}

	// Lista nikad nije prazna: bijeli fallback biom = danasnji izgled bez tinta
	if (BiomeDefinitions.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("BlockRegistry: nijedan biom nije ucitan - koristim fallback (bijeli tint, bez bioma)"));
		FBiomeDefinition Fallback;
		Fallback.Name = TEXT("Fallback");
		Fallback.DisplayName = FText::FromString(TEXT("Fallback"));
		BiomeDefinitions.Add(Fallback);
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

UTexture2D* UBlockRegistry::GetItemIconTexture(EItemType ItemType)
{
	if (const TObjectPtr<UTexture2D>* Cached = ItemIconCache.Find(ItemType))
	{
		return *Cached;
	}

	UTexture2D* Texture = nullptr;
	FString Path;
	if (const FItemDefinition* Def = GetItemDefinition(ItemType))
	{
		if (Def->Display.Type == TEXT("block") && !Def->Display.Block.IsEmpty())
		{
			// Generirana izometrijska kocka: /Game/Items/Generated/T_Item_<Block>
			// (generira je Tools > MinecraftClone > Generate Items Sprites)
			Path = FString::Printf(
				TEXT("/Game/Items/Generated/T_Item_%s.T_Item_%s"),
				*Def->Display.Block, *Def->Display.Block);
		}
		else if (Def->Display.Type == TEXT("sprite") && !Def->Display.Texture.IsEmpty())
		{
			// Rucno nacrtan sprite: /Game/Items/Textures/T_Item_<Texture>
			Path = FString::Printf(
				TEXT("/Game/Items/Textures/T_Item_%s.T_Item_%s"),
				*Def->Display.Texture, *Def->Display.Texture);
		}
	}

	if (!Path.IsEmpty())
	{
		Texture = Cast<UTexture2D>(FSoftObjectPath(Path).TryLoad());
		if (!Texture)
		{
			UE_LOG(LogTemp, Error, TEXT("BlockRegistry: ikona itema ne postoji na %s"), *Path);
		}
	}

	ItemIconCache.Add(ItemType, Texture);
	return Texture;
}

UMaterialInterface* UBlockRegistry::GetItemSpriteMaterial()
{
	if (!bItemSpriteMaterialLoaded)
	{
		bItemSpriteMaterialLoaded = true;
		ItemSpriteMaterial = Cast<UMaterialInterface>(FSoftObjectPath(
			TEXT("/Game/Items/Generated/Materials/M_ItemSprite.M_ItemSprite")).TryLoad());
		if (!ItemSpriteMaterial)
		{
			UE_LOG(LogTemp, Error, TEXT("BlockRegistry: M_ItemSprite ne postoji - pokreni Tools > MinecraftClone > Build Block Materials"));
		}
	}

	return ItemSpriteMaterial;
}

const FItemExtrudedMeshData* UBlockRegistry::GetItemExtrudedMesh(EItemType ItemType)
{
	if (const TSharedPtr<FItemExtrudedMeshData>* Cached = ItemExtrudedMeshCache.Find(ItemType))
	{
		return Cached->Get();
	}

	TSharedPtr<FItemExtrudedMeshData> MeshData;
	const FItemDefinition* Def = GetItemDefinition(ItemType);
	if (Def && Def->Display.Type == TEXT("sprite"))
	{
		if (UTexture2D* Texture = GetItemIconTexture(ItemType))
		{
			FItemExtrudedMeshData Built = FItemMeshExtruder::Extrude(Texture);
			if (Built.IsValid())
			{
				MeshData = MakeShared<FItemExtrudedMeshData>(MoveTemp(Built));
			}
			// Neuspjeh je vec logiran u Extrude(); nullptr u cacheu znaci
			// da pozivatelji trajno padaju na flat quad
		}
	}

	ItemExtrudedMeshCache.Add(ItemType, MeshData);
	return MeshData.Get();
}

UMaterialInterface* UBlockRegistry::GetBlockMaterialForItem(EItemType ItemType)
{
	if (const TObjectPtr<UMaterialInterface>* Cached = ItemBlockMaterialCache.Find(ItemType))
	{
		return *Cached;
	}

	UMaterialInterface* Material = nullptr;
	const FBlockDefinition* Def = GetBlockForItem(ItemType);
	if (Def && Def->Material.IsValid())
	{
		Material = Cast<UMaterialInterface>(Def->Material.TryLoad());
	}

	// Blok s biome tintom: item u ruci nosi default tint (prvi biom u JSON-u),
	// kao Minecraft - item je apstraktan, tek blok u svijetu ima biom
	if (Material && Def->BiomeTint != EBiomeTintType::None)
	{
		UMaterialInstanceDynamic* TintedMID = UMaterialInstanceDynamic::Create(Material, this);
		TintedMID->SetVectorParameterValue(TEXT("TintFallback"), GetDefaultBiomeTint(Def->BiomeTint));
		Material = TintedMID;
	}

	ItemBlockMaterialCache.Add(ItemType, Material);
	return Material;
}

// === Biome API ===

const FBiomeDefinition& UBlockRegistry::GetBiome(int32 Index) const
{
	// BiomeDefinitions nikad nije prazan (fallback u LoadBiomesFromJson);
	// indeks izvan raspona pada na prvi biom umjesto crasha
	return BiomeDefinitions.IsValidIndex(Index) ? BiomeDefinitions[Index] : BiomeDefinitions[0];
}

FBiomeDefinition UBlockRegistry::GetBiomeCopy(int32 Index) const
{
	return GetBiome(Index);
}

int32 UBlockRegistry::GetBiomeCount() const
{
	return BiomeDefinitions.Num();
}

FLinearColor UBlockRegistry::GetDefaultBiomeTint(EBiomeTintType TintType) const
{
	if (TintType == EBiomeTintType::None)
	{
		return FLinearColor::White;
	}
	const FBiomeDefinition& Biome = GetBiome(0);
	return TintType == EBiomeTintType::Foliage ? Biome.FoliageTint : Biome.GrassTint;
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
