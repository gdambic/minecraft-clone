# MinecraftClone - Pregled Projekta

## Tehnologije
- **Engine**: Unreal Engine 5.6
- **Jezik**: C++
- **Input System**: Enhanced Input

---

## Arhitektura

### Koordinatni sustav
- **X** = širina
- **Y** = duljina
- **Z** = visina (različito od Minecrafta gdje je Y visina)

### Veličina bloka
- 100 UE units = 1 blok

---

## C++ Klase (Source/MinecraftClone/Voxel/)

### BlockType.h
Enum za tipove blokova:
- `Air` - prazan prostor
- `Dirt` - zemlja
- `Stone` - kamen
- `Grass` - trava

### ItemType.h
Enum za tipove itema:
- `None` - ništa
- `Dirt` - zemlja
- `Stone` - kamen
- `Grass` - trava

### Block.h/.cpp
Actor klasa za pojedinačni blok:
- `MeshComponent` - UStaticMeshComponent za renderiranje
- `BlockType` - tip bloka
- `GridPosition` - FIntVector pozicija u gridu
- `HighlightMaterial` - materijal za highlight (postavlja se u BP_Block)
- `TimeToDestroy` - vrijeme za uništenje (default 1.5s)
- `DropClass` - TSubclassOf<AItemDrop> koji se spawna kad se blok uništi
- `SetHighlighted(bool)` - uključi/isključi highlight
- `AddDestroyProgress(DeltaTime)` - dodaj progress uništavanja, spawna drop
- `ResetDestroyProgress()` - resetiraj progress

### ItemDrop.h/.cpp
Actor klasa za item drop:
- `MeshComponent` - vizualni prikaz (scale 0.3)
- `PickupCollision` - USphereComponent za automatski pickup
- `ItemType` - tip itema koji daje
- `DespawnTime` - vrijeme do nestanka (default 300s)
- `RotationSpeed` - brzina rotacije (default 90°/s)
- `PickupRadius` - radius za pickup (default 150 units)
- Automatski rotira i despawna nakon vremena
- Pickup triggerira OnOverlapBegin s igračem

### InventoryComponent.h/.cpp
UActorComponent za slot-based inventory sustav:
- 36 slotova: 0-26 = main inventory, 27-35 = hotbar
- `FInventorySlot` struktura s `ItemType` i `Quantity`

**Slot API:**
- `GetSlot(SlotIndex)` - dohvati slot
- `SetSlot(SlotIndex, Type, Quantity)` - postavi slot
- `MoveItem(FromSlot, ToSlot)` - premjesti item
- `SwapSlots(SlotA, SlotB)` - zamijeni dva slota
- `GetAllSlots()` - dohvati sve slotove
- `FindFirstEmptySlot()` - pronađi prazan slot
- `FindSlotWithItem(Type)` - pronađi slot s itemom

**Utility funkcije:**
- `AddItem(Type, Amount)` - smart dodavanje (za pickup sustav)
- `RemoveItem(Type, Amount)` - makni item
- `GetItemCount(Type)` - dohvati ukupnu količinu
- `HasItem(Type, Amount)` - provjeri ima li dovoljno

**Konverzije:**
- `ItemTypeToBlockType(ItemType)` - EItemType → EBlockType
- `BlockTypeToItemType(BlockType)` - EBlockType → EItemType
- `CanItemBePlaced(ItemType)` - može li se item postaviti kao blok

**Delegati:**
- `OnSlotChanged(SlotIndex, NewSlot)` - za UI binding

### VoxelWorld.h/.cpp
Actor koji upravlja svijetom:
- `BlockClass` - TSubclassOf<ABlock> (postavlja se na BP_Block)
- `WorldSizeX/Y/Z` - dimenzije svijeta
- `BlockSize` - veličina bloka u UE units (100)
- `RandomBlockCount` - broj random blokova na vrhu
- `GetBlock(X, Y, Z)` - dohvati blok
- `SetBlockType(X, Y, Z, Type)` - promijeni tip
- `PlaceBlockAt(GridPosition, Type)` - postavi novi blok
- `GridToWorld(X, Y, Z)` - konvertiraj grid u world koordinate

### FirstPersonCharacter.h/.cpp
Igrač s first person kamerom:
- `FirstPersonCameraComponent` - kamera na visini očiju
- `InventoryComponent` - komponenta za praćenje itema
- `BlockInteractionRange` - doseg interakcije (500 units = 5 blokova)
- `CurrentlyLookedAtBlock` - blok u koji igrač gleda
- `CurrentHitNormal` - normala pogođene plohe
- `VoxelWorld` - referenca (pronađe se automatski u BeginPlay)
- Input Actions: Jump, Move, Look, MouseLook, Attack, Interact, Scroll, ToggleInventory

**Inventory selekcija:**
- `SelectedItemIndex` - index hotbar slota (0-8)
- `GetSelectedItemType()` - dohvati tip itema iz selektiranog hotbar slota
- `GetSelectedBlockType()` - konvertiraj u EBlockType
- `OnSelectedItemChanged` delegate - broadcast kad igrač scrolla
- `OnInventoryToggled` delegate - broadcast kad se otvori/zatvori inventory (E tipka)
- `bIsInventoryOpen` - stanje inventory panela
- `ToggleInventory()` - otvori/zatvori inventory s kursorom

**Interakcija:**
- `UpdateBlockLookAt()` - raycast za detekciju bloka
- `StartAttack/StopAttack()` - držanje LMB za uništavanje
- `PlaceBlock()` - RMB za postavljanje bloka iz hotbar slota

### FirstPersonCameraManager.h/.cpp
- Pitch limits: -70° do 80°

### FirstPersonPlayerController.h/.cpp
- `DefaultMappingContexts` - array input mapping konteksta
- Postavlja PlayerCameraManagerClass na FirstPersonCameraManager

### FirstPersonGameMode.h/.cpp
- Stub klasa za GameMode

---

## Blueprinti (Content/)

### Voxel sustav
- `Content/Voxel/BP_Dirt` - nasljeđuje Block, DropClass = BP_ItemDrop_Dirt
- `Content/Voxel/BP_Stone` - nasljeđuje Block, DropClass = BP_ItemDrop_Stone
- `Content/Voxel/BP_Grass` - nasljeđuje Block, DropClass = BP_ItemDrop_Dirt
- `Content/Voxel/BP_VoxelWorld` - nasljeđuje VoxelWorld
- `Content/Voxel/M_Dirt` - materijal za dirt blok
- `Content/Voxel/M_Stone` - materijal za stone blok
- `Content/Voxel/M_Grass` - materijal za grass blok
- `Content/Voxel/M_BlockHighlight` - materijal za highlight

### Item Drops
- `Content/Voxel/BP_ItemDrop_Dirt` - nasljeđuje ItemDrop, ItemType = Dirt
- `Content/Voxel/BP_ItemDrop_Stone` - nasljeđuje ItemDrop, ItemType = Stone
- `Content/Voxel/BP_ItemDrop_Grass` - nasljeđuje ItemDrop, ItemType = Grass

### First Person
- `Content/FirstPerson/Blueprints/BP_FirstPersonCharacter`
- `Content/FirstPerson/Blueprints/BP_FirstPersonPlayerController`
- `Content/FirstPerson/Blueprints/BP_FirstPersonGameMode`
- `Content/FirstPerson/Blueprints/BP_FirstPersonCameraManager`

### Input
- `Content/Input/Actions/IA_Jump`
- `Content/Input/Actions/IA_Move`
- `Content/Input/Actions/IA_Look`
- `Content/Input/Actions/IA_MouseLook`
- `Content/Input/Actions/IA_Attack` - LMB, uništavanje blokova
- `Content/Input/Actions/IA_Interact` - RMB, postavljanje blokova
- `Content/Input/IMC_Default`
- `Content/Input/IMC_MouseLook`

---

## Implementirane funkcionalnosti

### ✅ Generiranje svijeta
- 100x100x1 podloga od dirt blokova
- 20 random blokova na Z=1
- Konfigurabilan kroz BP_VoxelWorld

### ✅ First Person Controller
- WASD kretanje
- Mouse look
- Jump

### ✅ Block Selection (Highlight)
- Raycast od kamere
- Blok u koji gledaš se highlighta
- Highlight materijal konfigurabilan u BP_Block

### ✅ Block Destruction
- Drži LMB da uništiš blok
- 3 koraka: 100% → 67% → 33% → 0%
- TimeToDestroy konfigurabilan (default 1.5s)
- Ako pustiš LMB, progress se resetira
- Debug ispis na ekranu

### ✅ Block Placement
- RMB postavlja novi blok na plohu koju gledaš
- Koristi ImpactNormal za određivanje pozicije

### ✅ Item Drop sustav
- Kad se blok uništi, spawna se ItemDrop
- Svaki Block Blueprint definira svoj DropClass
- Drop rotira i automatski nestaje nakon 5 minuta
- Automatski pickup kad igrač uđe u radius (150 units)

### ✅ Inventory sustav
- Slot-based sustav s 36 slotova (27 main + 9 hotbar)
- InventoryComponent na igraču
- Početni inventory: Dirt (10), Stone (10), OakLog (10) u hotbaru
- Scroll za selekciju hotbar slota (0-8, wrap-around)
- Postavljanje bloka troši item iz selektiranog hotbar slota
- Automatski pickup dodaje iteme u prvi slobodan slot
- OnSlotChanged delegate za UI binding
- E tipka za toggle inventory panela (s kursorom)

---

## TODO / Planirano

### ⬜ Crosshair HUD
- Plan napravljen, nije implementiran
- WBP_Crosshair widget s "+" tekstom na sredini ekrana
- Dodaje se kroz BP_FirstPersonPlayerController

### ⬜ Chunk sustav (optimizacija)
- Trenutno svaki blok je zasebni Actor
- Za veće svjetove potreban chunk sustav

### ⬜ Više tipova blokova
- Grass, Stone, Wood, itd.
- Različiti materijali i teksture

### ⬜ Inventory GUI
- Povezati InventoryComponent s hotbar widgetom
- Vizualni prikaz količina

---

## Kako pokrenuti

1. Otvori projekt u Unreal Editor
2. Otvori level s BP_VoxelWorld i PlayerStart
3. World Settings → GameMode Override → BP_FirstPersonGameMode
4. Play

---

## Napomene

- Sve C++ klase su `abstract` - koriste se preko Blueprinta
- Konfiguracija se radi kroz Blueprinte, ne kroz C++ kod
- VoxelWorld se automatski pronalazi u BeginPlay
