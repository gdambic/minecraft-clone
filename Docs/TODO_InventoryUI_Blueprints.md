# TODO: Inventory UI Blueprint Implementation

**Zadnje ažurirano:** 2026-05-16

---

## Završeno

### WBP_InventorySlot - DONE

**Lokacija:** `Content/Blueprints/UI/WBP_InventorySlot`

**Designer struktura:**
```
[Border] - "SlotBorder" (Is Variable ✓)
 └── [Size Box] - Width/Height Override: 64
      └── [Overlay]
           ├── [Image] - "ItemIcon" (Is Variable ✓)
           │    - Stretch: Scale to Fit
           └── [TextBlock] - "QuantityText" (Is Variable ✓)
                - Anchors: Bottom Right
                - Font Size: 12-14
```

**Varijable:**
| Ime | Tip | Instance Editable | Default |
|-----|-----|-------------------|---------|
| ItemDataTable | Data Table | ✓ | DT_ItemData |
| SlotIndex | Integer | ✓ | 0 |
| bIsSelected | Boolean | ✗ | false |
| bIsEmpty | Boolean | ✗ | true |

**Funkcije implementirane:**
- `SetSlotData(ItemType, Quantity)` - učitava ikonu iz DT_ItemData, prikazuje količinu
- `SetSelected(bSelected)` - žuta boja kad selektirano, siva kad nije

---

### DT_ItemData - DONE

**Lokacija:** `Content/Data/DT_ItemData`

**Row Structure:** FItemData

**Redovi:**
| Row Name | ItemType | DisplayName | Icon | MaxStackSize |
|----------|----------|-------------|------|--------------|
| Dirt | Dirt | Dirt | (tekstura) | 64 |
| Stone | Stone | Stone | (tekstura) | 64 |
| Grass | Grass | Grass | (tekstura) | 64 |
| OakLog | OakLog | Oak Log | (tekstura) | 64 |
| BirchLog | BirchLog | Birch Log | (tekstura) | 64 |
| OakSapling | OakSapling | Oak Sapling | (tekstura) | 64 |
| BirchSapling | BirchSapling | Birch Sapling | (tekstura) | 64 |

---

## U tijeku

### WBP_Hotbar - SLJEDEĆI

**Lokacija:** `Content/Blueprints/UI/WBP_Hotbar`

#### Designer struktura
```
[Canvas Panel]
 └── [Horizontal Box] - "SlotContainer"
      - Anchors: Bottom Center
      - Alignment: (0.5, 1.0)
      - Position: (0, -20)
      - Size To Content: ✓
```

#### Varijable
| Ime | Tip | Default |
|-----|-----|---------|
| SlotWidgets | Array of WBP_InventorySlot | [] |
| PlayerCharacter | BP_FirstPersonCharacter | None |
| InventoryComp | InventoryComponent | None |
| CurrentSelectedIndex | Integer | 0 |

#### Event Construct - koraci
```
1. [Get Owning Player Pawn]
        │
        ▼
2. [Cast to BP_FirstPersonCharacter]
        │
        ▼
3. [Set PlayerCharacter]
        │
        ▼
4. [PlayerCharacter] → [Get Inventory Component]
        │
        ▼
5. [Set InventoryComp]
        │
        ▼
6. [CreateHotbarSlots] (Custom Event)
        │
        ▼
7. [InventoryComp] → [Assign OnSlotChanged] → [HandleSlotChanged]
        │
        ▼
8. [PlayerCharacter] → [Assign OnSelectedItemChanged] → [HandleSelectionChanged]
        │
        ▼
9. [RefreshAllSlots] (Custom Event)
```

#### CreateHotbarSlots - koraci
```
[For Loop] i = 0 to 8
        │
        ▼
[Create Widget] class = WBP_InventorySlot
        │
        ▼
[NewWidget] → [Set Slot Index] = 27 + i
        │
        ▼
[NewWidget] → [Set Item Data Table] = DT_ItemData
        │
        ▼
[SlotContainer] → [Add Child] ← NewWidget
        │
        ▼
[SlotWidgets] → [Add] ← NewWidget
```

#### RefreshAllSlots - koraci
```
[For Loop] i = 0 to 8
        │
        ▼
[InventoryComp] → [Get Slot] ← (27 + i)
        │
        ▼
[SlotWidgets] → [Get] ← i
        │
        ▼
[SlotWidget] → [SetSlotData] ← (Slot.ItemType, Slot.Quantity)
        │
        ▼
[Branch] ← i == PlayerCharacter.SelectedItemIndex
        │
        ├── True: [SlotWidget] → [SetSelected(true)]
        └── False: [SlotWidget] → [SetSelected(false)]
```

#### HandleSlotChanged - Custom Event
**Inputs:** SlotIndex (Integer), NewSlot (FInventorySlot)
```
[Branch] ← SlotIndex >= 27 AND SlotIndex <= 35
        │
        └── True:
                │
                ▼
        [HotbarIndex] = SlotIndex - 27
                │
                ▼
        [SlotWidgets] → [Get] ← HotbarIndex
                │
                ▼
        [SlotWidget] → [SetSlotData] ← (NewSlot.ItemType, NewSlot.Quantity)
```

#### HandleSelectionChanged - Custom Event
**Inputs:** NewIndex (Integer), NewItemType (EItemType)
```
[Branch] ← CurrentSelectedIndex != NewIndex
        │
        └── True:
                │
                ▼
        [SlotWidgets] → [Get] ← CurrentSelectedIndex
                │
                ▼
        [OldSlot] → [SetSelected(false)]
                │
                ▼
        [SlotWidgets] → [Get] ← NewIndex
                │
                ▼
        [NewSlot] → [SetSelected(true)]
                │
                ▼
        [Set CurrentSelectedIndex] = NewIndex
```

---

## TODO

### Prikaz Hotbara na ekranu

**U BP_FirstPersonCharacter → Event BeginPlay:**
```
[Event BeginPlay]
        │
        ▼
[Create Widget] ← class: WBP_Hotbar
        │
        ▼
[Add to Viewport] ← Z-Order: 0
```

---

### WBP_Inventory (glavni inventory) - KASNIJE

Struktura za 27 slotova (grid 9x3), toggle s E tipkom.

---

## Testiranje

Po završetku WBP_Hotbar:

1. [ ] Hotbar se prikazuje na dnu ekrana (9 slotova)
2. [ ] Početni itemi vidljivi (Dirt, Stone, OakLog)
3. [ ] Scroll mijenja selekciju (žuti highlight)
4. [ ] Desni klik postavlja blok, količina pada
5. [ ] Pickup novog itema - pojavljuje se u hotbaru
6. [ ] Pickup postojećeg itema - količina raste
7. [ ] Kad količina = 0, slot postaje prazan
