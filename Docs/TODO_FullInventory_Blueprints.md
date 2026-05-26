# Blueprint Implementation: Full Inventory Widget

Ovaj dokument opisuje korake za kreiranje Blueprint asseta za Full Inventory sustav.

## Pregled C++ promjena

Implementirane su sljedeće C++ promjene:

### InventoryComponent.h/.cpp
- Dodana `FInventorySlot` struktura (ItemType + Quantity)
- Inventory koristi `TArray<FInventorySlot>` s 36 slotova (0-26 = main, 27-35 = hotbar)
- Nove metode: `GetSlot()`, `SetSlot()`, `MoveItem()`, `SwapSlots()`
- Novi delegate: `OnSlotChanged(SlotIndex, NewSlot)`

### FirstPersonCharacter.h/.cpp
- Dodan `ToggleInventoryAction` za E tipku
- Dodana `bIsInventoryOpen` varijabla
- Dodana `ToggleInventory()` metoda (pokazuje/skriva kursor, mijenja input mode)
- Dodan `OnInventoryToggled` delegate
- Dodana `GetInventoryComponent()` metoda

---

## Faza 1: Input Action za E tipku

### 1.1 Kreiraj IA_ToggleInventory
1. Desni klik u `Content/Input/Actions/`
2. Create → Input → Input Action
3. Nazovi: `IA_ToggleInventory`
4. Otvori asset:
   - Value Type: `Digital (bool)`
   - Triggers: ostaviti prazno (default)

### 1.2 Dodaj u Input Mapping Context
1. Otvori `Content/Input/IMC_Default`
2. Klikni "+" za novo mapiranje
3. Odaberi `IA_ToggleInventory`
4. Klikni "+" za key
5. Odaberi `E` tipku

### 1.3 Poveži s BP_FirstPersonCharacter
1. Otvori `Content/FirstPerson/Blueprints/BP_FirstPersonCharacter`
2. U Details panelu pronađi kategoriju "Input"
3. Postavi `Toggle Inventory Action` na `IA_ToggleInventory`

---

## Faza 2: WBP_InventorySlot (pojedinačni slot widget)

### 2.1 Kreiraj Widget Blueprint
1. Desni klik u `Content/Blueprints/UI/`
2. User Interface → Widget Blueprint
3. Nazovi: `WBP_InventorySlot`

### 2.2 Designer - Hijerarhija
```
[Canvas Panel]
└── SlotBorder (Border) - 64x64, Anchor: Fill
    └── SlotOverlay (Overlay)
        ├── ItemIcon (Image) - Anchor: Fill
        └── QuantityText (Text Block) - Anchor: Bottom Right
```

### 2.3 Designer - Styling
- **SlotBorder**:
  - Size: 64×64 px
  - Brush Color: Dark gray (#333333)
  - Is Variable: ✓

- **ItemIcon**:
  - Size: 48×48 px (centriran)
  - Visibility: Collapsed (default)
  - Is Variable: ✓

- **QuantityText**:
  - Font Size: 12
  - Color: White
  - Visibility: Collapsed (default)
  - Is Variable: ✓

### 2.4 Variables
| Ime | Tip | Default |
|-----|-----|---------|
| SlotIndex | Integer | -1 |
| CurrentSlot | FInventorySlot | Empty |
| bIsSelected | Boolean | false |
| OwnerInventory | WBP_FullInventory (Object Reference) | None |

### 2.5 Functions

**UpdateSlot(NewSlot: FInventorySlot)**
```
Set CurrentSlot = NewSlot
If NewSlot.ItemType != None AND NewSlot.Quantity > 0:
    Set ItemIcon Visibility = Visible
    Set ItemIcon Brush from ItemType (vidi mapping dolje)
    Set QuantityText Text = NewSlot.Quantity
    Set QuantityText Visibility = Visible if Quantity > 1, else Collapsed
Else:
    Set ItemIcon Visibility = Collapsed
    Set QuantityText Visibility = Collapsed
```

**SetSelected(bSelected: Boolean)**
```
Set bIsSelected = bSelected
If bSelected:
    Set SlotBorder Brush Color = Yellow (#FFFF00)
Else:
    Set SlotBorder Brush Color = Dark Gray (#333333)
```

### 2.6 Item Icon Mapping
| EItemType | Texture |
|-----------|---------|
| Dirt | T_Dirt_Icon ili Block texture |
| Stone | T_Stone_Icon |
| Grass | T_Grass_Icon |
| OakLog | T_OakLog_Icon |
| BirchLog | T_BirchLog_Icon |
| OakSapling | T_OakSapling_Icon |
| BirchSapling | T_BirchSapling_Icon |

### 2.7 Drag & Drop Events

**OnMouseButtonDown**
```
If CurrentSlot is not empty:
    Create Drag Drop Operation
    Set Payload = Self (WBP_InventorySlot reference)
    Set Default Drag Visual = Create widget copy of this slot
    Return Handled
Return Unhandled
```

**OnDrop**
```
Get Payload as WBP_InventorySlot
If Valid:
    Get SourceSlotIndex from Payload
    Call OwnerInventory.SwapSlots(SourceSlotIndex, Self.SlotIndex)
Return Handled
```

---

## Faza 3: WBP_FullInventory (glavni widget)

### 3.1 Kreiraj Widget Blueprint
1. Desni klik u `Content/Blueprints/UI/`
2. User Interface → Widget Blueprint
3. Nazovi: `WBP_FullInventory`

### 3.2 Designer - Hijerarhija
```
[Canvas Panel]
├── DarkOverlay (Image) - semi-transparent black, Anchor: Fill
└── InventoryPanel (Vertical Box) - Anchor: Center
    ├── CraftingSection (Horizontal Box)
    │   ├── CraftingGrid (Grid Panel 2×2)
    │   │   ├── CraftingSlot0 (WBP_InventorySlot) - Row 0, Col 0
    │   │   ├── CraftingSlot1 (WBP_InventorySlot) - Row 0, Col 1
    │   │   ├── CraftingSlot2 (WBP_InventorySlot) - Row 1, Col 0
    │   │   └── CraftingSlot3 (WBP_InventorySlot) - Row 1, Col 1
    │   ├── ArrowImage (Image) - "→" arrow texture
    │   └── OutputSlot (WBP_InventorySlot)
    │
    ├── Spacer (height: 20px)
    │
    ├── MainInventoryGrid (Grid Panel 9×3)
    │   └── 27× WBP_InventorySlot (indices 0-26)
    │
    ├── Separator (Image) - thin line, height: 4px
    │
    └── HotbarRow (Horizontal Box)
        └── 9× WBP_InventorySlot (indices 27-35)
```

### 3.3 Variables
| Ime | Tip | Default |
|-----|-----|---------|
| PlayerCharacter | BP_FirstPersonCharacter (Object Reference) | None |
| InventoryComp | InventoryComponent (Object Reference) | None |
| SlotWidgets | Array of WBP_InventorySlot | Empty |
| CraftingSlots | Array of WBP_InventorySlot | Empty |
| OutputSlot | WBP_InventorySlot | None |

### 3.4 Event Graph - Construct

```
Event Construct:
    Get Owning Player Pawn
    Cast to BP_FirstPersonCharacter → Set PlayerCharacter
    Get InventoryComponent from PlayerCharacter → Set InventoryComp

    // Create 36 slot widgets
    For i = 0 to 35:
        Create WBP_InventorySlot widget
        Set SlotIndex = i
        Set OwnerInventory = Self
        Add to SlotWidgets array

        If i < 27:
            Add to MainInventoryGrid at Row=i/9, Col=i%9
        Else:
            Add to HotbarRow

    // Bind OnSlotChanged
    Bind Event to InventoryComp.OnSlotChanged → HandleSlotChanged

    // Initialize all slots
    For each slot in SlotWidgets:
        slot.UpdateSlot(InventoryComp.GetSlot(slot.SlotIndex))

    // Initialize crafting slots (local, not bound to backend)
    For each slot in CraftingSlots:
        slot.UpdateSlot(Empty FInventorySlot)
```

### 3.5 Functions

**HandleSlotChanged(SlotIndex: Integer, NewSlot: FInventorySlot)**
```
If SlotIndex is valid (0-35):
    SlotWidgets[SlotIndex].UpdateSlot(NewSlot)
```

**SwapSlots(SlotA: Integer, SlotB: Integer)**
```
Call InventoryComp.SwapSlots(SlotA, SlotB)
```

---

## Faza 4: Integracija s BP_FirstPersonCharacter

### 4.1 Dodaj varijable
1. Otvori `BP_FirstPersonCharacter`
2. Dodaj varijablu: `FullInventoryWidget` (WBP_FullInventory reference)

### 4.2 Bind OnInventoryToggled

U Event Graph:
```
Event BeginPlay:
    ... existing code ...
    Bind Event to OnInventoryToggled → HandleInventoryToggled

HandleInventoryToggled(bIsOpen: Boolean):
    If bIsOpen:
        If FullInventoryWidget is not valid:
            Create WBP_FullInventory widget
            Set FullInventoryWidget
        Add FullInventoryWidget to Viewport (ZOrder: 10)
    Else:
        If FullInventoryWidget is valid:
            Remove FullInventoryWidget from Parent
```

---

## Faza 5: Ažuriraj WBP_BlockInventory (HUD Hotbar)

Postojeći HUD hotbar treba ažurirati da koristi slot-based pristup.

### 5.1 Promjene
1. Umjesto dinamičke liste, koristi fiksnih 9 slotova
2. Svaki slot mapira na Slots[27+i]
3. Bind na `OnSlotChanged` i filtriraj za indekse 27-35
4. Highlight selektirani slot (koristi `SelectedItemIndex`)

### 5.2 Primjer koda

```
Event Construct:
    Get InventoryComp from PlayerCharacter

    For i = 0 to 8:
        Create slot widget
        Set SlotIndex = 27 + i
        Add to HotbarContainer

    Bind OnSlotChanged → HandleSlotChanged
    Bind OnSelectedItemChanged → HandleSelectionChanged

HandleSlotChanged(SlotIndex, NewSlot):
    If SlotIndex >= 27 AND SlotIndex <= 35:
        HotbarSlots[SlotIndex - 27].UpdateSlot(NewSlot)

HandleSelectionChanged(NewIndex, NewItemType):
    // Unhighlight all, highlight selected
    For i = 0 to 8:
        HotbarSlots[i].SetSelected(i == NewIndex)
```

---

## Verifikacija

1. **Build projekt** - Visual Studio Build Solution
2. **Pokreni Editor** - Open MinecraftClone.uproject
3. **Kreiraj Blueprint assete** prema uputama iznad
4. **Testiraj**:
   - [ ] Pritisni E → Inventory se otvori, kursor vidljiv
   - [ ] Pritisni E opet → Inventory se zatvori
   - [ ] Hotbar u inventoryju prikazuje iste iteme kao HUD
   - [ ] Drag item → slotovi se zamijene
   - [ ] Igra nastavlja dok je inventory otvoren
   - [ ] Scroll miša mijenja selekciju u hotbaru

---

## Napomene

- Crafting grid je samo vizualan (bez recepta)
- Slotovi 0-26 su main inventory, 27-35 su hotbar
- Drag & drop koristi swap logiku (ne stacking)
- Igra se ne pauzira dok je inventory otvoren
