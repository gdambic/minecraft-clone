# Teksture i materijali blokova

Kako napraviti blok koji ima drukčiji izgled na svakoj strani (npr. trava: zeleno gore,
zemlja dolje, prijelaz sa strane).

## Odgovor ukratko

Tekstura **nije jedna slika**. Za blok s različitim stranama crtaš **tri odvojene kvadratne
slike** — jednu za gornju stranu, jednu za bočne, jednu za donju. Tako radi i sam Minecraft.

Za blok trave:

```
grass_top.png     16 x 16 px    zelena
grass_side.png    16 x 16 px    zemlja, gore zeleni rub
dirt.png          16 x 16 px    zemlja  (koristi se za dno)
```

Sve četiri bočne strane dijele **istu** sliku. Zato su tri slike, a ne šest.

Blok koji je sa svih strana isti (kamen, daske) ima samo jednu sliku.

Ono što slike spaja u kocku nije tekstura nego **materijal** — on odlučuje koju sliku
staviti na koju stranu.

---

## Korak 1 — Nacrtaj tri slike

Alat: Aseprite, Pixelorama ili GIMP. Novi dokument **16 x 16 px**.

Pravila:

- Veličina mora biti potencija broja 2 i kvadrat (16x16, 32x32). Ne miješaj rezolucije
  između blokova — inače će jedan izgledati oštro, a susjedni mutno.
- **Rubovi se moraju bešavno spajati.** Blokovi stoje jedan do drugog, pa tekstura mora
  tileati: `top` u oba smjera, `side` vodoravno (susjedni blokovi) i okomito (stup zemlje).
  Provjera: složi sliku u mrežu 3x3 i pogledaj vide li se šavovi.
- Ne crtaj sjene ni usmjereno svjetlo — to radi engine. Peče se samo ambient occlusion,
  i to rijetko.
- Ne stavljaj tamni okvir oko pločice.
- 3–5 nijansi po materijalu je dovoljno za Minecraft izgled.

Spremi kao PNG.

## Korak 2 do 4 — automatizirano

Koraci importa, master materijala i MI instanci **rade se skriptom**, ne ručno:

```
Scripts/build_block_materials.py
```

Pokretanje iz Output Log > Python:

```python
exec(open(r"C:/RVS/C++GameDev/MinecraftClone/Scripts/build_block_materials.py").read())
```

Skripta je idempotentna — postojeći materijali se ne brišu nego im se graf očisti i
ponovno izgradi, pa reference iz koda prežive. Pokreni je ponovno svaki put kad dodaš
ili promijeniš teksturu.

Popis blokova skripta čita iz `Content/Data/Blocks.json` (jedini izvor istine —
isti file koji čita `UBlockRegistry`): polje `material` određuje putanju MI asseta,
a opcionalno polje `masked` (default `false`) bira masked master s alpha-cutoutom
(lišće). Novi blok dakle NE zahtijeva diranje skripte — dovoljan je JSON unos i
tri PNG-a.

Točan popis PNG datoteka koje treba nacrtati je u `TEXTURE_SPEC.md`.

### Što skripta postavlja na teksturama

| Postavka | Vrijednost |
|---|---|
| Filter | `Nearest` |
| Compression Settings | `UserInterface2D (RGBA)` |
| sRGB | uključen |
| Mip Gen Settings | `FromTextureGroup`, LOD Group `World` |

`Filter = Nearest` je najvažniji — bez njega 16x16 tekstura izgleda kao mutna mrlja
umjesto oštrih piksela.

`UserInterface2D` se koristi jer DXT/BC kompresija radi u blokovima 4x4 piksela — na
16x16 teksturi to vidljivo uništava sliku.

Mipovi su **uključeni**, za razliku od originalnog prijedloga. Bez njih blokovi u
daljini titraju kroz TSR. Prebaci `GENERATE_MIPS = False` na vrhu skripte za čisti
Minecraft izgled.

Ako ikad dodaš Normal ili Roughness mape: **sRGB mora biti isključen** za njih, jer to
nisu boje nego podaci.

### Graf koji skripta gradi

```
VertexNormalWS ─▶ Transform(World→Instance) ─▶ Mask(B) ─┬─▶ Multiply(10)  ─▶ Saturate ─▶ TopMask
                                                        └─▶ Multiply(-10) ─▶ Saturate ─▶ BottomMask

Lerp( A=Side, B=Top,    Alpha=TopMask )    ─┐
                                            ├─▶ Lerp( A=•, B=Bottom, Alpha=BottomMask ) ─▶ Base Color
```

`Mask(B)` daje **+1** na gornjoj strani, **−1** na donjoj, **0** na bokovima. Množenje
s ±10 i `Saturate` to pretvore u oštru 0/1 masku. Materijal sam bira sliku po smjeru
strane, bez ikakvih promjena na meshu.

**Zašto Instance, a ne World ili Local.** World space bi značio da maska bira teksturu
po smjeru u svijetu — rotacija instance ne bi imala nikakav vizualni učinak. `Local` je
kod ISM-a prostor **komponente**, dakle cijelog `AVoxelWorld`, pa bi ispalo isto.
Jedino `Instance` prati transformaciju pojedine ISM instance. Za trenutne, nerotirane
blokove sve tri opcije izgledaju identično — Instance je odabran da rotacija blokova
kasnije ne traži prepravku materijala. Vidi odjeljak *Rotacija blokova* na dnu.

### Dva master materijala

- `M_VoxelBlock` — Opaque, za sve obične blokove
- `M_VoxelBlock_Masked` — Masked + two-sided, `Side` alpha ide u Opacity Mask
  (cutoff 0.333). Koristi ga samo lišće.

Bilo koja izmjena shadera kasnije se radi u master materijalu i automatski se propagira
na sve `MI_*` instance.

### Pravilo imenovanja tekstura

Skripta traži točno `T_<Blok>_Top`, `T_<Blok>_Side` i `T_<Blok>_Bottom`. **Nema
fallbacka i nema posuđivanja tekstura između blokova.**

Ime datoteke jednoznačno određuje kojem bloku i kojoj strani pripada, pa se iz popisa
datoteka vidi cijela istina o izgledu bloka. Blok koji je sa svih strana isti svejedno
treba sve tri — izvezi isti crtež tri puta.

Ako bloku nedostaje ijedna od tri, skripta ga preskoči cijelog i ispiše koja fali.
Blok ostaje siv. Polovično postavljen `MI_` bi tiho pokazivao placeholder teksturu
na strani koja nedostaje, što je gore od očito sivog bloka.

## Korak 5 — Uključi ih u igru

U `Source/MinecraftClone/Voxel/BlockRegistry.cpp` (linije ~54-60 i ~219-225) zamijeni
putanje materijala:

```cpp
const FString GrassMaterial = TEXT("/Game/Blocks/Materials/MI_Grass.MI_Grass");
```

Nema drugih promjena u kodu — isti mesh (`/Engine/BasicShapes/Cube.Cube`), isti ISM
rendering, ista logika blokova.

---

## Rotacija blokova

**Trenutno ne radi.** Materijal je za nju pripremljen, kod nije.

Ono što nedostaje, redom:

1. `BlockData` (`VoxelWorld.h:169`) je `TMap<FIntVector, EBlockType>` — drži samo tip.
   Vrijednost mora postati struct s tipom i rotacijom.
2. `PlaceBlockAt(FIntVector, EBlockType)` (`VoxelWorld.h:158`) nema parametar rotacije.
3. `AddBlockInstance` (`VoxelWorld.cpp:263`) i `AddBlockInstancesBatch` grade
   `FTransform(GridToWorld(...))` — identitet, bez rotacije.
4. `PromoteToActor` mora primijeniti istu rotaciju na `ABlock` actor, inače fokusirani
   blok vizualno odskoči od instance.
5. `FBlockDefinition` treba `EBlockRotationMode { None, Axis, HorizontalFacing }`.
6. `AFirstPersonCharacter` mora izračunati rotaciju iz normale pogođene strane (logovi)
   ili iz yaw-a igrača (crafting table, furnace).

Materijalna strana je riješena: `Transform(World→Instance)` znači da maska prati lokalne
osi svake pojedine instance, pa rotiranje instance rotira i raspored tekstura.

Za blokove s **prednjom** stranom (furnace, crafting table) treba proširiti master sa
tri na šest teksturnih parametara — u instance spaceu maska može razlikovati svih šest
smjerova, u world spaceu ne može.

Alternativa rotiranju mesha je `PerInstanceCustomData`: rotacija se pošalje kao broj po
instanci, a shader samo premapira koju teksturu na koju stranu. Mesh ostaje neokrenut,
čime se zaobilazi problem UV orijentacije opisan niže.

## Zašto ne drugačije

**Zašto ne 6 material slotova na kocki?** Radi, ali daje 6 draw callova po bloku i ruši
ISM optimizaciju. Neupotrebljivo za voxel svijet s desecima tisuća blokova. Forumski
savjeti koji to preporučuju odnose se na pojedinačne dekoracije, ne na voxel teren.

**Zašto ne texture atlas (jedna slika s više pločica)?** Zahtijeva vlastiti cube mesh iz
Blendera s ručno posloženim UV-ovima, jer `/Engine/BasicShapes/Cube` mapira svaku stranu
na cijeli 0–1 UV raspon. Atlas ima smisla tek kad želiš spojiti sve blokove u jedan
materijal radi smanjenja draw callova, ili kad ti zatreba blok gdje se prednja strana
razlikuje od bočnih (crafting table, furnace) — normal maska to ne može razlikovati.

Ako se ikad krene tim putem, glavni problem je **bleeding** (rubni pikseli jedne pločice
cure u susjednu pri filtriranju). Rješenja: isključeni mipovi, 2–4 px padding oko svake
pločice, ili `Texture2DArray` u UE5 — svaka pločica je svoj sloj pa je bleeding fizički
nemoguć.

## Napomena o postojećim Megascans materijalima

Trenutni blokovi koriste Megascans surface materijale s **8K** teksturama
(`Content/Fab/Megascans/Surfaces/...`). To je ogroman potrošač VRAM-a i vizualno pogrešno:
ti su materijali dizajnirani za velike plohe, pa se na kocki od 100 uu vidi jedna velika
mrlja koja se očito ponavlja. Nakon prelaska na `MI_*` instance mogu se obrisati iz
projekta.

## Naming konvencija

```
T_Grass_Top_BC     BC  = Base Color
T_Grass_Side_N     N   = Normal
T_Grass_Side_ORM   ORM = Occlusion / Roughness / Metallic u R/G/B kanalima
```

Za pixel-art blokove Normal i ORM vjerojatno uopće ne trebaju — dovoljan je Base Color
uz konstantan Roughness ~0.9.

---

# Dodavanje novog bloka — trenutno stanje i prijedlog pojednostavljenja

## Kako to izgleda danas

Sustav je već data-driven — nema `switch`-eva po tipu bloka. `AVoxelWorld` sam kreira
ISM komponentu po tipu bloka iz registryja (`VoxelWorld.cpp:576`), a
`ItemTypeToBlockType` / `BlockTypeToItemType` idu preko `UBlockRegistry`.

Za jedan novi blok treba:

1. `BlockType.h` — nova vrijednost u `EBlockType`
2. `ItemType.h` — nova vrijednost u `EItemType`
3. `BlockRegistry.cpp` → `RegisterAllBlocks()` — novi blok koda (copy-paste postojećeg,
   npr. `// === STONE ===` na liniji 77)
4. `BlockRegistry.cpp` → `RegisterAllItems()` — isto za item verziju
5. Ručno napravljen `MI_*` materijal
6. Ručno dodan redak u `DT_ItemData` — **ime retka mora biti točno ime enum vrijednosti**

Opcionalno: simbol u `CraftingRecipeRegistry.cpp:319` ako se blok craftna, i pojava u
generiranju terena (`VoxelWorld.cpp:62`).

## Što je ovdje problem

- `BlockRegistry.cpp` ima ~300 linija copy-paste koda koji je čisti **podatak**, ne logika.
- Svaka promjena tvrdoće ili drop šanse traži rekompilaciju.
- `FItemDefinition::Icon` (`BlockDefinition.h:107`) **nikad se ne čita.** UI vuče ikone iz
  zasebnog `DT_ItemData` preko `FItemData::Icon` (`InventorySlotWidget.cpp:77`). Dva
  paralelna izvora istih podataka.

## Prijedlog A — definicije u JSON / DataTable

UE ima ugrađen mehanizam: DataTable importan iz JSON-a. Nema parsera, uređuje se u
editoru, JSON ostaje source file u gitu.

1. `FBlockDefinition` i `FItemDefinition` naslijede `FTableRowBase`
2. `Blocks.json` i `Items.json` → import kao `DT_Blocks`, `DT_Items`
3. `RegisterAllBlocks()` / `RegisterAllItems()` postanu petlja po tablici
   (~300 linija → ~40)
4. `DT_ItemData` se stopi u `DT_Items` (rješava duplikaciju gore)

Format retka:

```json
{ "Name": "Sand", "BlockType": "Sand", "DropItemType": "Sand",
  "PlaceableFromItem": "Sand", "TimeToDestroy": 0.8,
  "DisplayName": "Sand", "Material": "/Game/Blocks/Materials/MI_Sand.MI_Sand" }
```

## Prijedlog B — automatsko generiranje Material Instanci

Python skripta (traži *Python Editor Script Plugin*) prođe folder s teksturama, grupira
ih po imenu i generira MI iz `M_VoxelBlock`:

```
T_Sand_Top.png     ┐
T_Sand_Side.png    ├──▶  MI_Sand    (parent = M_VoxelBlock)
T_Sand_Bottom.png  ┘

T_Stone_Side.png   ──▶   MI_Stone   (Top i Bottom fallbackaju na Side)
```

Relevantni API:
- `unreal.AssetToolsHelpers.get_asset_tools().create_asset(...)` s
  `unreal.MaterialInstanceConstantFactoryNew()`
- `unreal.MaterialEditingLibrary.set_material_instance_parent(...)`
- `unreal.MaterialEditingLibrary.set_material_instance_texture_parameter_value(mi, "Top", tex)`
- `unreal.EditorAssetLibrary.save_asset(...)`

Ista skripta može regenerirati postojeće MI-jeve kad se tekstura promijeni.

## Tijek nakon A i B

1. Nacrtaj 1–3 PNG-a, ubaci u `Content/Blocks/Textures/`
2. Pokreni Python skriptu → `MI_Sand` nastane sam
3. Dodaj `Sand` u `EBlockType` i `EItemType`, rekompajliraj
4. Dodaj po jedan redak u `Blocks.json` i `Items.json`

## Zašto enum ostaje

Potpuno uklanjanje rekompilacije znači zamjenu `EBlockType` s `FName` ID-em (tako radi
pravi Minecraft). To dira `VoxelWorld`, `InventoryComponent`, crafting, UI i
`TreeGenerator`, a `BlockData` mapa naraste s 1 na 8 bajta po bloku. Dobitak je jedna
linija enuma — ne isplati se u ovoj fazi.

## Status

**Prijedlog B je implementiran** — `Scripts/build_block_materials.py` generira i master
materijale i sve `MI_*` instance iz mape s teksturama. Skripta traži `PythonScriptPlugin`,
koji je uključen u `MinecraftClone.uproject`.

**Prijedlog A nije implementiran** — definicije blokova i itema su i dalje ~300 linija
copy-paste koda u `BlockRegistry.cpp`. `FItemDefinition::Icon` se i dalje ne čita.
