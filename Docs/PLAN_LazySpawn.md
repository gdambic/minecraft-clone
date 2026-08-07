# PLAN: Lazy spawn zakopanih blokova (4.1 iz PLAN_UbrzanjePokretanja.md)

> ✅ **IMPLEMENTIRANO I TESTIRANO** (7.8.2026.) — implementirano po planu,
> ručno provjereno u editoru: svi testovi iz sekcije 7 prolaze.

## 1. Cilj

Ne spawnati `ABlock` actore za blokove koje igrač ne može vidjeti (potpuno
zakopani kamen). Tipovi blokova žive u laganoj mapi podataka, a actor se
spawna tek kad blok postane izložen (ima barem jednu vidljivu stranu).

Očekivano za 100×100, SurfaceLevel=3:

```
Danas:  40.020 actora  (~4 s)
Poslije: ~11.200 actora (~1 s)
  - 10.000  površinski sloj (Grass, gornja strana izložena)
  -  ~1.200 rubne kolone donjih slojeva (bočne strane na rubu svijeta)
  -    ~20  random Dirt + blokovi stabala
```

Uz brži start: manje memorije, manje draw poziva, manji nav-octree.

## 2. Novi model podataka: dva sloja

**Sloj podataka** — izvor istine za "što je gdje":

```cpp
// AVoxelWorld (private)
TMap<FIntVector, EBlockType> BlockData;   // NEMA unosa = Air
```

**Sloj actora** — postojeći `Blocks`, ali sada sadrži SAMO izložene blokove:

```cpp
UPROPERTY()
TMap<FIntVector, ABlock*> Blocks;         // podskup BlockData
```

Invarijanta: svaka pozicija u `Blocks` ima unos u `BlockData` istog tipa.
Obrnuto NE vrijedi (zakopani blokovi postoje samo u `BlockData`).

### Ukidanje "Air actora"

Danas uništeni blok ostaje zauvijek kao nevidljivi actor s
`BlockType = Air` (`ABlock::AddDestroyProgress` → `SetBlockType(Air)`), a
`PlaceBlockAt` ima poseban slučaj za zamjenu Air actora. S mapom podataka
Air = odsutnost ključa:

- uništeni blok → makni iz `BlockData`, `Destroy()` actor, makni iz `Blocks`
- poseban slučaj u `PlaceBlockAt` se briše
- `ABlock::UpdateVisibility` više nema Air granu (actor uvijek predstavlja
  čvrst blok)

Sigurno je jer je `CurrentlyLookedAtBlock` u `FirstPersonCharacter.h`
`UPROPERTY` — UE ga automatski nulira kad se actor uništi.

## 3. Pravilo izloženosti

Blok je izložen ako mu je barem jedan od 6 susjeda "prozičan":

```cpp
bool AVoxelWorld::IsBlockExposed(FIntVector Pos) const
{
    // susjed je prozičan ako: nema unosa u BlockData (Air)
    // ILI je izvan granica svijeta — s iznimkom dna (vidi dolje)
}
```

Rubni slučajevi granica svijeta:

| Susjed | Tretman | Razlog |
|--------|---------|--------|
| Z - 1 < 0 (ispod dna) | **čvrst** (nije izloženo) | donju stranu svijeta nitko ne vidi; štedi 10.000 actora donjeg sloja |
| X/Y izvan `WorldSize` | **prozičan** (izloženo) | bočne strane svijeta vide se izvana/s ruba |
| Z iznad | prozičan | normalan zrak |

## 4. Promjene po datotekama

### 4.1. `VoxelWorld.h` — novi API

```cpp
public:
    /** Tip bloka iz mape podataka; Air ako nema unosa. Radi i za zakopane. */
    UFUNCTION(BlueprintPure, Category = "World")
    EBlockType GetBlockTypeAt(FIntVector GridPosition) const;

    /** Poziva ABlock nakon što je uništen (drop je već spawnan). */
    void NotifyBlockDestroyed(FIntVector GridPosition, EBlockType DestroyedType);

private:
    TMap<FIntVector, EBlockType> BlockData;

    bool IsBlockExposed(FIntVector Pos) const;

    /** Spawnaj actor za poziciju iz BlockData ako već nije spawnan. */
    ABlock* EnsureBlockActor(FIntVector Pos);
```

`GetBlock` (vraća `ABlock*`) ostaje za pozivatelje kojima treba actor
(highlight, destrukcija), ali svi koji samo čitaju TIP prelaze na
`GetBlockTypeAt`.

### 4.2. `VoxelWorld.cpp` — GenerateWorld u dva prolaza

Faza 2 (teren) se dijeli:

```cpp
// PROLAZ 1: samo podaci — O(40.000 × TMap::Add), par ms
for (Z, X, Y u slojevima 0..SurfaceLevel)
    BlockData.Add(FIntVector(X, Y, Z), Type);
// + random Dirt isto samo u BlockData

// PROLAZ 2: actori samo za izložene — ~11.200 SpawnActor-a
for (auto& Pair : BlockData)
    if (IsBlockExposed(Pair.Key))
        EnsureBlockActor(Pair.Key);
```

`EnsureBlockActor` preuzima tijelo postojećeg `SpawnBlock`-a (registry
definicija + `BlockAssetCache` + `SpawnActor` + `InitializeFromRegistry`),
ali tip čita iz `BlockData`. `SpawnBlock(X,Y,Z,Type)` se briše ili postaje
tanki wrapper `BlockData.Add + EnsureBlockActor` (koristi se još samo za
random Dirt).

`PlaceBlockAt`:
- provjera zauzetosti: `BlockData.Contains` umjesto `Blocks.Contains`
- briše se poseban slučaj Air actora
- upisuje u OBJE mape (postavljeni blok je po definiciji izložen — igrač
  ga je upravo postavio na vidljivu poziciju)

`SetBlockType(X,Y,Z,NewType)`: ažurira `BlockData` + actor ako postoji.

`NotifyBlockDestroyed`:

```cpp
void AVoxelWorld::NotifyBlockDestroyed(FIntVector Pos, EBlockType DestroyedType)
{
    BlockData.Remove(Pos);
    if (ABlock* Actor = Blocks.FindRef(Pos)) { Blocks.Remove(Pos); Actor->Destroy(); }

    // Susjedi su možda upravo postali izloženi → lazy spawn
    for (6 susjeda) EnsureBlockActor(Susjed);   // no-op ako nema podataka ili već ima actor

    // Leaf decay notifikacije (premješteno iz ABlock::AddDestroyProgress)
    if (DestroyedType == OakLog || BirchLog)   OnLogDestroyed(Pos);
    if (DestroyedType == OakLeaves || BirchLeaves) OnLeafDecayed(Pos);
}
```

Leaf decay prelazi na podatke:
- `OnLogDestroyed` / `OnLeafDecayed` / `HasLogConnection`: umjesto
  `GetBlock(...)->BlockType` koristiti `GetBlockTypeAt` (BFS radi na
  podacima, `const` bez `const_cast`-a)
- `ProcessLeafDecay`: prije `AddDestroyProgress` pozvati
  `EnsureBlockActor(LeafPos)` (lišće je praktički uvijek izloženo pa
  actor već postoji, ali ovako je robusno)

### 4.3. `Block.cpp` — destrukcija ide kroz svijet

U `AddDestroyProgress`, granu `DestroyProgress >= 1.0f` promijeniti:

1. Spawn dropa ostaje kako jest (prije uklanjanja).
2. Umjesto `SetBlockType(EBlockType::Air)` →
   `VoxelWorld->NotifyBlockDestroyed(GridPosition, BlockType)`.
3. `VoxelWorld` dohvatiti preko `Cast<AVoxelWorld>(GetOwner())` —
   `SpawnParams.Owner = this` je već postavljen u oba spawn puta; briše se
   skupi `GetAllActorsOfClass` po svakoj destrukciji.
4. Log/leaf notifikacije se brišu iz `ABlock` (sele u
   `NotifyBlockDestroyed`).

`UpdateVisibility` se pojednostavljuje (actor više nikad nije Air), a
collision se može ostaviti kako jest — 4.2 mikro-optimizacije idu posebno.

### 4.4. `TreeGenerator.cpp` — provjere na podacima

- `CanPlaceTreeAt`: `World->GetBlock(...)` + null/Air provjere →
  `World->GetBlockTypeAt(...) != EBlockType::Air`
- `GenerateRandomTrees` (provjera tla): isto
- `GenerateCanopy` (ne prepiši log): isto
- `PlaceBlockAt` pozivi ostaju — blokovi stabala su iznad površine,
  uvijek izloženi, pa je spawn actora ispravan

### 4.5. `FirstPersonCharacter` — bez promjena

Raycast pogađa samo spawnane actore, a zakopani blokovi su ionako
zaklonjeni izloženima pa ih zraka ne može doseći. Nakon uništenja bloka
`NotifyBlockDestroyed` u istom frameu spawna susjede, pa sljedeći
`UpdateBlockLookAt` tick već ima što pogoditi.

## 5. Redoslijed implementacije (svaki korak kompajlira i radi)

1. **Uvedi `BlockData`** paralelno s postojećim ponašanjem: `SpawnBlock` i
   `PlaceBlockAt` upisuju u obje mape, sve se i dalje spawna. Dodaj
   `GetBlockTypeAt`. → ponašanje identično, podaci postoje.
2. **Prebaci čitatelje tipa na podatke**: TreeGenerator, leaf decay
   (`OnLogDestroyed`, `OnLeafDecayed`, `HasLogConnection`,
   `ProcessLeafDecay`). → ponašanje identično.
3. **Novi put destrukcije**: `NotifyBlockDestroyed` + `GetOwner()` u
   `ABlock`, ukidanje Air actora, čišćenje `PlaceBlockAt`. Test: kopanje,
   dropovi, leaf decay, ponovno postavljanje na iskopano mjesto.
4. **Uključi filtar izloženosti**: dva prolaza u `GenerateWorld` +
   `IsBlockExposed` + lazy spawn susjeda u `NotifyBlockDestroyed`.
   → ovdje se realizira ubrzanje.
5. **[PERF] logovi**: u fazi 2 ispisati i broj unosa u `BlockData` i broj
   spawnanih actora, npr:

   ```
   [PERF] 2/4 Teren   xxx.x ms   40020 blokova (11234 actora, 28786 lazy)
   ```

## 6. Rubni slučajevi i odluke

- **Postavljanje bloka koje zakopa susjeda**: susjed ostaje spawnan iako
  više nije izložen. Svjesno preskačemo despawn — rijetko, jeftino, a
  pojednostavljuje logiku. (Opcionalna kasnija optimizacija.)
- **Random Dirt na površini**: Grass ispod njega ostaje izložen bočno —
  nema posebnog slučaja.
- **Leaf decay zakopanog lišća**: nemoguće u praksi, ali
  `EnsureBlockActor` u `ProcessLeafDecay` pokriva i to.
- **Mobovi / navigacija**: nav mesh se gradi samo od spawnanih actora —
  površina je spawnana, pa navigacija radi kao i dosad (i brže se builda).
- **`Blocks.Num()` u [PERF] sažetku**: sada broji actore; za "ukupno
  blokova" koristiti `BlockData.Num()`.

## 7. Testni checklist (ručno u editoru)

- [+] Start igre: teren izgleda identično kao prije (površina, rubovi
      svijeta izvana, stabla, random Dirt)
- [+] `[PERF] UKUPNO` pao s ~4000 ms na ~1000-1200 ms
- [+] Iskopaj Grass → ispod se pojavi Stone (lazy spawn), može se dalje
      kopati u dubinu i bočno
- [+] Iskopaj blok na rubu svijeta → susjedi se pojave ispravno
- [+] Postavi blok na iskopano mjesto → radi (nema ostataka Air actora)
- [+] Sruši deblo → lišće propada, saplingi padaju (decay na podacima)
- [+] Dropovi se spawnaju i pokupe normalno
- [+] Zombie i ovce hodaju po površini (nav mesh OK)
- [+] Highlight bloka radi, nema crash-a kad decay uništi blok koji
      igrač gleda
