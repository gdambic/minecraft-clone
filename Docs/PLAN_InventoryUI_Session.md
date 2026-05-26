# Plan: Inventory UI implementacija - Stanje sessiona

**Datum:** 2025-05-24
**Status:** U tijeku

---

## Sažetak zahtjeva

- **E tipka** otvara/zatvara inventory
- **Blokiraj kretanje** dok je inventory otvoren (IMC swap pristup)
- **Hotbar ostaje vidljiv** kad je inventory otvoren
- **Interakcije**: Drag & drop, Click-to-select/place, Shift+click
- **Crafting grid**: 2×2 placeholder (bez funkcionalnosti)

---

## Faza 1: C++ promjene - IMPLEMENTIRANO

### Što je napravljeno:

**FirstPersonPlayerController.h** - dodano:
```cpp
public:
    UFUNCTION(BlueprintCallable, Category = "Input")
    void SetInventoryInputMode(bool bInventoryMode);

protected:
    UPROPERTY(EditAnywhere, Category = "Input|Input Mappings")
    TArray<UInputMappingContext*> InventoryMappingContexts;
```

**FirstPersonPlayerController.cpp** - dodano:
```cpp
void AFirstPersonPlayerController::SetInventoryInputMode(bool bInventoryMode)
{
    if (!IsLocalPlayerController()) return;

    UEnhancedInputLocalPlayerSubsystem* Subsystem =
        ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
    if (!Subsystem) return;

    if (bInventoryMode)
    {
        for (UInputMappingContext* IMC : DefaultMappingContexts)
            Subsystem->RemoveMappingContext(IMC);
        for (UInputMappingContext* IMC : InventoryMappingContexts)
            Subsystem->AddMappingContext(IMC, 0);
    }
    else
    {
        for (UInputMappingContext* IMC : InventoryMappingContexts)
            Subsystem->RemoveMappingContext(IMC);
        for (UInputMappingContext* IMC : DefaultMappingContexts)
            Subsystem->AddMappingContext(IMC, 0);
    }
}
```

**FirstPersonCharacter.cpp** - ažurirano:
- Dodan include: `#include "FirstPersonPlayerController.h"`
- `ToggleInventory()` sada poziva `PC->SetInventoryInputMode(bIsInventoryOpen)`

### Što treba napraviti u Blueprintu:

1. Compile projekt
2. Otvori BP_FirstPersonPlayerController
3. U Details → Input | Input Mappings → Inventory Mapping Contexts → dodaj `IMC_Inventory`
4. Provjeri da IMC_Inventory sadrži IA_ToggleInventory (E tipka)
5. Testiraj - E tipka treba blokirati kretanje

---

## Faza 2: WBP_InventorySlot poboljšanja - NIJE IMPLEMENTIRANO

### Potrebne promjene:

**1. Dodaj varijablu:**
- `CachedPlayerController` (Player Controller, Object Reference)

**2. Dodaj Event Dispatchere:**
- `OnSlotClicked` (Input: SlotIndex Integer)
- `OnSlotShiftClicked` (Input: SlotIndex Integer)

**3. Event Pre Construct:**
```
Get Owning Player → Get Player Controller → SET CachedPlayerController
```

**4. Override On Mouse Button Down:**
```
On Mouse Button Down (My Geometry, Mouse Event)
    │
    ▼
GET CachedPlayerController
    │
    ▼
Is Input Key Down (Key: Left Shift)
    │
    ├─ True:  Call OnSlotShiftClicked (SlotIndex) → Handled → Return Node (Return Value)
    │
    └─ False: Call OnSlotClicked (SlotIndex) → Handled → Return Node (Return Value)
```

**Napomena:**
- Parametri eventa su `My Geometry` i `Mouse Event` (NE Pointer Event)
- Za Return Value koristi čvor `Handled` koji vraća Event Reply strukturu
- `Is Input Key Down` se poziva na Player Controller, NE na Mouse Event

---

## Faza 3: WBP_FullInventory kreiranje - NIJE IMPLEMENTIRANO

### 3.1 Kreiranje

Content Browser → desni klik → User Interface → Widget Blueprint → `WBP_FullInventory`

### 3.2 Designer hijerarhija

```
Canvas Panel (root)
├── Border "Background"
│   └── Vertical Box "MainContainer"
│       ├── Text Block "TitleText"
│       ├── Uniform Grid Panel "CraftingGrid"
│       ├── Spacer
│       ├── Uniform Grid Panel "MainInventoryGrid"
│       ├── Spacer
│       └── Horizontal Box "HotbarRow"  ← OTVORENO PITANJE (vidi dolje)
└── WBP_InventorySlot "CursorSlot"
```

### 3.3 Postavke elemenata

| Widget | Postavka | Vrijednost |
|--------|----------|------------|
| Border "Background" | Anchors | Center |
| | Alignment | (0.5, 0.5) |
| | Brush Color | (0, 0, 0, 0.7) |
| | Padding | 20 |
| | Is Variable | ✓ |
| Uniform Grid Panel "CraftingGrid" | Is Variable | ✓ |
| | Slot Padding | 2 |
| Uniform Grid Panel "MainInventoryGrid" | Is Variable | ✓ |
| | Slot Padding | 2 |
| Spacer (prvi) | Size Y | 20 |
| Spacer (drugi) | Size Y | 10 |
| Horizontal Box "HotbarRow" | Is Variable | ✓ |
| WBP_InventorySlot "CursorSlot" | Is Variable | ✓ |
| | Visibility | Hidden |

### 3.4 Varijable

| Naziv | Tip |
|-------|-----|
| PlayerCharacter | BP_FirstPersonCharacter |
| InventoryComp | Inventory Component |
| MainSlotWidgets | Array of WBP_InventorySlot |
| HotbarSlotWidgets | Array of WBP_InventorySlot |
| HeldItem | FInventorySlot |
| OriginSlotIndex | Integer |

### 3.5 Kreiranje slotova (Add Child to Uniform Grid)

**Main slotovi (0-26):**
```
For Loop (0 to 26)
    │
    Create Widget (WBP_InventorySlot)
    │
    SET SlotIndex = Index
    │
    Add Child to Uniform Grid
        Target: MainInventoryGrid
        Content: widget
        In Row: Index / 9
        In Column: Index % 9
    │
    Bind OnSlotClicked → HandleSlotClicked
    Bind OnSlotShiftClicked → HandleShiftClick
    │
    Add to MainSlotWidgets array
```

**Hotbar slotovi (27-35):**
```
For Loop (0 to 8)
    │
    Create Widget (WBP_InventorySlot)
    │
    SET SlotIndex = 27 + Index
    │
    Add Child (Target: HotbarRow)
    │
    Bind events...
    │
    Add to HotbarSlotWidgets array
```

**Crafting slotovi (placeholder):**
```
For Loop (0 to 3)
    │
    Create Widget (WBP_InventorySlot)
    │
    SET SlotIndex = -1
    │
    Add Child to Uniform Grid
        Target: CraftingGrid
        In Row: Index / 2
        In Column: Index % 2
```

### 3.6 Funkcije (pseudokod)

**HandleSlotClicked(SlotIndex):**
```
IF HeldItem.IsEmpty():
    Slot = InventoryComp.GetSlot(SlotIndex)
    IF NOT Slot.IsEmpty():
        HeldItem = Slot
        InventoryComp.SetSlot(SlotIndex, None, 0)
        CursorSlot.SetSlotData(HeldItem)
        CursorSlot.SetVisibility(Visible)
        OriginSlotIndex = SlotIndex
ELSE:
    Slot = InventoryComp.GetSlot(SlotIndex)
    IF Slot.IsEmpty():
        InventoryComp.SetSlot(SlotIndex, HeldItem)
        HeldItem.Clear()
        CursorSlot.SetVisibility(Hidden)
    ELSE:
        // Swap
        TempSlot = Slot
        InventoryComp.SetSlot(SlotIndex, HeldItem)
        HeldItem = TempSlot
        CursorSlot.SetSlotData(HeldItem)
```

**HandleShiftClick(SlotIndex):**
```
Slot = InventoryComp.GetSlot(SlotIndex)
IF Slot.IsEmpty(): RETURN

IF SlotIndex >= 27:
    // Iz hotbara u main
    FOR i = 0 to 26:
        IF InventoryComp.GetSlot(i).IsEmpty():
            InventoryComp.SwapSlots(SlotIndex, i)
            RETURN
ELSE:
    // Iz main u hotbar
    FOR i = 27 to 35:
        IF InventoryComp.GetSlot(i).IsEmpty():
            InventoryComp.SwapSlots(SlotIndex, i)
            RETURN
```

**ShowInventory / HideInventory:**
```
ShowInventory:
    RefreshAllSlots()
    SetVisibility(Visible)

HideInventory:
    IF NOT HeldItem.IsEmpty():
        InventoryComp.SetSlot(OriginSlotIndex, HeldItem)
        HeldItem.Clear()
        CursorSlot.SetVisibility(Hidden)
    SetVisibility(Collapsed)
```

---

## Faza 4: BP_FirstPersonCharacter integracija - NIJE IMPLEMENTIRANO

1. Dodaj varijablu `FullInventoryWidget` (WBP_FullInventory)
2. U Event BeginPlay: Create Widget → Add to Viewport (Z-Order: 10)
3. Bind OnInventoryToggled → HandleInventoryToggle
4. HandleInventoryToggle poziva ShowInventory/HideInventory

---

## OTVORENO PITANJE

**HotbarRow u inventoryju - potreban ili ne?**

WBP_Hotbar već postoji i prikazuje slotove 27-35 stalno na ekranu.

**Opcija A: S HotbarRow (Minecraft stil)**
- Duplicirani prikaz hotbara unutar inventory prozora
- Omogućuje drag-drop unutar inventory prozora
- Oba prikaza se ažuriraju kroz OnSlotChanged delegate

**Opcija B: Bez HotbarRow**
- WBP_FullInventory prikazuje samo slotove 0-26 + crafting
- Hotbar ostaje odvojen (WBP_Hotbar)
- Jednostavnije, bez dupliciranja

**Odluka:** Čeka se odgovor korisnika

---

## Provjereni resursi

- [Add Child to Uniform Grid - UE 5.3](https://dev.epicgames.com/documentation/en-us/unreal-engine/BlueprintAPI/Widget/AddChildtoUniformGrid/)
- [UniformGridPanel properties](https://dev.epicgames.com/documentation/en-us/unreal-engine/python-api/class/UniformGridPanel?application_version=5.1)
- [Inventory System Tutorial - Rambod Dev](https://rambod.net/tutorial/unreal-engine-blueprint-inventory-systems-in-under-30-minutes)
- [Is Input Key Down from PlayerController](https://forums.unrealengine.com/t/how-do-i-supply-isshiftdown-from-blueprints-the-input-event-structure-it-needs/323379)

---

## Naučene lekcije (za CLAUDE.md)

1. **NIKAD ne zaključuj da nešto ne postoji** bez pretrage cijelog codebase-a
2. **Pretraži prije nego zaključiš** - koristi Grep za ključne pojmove
3. **Provjeri činjenice online** prije pisanja Blueprint uputa
4. **Ne izmišljaj property imena** - provjeri dokumentaciju
5. **Blueprint pin imena** mogu se razlikovati od C++ naziva (npr. Mouse Event vs FPointerEvent)
