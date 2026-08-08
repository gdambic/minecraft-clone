# PLAN: Instanced Static Mesh (ISM) — hibridni pristup

> ✅ IMPLEMENTIRANO (faze 1-4; faza 5 = ručni test u editoru)
>
> Implementira sekciju 4.3 iz [PLAN_UbrzanjePokretanja.md](PLAN_UbrzanjePokretanja.md).
> Očekivani dobitak: ~10-50× brži spawn terena, drastično manje draw poziva
> (s ~10.000 komponenti na ~8), znatno manje memorije. Preduvjet za veće svjetove.

## 1. Ideja u jednoj rečenici

Svijet se renderira kao **instance** (jedan `UInstancedStaticMeshComponent` po
tipu bloka na `AVoxelWorld`), a pravi `ABlock` actor postoji **samo za blok koji
igrač trenutno gleda** — on preuzima highlight, progress uništavanja i drop.

## 2. Zašto je ovo sad lako(-α)

Lazy spawn refactor (4.1) je već napravio ključne pripreme:

- `BlockData` (`TMap<FIntVector, EBlockType>`) je **izvor istine** — actori su
  već samo "vizualni prikaz" podskupa blokova.
- `BlockAssetCache` već drži gotove `UStaticMesh*` / `UMaterialInterface*`
  pointere po tipu — točno ono što treba za kreiranje ISM komponenti.
- Sva logika mutacije svijeta već ide kroz `AVoxelWorld`
  (`PlaceBlockAt`, `NotifyBlockDestroyed`, `SetBlockType`).

Mijenja se samo **kako se izloženi blok prikazuje**: umjesto `SpawnActor` →
`AddInstance`. Logika "koji blok je izložen" (`IsBlockExposed`) ostaje ista.

## 3. Ciljna arhitektura

### 3.1. Instance setovi na AVoxelWorld

```cpp
// Jedan set po tipu bloka (Grass, Stone, Dirt, OakLog, ...)
USTRUCT()
struct FBlockInstanceSet
{
    GENERATED_BODY()

    UPROPERTY()
    UInstancedStaticMeshComponent* Component = nullptr;

    // Grid pozicija -> indeks instance u komponenti
    TMap<FIntVector, int32> GridToInstance;

    // Obrnuto: indeks instance -> grid pozicija.
    // NUŽNO zbog swap-remove semantike (vidi 5.1).
    TArray<FIntVector> InstanceToGrid;
};

UPROPERTY()
TMap<EBlockType, FBlockInstanceSet> InstanceSets;
```

Komponente se kreiraju **runtime u `BuildBlockAssetCache()`** (`NewObject` +
`SetupAttachment(RootComponent)` + `RegisterComponent`), jedna po definiciji iz
registry-a, s mesh + materijalom iz cachea. Postavke po komponenti:

- `SetCollisionEnabled(QueryOnly)` — dovoljno za raycast, hodanje i ItemDrop
  trace (objektni tip ostaje `WorldStatic`).
- `SetCanEverAffectNavigation(true)` — mobovi i dalje hodaju po terenu, ali
  sada je to **1 nav registracija po tipu** umjesto 1 po bloku.

### 3.2. Interne primitive (jedino mjesto koje smije dirati indekse)

```cpp
void AddBlockInstance(FIntVector Pos, EBlockType Type);   // AddInstance + bookkeeping
void RemoveBlockInstance(FIntVector Pos);                 // RemoveInstance + swap fix (5.1)
bool ResolveHitToGrid(const FHitResult& Hit, FIntVector& OutPos, EBlockType& OutType) const;
```

`ResolveHitToGrid`: ako je `Hit.Component` jedna od ISM komponenti,
`Hit.Item` je indeks instance → `InstanceToGrid[Hit.Item]`. Ako je
`Hit.GetActor()` promovirani `ABlock`, vrati njegov `GridPosition`.

### 3.3. Hibrid: promote / demote fokus-bloka

```cpp
// Ukloni instancu, spawnaj ABlock na istoj poziciji (postojeći EnsureBlockActor put).
// Actor preuzima highlight + AddDestroyProgress + drop.
ABlock* PromoteToActor(FIntVector Pos);

// Uništi actor (bez dropa!), vrati instancu. Poziva se kad igrač skrene pogled.
void DemoteToInstance(FIntVector Pos);
```

Invarijanta: **za jednu grid poziciju postoji ILI instanca ILI actor, nikad
oboje** (inače Z-fighting i dupli hitovi). Promote prvo uklanja instancu,
demote prvo uništava actor.

U praksi postoji **najviše 1 promovirani actor** u svakom trenutku
(blok koji igrač gleda) — `Blocks` mapa se svodi na to.

## 4. Faze implementacije

### Faza 1: Infrastruktura (VoxelWorld.h/.cpp)
- `FBlockInstanceSet`, `InstanceSets`, kreiranje komponenti u
  `BuildBlockAssetCache()`.
- `AddBlockInstance` / `RemoveBlockInstance` / `ResolveHitToGrid`.
- Ništa ih još ne zove — build mora proći, ponašanje nepromijenjeno.

### Faza 2: Generacija ide kroz instance
- U `GenerateWorld()` PROLAZ 2: umjesto `EnsureBlockActor(Pos)` skupi
  transform-e po tipu pa **batch** `AddInstances()` (jedan poziv po tipu —
  bitno brže od 10.000 × `AddInstance`).
- `NotifyBlockDestroyed`: susjede izlaže kroz `AddBlockInstance` (novi helper
  `EnsureBlockVisual(Pos)` = instanca ako nema ni instance ni actora).
- `SetBlockType` / `PlaceBlockAt`: isto — vizual je instanca, ne actor.
- `[PERF]` log: dodati broj instanci po tipu.
- **Nakon ove faze igra se pokreće brzo, ali interakcija je pokvarena** —
  odmah nastaviti na Fazu 3.

### Faza 3: Interakcija igrača (FirstPersonCharacter.cpp)
- `UpdateBlockLookAt()`: nakon trace-a pozvati
  `VoxelWorld->ResolveHitToGrid(...)`. Promjena fokusa:
  `DemoteToInstance(stari)` + `PromoteToActor(novi)` + `SetHighlighted(true)`.
- `CurrentlyLookedAtBlock` ostaje `ABlock*` (promovirani actor) → destrukcija
  (`Tick`, `StartAttack`, `StopAttack`) i `PlaceBlock()` (koristi
  `GridPosition` + normalu) rade **bez izmjena**.
- `StopAttack`/skretanje pogleda već resetira progress — demote to zadrži.

### Faza 4: Ostali pozivatelji
- `PlaceBlockAt` mijenja povratni tip u `bool` (actor više ne nastaje):
  prilagoditi `TreeGenerator.cpp:38` i `FirstPersonCharacter::PlaceBlock`.
- **Leaf decay** (`ProcessLeafDecay`): umjesto promote-pa-uništi uvesti
  `AVoxelWorld::DestroyBlockAt(FIntVector Pos)` — spawna drop iz registry
  podataka (`DropItemType`, `DropChance`) i zove postojeću
  `NotifyBlockDestroyed` logiku. Bez actora uopće.
- **ItemDrop.cpp** (provjera tla): NIJE trebala izmjena — postojeći fallback
  branch (ne-`ABlock` hit → `Hit.ImpactPoint.Z` kao površina) ispravno pokriva
  ISM instance; trace ide okomito dolje pa je impact na gornjoj plohi bloka.
- `GetBlock(X,Y,Z)`: vraća samo promovirani actor — dokumentirati da je za
  tip bloka jedini ispravan API `GetBlockTypeAt`.

### Faza 5: Validacija
- `[PERF]` prije/poslije (očekivano: faza 2/4 Teren s ~1000 ms na <50 ms).
- `stat scenerendering` — draw pozivi terena s ~10.000 na ~broj tipova.
- Ručni test checklist:
  - [ ] highlight prati pogled, nema duplog bloka (Z-fighting)
  - [ ] kopanje: progress, drop, pickup, lazy susjedi postaju vidljivi
  - [ ] postavljanje bloka (RMB) na sve strane, inventar se smanjuje
  - [ ] stabla se generiraju, leaf decay radi i dropa sapling
  - [ ] zombie/ovce hodaju po terenu (navigacija na ISM)
  - [ ] ItemDrop pada i staje na tlu (i na instanci i na promoviranom actoru)
  - [ ] rušenje bloka ispod sebe / kopanje u dubinu

## 5. Rizici i rubni slučajevi

### 5.1. ⚠ RemoveInstance mijenja tuđi indeks (glavni izvor bugova)
**Nalaz iz UE 5.6 sourcea:** `RemoveInstance(i)` po *defaultu* radi
order-preserving `RemoveAt` (pomiče SVE kasnije indekse!), a swap-remove se
uključuje eksplicitno sa `SetRemoveSwap()` na komponenti — što radimo pri
kreiranju u `BuildBlockAssetCache`. Uz swap-remove zadnja instanca dobiva
indeks `i`, pa `RemoveBlockInstance` ažurira bookkeeping za premještenu
instancu:

```cpp
const int32 Removed = GridToInstance[Pos];
const int32 Last = InstanceToGrid.Num() - 1;
Component->RemoveInstance(Removed);
if (Removed != Last)
{
    const FIntVector MovedPos = InstanceToGrid[Last];
    InstanceToGrid[Removed] = MovedPos;
    GridToInstance[MovedPos] = Removed;
}
InstanceToGrid.RemoveAt(Last);
GridToInstance.Remove(Pos);
```

Indeksi se **nikad ne smiju čuvati izvan** `FBlockInstanceSet`-a — uvijek
ići kroz `GridToInstance` u trenutku korištenja.

### 5.2. Navigacija — ⚠ tri stvarna problema nađena i riješena

Nakon prelaska na ISM mobovi su stajali na mjestu. Headless test
(`-game -nullrhi` + log) otkrio je tri neovisna uzroka:

1. **Nav octree element = samo prva instanca.** Engine registrira ISM u nav
   octree kod PRVE dodane instance, s bounds te jedne instance; batch
   `AddInstances` nakon toga NE re-registrira element → navmesh builder je
   svugdje osim oko prve instance (grid 0,0) vidio prazninu. Fix: nakon
   batcha `UpdateBounds()` + `FNavigationSystem::UpdateComponentData()`
   (`AddBlockInstancesBatch`).
2. **NavMeshBoundsVolume u leveli je ogroman** → uz tile od 1000uu traži se
   8,2M tileova, iznad `TileNumberHardLimit` (1M), pa engine odsijeca
   adresabilni prostor i dijelovi svijeta nikad ne dobiju navmesh (log:
   `Navmesh bounds are too large!`). Fix: `TileSizeUU=3000` u
   `DefaultEngine.ini` (9× manje tileova). Alternativa: smanjiti volumen u
   leveli na veličinu svijeta.
3. **Invoker-only generacija bez invokera na ovcama.** Projekt ima
   `bGenerateNavigationOnlyAroundNavigationInvokers=True` — navmesh se gradi
   samo oko `UNavigationInvokerComponent`. Imao ga je samo BP_Zombie (u BP-u).
   Fix: invoker u C++ na `ACreatureBase` (3000/3500) — svi mobovi ga
   nasljeđuju. Invoker u BP_Zombie je time redundantan (bezopasan, može se
   maknuti iz BP-a).

Verifikacija (45 s headless run): `Pawn on NavMesh: YES`,
`MoveToActor: RequestSuccessful`, wander failovi 69.228 → 11.

Napomena: postavljanje blokova DALEKO izvan originalnih bounds terena
(npr. most u zrak izvan ruba svijeta) ne proširuje nav octree element —
ako to ikad zatreba, pozvati `UpdateComponentData` i u `AddBlockInstance`
kad pozicija ispadne izvan cache-iranih bounds.

### 5.3. Collision mesh kocke
ISM koristi simple collision static mesha — provjeriti da mesh kocke ima
simple box collision (engine Cube ima). Bez toga trace/hodanje ne radi.

### 5.4. Highlight materijal
Promovirani actor koristi postojeći material-swap — instanca istog tipa oko
njega izgleda identično (isti mesh/materijal), pa promote/demote vizualno
nema "pop".

## 6. Što se NE mijenja

- `BlockData` ostaje izvor istine; `GetBlockTypeAt`, `IsBlockExposed`,
  leaf-decay BFS, inventar, drop sustav, registry — netaknuti.
- `ABlock` klasa ostaje (highlight, destrukcija, drop) — samo živi kraće:
  od pogleda do skretanja pogleda.
- Redoslijed generacije (assets → teren → stabla → mobovi) i `[PERF]` mjerenje.

## 7. Procjena

| Datoteka | Opseg |
|----------|-------|
| `VoxelWorld.h/.cpp` | glavnina — instance setovi, promote/demote, generacija |
| `FirstPersonCharacter.cpp` | `UpdateBlockLookAt` refactor |
| `TreeGenerator.cpp` | trivijalno (povratni tip) |
| `ItemDrop.cpp` | provjera tla bez `Cast<ABlock>` |
| `Block.cpp/.h` | minimalno (ništa strukturno) |

Faze 1-2 daju startup dobitak, faze 3-4 vraćaju funkcionalnost — raditi ih
kao jednu cjelinu (jedan commit/branch), jer je između njih igra neigriva.
