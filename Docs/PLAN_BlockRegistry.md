# Plan: Unified Block Registry (C++ First)

## Cilj
Pojednostaviti dodavanje novih blokova - sve u C++, minimalni Blueprint rad.

## Trenutno stanje (PRIJE)
- `EBlockType` enum + `EItemType` enum (2 datoteke)
- 3 switch statementa u `InventoryComponent.cpp` (ručno održavanje)
- `BlockClasses` TMap u BP_VoxelWorld (editor konfiguracija)
- `DT_ItemData` Data Table (editor)
- Zasebni BP_Dirt, BP_Stone... blueprinti
- Zasebni BP_ItemDrop_Dirt, BP_ItemDrop_Stone... blueprinti

## Novo stanje (POSLIJE)
- `FBlockDefinition` struct (SVE o bloku na jednom mjestu)
- `UBlockRegistry` singleton (automatski TMap lookups)
- Jedan generički `AItemDrop` (prima ItemType, bez zasebnih BP)
- Dodavanje bloka = 1 enum + 1 Register() poziv
- Zero Blueprint konfiguracija za nove blokove

---

## Faza 1: Novi sustav (dodaj pored starog)

### 1.1 Kreirati FBlockDefinition struct
**Datoteka:** `Source/MinecraftClone/Voxel/BlockDefinition.h` (NOVA)

```cpp
#pragma once

#include "CoreMinimal.h"
#include "BlockType.h"
#include "ItemType.h"
#include "BlockDefinition.generated.h"

USTRUCT(BlueprintType)
struct FBlockDefinition
{
    GENERATED_BODY()

    // Identifikacija
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EBlockType BlockType = EBlockType::Air;

    // Drop sustav
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EItemType DropItemType = EItemType::None;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float DropChance = 1.0f;

    // Placement sustav
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    EItemType PlaceableFromItem = EItemType::None;  // None = ne može se postaviti

    // Gameplay
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    float TimeToDestroy = 1.5f;

    // Posebna ponašanja
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bTriggerLeafDecay = false;  // Za log blokove

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    bool bCanDecay = false;  // Za leaf blokove

    // UI
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    FText DisplayName;

    // Vizuali (soft references - loadaju se lazy)
    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<UStaticMesh> Mesh;

    UPROPERTY(EditAnywhere, BlueprintReadWrite)
    TSoftObjectPtr<UMaterialInterface> Material;
};
```

### 1.2 Kreirati UBlockRegistry klasu
**Datoteke:** `Source/MinecraftClone/Voxel/BlockRegistry.h` i `.cpp` (NOVE)

```cpp
// BlockRegistry.h
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BlockDefinition.h"
#include "BlockRegistry.generated.h"

UCLASS()
class MINECRAFTCLONE_API UBlockRegistry : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    // Registracija
    void Register(const FBlockDefinition& Definition);

    // Lookups
    const FBlockDefinition* GetDefinition(EBlockType BlockType) const;
    EBlockType GetBlockForItem(EItemType ItemType) const;
    EItemType GetItemForBlock(EBlockType BlockType) const;
    bool CanItemBePlaced(EItemType ItemType) const;

    // Utility
    TArray<EBlockType> GetAllPlaceableBlockTypes() const;

private:
    void RegisterDefaultBlocks();

    UPROPERTY()
    TMap<EBlockType, FBlockDefinition> BlockDefinitions;

    // Reverse lookup cache: Item -> Block
    TMap<EItemType, EBlockType> ItemToBlockMap;
};
```

### 1.3 Registrirati sve postojeće blokove
**U:** `BlockRegistry.cpp`

```cpp
void UBlockRegistry::RegisterDefaultBlocks()
{
    // Dirt
    Register({
        .BlockType = EBlockType::Dirt,
        .DropItemType = EItemType::Dirt,
        .PlaceableFromItem = EItemType::Dirt,
        .TimeToDestroy = 1.5f,
        .DisplayName = NSLOCTEXT("Blocks", "Dirt", "Dirt"),
        .Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Meshes/SM_Block.SM_Block"))),
        .Material = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Materials/M_Dirt.M_Dirt")))
    });

    // Stone
    Register({
        .BlockType = EBlockType::Stone,
        .DropItemType = EItemType::Stone,
        .PlaceableFromItem = EItemType::Stone,
        .TimeToDestroy = 3.0f,
        .DisplayName = NSLOCTEXT("Blocks", "Stone", "Stone"),
        ...
    });

    // Grass
    Register({
        .BlockType = EBlockType::Grass,
        .DropItemType = EItemType::Grass,
        .PlaceableFromItem = EItemType::Grass,
        .TimeToDestroy = 1.5f,
        ...
    });

    // OakLog
    Register({
        .BlockType = EBlockType::OakLog,
        .DropItemType = EItemType::OakLog,
        .PlaceableFromItem = EItemType::OakLog,
        .TimeToDestroy = 2.0f,
        .bTriggerLeafDecay = true,  // POSEBNO
        ...
    });

    // BirchLog
    Register({
        .BlockType = EBlockType::BirchLog,
        .DropItemType = EItemType::BirchLog,
        .PlaceableFromItem = EItemType::BirchLog,
        .TimeToDestroy = 2.0f,
        .bTriggerLeafDecay = true,  // POSEBNO
        ...
    });

    // OakLeaves
    Register({
        .BlockType = EBlockType::OakLeaves,
        .DropItemType = EItemType::OakSapling,
        .DropChance = 0.05f,  // 5% POSEBNO
        .PlaceableFromItem = EItemType::None,  // Ne može se postaviti
        .TimeToDestroy = 0.5f,
        .bCanDecay = true,  // POSEBNO
        ...
    });

    // BirchLeaves
    Register({
        .BlockType = EBlockType::BirchLeaves,
        .DropItemType = EItemType::BirchSapling,
        .DropChance = 0.05f,
        .PlaceableFromItem = EItemType::None,
        .TimeToDestroy = 0.5f,
        .bCanDecay = true,
        ...
    });
}
```

---

## Faza 2: Migracija AItemDrop

### 2.1 Pojednostaviti AItemDrop
**Datoteka:** `Source/MinecraftClone/Voxel/ItemDrop.h/.cpp`

Promjene:
- Ukloni potrebu za zasebnim BP_ItemDrop_* blueprintima
- ItemDrop prima ItemType i sam konfigurira vizuale iz Registry-ja
- Dodaj statičku helper funkciju za spawn

```cpp
// Nova statička metoda u AItemDrop ili VoxelWorld
static AItemDrop* SpawnItemDrop(UWorld* World, FVector Location, EItemType ItemType, int32 Quantity = 1);
```

---

## Faza 3: Migracija ABlock

### 3.1 ABlock koristi Registry umjesto Blueprint svojstava
**Datoteka:** `Source/MinecraftClone/Voxel/Block.h/.cpp`

Promjene:
- `InitializeFromRegistry(EBlockType Type)` metoda
- Učitaj Mesh, Material, TimeToDestroy iz FBlockDefinition
- Drop logika koristi DropItemType + DropChance iz definicije
- Ukloni `DropClass` UPROPERTY (više nije potreban)

```cpp
void ABlock::InitializeFromRegistry(EBlockType Type)
{
    BlockType = Type;

    if (UBlockRegistry* Registry = GetGameInstance()->GetSubsystem<UBlockRegistry>())
    {
        if (const FBlockDefinition* Def = Registry->GetDefinition(Type))
        {
            TimeToDestroy = Def->TimeToDestroy;

            if (UStaticMesh* LoadedMesh = Def->Mesh.LoadSynchronous())
            {
                MeshComponent->SetStaticMesh(LoadedMesh);
            }
            if (UMaterialInterface* LoadedMat = Def->Material.LoadSynchronous())
            {
                MeshComponent->SetMaterial(0, LoadedMat);
            }
        }
    }
}
```

---

## Faza 4: Migracija InventoryComponent

### 4.1 Zamijeni switch statemente
**Datoteka:** `Source/MinecraftClone/Voxel/InventoryComponent.cpp`

PRIJE (linije 335-383):
```cpp
EBlockType UInventoryComponent::ItemTypeToBlockType(EItemType ItemType)
{
    switch (ItemType)
    {
    case EItemType::Dirt: return EBlockType::Dirt;
    case EItemType::Stone: return EBlockType::Stone;
    // ... ručno za svaki tip
    }
}
```

POSLIJE:
```cpp
EBlockType UInventoryComponent::ItemTypeToBlockType(EItemType ItemType)
{
    if (UBlockRegistry* Registry = GEngine->GetEngineSubsystem<UBlockRegistry>())
    {
        return Registry->GetBlockForItem(ItemType);
    }
    return EBlockType::Air;
}

EItemType UInventoryComponent::BlockTypeToItemType(EBlockType BlockType)
{
    if (UBlockRegistry* Registry = ...)
    {
        return Registry->GetItemForBlock(BlockType);
    }
    return EItemType::None;
}

bool UInventoryComponent::CanItemBePlaced(EItemType ItemType)
{
    if (UBlockRegistry* Registry = ...)
    {
        return Registry->CanItemBePlaced(ItemType);
    }
    return false;
}
```

---

## Faza 5: Migracija VoxelWorld

### 5.1 Spawn blokova iz Registry-ja
**Datoteka:** `Source/MinecraftClone/Voxel/VoxelWorld.cpp`

PRIJE:
```cpp
// BlockClasses TMap konfiguriran u BP_VoxelWorld
TSubclassOf<ABlock> BlockClass = BlockClasses.FindRef(Type);
ABlock* NewBlock = GetWorld()->SpawnActor<ABlock>(BlockClass, ...);
```

POSLIJE:
```cpp
// Spawn generički ABlock i inicijaliziraj iz Registry-ja
ABlock* NewBlock = GetWorld()->SpawnActor<ABlock>(ABlock::StaticClass(), ...);
NewBlock->InitializeFromRegistry(Type);
```

### 5.2 Ukloni BlockClasses UPROPERTY
**Datoteka:** `Source/MinecraftClone/Voxel/VoxelWorld.h`

```cpp
// OBRISATI:
// UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World")
// TMap<EBlockType, TSubclassOf<ABlock>> BlockClasses;
```

---

## Faza 6: Cleanup

### 6.1 Blueprint cleanup (u editoru)
- BP_VoxelWorld: ukloni BlockClasses konfiguraciju (više nije potrebna)
- Opcionalno: obriši BP_Dirt, BP_Stone... (više nisu potrebni)
- Opcionalno: obriši BP_ItemDrop_Dirt... (više nisu potrebni)

### 6.2 DT_ItemData integracija
- Preseli DisplayName, Icon, MaxStackSize u FBlockDefinition
- Ili: zadrži DT_ItemData samo za UI podatke (ikone)

---

## Testiranje nakon svake faze

| Faza | Test |
|------|------|
| 1 | Kompilacija prolazi, igra se pokreće |
| 2 | Item dropovi rade, pickup radi |
| 3 | Blokovi se renderiraju, uništavanje radi |
| 4 | Inventory placement radi, hotbar radi |
| 5 | World generation radi, svi blokovi vidljivi |
| 6 | Sve radi bez starih Blueprinta |

---

## Rezultat: Dodavanje novog bloka

```cpp
// 1. BlockType.h - dodaj enum
UENUM(BlueprintType)
enum class EBlockType : uint8
{
    // ...postojeći...
    OakPlanks,  // NOVO
};

// 2. ItemType.h - dodaj enum
UENUM(BlueprintType)
enum class EItemType : uint8
{
    // ...postojeći...
    OakPlanks,  // NOVO
};

// 3. BlockRegistry.cpp - registriraj (GOTOVO!)
Register({
    .BlockType = EBlockType::OakPlanks,
    .DropItemType = EItemType::OakPlanks,
    .PlaceableFromItem = EItemType::OakPlanks,
    .TimeToDestroy = 2.0f,
    .DisplayName = NSLOCTEXT("Blocks", "OakPlanks", "Oak Planks"),
    .Mesh = TSoftObjectPtr<UStaticMesh>(FSoftObjectPath(TEXT("/Game/Meshes/SM_Block"))),
    .Material = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Materials/M_OakPlanks")))
});
```

**ZERO Blueprint rad** - samo dodaj materijal ako ne postoji.

---

## Procjena kompleksnosti

| Komponenta | Datoteke | Novi/Izmjena |
|------------|----------|--------------|
| FBlockDefinition | 1 | Nova |
| UBlockRegistry | 2 | Nova |
| ABlock | 2 | Izmjena |
| AItemDrop | 2 | Izmjena |
| InventoryComponent | 1 | Izmjena |
| VoxelWorld | 2 | Izmjena |
| **UKUPNO** | **10** | 3 nove + 7 izmjena |
