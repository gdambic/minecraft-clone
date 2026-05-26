# Plan: Inventory UI sustav

**STATUS: Hotbar UI implementacija u tijeku**

---

## Arhitektura

### Dva odvojena widgeta
| Widget | Vidljivost | Svrha |
|--------|------------|-------|
| `WBP_Hotbar` | Uvijek vidljiv | 9 slotova na dnu ekrana |
| `WBP_Inventory` | Toggle (E tipka) | 27 slotova glavnog inventoryja (KASNIJE) |

### Dijeljeni widget
- `WBP_InventorySlot` - univerzalni slot widget korišten u oba kontejnera

---

## C++ Implementacija - ZAVRŠENO

### InventoryComponent

**Slot API:**
```cpp
FInventorySlot GetSlot(int32 SlotIndex) const;
void SetSlot(int32 SlotIndex, EItemType Type, int32 Quantity);
bool MoveItem(int32 FromSlot, int32 ToSlot);
bool SwapSlots(int32 SlotA, int32 SlotB);
const TArray<FInventorySlot>& GetAllSlots() const;
int32 FindFirstEmptySlot() const;      // Prioritizira hotbar (27-35)
int32 FindSlotWithItem(EItemType Type) const;  // Prioritizira hotbar
```

**Utility Functions:**
```cpp
void AddItem(EItemType Type, int32 Amount = 1);  // Prioritizira hotbar za stacking
int32 RemoveItem(EItemType Type, int32 Amount = 1);
int32 GetItemCount(EItemType Type) const;
bool HasItem(EItemType Type, int32 Amount = 1) const;
```

**Data Table podrška:**
```cpp
static FItemData GetItemData(EItemType ItemType, UDataTable* ItemDataTable);
```

**Delegati:**
```cpp
FOnSlotChanged OnSlotChanged;  // Broadcast kad se slot promijeni
```

### FItemData struktura (ItemData.h)
```cpp
USTRUCT(BlueprintType)
struct FItemData : public FTableRowBase
{
    EItemType ItemType;
    FText DisplayName;
    TSoftObjectPtr<UTexture2D> Icon;
    int32 MaxStackSize = 64;
};
```

### FirstPersonCharacter
```cpp
int32 SelectedItemIndex;  // 0-8, mapira na hotbar slotove 27-35
EItemType GetSelectedItemType() const;
EBlockType GetSelectedBlockType() const;
FOnSelectedItemChanged OnSelectedItemChanged;
FOnInventoryToggled OnInventoryToggled;
```

### Hotbar prioritet
- `AddItem` - prvo stacka na hotbar, zatim main inventory
- `FindFirstEmptySlot` - prvo traži prazan u hotbaru
- `FindSlotWithItem` - prvo traži item u hotbaru

---

## Data Table - ZAVRŠENO

**Asset:** `Content/Data/DT_ItemData`

| Row Name | ItemType | DisplayName | Icon | MaxStackSize |
|----------|----------|-------------|------|--------------|
| Dirt | Dirt | Dirt | T_Dirt | 64 |
| Stone | Stone | Stone | T_Stone | 64 |
| Grass | Grass | Grass | T_Grass | 64 |
| OakLog | OakLog | Oak Log | T_OakLog | 64 |
| BirchLog | BirchLog | Birch Log | T_BirchLog | 64 |
| OakSapling | OakSapling | Oak Sapling | T_OakSapling | 64 |
| BirchSapling | BirchSapling | Birch Sapling | T_BirchSapling | 64 |

---

## Blueprint Implementacija - U TIJEKU

### WBP_InventorySlot - ZAVRŠENO

**Designer struktura:**
```
[Border] - "SlotBorder" (Is Variable ✓)
 └── [Size Box] - 64x64
      └── [Overlay]
           ├── [Image] - "ItemIcon" (Is Variable ✓)
           └── [TextBlock] - "QuantityText" (Is Variable ✓)
```

**Varijable:**
- `ItemDataTable` (Data Table) - referenca na DT_ItemData
- `SlotIndex` (Integer)
- `bIsSelected` (Boolean)
- `bIsEmpty` (Boolean)

**Funkcije:**
- `SetSlotData(ItemType, Quantity)` - postavlja ikonu iz Data Table i količinu
- `SetSelected(bSelected)` - mijenja boju bordera (žuta/siva)

### WBP_Hotbar - SLJEDEĆI KORAK

**Designer struktura:**
```
[Canvas Panel]
 └── [Horizontal Box] - "SlotContainer"
      - Anchor: Bottom Center
      - Alignment: (0.5, 1.0)
      - Position: (0, -20)
```

**Varijable:**
- `SlotWidgets` (Array of WBP_InventorySlot)
- `PlayerCharacter` (BP_FirstPersonCharacter ref)
- `InventoryComp` (InventoryComponent ref)
- `CurrentSelectedIndex` (Integer)

**Event Construct:**
1. Get Owning Player Pawn → Cast to BP_FirstPersonCharacter
2. Get InventoryComponent → Save reference
3. Create 9x WBP_InventorySlot, dodaj u SlotContainer
4. Bind OnSlotChanged → HandleSlotChanged
5. Bind OnSelectedItemChanged → HandleSelectionChanged
6. RefreshAllSlots()

**Funkcije:**
- `RefreshAllSlots()` - osvježi svih 9 slotova iz InventoryComponent
- `HandleSlotChanged(SlotIndex, NewSlot)` - ažuriraj pojedinačni slot
- `HandleSelectionChanged(NewIndex, NewItemType)` - ažuriraj highlight

### Prikaz na ekranu - TODO
U BP_FirstPersonCharacter BeginPlay:
```
Create Widget (WBP_Hotbar) → Add to Viewport
```

---

## Verifikacija - Checklist

### Hotbar funkcionalnost
- [x] C++ InventoryComponent slot sustav
- [x] C++ hotbar prioritet za pickup
- [x] Data Table s item podacima i ikonama
- [x] WBP_InventorySlot widget s ikonama
- [ ] WBP_Hotbar kontejner widget
- [ ] Prikaz hotbara na ekranu
- [ ] Selekcija slota (scroll)
- [ ] Highlight selektiranog slota
- [ ] Ažuriranje kad se slot promijeni

### Gameplay
- [x] Početni inventory (Dirt, Stone, OakLog po 10)
- [x] Pickup dodaje u hotbar prvo
- [x] Postavljanje bloka troši iz selektiranog slota
- [ ] Prazan slot se prikazuje bez ikone

---

## Sljedeći koraci

1. Kreirati WBP_Hotbar widget
2. Dodati 9 WBP_InventorySlot instanci
3. Implementirati bindanje na delegate
4. Dodati WBP_Hotbar u Character BeginPlay
5. Testirati cijeli flow
