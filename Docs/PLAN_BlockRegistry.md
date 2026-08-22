# PLAN: JSON Block Registry

**Status: IMPLEMENTIRANO 2026-08-22.** (Plan dogovoren 2026-08-19, dopunjen
zahtjevom za fallback definicijom.) Odstupanje od plana: `Items.json` odmah
pokriva svih 17 itema (i Wool/mačeve/Porkchop koji prije nisu imali definicije,
sada su sive kocke) pa Error log ostaje rezerviran za stvarne rupe. Umjesto
`JsonArrayStringToUStruct` koristi se ručna petlja s `JsonObjectToUStruct` po
unosu — jedan neispravan unos preskače se pojedinačno umjesto da sruši cijelu
datoteku. Oba testa iz koraka 5 prošla (10 blocks / 17 items / 10 ISM;
tipfeler → Error + fallback, svijet se normalno generira).

## Cilj

1. Svi blokovi i itemi definiraju se u JSON datotekama umjesto ~300 linija
   copy-paste koda u `BlockRegistry.cpp` (`RegisterAllBlocks` / `RegisterAllItems`).
2. **Blok bez JSON definicije ne smije nestati iz igre** — dobiva automatsku
   fallback definiciju: defaultna kocka (`/Engine/BasicShapes/Cube.Cube`) bez
   materijala (siva), plus `UE_LOG(Error)` da se tipfeler odmah vidi.
3. Sve u C++ i konfiguracijskim datotekama — bez klikanja po Editoru.
   Enum ostaje izvor istine: novi blok = 1 linija u `EBlockType` (+ `EItemType`),
   rekompilacija, pa JSON objekt.

## Zašto je jeftino

- `Json` i `JsonUtilities` već su u `MinecraftClone.Build.cs` (linije 23–24).
- `UCraftingRecipeRegistry` već čita JSON iz `Content/Data/Recipes/` — isti tip
  subsystema, isti lifecycle. Kopira se postojeći obrazac.
- Za razliku od recepata (ručni parser ~250 linija), za blokove je dovoljan
  `FJsonObjectConverter::JsonArrayStringToUStruct<FBlockDefinition>` — mapira
  cijeli struct odjednom, UENUM čita po imenu (`"blockType": "Dirt"`), a polja
  koja u JSON-u fale zadržavaju defaulte iz USTRUCT-a.
- `BuildBlockAssetCache` (`VoxelWorld.cpp:539`) već tolerira null materijal:
  ISM komponenta se kreira čim se mesh učita, bez materijala ostaje siva.
  Fallback definicija zato "besplatno" prolazi kroz cijeli render put.

## Koraci

### 1. JSON datoteke — `Content/Data/Blocks.json` i `Content/Data/Items.json`

Jedna datoteka, jedan array (za razliku od recepata koji su file-per-recipe —
blokova je malo i mijenjaju se zajedno). Ključevi = imena polja iz
`FBlockDefinition` / `FItemDefinition`:

```json
[
  {
    "blockType": "Dirt",
    "dropItemType": "Dirt",
    "placeableFromItem": "Dirt",
    "dropChance": 1.0,
    "timeToDestroy": 1.5,
    "displayName": "Dirt",
    "material": "/Game/Blocks/Materials/MI_Dirt.MI_Dirt"
  }
]
```

Polja `mesh` i `highlightMaterial` u pravilu se izostavljaju — post-load prolaz
u C++ puni prazan `Mesh` s `/Engine/BasicShapes/Cube.Cube` i prazan
`HighlightMaterial` s `M_BlockHighlight`. Time JSON sadrži samo ono što je
stvarno specifično po bloku.

Napomena: `FText DisplayName` iz JSON stringa postaje kulturno-invarijantan
tekst (gubi se `NSLOCTEXT` lokalizacija) — svjesna i prihvaćena posljedica.

### 2. Loader u `UBlockRegistry` (C++)

- Nove privatne metode `LoadBlocksFromJson()` / `LoadItemsFromJson()` zamjenjuju
  tijela `RegisterAllBlocks()` / `RegisterAllItems()`.
- Putanja: `FPaths::ProjectContentDir() / TEXT("Data/Blocks.json")` —
  isti obrazac kao `CraftingRecipeRegistry.cpp:21`.
- `FFileHelper::LoadFileToString` → `FJsonObjectConverter::JsonArrayStringToUStruct`
  → petlja poziva postojeći `RegisterBlock()` / `RegisterItem()` (cache mape
  Item↔Block ostaju kako jesu).
- Ako datoteka ne postoji ili se ne parsira: `UE_LOG(Error)` s putanjom i
  nastavak praznog registryja — fallback prolaz (korak 3) tada pokrije SVE
  blokove, pa igra i dalje radi sa sivim kockama umjesto da pukne.

### 3. Validacijski + fallback prolaz (ključna novost)

Nakon učitavanja, u `Initialize()`:

- Iteriraj `StaticEnum<EBlockType>()` preko svih vrijednosti osim `Air`.
  Za svaku bez definicije:
  - `UE_LOG(Error, "BlockRegistry: nema JSON definicije za blok '%s' - koristim fallback")`
  - Registriraj sintetsku `FBlockDefinition`: `Mesh` = default kocka,
    `Material` = prazan (ISM ostaje siv), `HighlightMaterial` = `M_BlockHighlight`,
    `DisplayName` = ime enum vrijednosti, `DropItemType`/`PlaceableFromItem` =
    `None`, ostalo defaulti.
- Isto za `EItemType` (osim `None`): fallback `FItemDefinition` s default kockom
  i imenom iz enuma, `MaxStackSize = 64`.
- Cross-check reference: `dropItemType`/`placeableFromItem` koji pokazuju na
  item bez definicije → `UE_LOG(Warning)` (fallback item svejedno postoji).

Efekt: `BuildBlockAssetCache` uvijek dobije definiciju za svaki tip →
`InstanceSets` uvijek ima ISM set → `PlaceBlockAt` (`VoxelWorld.cpp:500-507`)
više nikad ne odbija blok zbog rupe u registryju. Tipfeler u JSON-u vidi se kao
siva kocka + Error u logu, ne kao nestali blok.

### 4. Packaging (config, ne Editor)

`.json` u `Content/` se ne cooka — samo `.uasset`. Rješenje bez Editora, ručni
unos u `Config/DefaultGame.ini`:

```ini
[/Script/UnrealEd.ProjectPackagingSettings]
+DirectoriesToAlwaysStageAsNonUFS=(Path="Data")
```

Time se pokriva i postojeći latentni problem s `Data/Recipes/`.

### 5. Test (headless, bez Editora)

1. Rekompilacija + headless run (vidi CLAUDE.md, `-nullrhi`); u logu provjeriti
   `BlockRegistry: Initialized with N blocks and M items` (N=10, M=11) i
   `[PERF] 1/4 Assets` (broj definicija i ISM komponenti nepromijenjen).
2. Negativni test: privremeno preimenovati `"Stone"` u `"Stonee"` u JSON-u →
   očekuje se Error linija o fallbacku za Stone, svijet se i dalje generira,
   `[PERF]` broji istu količinu ISM setova.
3. Ručno u Editoru: postaviti/razbiti blok s fallback definicijom (siva kocka,
   highlight radi, nema dropa).

## Odgođeno (ne u ovom zahvatu)

- Runtime reload JSON-a (`Blocks.Reload` exec) — zahtijeva rebuild ISM setova u
  `AVoxelWorld`; zasad je restart PIE-a dovoljan.
- Spajanje `DT_ItemData` (ikone za UI) u `Items.json` — dira
  `InventorySlotWidget`, `HotbarWidget`, `FullInventoryWidget`,
  `InventoryComponent::GetItemData` i Blueprint reference.
- Ukidanje enuma u korist string ID-ova — svjesno NE: enum + rekompilacija je
  željeni workflow.
