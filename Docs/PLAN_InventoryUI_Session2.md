# Plan: Inventory UI - Session 2

**Datum:** 2025-05-24
**Status:** U tijeku

---

## Completed

### Faza 1: C++ promjene - DONE
- `FirstPersonPlayerController.h/cpp` - SetInventoryInputMode()
- `FirstPersonCharacter.cpp` - ToggleInventory() koristi IMC swap

### Faza 3.1-3.4: WBP_FullInventory dizajn - DONE
- Widget kreiran s hijerarhijom
- Border, VerticalBox, MainInventoryGrid, HotbarRow, CraftingGrid
- Varijable definirane

### Faza 3.5: WBP_FullInventory funkcije - DONE

#### Event Construct
```
Event Construct
    │
    ▼
Get Owning Player Pawn
    │
    ▼
Cast To BP_FirstPersonCharacter
    │
    ▼
SET PlayerCharacter
    │
    ▼
Get Component by Class
   Target: PlayerCharacter
   Component Class: InventoryComponent
    │
    ▼
SET InventoryComp
    │
    ▼
Set Visibility
   Target: Self
   In Visibility: Collapsed
    │
    ▼
CreateMainSlots
    │
    ▼
CreateHotbarSlots
    │
    ▼
CreateCraftingSlots
    │
    ▼
RefreshAllSlots
```

#### CreateMainSlots (Function)
```
[Entry]
    │
    ▼
For Loop
   First Index: 0
   Last Index: 26
    │ (Loop Body)
    ▼
Create Widget
   Class: WBP_InventorySlot
   Owning Player: Get Owning Player
    │
    ▼
SET Item Data Table
   Target: [Return Value]
   Item Data Table: DT_ItemData
    │
    ▼
SET Slot Index
   Target: [Return Value]
   Slot Index: [Index]
    │
    ▼
Add Child to Uniform Grid
   Target: MainInventoryGrid
   Content: [Return Value]
   In Row: [Index] ÷ 9
   In Column: [Index] % 9
    │
    ▼
Add
   Target Array: MainSlotWidgets
   New Item: [Return Value iz Create Widget]
```

#### CreateHotbarSlots (Function)
```
[Entry]
    │
    ▼
For Loop
   First Index: 0
   Last Index: 8
    │ (Loop Body)
    ▼
Create Widget
   Class: WBP_InventorySlot
   Owning Player: Get Owning Player
    │
    ▼
SET Item Data Table
   Target: [Return Value]
   Item Data Table: DT_ItemData
    │
    ▼
SET Slot Index
   Target: [Return Value]
   Slot Index: [Index] + 27
    │
    ▼
Add Child
   Target: HotbarRow
   Content: [Return Value]
    │
    ▼
Add
   Target Array: HotbarSlotWidgets
   New Item: [Return Value iz Create Widget]
```

#### CreateCraftingSlots (Function)
```
[Entry]
    │
    ▼
For Loop
   First Index: 0
   Last Index: 3
    │ (Loop Body)
    ▼
Create Widget
   Class: WBP_InventorySlot
   Owning Player: Get Owning Player
    │
    ▼
SET Item Data Table
   Target: [Return Value]
   Item Data Table: DT_ItemData
    │
    ▼
SET Slot Index
   Target: [Return Value]
   Slot Index: -1
    │
    ▼
Add Child to Uniform Grid
   Target: CraftingGrid
   Content: [Return Value]
   In Row: [Index] ÷ 2
   In Column: [Index] % 2
```

#### RefreshAllSlots (Function)
```
[Entry]
    │
    ▼
For Loop
   First Index: 0
   Last Index: 26
    │ (Loop Body)
    ▼
Get
   Target: MainSlotWidgets
   Index: [Index]
    │
    ▼
Get Slot
   Target: InventoryComp
   Slot Index: [Index]
    │
    ▼
Break FInventorySlot
   Struct: [Return Value iz Get Slot]
    │
    ▼
Set Slot Data
   Target: [Return Value iz Get]
   Item Type: [Item Type iz Break]
   Quantity: [Quantity iz Break]
    │
    ▼ (Completed)
    │
For Loop
   First Index: 0
   Last Index: 8
    │ (Loop Body)
    ▼
Get
   Target: HotbarSlotWidgets
   Index: [Index]
    │
    ▼
Integer + Integer
   A: [Index]
   B: 27
    │
    ▼
Get Slot
   Target: InventoryComp
   Slot Index: [Return Value iz +]
    │
    ▼
Break FInventorySlot
   Struct: [Return Value iz Get Slot]
    │
    ▼
Set Slot Data
   Target: [Return Value iz Get]
   Item Type: [Item Type iz Break]
   Quantity: [Quantity iz Break]
```

### Faza 4: BP_FirstPersonCharacter integracija - DONE

#### Nova varijabla
- `FullInventoryWidget` (WBP_FullInventory Object Reference)

#### Event BeginPlay (dodano na kraj)
```
[Postojeći čvorovi]
    │
    ▼
Get Controller
    │
    ▼
Cast To PlayerController
    │
    ▼
Create Widget
   Class: WBP_FullInventory
   Owning Player: [As Player Controller]
    │
    ▼
SET FullInventoryWidget
    │
    ▼
Add to Viewport
   Target: FullInventoryWidget
   Z-Order: 10
    │
    ▼
Self → Assign OnInventoryToggled
   [Automatski stvara Custom Event]
```

#### OnInventoryToggled (Custom Event)
```
OnInventoryToggled
   bIsOpen (Boolean)
    │
    ▼
Branch
   Condition: bIsOpen
    │
    ├── True:
    │       │
    │       ▼
    │   Set Visibility
    │      Target: FullInventoryWidget
    │      In Visibility: Visible
    │
    └── False:
            │
            ▼
        Set Visibility
           Target: FullInventoryWidget
           In Visibility: Collapsed
```

---

## Preostali koraci

### Korak 1: WBP_InventorySlot - Event Dispatcheri

#### 1.1 Dodaj Event Dispatchere
1. My Blueprint → Event Dispatchers → +
2. `OnSlotClicked` s parametrom SlotIndex (Integer)
3. `OnSlotShiftClicked` s parametrom SlotIndex (Integer)

#### 1.2 Dodaj varijablu
- `CachedPlayerController` (Player Controller Object Reference)

#### 1.3 Event Pre Construct
```
Event Pre Construct
    │
    ▼
Get Owning Player
    │
    ▼
SET CachedPlayerController
```

#### 1.4 Override OnMouseButtonDown
```
OnMouseButtonDown
   My Geometry
   Mouse Event
    │
    ▼
Is Input Key Down
   Target: CachedPlayerController
   Key: Left Shift
    │
    ▼
Branch
   Condition: [Return Value]
    │
    ├── True:
    │       │
    │       ▼
    │   Call OnSlotShiftClicked
    │      Slot Index: SlotIndex
    │       │
    │       ▼
    │   Handled
    │       │
    │       ▼
    │   Return Node
    │      Return Value: [Handled Return Value]
    │
    └── False:
            │
            ▼
        Call OnSlotClicked
           Slot Index: SlotIndex
            │
            ▼
        Handled
            │
            ▼
        Return Node
           Return Value: [Handled Return Value]
```

---

### Korak 2: WBP_FullInventory - Binding eventova

U CreateMainSlots i CreateHotbarSlots, nakon Add to array, dodaj:
- Bind OnSlotClicked → HandleSlotClicked
- Bind OnSlotShiftClicked → HandleShiftClick

---

### Korak 3: WBP_FullInventory - HandleSlotClicked

Varijable potrebne:
- `HeldItem` (FInventorySlot)
- `OriginSlotIndex` (Integer)

Logika:
- Ako HeldItem.ItemType == None: uzmi item iz slota
- Inače: stavi HeldItem u slot (swap ako zauzet)

---

### Korak 4: WBP_FullInventory - HandleShiftClick

Logika:
- Ako SlotIndex >= 27: premjesti u main (0-26)
- Inače: premjesti u hotbar (27-35)

---

### Korak 5: WBP_FullInventory - CursorSlot

- Dodaj WBP_InventorySlot kao child Canvas Panela (ime: CursorSlot)
- Event Tick: prati poziciju miša
- Prikazuje HeldItem

---

### Korak 6: WBP_FullInventory - HandleSlotChanged

- Bind na InventoryComp.OnSlotChanged u Event Construct
- Ažuriraj odgovarajući slot widget

---

### Korak 7: HideInventory logika

- Kad se inventory zatvori, vrati HeldItem na OriginSlotIndex

---

## Napomene

- `FInventorySlot.IsEmpty()` i `Clear()` NISU dostupni u Blueprintu
- Koristi `ItemType == EItemType::None` umjesto IsEmpty()
- Koristi `Make FInventorySlot (None, 0)` umjesto Clear()
- Za Canvas Panel child pozicioniranje: `Slot as Canvas Slot` → `Set Position`
