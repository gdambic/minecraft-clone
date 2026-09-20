# PLAN — Prikaz HOLDINGA (item u ruci)

> **Status: implementirano** (2026-09-20). Konačna izvedba: kocka u ruci
> koristi **materijal bloka s terena** (`MI_<Blok>` iz `FBlockDefinition.Material`),
> pa izgleda točno kao blok u svijetu. Sprite pristup (korak 2, `M_ItemSprite`)
> je isproban i odbačen za ruku — asset i Python builder ostaju za eventualni
> budući flat prikaz ne-blok itema (alati, hrana).

## Cilj

Kad igrač u hotbaru odabere item, u ruci (HOLDING) se prikazuje sprite tog itema:

- **Običan item** → prikaži generiranu ikonu itema (ista tekstura kao u Inventoryju,
  `Content/Items/Generated/T_Item_<Block>`), na quadu u ruci.
- **Oružje** → ne dirati postojeće ponašanje (placeholder mač ostaje; pravi prikaz
  oružja je budući posao).
- **Prazan slot** → ne prikazuj ništa.
- Udaljenost HOLDINGA od igrača parametrizirana kroz Editor.

Sve u C++; jedini ne-C++ dio je generiranje sprite materijala kroz postojeći
Python editor alat (nula Blueprint izmjena).

## Postojeće stanje

- `UFirstPersonArmComponent` (`FirstPersonArmComponent.cpp:291`) — `SetHeldItem()`
  prikazuje `HeldItemMesh` (kocku) samo za 4 hardkodirana mača; obični itemi ne
  prikazuju ništa.
- Princip ikone u Inventoryju: `LoadGeneratedItemIcon()` (anonimni namespace,
  `InventorySlotWidget.cpp:17`) — iz `FItemDefinition.Display` (type `"block"`,
  polje `Block`) gradi putanju `/Game/Items/Generated/T_Item_<Block>` i učita
  `UTexture2D`.
- Materijali se već grade programski: `Scripts/build_block_materials.py` koristi
  `MaterialEditingLibrary` (menu Tools > MinecraftClone > Build Block Materials).
- `UWeaponDataLibrary::IsWeapon()` (`WeaponData.h:73`) postoji za detekciju oružja.
- `UInventoryComponent` ima `FOnSlotChanged` delegate — potreban za slučaj kad se
  sadržaj odabranog slota promijeni bez promjene indeksa (zadnji blok postavljen,
  pickup u prazan odabrani slot, drag-drop u Full Inventoryju).

## Koraci

### 1. Zajednički icon lookup u `UBlockRegistry`

- Novi member: `UTexture2D* GetItemIconTexture(EItemType ItemType)` — preseljena
  logika iz `LoadGeneratedItemIcon()`, plus cache
  (`TMap<EItemType, TObjectPtr<UTexture2D>>`) da se `TryLoad` ne ponavlja.
- `InventorySlotWidget.cpp` prelazi na taj poziv (briše se lokalna kopija).
  Time je "koju teksturu prikazati" doslovno isti kod za Inventory i HOLDING.

### 2. Sprite materijal kroz Python alat (ne Blueprint)

- Proširiti `Scripts/build_block_materials.py` da uz blok materijale kreira i
  `/Game/Items/Generated/Materials/M_ItemSprite`
  (`Content/Items/Generated/Materials/`, uz generirane sprite teksture):
  - `TextureSampleParameter2D` s imenom parametra **`SpriteTexture`**
  - RGB → Base Color, Alpha → Opacity Mask
  - Blend mode **Masked**, **Two Sided**, Fully Rough (pikselasta ikona, bez sjaja)
- Pokreće se postojećim menijem Tools > MinecraftClone > Build Block Materials
  (jednokratno; idempotentno kao i ostatak skripte).

### 3. `UFirstPersonArmComponent` — blok materijal na postojećoj kocki

HOLDING je postojeća kocka `ArmMesh` (dosadašnji sivi placeholder) — ne dodaje
se novi mesh, samo joj se mijenja materijal i skala:

- Novi lookup `UBlockRegistry::GetBlockMaterialForItem(EItemType)` (s cacheom):
  item → placeable blok (`GetBlockForItem`) → `FBlockDefinition.Material`
  (`TryLoad`). To je **isti MI koji koristi teren**, pa kocka u ruci izgleda
  točno kao blok u svijetu (per-face Top/Side/Bottom teksture iz
  `M_VoxelBlock` master materijala).
- Originalni materijal kocke se zapamti (`DefaultArmMaterial`) da se može
  vratiti za oružje/fallback.
- Novi parametar `HeldBlockScale` (`EditAnywhere`, default 0.25 = kocka
  25 UU) — dok se drži blok, kocka je uniformna (prava kocka), a za
  oružje/fallback se vraća izduženi `ArmScale`.
- `ArmBaseOffset` (X = udaljenost od igrača — traženi parametar),
  `ArmBaseRotation` i `ArmScale` postaju `EditAnywhere` pa se podešavaju u
  Editoru na BP_FirstPersonCharacter → FirstPersonArmComponent.

### 4. Nova logika `SetHeldItem(EItemType)`

Redoslijed grana:

1. `EItemType::None` (prazan slot) → sakrij `ArmMesh` i `HeldItemMesh`
   (ništa u ruci).
2. `UWeaponDataLibrary::IsWeapon(ItemType)` → kocka s `DefaultArmMaterial` i
   `ArmScale` + postojeća sword logika (`UpdateSwordAppearance`) netaknuta.
   Hardkodirani popis 4 mača zamijenjen pozivom `IsWeapon()`.
3. Inače → `Registry->GetBlockMaterialForItem(ItemType)`:
   - materijal postoji → `ArmMesh->SetMaterial(0, MI_<Blok>)` + `HeldBlockScale`
   - materijala nema (item nije placeable blok, ili blok nema definiran
     materijal) → siva kocka s `DefaultArmMaterial` i `ArmScale` — ista
     fallback konvencija kao za blokove u svijetu.

### 5. Osvježavanje kad se promijeni sadržaj odabranog slota

- `AFirstPersonCharacter::BeginPlay`: subscribe na
  `InventoryComponent->OnSlotChanged`.
- Handler: ako je `SlotIndex == HotbarStartIndex + SelectedItemIndex` →
  `FirstPersonArmComponent->SetHeldItem(GetSelectedItemType())`.
- Pokriva: postavljen zadnji blok (ruka se isprazni), pickup u odabrani prazan
  slot, drag-drop izmjene u Full Inventoryju.

### 6. Build i verifikacija

- Build Development Editor (VS 2022).
- Headless run (`-nullrhi`) za provjeru da nema Error logova iz registry/arm koda.
- Ručno u Editoru:
  - pokrenuti Tools > MinecraftClone > Build Block Materials (kreira `M_ItemSprite`)
  - scroll kroz hotbar: Dirt/Stone/OakLog → sprite u ruci; mačevi → stari
    placeholder; prazni slotovi → ništa
  - postaviti sve blokove iz slota → ruka se isprazni
  - promijeniti `HeldItemOffset` u Editoru → udaljenost se mijenja

## Izvan opsega

- Pravi 3D prikaz oružja u ruci (budući posao, gore grana 2).
- Flat sprite prikaz ne-blok itema (alati, hrana) u ruci — za to su spremni
  `M_ItemSprite` materijal i `GetItemIconTexture()`; za sad ti itemi
  pokazuju sivu fallback kocku.
