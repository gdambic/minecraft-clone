# Plan: Implementacija Oak i Birch stabala

**STATUS: ZAVRŠENO**
- C++ implementacija: DONE
- Blueprint konfiguracija: DONE (BP_OakLog, BP_BirchLog, BP_OakLeaves, BP_BirchLeaves)
- Generiranje stabala: RADI
- Leaf decay: RADI (timer 2.5s)
- Item drops s gravitacijom: RADI

## Pregled

Implementacija dva tipa stabala (Oak i Birch) s:
- 4 nova tipa blokova (OakLog, BirchLog, OakLeaves, BirchLeaves)
- 4 nova tipa itema (OakLog, BirchLog, OakSapling, BirchSapling)
- 5% šansa da lišće dropa sapling
- Leaf decay sustav (lišće propada ako nije spojeno s logom unutar 6 blokova)
- Generiranje ~10 stabala pri stvaranju svijeta

---

## Faza 1: Novi tipovi (BlockType.h, ItemType.h)

### BlockType.h
```cpp
enum class EBlockType : uint8
{
    Air,
    Dirt,
    Stone,
    Grass,
    OakLog,       // NOVO
    BirchLog,     // NOVO
    OakLeaves,    // NOVO
    BirchLeaves   // NOVO
};
```

### ItemType.h
```cpp
enum class EItemType : uint8
{
    None,
    Dirt,
    Stone,
    Grass,
    OakLog,       // NOVO
    BirchLog,     // NOVO
    OakSapling,   // NOVO
    BirchSapling  // NOVO
};
```

---

## Faza 2: Sustav dropanja sa šansom (Block.h, Block.cpp)

### Block.h - Novo svojstvo
```cpp
/** Šansa za drop (0.0-1.0), default 1.0 */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|Drop", meta = (ClampMin = "0.0", ClampMax = "1.0"))
float DropChance = 1.0f;
```

### Block.cpp - Logika dropanja
```cpp
// U AddDestroyProgress(), zamijeni postojeći drop kod:
if (DestroyProgress >= 1.0f)
{
    // Spawn drop samo ako roll prođe šansu
    if (DropClass && FMath::FRand() < DropChance)
    {
        const float HalfBlock = BlockSize / 2.0f;
        FVector SpawnLocation = GetActorLocation() + FVector(HalfBlock, HalfBlock, HalfBlock);
        FActorSpawnParameters SpawnParams;
        GetWorld()->SpawnActor<AItemDrop>(DropClass, SpawnLocation, FRotator::ZeroRotator, SpawnParams);
    }

    // ... ostatak koda ...
}
```

---

## Faza 3: TreeGenerator (nova klasa)

### TreeGenerator.h
```cpp
UENUM(BlueprintType)
enum class ETreeType : uint8 { Oak, Birch };

class FTreeGenerator
{
public:
    static void GenerateTree(AVoxelWorld* World, FIntVector Base, ETreeType Type);
    static void GenerateRandomTrees(AVoxelWorld* World, int32 Count, int32 SizeX, int32 SizeY, int32 Surface);

private:
    static void GenerateOakTree(AVoxelWorld* World, FIntVector Base);   // 4-6 trunk
    static void GenerateBirchTree(AVoxelWorld* World, FIntVector Base); // 5-7 trunk
    static void GenerateCanopy(AVoxelWorld* World, FIntVector Center, EBlockType LeafType, int32 Radius);
    static bool CanPlaceTreeAt(AVoxelWorld* World, FIntVector Pos, int32 Height);
};
```

### Struktura stabla
```
Oak:  4-6 blokova deblo, 5x5 krošnja (bez kutova)
Birch: 5-7 blokova deblo, 5x5 krošnja (bez kutova)

Krošnja (pogled odozgo, Radius=2):
  . X X X .
  X X X X X
  X X L X X   (L = Log na vrhu)
  X X X X X
  . X X X .
```

---

## Faza 4: Leaf Decay sustav (VoxelWorld.h, VoxelWorld.cpp)

### VoxelWorld.h - Nova svojstva i metode
```cpp
UPROPERTY(EditAnywhere, Category = "World")
int32 TreeCount = 10;

void OnLogDestroyed(FIntVector LogPosition);

private:
    TArray<FIntVector> LeavesToCheck;
    FTimerHandle LeafDecayTimerHandle;
    void ProcessLeafDecay();
    bool HasLogConnection(FIntVector LeafPosition) const;
```

### Algoritam decay-a
1. Kad se log uništi → `OnLogDestroyed()` dodaje svo lišće unutar 6 blokova u `LeavesToCheck`
2. Timer (0.5s) procesira 10 listova po tikcu
3. Za svaki list: BFS pretraga traži log unutar Manhattan distance 6
4. Ako nema loga → list se uništava (može dropati sapling)

### Block.cpp - Notifikacija pri uništenju loga
```cpp
// U AddDestroyProgress(), prije SetBlockType(Air):
if (BlockType == EBlockType::OakLog || BlockType == EBlockType::BirchLog)
{
    // Pronađi VoxelWorld i obavijesti ga
    AVoxelWorld* VoxelWorld = /* pronađi */;
    VoxelWorld->OnLogDestroyed(GridPosition);
}
```

---

## Faza 5: Blueprint konfiguracija

### Novi Block Blueprinti
| Blueprint | Mesh | DropClass | DropChance |
|-----------|------|-----------|------------|
| BP_OakLog | Oak log | BP_ItemDrop_OakLog | 1.0 (100%) |
| BP_BirchLog | Birch log | BP_ItemDrop_BirchLog | 1.0 (100%) |
| BP_OakLeaves | Oak leaves | BP_ItemDrop_OakSapling | 0.05 (5%) |
| BP_BirchLeaves | Birch leaves | BP_ItemDrop_BirchSapling | 0.05 (5%) |

### Novi ItemDrop Blueprinti
- BP_ItemDrop_OakLog (ItemType = OakLog)
- BP_ItemDrop_BirchLog (ItemType = BirchLog)
- BP_ItemDrop_OakSapling (ItemType = OakSapling)
- BP_ItemDrop_BirchSapling (ItemType = BirchSapling)

### BP_VoxelWorld ažuriranje
Dodati u BlockClasses mapu:
- OakLog → BP_OakLog
- BirchLog → BP_BirchLog
- OakLeaves → BP_OakLeaves
- BirchLeaves → BP_BirchLeaves

---

## Datoteke za izmjenu/kreiranje

| Datoteka | Akcija |
|----------|--------|
| `Voxel/BlockType.h` | Izmjena - dodaj 4 enum vrijednosti |
| `Voxel/ItemType.h` | Izmjena - dodaj 4 enum vrijednosti |
| `Voxel/Block.h` | Izmjena - dodaj DropChance svojstvo |
| `Voxel/Block.cpp` | Izmjena - random drop logika + log notifikacija |
| `Voxel/TreeGenerator.h` | **NOVA** - deklaracija generatora |
| `Voxel/TreeGenerator.cpp` | **NOVA** - implementacija generatora |
| `Voxel/VoxelWorld.h` | Izmjena - decay sustav deklaracije |
| `Voxel/VoxelWorld.cpp` | Izmjena - decay implementacija + GenerateTrees() |

---

## Verifikacija

1. **Kompilacija**: Build projekt bez grešaka
2. **Generiranje stabala**: Pokrenuti igru, vidjeti 10 stabala na terenu
3. **Dropovi**: Uništiti log → uvijek dropa log, uništiti lišće → 5% šansa za sapling
4. **Leaf decay**: Uništiti donje logove stabla → lišće bez podrške propada nakon kratkog vremena
5. **BFS test**: Lišće spojeno s logom preko drugih listova NE propada

---

## Izvori
- [Minecraft Wiki - Leaves](https://minecraft.wiki/w/Leaves) - decay mehanika, 5% sapling drop
- [Minecraft Wiki - Tree](https://minecraft.wiki/w/Tree) - struktura stabala
