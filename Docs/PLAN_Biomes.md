# PLAN: Biomi i biome tint (Grass, lišće)

> **STATUS (2026-09-25): IMPLEMENTIRANO za Grass** (faze 1-6 ispod). Lišće po
> odluci izvan opsega. Side rub se tinta kroz `T_Grass_SideOverlay` teksturu
> **automatski generiranu** iz sivih piksela `T_Grass_Side` (kriterij: kanali
> se razlikuju ≤ 12); isti kriterij koristi `generate_item_sprites.py` za
> ikonu. Novi dijelovi: `BiomeGenerator.h/.cpp`, `BiomeDefinition.h`,
> `Biomes.json`, tint grana u `M_VoxelBlock` (build skripta), custom data u
> `VoxelWorld`, TintFallback MID u promote/drop/ruci. Otvoreno: blending
> granica, biom u generaciji (stabla/surfaceBlock), foliage tint.

## Cilj

Grass (i kasnije lišće) trenutno imaju sivo-bijelu baznu teksturu. Cilj je da boja
ovisi o biomu: ravnica žućkasto-zelena, šuma zasićeno zelena, kasnije snježni biom
gotovo bijel itd. Biom pritom nije samo boja — isti sustav kasnije određuje i gustoću
stabala, vrstu drveća, površinski blok (pijesak u pustinji) i spawn mobova.

## Odgovor ukratko

1. **Biomi se definiraju u `Content/Data/Biomes.json`** — isti data-driven obrazac kao
   `Blocks.json`/`Items.json`. Novi `FBiomeDefinition` + registry, bez hardkodiranja.
2. **Biom stupca (X,Y) je čista funkcija noisea** — kao `FTerrainGenerator::GetColumnHeight`,
   deterministična iz seeda. Nigdje se ne sprema; uvijek se izračuna iz pozicije.
3. **Tint se rendera kroz `PerInstanceCustomData`** (3 floata = RGB po ISM instanci).
   Jedan Grass ISM i dalje drži sve grass blokove svih bioma — jedan draw call.
4. Promovirani `ABlock`, item drop i inventory ikona dobivaju tint drugim putem
   (MID parametar / default tint), jer izvan ISM-a custom data ne postoji.

---

## Zašto JSON ima smisla

Sve u projektu što je **podatak, a ne logika** već živi u JSON-u (`Blocks.json`,
`Items.json`, `Recipes/`). Biom je čisti podatak: ime, dvije boje, nekoliko brojeva
koji ugađaju generaciju. Argumenti:

- Dodavanje/ugađanje bioma (boja, gustoća stabala) ne smije tražiti rekompilaciju —
  tint će se ugađati vizualno, iterativno.
- `build_block_materials.py` i `generate_item_sprites.py` već čitaju `Blocks.json`
  kao izvor istine; `Biomes.json` im daje default tint za ikone na isti način.
- Za razliku od blokova, biomu **ne treba enum u C++** — na biom se ništa ne switcha
  po tipu; sve što kod radi je "daj mi definiciju bioma na (X,Y)". Identitet bioma je
  ime (FName) + indeks u listi. Novi biom = novi JSON unos, nula linija C++.

### Prijedlog formata `Biomes.json`

```json
[
  {
    "name": "Plains",
    "displayName": "Ravnica",
    "grassTint": "#6FAD6A",
    "foliageTint": "#6FAD6A",
    "treeDensity": 1.0,
    "treeTypes": ["Oak", "Birch"],
    "surfaceBlock": "Grass",
    "heightAmplitudeScale": 1.0
  },
  {
    "name": "Desert",
    "displayName": "Pustinja",
    "grassTint": "#99C299",
    "foliageTint": "#99C299",
    "treeDensity": 0.1,
    "treeTypes": ["Oak"],
    "surfaceBlock": "Grass",
    "heightAmplitudeScale": 0.5
  }
]
```

Za prvu fazu koriste se samo `name` i `grassTint`. Ostala polja (uključivo `foliageTint`)
(`treeDensity`, `treeTypes`, `surfaceBlock`, `heightAmplitudeScale`) su rezervirana —
parser ih učita u `FBiomeDefinition`, a generacija ih počne koristiti u kasnijoj fazi.
Tako format ne treba mijenjati kad biomi počnu utjecati na teren.

Uz to, `Blocks.json` dobiva opcionalno polje **po bloku** koje kaže *koji* tint blok
prima (analogno postojećem `masked`):

```json
{ "blockType": "Grass", "biomeTint": "grass", ... }
```

Lišće (`"biomeTint": "foliage"` na OakLeaves/BirchLeaves) je **izvan opsega prve
implementacije** — dodaje se kasnije samo JSON unosom, bez izmjena koda.

Blok bez polja ne prima tint. Time odluka "što se tinta" ostaje uz blok, a
"kojom bojom" uz biom — svaka informacija na svom mjestu.

---

## Arhitektura

### 1. `FBiomeDefinition` + učitavanje

- `Source/MinecraftClone/Voxel/BiomeDefinition.h` — struct s poljima iz JSON-a
  (`FName Name`, `FLinearColor GrassTint`, `FLinearColor FoliageTint`, rezervirana polja).
- Učitava se u `UBlockRegistry` (isti lifecycle i error handling kao Blocks/Items;
  poseban `UBiomeRegistry` bi dublicirao GameInstance-subsystem boilerplate bez dobitka).
  Fallback kad `Biomes.json` fali ili je prazan: jedan implicitni biom s bijelim tintom
  (= današnje ponašanje) + `Error` u logu.
- Boje u JSON-u kao `#RRGGBB` (čitljivo, copy-paste iz color pickera);
  parse preko `FColor::FromHex`.

### 2. Odabir bioma: `FBiomeGenerator`

Nova statična utility klasa uz `FTerrainGenerator`, isti obrazac — čista funkcija:

```cpp
// indeks bioma u registry listi za stupac (X,Y)
static int32 GetBiomeIndexAt(int32 X, int32 Y, float BiomeOffsetX, float BiomeOffsetY,
                             float BiomeScale, int32 BiomeCount);
```

- Jedan **niskofrekventni** Perlin noise (znatno manji scale od terenskog —
  biomi su regije od ~50+ blokova, brda su unutar njih). Vrijednost noisea se
  podijeli na `BiomeCount` jednakih pragova → indeks bioma.
- Offseti se izvode iz `WorldSeed` kao i terenski (drugi par vrijednosti iz istog
  `FRandomStream`), pa isti seed uvijek daje iste biome.
- `AVoxelWorld` dobiva `GetBiomeAt(X, Y)` (BlueprintPure) koji vraća definiciju —
  to je jedina točka kroz koju ostatak koda pita za biom.
- Svjesno **bez spremanja bioma po bloku**: biom je funkcija (X,Y), pa ga lazy putevi
  (EnsureBlockVisual, postavljanje bloka, decay) uvijek mogu ponovno izračunati.
  Nula bajta po bloku, nema sinkronizacije.

Kasnija nadogradnja (ne sada): dva noisea (temperatura + vlaga) i odabir najbližeg
bioma u tom 2D prostoru — Minecraftov pristup, daje prirodnije susjedstvo bioma.
Potpis funkcije se ne mijenja.

### 3. Rendering: `PerInstanceCustomData` (RGB po instanci)

Ključno ograničenje: svi Grass blokovi su instance **jedne** ISM komponente s
**jednim** dijeljenim `MI_Grass`. Tint po biomu dakle ne može biti parametar
materijala. Opcije su bile:

- **ISM komponenta po (tip × biom)** — bez izmjene shadera, ali množi komponente,
  komplicira bookkeeping (`FBlockInstanceSet` ključ više nije samo tip) i zauvijek
  onemogućuje glatko stapanje boje na granici bioma. Odbačeno.
- **`PerInstanceCustomData`, 1 float = indeks bioma** — shader radi lookup boje.
  Lookup tablica u materijalu (curve atlas / niz parametara) je nespretna i opet
  veže shader uz broj bioma. Odbačeno.
- **`PerInstanceCustomData`, 3 floata = RGB** — odabrano. Shader ostaje glup
  (množenje bojom), broj bioma ga se ne tiče, a granice bioma kasnije mogu
  interpolirati boju po instanci (blending) bez ikakve izmjene materijala.

Trošak: 12 bajta po instanci samo na komponentama blokova koji imaju `biomeTint`
(Grass, lišće) — zanemarivo uz postojeći transform od 64 bajta.

#### Izmjena master materijala (`build_block_materials.py`)

U prvoj fazi samo `M_VoxelBlock` (Grass je opaque; `M_VoxelBlock_Masked` dobije istu
granu tek kad na red dođe lišće). Na kraj postojećeg lanca:

```
postojeći Lerp lanac (Top/Side/Bottom) ─▶ Multiply ─▶ Base Color
                                             ▲
     PerInstanceCustomData(0,1,2, default=1,1,1) ─▶ Multiply ◀─ VectorParameter "TintFallback" (default bijelo)
```

- `PerInstanceCustomData` s **defaultom 1** znači: mesh koji nije ISM instanca
  (promovirani `ABlock`, item drop, ruka) dobije neutralno bijelo — ništa se ne
  razbije na blokovima bez tinta.
- `TintFallback` je put za ne-ISM prikaze (točka 4). Na ISM putu je bijel, na
  ne-ISM putu custom data je bijela — uvijek množi točno jedna boja, nema duplog tintanja.
- **Otvoreno pitanje — maska tinta po strani.** Minecraft tinta samo top teksturu
  (cijelu) i poseban side *overlay* (zeleni rub preko netintane zemlje). Ako se tinta
  cijeli side, zemljani dio postane zelen. Dvije opcije, odluka pri implementaciji
  prema tome kako je nacrtan `T_Grass_Side`:
  - **(a)** tint množi samo Top granu (preko postojeće `TopMask`), side rub ostane
    fiksno zelen nacrtan u teksturi — najjednostavnije, bez novih tekstura;
  - **(b)** četvrta opcionalna tekstura `T_<Blok>_SideOverlay` (tintana, composited
    preko netintanog Side) — vjerni Minecraft, skripta je doda samo blokovima koji
    imaju tu datoteku.
  Za lišće je trivijalno: tinta se sve strane (ista tekstura svuda).

#### Izmjene u `AVoxelWorld`

1. `BuildBlockAssetCache` / kreiranje ISM komponenti (`VoxelWorld.cpp:576`):
   za tipove s `biomeTint` postavi `SetNumCustomDataFloats(3)` **prije** prvog
   `AddInstance`.
2. `AddBlockInstance` (`VoxelWorld.cpp:321`): nakon `AddInstance` postavi
   `SetCustomDataValue(Index, 0..2, Tint)` iz `GetBiomeAt(Pos.X, Pos.Y)`.
3. `AddBlockInstancesBatch` (`VoxelWorld.cpp:350`): `AddInstances` s
   `bShouldReturnIndices=true`, zatim custom data **range overloadom**
   `SetCustomData(InstanceIndexStart, InstanceIndexEnd, TConstArrayView<float>)`
   (UE 5.6, `InstancedStaticMesh.cpp:3734`) — jedan memcpy za cijeli batch.
   Indeksi iz generacijskog batcha su kontinuirani (append na svježu komponentu),
   pa range odgovara. `MarkRenderStateDirty` NE treba: u 5.6
   `FInstanceDataManager::CustomDataChanged` → `MarkRenderInstancesDirty()`
   sam gura delta update na GPU krajem framea. Izbjegavati petlju
   `SetCustomDataValue` po floatu — svaki poziv radi bezuvjetni `Modify()`
   (`InstancedStaticMesh.cpp:3697`) i zasebno označavanje u update trackeru.
4. **Provjeriti** (ne pretpostaviti): da `RemoveInstance` uz `SetRemoveSwap`
   ispravno swapa i `PerInstanceSMCustomData` — test: obriši grass blok u sredini
   šume i provjeri da nijedan drugi grass nije promijenio boju. Ako engine to ne
   održava, `RemoveBlockInstance` mora ručno prekopirati custom data zadnje instance
   (bookkeeping za pozicije već postoji, `FBlockInstanceSet` komentar upozorava na isto).

### 4. Ne-ISM prikazi bloka

| Prikaz | Put do tinta |
|---|---|
| Promovirani `ABlock` (fokus) | U `PromoteToActor`/`ABlock::Initialize`: ako blok ima `biomeTint`, kreiraj MID od materijala i postavi `TintFallback` = tint bioma na toj poziciji. Bez toga bi blok vidljivo posivio čim ga igrač pogleda. |
| `AItemDrop` (grass kocka na podu) | `TintFallback` = **default tint** (tint prvog bioma u JSON-u, kao Minecraftov plains-default za ikone). Drop ne mora pratiti biom u kojem leži. |
| Inventory/hotbar ikona | `generate_item_sprites.py` pri kompozitiranju pomnoži Top (i side rub, ovisno o opciji a/b) default tintom iz `Biomes.json`. Regenerirati sprite-ove. |
| Blok u ruci (`FirstPersonArmComponent`) | Isti MID pristup kao ItemDrop — default tint. |

### 5. Kasnije faze (izvan opsega prve implementacije)

- **Blending na granicama**: prosjek RGB tinta susjednih stupaca (npr. 3×3 ili 5×5)
  po instanci — Minecraftov glatki prijelaz. Zahvaljujući RGB custom data pristupu
  ovo je izmjena samo u C++ petlji koja računa tint, shader se ne dira.
- **Biom utječe na generaciju**: `GenerateTrees` čita `treeDensity`/`treeTypes` bioma
  umjesto globalnog `TreeCount`; PROLAZ 1 čita `surfaceBlock` (pustinja → Sand) i
  `heightAmplitudeScale`; `SpawnMobs` po biomu.
- **Snijeg/voda**: snježni biom kao test da treći biom stvarno ne traži ništa osim
  JSON unosa.

---

## Redoslijed implementacije

1. **Podaci**: `Biomes.json` (2 bioma: Plains #6FAD6A, Desert #99C299) + `FBiomeDefinition` + parse u
   `UBlockRegistry` + `biomeTint` polje u `Blocks.json`/`FBlockDefinition`.
2. **Odabir bioma**: `FBiomeGenerator::GetBiomeIndexAt` + `AVoxelWorld::GetBiomeAt` +
   seed offseti u `GenerateWorld`. Privremena dijagnostika: log broja stupaca po biomu.
3. **Shader**: tint grana u oba mastera u `build_block_materials.py` (+ odluka a/b za
   side), regeneracija materijala.
4. **ISM custom data**: točke 1–4 iz odjeljka *Izmjene u `AVoxelWorld`*.
5. **Ne-ISM putevi**: PromoteToActor MID, ItemDrop/ruka default tint, regeneracija ikona.
6. **Test**: headless run (markeri broja bioma), pa ručno u editoru — granica bioma
   vidljiva, kopanje/postavljanje/fokus bloka ne mijenja boju susjeda.
