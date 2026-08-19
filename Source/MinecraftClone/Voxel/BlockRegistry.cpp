#include "BlockRegistry.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"

void UBlockRegistry::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	RegisterAllBlocks();
	RegisterAllItems();

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

void UBlockRegistry::RegisterAllBlocks()
{
	// Standardne putanje do asseta
	const FString MeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const FString HighlightPath = TEXT("/Game/Blueprints/Materials/M_BlockHighlight.M_BlockHighlight");

	// Pixel-art materijali generirani skriptom Scripts/build_block_materials.py
	// iz tekstura u /Game/Blocks/Textures. Blok cija tekstura jos nije nacrtana
	// nema MI asset - TryLoad vrati null, blok dobije default sivi materijal i
	// pojavi se u [PERF] NEUSPJEH liniji. Vidi Docs/TEXTURE_SPEC.md.
	const FString DirtMaterial = TEXT("/Game/Blocks/Materials/MI_Dirt.MI_Dirt");
	const FString StoneMaterial = TEXT("/Game/Blocks/Materials/MI_Stone.MI_Stone");
	const FString GrassMaterial = TEXT("/Game/Blocks/Materials/MI_Grass.MI_Grass");
	const FString OakLogMaterial = TEXT("/Game/Blocks/Materials/MI_OakLog.MI_OakLog");
	const FString BirchLogMaterial = TEXT("/Game/Blocks/Materials/MI_BirchLog.MI_BirchLog");
	const FString OakLeavesMaterial = TEXT("/Game/Blocks/Materials/MI_OakLeaves.MI_OakLeaves");
	const FString BirchLeavesMaterial = TEXT("/Game/Blocks/Materials/MI_BirchLeaves.MI_BirchLeaves");
	const FString OakPlankMaterial = TEXT("/Game/Blocks/Materials/MI_OakPlanks.MI_OakPlanks");
	const FString BirchPlankMaterial = TEXT("/Game/Blocks/Materials/MI_BirchPlanks.MI_BirchPlanks");
	const FString CraftingTableMaterial = TEXT("/Game/Blocks/Materials/MI_CraftingTable.MI_CraftingTable");

	// === DIRT ===
	{
		FBlockDefinition Def;
		Def.BlockType = EBlockType::Dirt;
		Def.DropItemType = EItemType::Dirt;
		Def.PlaceableFromItem = EItemType::Dirt;
		Def.DropChance = 1.0f;
		Def.TimeToDestroy = 1.5f;
		Def.DisplayName = NSLOCTEXT("Blocks", "Dirt", "Dirt");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(DirtMaterial);
		Def.HighlightMaterial = FSoftObjectPath(HighlightPath);
		RegisterBlock(Def);
	}

	// === STONE ===
	{
		FBlockDefinition Def;
		Def.BlockType = EBlockType::Stone;
		Def.DropItemType = EItemType::Stone;
		Def.PlaceableFromItem = EItemType::Stone;
		Def.DropChance = 1.0f;
		Def.TimeToDestroy = 3.0f;
		Def.DisplayName = NSLOCTEXT("Blocks", "Stone", "Stone");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(StoneMaterial);
		Def.HighlightMaterial = FSoftObjectPath(HighlightPath);
		RegisterBlock(Def);
	}

	// === GRASS ===
	{
		FBlockDefinition Def;
		Def.BlockType = EBlockType::Grass;
		Def.DropItemType = EItemType::Grass;
		Def.PlaceableFromItem = EItemType::Grass;
		Def.DropChance = 1.0f;
		Def.TimeToDestroy = 1.5f;
		Def.DisplayName = NSLOCTEXT("Blocks", "Grass", "Grass");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(GrassMaterial);
		Def.HighlightMaterial = FSoftObjectPath(HighlightPath);
		RegisterBlock(Def);
	}

	// === OAK LOG ===
	{
		FBlockDefinition Def;
		Def.BlockType = EBlockType::OakLog;
		Def.DropItemType = EItemType::OakLog;
		Def.PlaceableFromItem = EItemType::OakLog;
		Def.DropChance = 1.0f;
		Def.TimeToDestroy = 2.0f;
		Def.DisplayName = NSLOCTEXT("Blocks", "OakLog", "Oak Log");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(OakLogMaterial);
		Def.HighlightMaterial = FSoftObjectPath(HighlightPath);
		RegisterBlock(Def);
	}

	// === BIRCH LOG ===
	{
		FBlockDefinition Def;
		Def.BlockType = EBlockType::BirchLog;
		Def.DropItemType = EItemType::BirchLog;
		Def.PlaceableFromItem = EItemType::BirchLog;
		Def.DropChance = 1.0f;
		Def.TimeToDestroy = 2.0f;
		Def.DisplayName = NSLOCTEXT("Blocks", "BirchLog", "Birch Log");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(BirchLogMaterial);
		Def.HighlightMaterial = FSoftObjectPath(HighlightPath);
		RegisterBlock(Def);
	}

	// === OAK LEAVES ===
	{
		FBlockDefinition Def;
		Def.BlockType = EBlockType::OakLeaves;
		Def.DropItemType = EItemType::OakSapling;  // Droppa sapling, ne leaves
		Def.PlaceableFromItem = EItemType::None;   // Ne može se postaviti
		Def.DropChance = 0.05f;  // 5% šansa
		Def.TimeToDestroy = 0.5f;
		Def.DisplayName = NSLOCTEXT("Blocks", "OakLeaves", "Oak Leaves");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(OakLeavesMaterial);
		Def.HighlightMaterial = FSoftObjectPath(HighlightPath);
		RegisterBlock(Def);
	}

	// === BIRCH LEAVES ===
	{
		FBlockDefinition Def;
		Def.BlockType = EBlockType::BirchLeaves;
		Def.DropItemType = EItemType::BirchSapling;  // Droppa sapling, ne leaves
		Def.PlaceableFromItem = EItemType::None;     // Ne može se postaviti
		Def.DropChance = 0.05f;  // 5% šansa
		Def.TimeToDestroy = 0.5f;
		Def.DisplayName = NSLOCTEXT("Blocks", "BirchLeaves", "Birch Leaves");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(BirchLeavesMaterial);
		Def.HighlightMaterial = FSoftObjectPath(HighlightPath);
		RegisterBlock(Def);
	}

	// === OAK PLANKS ===
	{
		FBlockDefinition Def;
		Def.BlockType = EBlockType::OakPlanks;
		Def.DropItemType = EItemType::OakPlanks;
		Def.PlaceableFromItem = EItemType::OakPlanks;
		Def.DropChance = 1.0f;
		Def.TimeToDestroy = 2.0f;
		Def.DisplayName = NSLOCTEXT("Blocks", "OakPlanks", "Oak Planks");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(OakPlankMaterial);
		Def.HighlightMaterial = FSoftObjectPath(HighlightPath);
		RegisterBlock(Def);
	}

	// === BIRCH PLANKS ===
	{
		FBlockDefinition Def;
		Def.BlockType = EBlockType::BirchPlanks;
		Def.DropItemType = EItemType::BirchPlanks;
		Def.PlaceableFromItem = EItemType::BirchPlanks;
		Def.DropChance = 1.0f;
		Def.TimeToDestroy = 2.0f;
		Def.DisplayName = NSLOCTEXT("Blocks", "BirchPlanks", "Birch Planks");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(BirchPlankMaterial);
		Def.HighlightMaterial = FSoftObjectPath(HighlightPath);
		RegisterBlock(Def);
	}

	// === CRAFTING TABLE ===
	{
		FBlockDefinition Def;
		Def.BlockType = EBlockType::CraftingTable;
		Def.DropItemType = EItemType::CraftingTable;
		Def.PlaceableFromItem = EItemType::CraftingTable;
		Def.DropChance = 1.0f;
		Def.TimeToDestroy = 2.5f;
		Def.DisplayName = NSLOCTEXT("Blocks", "CraftingTable", "Crafting Table");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(CraftingTableMaterial);
		Def.HighlightMaterial = FSoftObjectPath(HighlightPath);
		RegisterBlock(Def);
	}

}

void UBlockRegistry::RegisterAllItems()
{
	const FString MeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");

	// Iste MI putanje kao za blokove - item drop mora izgledati kao blok iz kojeg
	// je pao, pa se dvije liste moraju mijenjati zajedno.
	const FString DirtMaterial = TEXT("/Game/Blocks/Materials/MI_Dirt.MI_Dirt");
	const FString StoneMaterial = TEXT("/Game/Blocks/Materials/MI_Stone.MI_Stone");
	const FString GrassMaterial = TEXT("/Game/Blocks/Materials/MI_Grass.MI_Grass");
	const FString OakLogMaterial = TEXT("/Game/Blocks/Materials/MI_OakLog.MI_OakLog");
	const FString BirchLogMaterial = TEXT("/Game/Blocks/Materials/MI_BirchLog.MI_BirchLog");
	const FString OakLeavesMaterial = TEXT("/Game/Blocks/Materials/MI_OakLeaves.MI_OakLeaves");
	const FString BirchLeavesMaterial = TEXT("/Game/Blocks/Materials/MI_BirchLeaves.MI_BirchLeaves");
	const FString OakPlankMaterial = TEXT("/Game/Blocks/Materials/MI_OakPlanks.MI_OakPlanks");
	const FString BirchPlankMaterial = TEXT("/Game/Blocks/Materials/MI_BirchPlanks.MI_BirchPlanks");
	const FString CraftingTableMaterial = TEXT("/Game/Blocks/Materials/MI_CraftingTable.MI_CraftingTable");

	// === DIRT ===
	{
		FItemDefinition Def;
		Def.ItemType = EItemType::Dirt;
		Def.DisplayName = NSLOCTEXT("Items", "Dirt", "Dirt");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(DirtMaterial);
		RegisterItem(Def);
	}

	// === STONE ===
	{
		FItemDefinition Def;
		Def.ItemType = EItemType::Stone;
		Def.DisplayName = NSLOCTEXT("Items", "Stone", "Stone");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(StoneMaterial);
		RegisterItem(Def);
	}

	// === GRASS ===
	{
		FItemDefinition Def;
		Def.ItemType = EItemType::Grass;
		Def.DisplayName = NSLOCTEXT("Items", "Grass", "Grass");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(GrassMaterial);
		RegisterItem(Def);
	}

	// === OAK LOG ===
	{
		FItemDefinition Def;
		Def.ItemType = EItemType::OakLog;
		Def.DisplayName = NSLOCTEXT("Items", "OakLog", "Oak Log");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(OakLogMaterial);
		RegisterItem(Def);
	}

	// === BIRCH LOG ===
	{
		FItemDefinition Def;
		Def.ItemType = EItemType::BirchLog;
		Def.DisplayName = NSLOCTEXT("Items", "BirchLog", "Birch Log");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(BirchLogMaterial);
		RegisterItem(Def);
	}

	// === OAK SAPLING ===
	{
		FItemDefinition Def;
		Def.ItemType = EItemType::OakSapling;
		Def.DisplayName = NSLOCTEXT("Items", "OakSapling", "Oak Sapling");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(OakLeavesMaterial);
		RegisterItem(Def);
	}

	// === BIRCH SAPLING ===
	{
		FItemDefinition Def;
		Def.ItemType = EItemType::BirchSapling;
		Def.DisplayName = NSLOCTEXT("Items", "BirchSapling", "Birch Sapling");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(BirchLeavesMaterial);
		RegisterItem(Def);
	}

	// === OAK PLANKS ===
	{
		FItemDefinition Def;
		Def.ItemType = EItemType::OakPlanks;
		Def.DisplayName = NSLOCTEXT("Items", "OakPlanks", "Oak Planks");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(OakPlankMaterial);
		RegisterItem(Def);
	}

	// === BIRCH PLANKS ===
	{
		FItemDefinition Def;
		Def.ItemType = EItemType::BirchPlanks;
		Def.DisplayName = NSLOCTEXT("Items", "BirchPlanks", "Birch Planks");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(BirchPlankMaterial);
		RegisterItem(Def);
	}

	// === STICK ===
	{
		FItemDefinition Def;
		Def.ItemType = EItemType::Stick;
		Def.DisplayName = NSLOCTEXT("Items", "Stick", "Stick");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(OakLogMaterial);  // Koristi log materijal
		RegisterItem(Def);
	}

	// === CRAFTING TABLE ===
	{
		FItemDefinition Def;
		Def.ItemType = EItemType::CraftingTable;
		Def.DisplayName = NSLOCTEXT("Items", "CraftingTable", "Crafting Table");
		Def.Mesh = FSoftObjectPath(MeshPath);
		Def.Material = FSoftObjectPath(CraftingTableMaterial);
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
