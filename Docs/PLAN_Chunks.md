# PLAN: Chunk sustav — beskonačan svijet

> ✅ IMPLEMENTIRANO 2026-09-27 (sve 4 faze). Headless soak test
> (`-VoxelSoakTest`): determinizam regeneracije 200/200 chunkova (uklj.
> negativne koordinate), retained-modified preživljava unload+reload, unload
> drži korak s loadom (~225-240 učitanih tijekom putovanja). Preostaje ručni
> vizualni test u editoru (highlight/kopanje na granici chunkova) i smanjenje
> NavMeshBoundsVolume u leveli (BACKLOG #1). Konzolna komanda za ručni test:
> `voxel.tp <X> <Y>`.
>
> Odluke iz razgovora: **beskonačan svijet** (generacija na zahtjev oko igrača),
> **ISM po chunku** (zadržava se hibrid instanca/promovirani actor),
> **perzistencija samo u memoriji** (restart = novi svijet iz seeda),
> **generacija na background threadu**, **despawn mobova izvan radijusa**,
> **dublji svijet** (visina 48, površina oko Z=24).

## 1. Kako chunking radi u Minecraftu (sažetak studije)

Izvor: [minecraft.wiki/w/Chunk](https://minecraft.wiki/w/Chunk) + poznata
arhitektura Java Editiona.

- **Chunk = stupac 16×16 blokova kroz cijelu visinu svijeta** (384 u modernom
  Java Editionu), interno podijeljen u sekcije 16×16×16 radi spremanja i
  renderiranja. Svijet je beskonačan — chunkovi se generiraju tek kad ih netko
  zatraži.
- **Generacija je deterministička iz seeda i ide u fazama** (noise → surface →
  carvers → features → light). Featuri poput stabala smiju prelaziti granicu
  chunka jer se generiraju tek kad susjedni chunkovi postoje u ranijoj fazi —
  zato stablo na granici izgleda isto bez obzira iz kojeg je smjera igrač došao.
- **Učitavanje vodi ticket sustav s razinama aktivnosti**: oko igrača se
  chunkovi učitavaju u prstenovima — najbliži pun tick (entiteti + blokovi),
  dalje samo block tick, pa "border" (učitan ali pasivan), pa neučitan. Render
  distance (što se vidi) i simulation distance (što se simulira) su odvojene
  postavke.
- **Spremanje**: region datoteke (Anvil, 32×32 chunka po datoteci), sekcije
  koriste paletu tipova + bit-pakirane indekse. Sprema se samo što je
  posjećeno; ostalo se regenerira iz seeda.
- **Mobovi su vezani uz udaljenost od igrača**: spawn samo unutar 128 blokova,
  hostile izvan 128 blokova se despawnaju.

**Što preuzimamo:** 16×16 chunkove pune visine, determinističku generaciju po
chunku, feature spillover za stabla, prsten podataka širi od prstena vizuala
(naš ekvivalent MC faza), despawn mobova po udaljenosti.

**Što svjesno NE preuzimamo (za sada):** region datoteke i palete (perzistencija
je memorija), gradaciju load levela (kod nas binarno: učitan/neučitan + data
ring), simulation distance (mobovi žive samo oko spawna), sekcije po visini
(naš svijet je visok 48 blokova — dvije-tri sekcije; dodaju se kasnije s
visokim svjetovima, vidi pravila spremnosti u 3.1).

## 2. Polazno stanje i što se mora promijeniti

Današnja arhitektura (nakon [PLAN_InstancedStaticMesh.md](PLAN_InstancedStaticMesh.md)):

- `BlockData` (`TMap<FIntVector, EBlockType>`) — globalni izvor istine.
- Jedan **globalni ISM po tipu bloka**; instanca postoji samo za izložene
  blokove; fokusirani blok se promovira u `ABlock` actor.
- `GenerateWorld()` u `BeginPlay` sinkrono generira **cijeli** fiksni svijet
  (WorldSizeX×WorldSizeY), pa stabla (globalni RNG), pa mobove.

Dobre vijesti za beskonačan svijet (provjereno u kodu):

- `FTerrainGenerator::GetColumnHeight` i `FBiomeGenerator::GetBiomeIndexAt` su
  **čiste funkcije od (X,Y) i seeda** — rade za bilo koje, i negativne,
  koordinate. `FMath::PerlinNoise2D` je deterministički (fiksna permutacijska
  tablica).
- Navigacija je već **invoker-only** (`ACreatureBase` ima invoker) — navmesh se
  ionako gradi samo oko mobova, ne oko cijelog svijeta.
- Sva mutacija svijeta već ide kroz `AVoxelWorld` API.

Što se lomi i mora se redizajnirati:

- Globalni `BlockData` TMap (s površinom na Z≈24 to je ~250k unosa za 100×100 —
  po-chunk flat array je red veličine štedljiviji i brži).
- Globalni ISM setovi (unload chunka mora moći ukloniti SAMO njegove instance).
- `FMath::RandInit(WorldSeed)` + globalni RNG za stabla/mobove — redoslijed
  generacije chunkova ovisi o kretanju igrača, pa determinizam smije ovisiti
  samo o (seed, chunk koordinata).
- `WorldSizeX/Y` granice (petlje generacije, `PlaceShowcaseBlocks`, spawn
  mobova, `TreeGenerator`).

## 3. Ciljna arhitektura

### 3.1. Chunk: koordinate i storage

```cpp
// Konstante
static constexpr int32 ChunkSize   = 16;  // XY stranica chunka u blokovima
static constexpr int32 WorldHeight = 48;  // Z stupaca po chunku (dno Z=0)

// Grid -> chunk koordinata. OBAVEZNO floor-division (negativne koordinate!):
// (-1 >> 4) == -1 (ispravno), ali (-1 / 16) == 0 (bug). Koristiti bit shift
// ili FMath::FloorToInt(float(X) / ChunkSize) — nikad integer '/'.
FIntPoint GridToChunk(int32 X, int32 Y);   // (X >> 4, Y >> 4)
FIntVector GridToLocal(FIntVector Pos);    // (X & 15, Y & 15, Z)

enum class EChunkState : uint8 { Queued, Generating, DataReady, Applied };

struct FVoxelChunk
{
    FIntPoint Coord;
    EChunkState State = EChunkState::Queued;

    // Izvor istine za ovaj chunk: flat array 16*16*48 = 12288 B.
    // Indeks = (Z * ChunkSize + LocalY) * ChunkSize + LocalX. Air = 0.
    TArray<EBlockType> Blocks;

    // Vizual: ISM setovi SAMO za tipove prisutne u chunku.
    // FBlockInstanceSet se ne mijenja (Component + GridToInstance +
    // InstanceToGrid, swap-remove pravila iz PLAN_InstancedStaticMesh 5.1).
    TMap<EBlockType, FBlockInstanceSet> InstanceSets;

    bool bModified = false;  // igrač je nešto kopao/gradio u ovom chunku
};
```

Na `AVoxelWorld`:

```cpp
TMap<FIntPoint, TUniquePtr<FVoxelChunk>> Chunks;

// "Samo memorija" perzistencija: unload IZMIJENJENOG chunka zadrži mu Blocks
// array ovdje (12 KB po chunku); reload ga vrati umjesto regeneracije.
// Neizmijenjeni chunkovi se bacaju u cijelosti — regeneriraju se iz seeda.
TMap<FIntPoint, TArray<EBlockType>> RetainedModifiedChunks;

// ResolveHitToGrid treba naći set iz pogođene komponente:
TMap<TObjectPtr<UInstancedStaticMeshComponent>, TPair<FIntPoint, EBlockType>> ComponentToChunk;
```

**Javni API se NE mijenja** (`GetBlockTypeAt`, `SetBlockType`, `PlaceBlockAt`,
`DestroyBlockAt`, `PromoteToActor`/`DemoteToInstance`, `ResolveHitToGrid`,
`GridToWorld`) — implementacije se preusmjere kroz chunk lookup. Pozicija u
neučitanom chunku vraća `Air`, a mutacije na neučitanom chunku su no-op s
Warning logom (igrač ionako doseže samo 5 blokova).

Properties: `WorldSizeX/Y` se **brišu**. Novi defaulti: `SurfaceLevel = 24`,
`HeightAmplitude = 6` (površina 18–30, dno Z=0 čvrsto).

**Pravilo dimenzioniranja visine svijeta** — `WorldHeight` je zbroj tri
komponente, ne jedna brojka (početna verzija plana je gledala samo prvu i
strop 32 bi odrezao stabla na brdima):

1. dubina ispod najniže površine, za kopanje: 18 slojeva (Z=0–17);
2. njihaj terena, `2 × HeightAmplitude`: 12 (površina Z=18–30);
3. zrak iznad najvišeg vrha, za stabla (~7 blokova) i gradnju: 17 (Z=31–47).

Ukupno 48. Zrak ne košta ništa u renderingu (instance dobivaju samo izloženi
čvrsti blokovi); trošak je samo veličina arraya po chunku (8 KB → 12 KB).

**Pravila spremnosti za subchunkove** (visoki svjetovi kasnije — sekcije
16×16×16 unutar stupca postaju interna promjena `FVoxelChunk`, streaming
ostaje 2D po stupcima):

1. Formulu indeksiranja `Blocks` arraya zna SAMO `FVoxelChunk` — sav vanjski
   pristup ide kroz helpere (`GetBlockTypeAt` / interni `BlockAt(Local)`).
2. `WorldHeight` je jedna konstanta — nikad hardkodirana brojka po kodu.

Novi properties:
`VisualRadius = 6`, plus izvedeni `DataRadius = VisualRadius + 1` i
`UnloadRadius = VisualRadius + 2`.

### 3.2. Dva prstena: podaci šire od vizuala

Ekvivalent Minecraftovih generacijskih faza, sveden na dvije:

- **Data ring** (`DataRadius`): chunk ima `Blocks` array, ali ne i instance.
- **Visual ring** (`VisualRadius`): chunk uz podatke ima i ISM instance.

Zašto: `IsBlockExposed` na rubu chunka pita susjedni blok. Budući da vizualni
chunk uvijek ima susjede s **pravim podacima** (uključujući stabla koja vire iz
susjednog chunka), exposure na granicama je točan bez ikakvih posebnih
slučajeva. Kad chunk uđe u visual ring, samo mu se izgrade instance iz već
postojećih podataka (jeftin prolaz kroz 12288 ćelija).

### 3.3. Streaming petlja (game thread)

Timer na `AVoxelWorld` (npr. svakih 0,25 s) ili provjera pri prelasku igrača u
novi chunk:

1. Izračunaj igračev chunk `(PCX, PCY)`.
2. **Unload**: za svaki učitani chunk s Chebyshev udaljenošću > `UnloadRadius`:
   despawnaj mobove čiji je root u tom chunku, uništi ISM komponente
   (+ `ComponentToChunk` unose), demote-aj promovirani actor ako je u njemu,
   očisti njegove unose iz `LeavesToCheck`; ako je `bModified`, premjesti
   `Blocks` u `RetainedModifiedChunks`; obriši chunk.
3. **Load želja**: svi chunkovi unutar `DataRadius` koji ne postoje → u queue,
   sortiran po udaljenosti od igrača (najbliži prvi).
4. **Apply budžet**: max N (default 2) `DataReady` chunkova po ticku petlje
   dobije instance ako je unutar `VisualRadius` (vidi 3.4).

Histereza `UnloadRadius > DataRadius` sprječava load/unload treperenje kad
igrač šeta po granici chunka.

### 3.4. Async generacija (background thread)

Podjela posla po pravilu "worker ne dira UObjecte":

```
WORKER (UE::Tasks::Launch):
  GenerateChunkData(WorldSeed, ChunkCoord, TerrainParams) -> TArray<EBlockType>
    - teren: FTerrainGenerator po stupcu (čista funkcija)
    - stabla: deterministički spillover (vidi 3.5)
    - čisti C++ podaci, nula UObject poziva

GAME THREAD (apply, budžetirano):
  ApplyChunk(Chunk)
    - RetainedModifiedChunks.Contains(Coord) ? vrati sačuvane podatke
      (POBJEĐUJU regeneraciju) : preuzmi worker rezultat
    - ako je chunk u visual ringu: prolaz kroz Blocks, IsBlockExposed,
      batch AddInstances po tipu (postojeći AddBlockInstancesBatch put,
      uključujući UpdateBounds + FNavigationSystem::UpdateComponentData)
```

Rubni slučajevi:

- **Zastarjeli rezultat**: igrač pobjegao dok je worker radio → ako chunk više
  nije u želji (ili je u međuvremenu unloadan pa opet zatražen), rezultat se
  baca. Svaki zahtjev nosi redni broj generacije; apply prihvaća samo
  najnoviji.
- **Edit za vrijeme generacije**: mutacije rade samo na `Applied` chunku —
  do tada za igrača chunk ne postoji (nema instanci → nema hita → nema edita).
- **Initial spawn**: u `BeginPlay` se 3×3 chunka oko igračevog starta generira
  i primijeni **sinkrono** (igrač ne smije propasti kroz svijet); ostatak ide
  async. Jednokratni štucaj pri startu je prihvaćen.

### 3.5. Determinizam: per-chunk RNG i stabla preko granica

- Briše se `FMath::RandInit(WorldSeed)` kao mehanizam determinizma. Svaki chunk
  dobiva vlastiti stream:
  `FRandomStream(HashCombine(HashCombine(GetTypeHash(WorldSeed), GetTypeHash(CX)), GetTypeHash(CY)))`.
- **Stabla** (zamjena za globalni `TreeCount`): svaki chunk iz svog streama
  deterministički izvuče 0–N kandidata (pozicija + vrsta iz postojećeg
  `FTreeGenerator` repertoara). Pri generaciji chunka C worker izračuna
  kandidate za **C i svih 8 susjeda** (njihovi streamovi su dostupni — samo
  seed i koordinata!) i u C upiše **samo blokove koji padaju unutar C**.
  Rezultat: stablo na granici je identično bez obzira koji se chunk generirao
  prvi — isti trik kojim MC rješava cross-chunk feature, bez sustava faza.
  Krošnja je ≤ 2 bloka radijusa pa je 3×3 susjedstvo dovoljno.
- **Showcase red** (`PlaceShowcaseBlocks`): veže se na fiksne apsolutne
  koordinate unutar chunka (0,0) i generira kao dio podataka tog chunka
  (worker), ne kao poseban globalni korak.
- **Biomi**: ništa se ne mijenja — već su čista funkcija od (X,Y).

### 3.6. Mobovi

- `SpawnEnemies()`/`SpawnMobs()` gube `WorldSizeX/Y`: spawn pozicije se biraju
  unutar početnog sinkronog 3×3 područja oko igračevog starta (visina iz
  `GetTerrainHeightAt` — radi bez učitanog chunka).
- **Despawn**: unload korak (3.3/2) uništi moba čiji je chunk izvan
  `UnloadRadius` — MC-ov princip "izvan 128 blokova despawn", izveden preko
  chunk radijusa. Bez toga bi mob propao kroz svijet kad mu tlo nestane.
- Per-chunk spawn novih mobova pri istraživanju → **BACKLOG** (posebna cjelina:
  spawn pravila, cap po području, despawn/respawn ciklus).

### 3.7. NavMesh

- Invoker-only generacija već odgovara beskonačnom svijetu (gradi se samo oko
  mobova, a mobovi žive oko spawna).
- **NavMeshBoundsVolume mora pratiti igrača**: periodički (ista streaming
  petlja) `SetActorLocation` volumena na centar igračevog chunka. Volumen se u
  leveli smanji na ~`2 * UnloadRadius * ChunkSize * BlockSize` po stranici —
  time se rješava i BACKLOG #1 (divovski volumen), pa `TileSizeUU=3000`
  override u `DefaultEngine.ini` može natrag na default.
- Per-chunk ISM komponente navigaciji **pomažu**: nav octree element po chunku
  ima male bounds, a postojeći `UpdateComponentData` put iz
  `AddBlockInstancesBatch` pokriva i load i unload.

### 3.8. Leaf decay preko granica

- BFS (`HasLogConnection`) ide kroz `GetBlockTypeAt` pa radi preko granica
  chunkova dok su podaci učitani. Ako BFS dotakne poziciju u **neučitanom**
  chunku, provjera se prekida i list se NE ruši (konzervativno — decay je
  kozmetika, bolje lažno "ima vezu" nego rupa u krošnji).
- Unload chunka čisti njegove pozicije iz `LeavesToCheck`.

## 4. Što se mijenja, što ne

| Datoteka | Opseg |
|----------|-------|
| `VoxelWorld.h/.cpp` | glavnina: chunk mapa, streaming petlja, async, preusmjeravanje API-ja |
| `TerrainGenerator` | bez izmjena (već čista funkcija) |
| `TreeGenerator.h/.cpp` | refactor: deterministički kandidati po chunku umjesto globalnog broja |
| `FirstPersonCharacter.cpp` | ništa strukturno (`ResolveHitToGrid`/promote API ostaje) |
| `Block.cpp/.h`, `ItemDrop` | bez izmjena |
| level (`Lvl_ThirdPerson`) | NavMeshBoundsVolume smanjiti; `DefaultEngine.ini` TileSizeUU natrag |
| `EnemyAIController`, BT taskovi | bez izmjena (navmesh i dalje postoji oko mobova) |

Ne mijenja se: hibrid instanca/promovirani `ABlock`, inventar, crafting,
registry, biomi, block/item JSON-ovi, leaf decay algoritam (samo granični
uvjet), `[PERF]` filozofija mjerenja.

## 5. Faze implementacije

Svaka faza završava buildom koji se može pokrenuti i ručno testirati.

### Faza 1: Chunk storage refactor (svijet i dalje fiksan)
- `FVoxelChunk`, `GridToChunk`/`GridToLocal` (floor-division!), per-chunk
  `Blocks` array i per-chunk `InstanceSets`; `ComponentToChunk` mapa.
- `GenerateWorld()` i dalje sinkrono generira fiksno područje (npr. chunkove
  (0,0)–(6,6)), ali kroz novi per-chunk put; globalni `BlockData` i globalni
  ISM setovi se brišu.
- Stabla prelaze na deterministički per-chunk model (3.5) već sada.
- Dubina: `SurfaceLevel=24`, `WorldHeight=48`.
- **Test**: igra izgleda i ponaša se kao danas (kopanje, gradnja, highlight,
  drop, mobovi, leaf decay); `[PERF]` log po chunku; stablo na granici
  chunkova cijelo.

### Faza 2: Streaming na game threadu (svijet postaje beskonačan)
- Streaming petlja (3.3): load/unload po radijusima, queue po udaljenosti,
  histereza; generacija chunka zasad **sinkrona u applyu** (štucanje je
  privremeno prihvatljivo — mjeri se za usporedbu s fazom 3).
- Data ring vs visual ring; `RetainedModifiedChunks`; mob despawn;
  NavMeshBoundsVolume prati igrača; spawn igrača/mobova bez `WorldSize`.
- **Test**: hodanje u bilo kojem smjeru generira teren (i u negativnim
  koordinatama!); odlazak i povratak: neizmijenjen teren identičan (seed),
  izmjene sačuvane; mobovi nestaju kad se ode daleko.

### Faza 3: Async generacija
- `GenerateChunkData` na worker thread (UE::Tasks), apply budžet po frameu,
  redni brojevi generacije za bacanje zastarjelih rezultata, sinkroni 3×3
  bootstrap na spawnu.
- **Test**: `stat unit` bez skokova pri hodanju; headless run s brzim
  teleportima — determinizam neovisno o redoslijedu generacije (isti seed +
  ista pozicija = isti teren, provjera preko block-count markera u logu).

### Faza 4: Rubovi i čišćenje
- Leaf decay granični uvjet (3.8) + čišćenje `LeavesToCheck` pri unloadu.
- `DefaultEngine.ini` TileSizeUU natrag na default; BACKLOG #1 zatvoriti.
- BACKLOG unos: per-chunk mob spawn.
- Headless test checklist i `[PERF]` markeri finalizirani.

## 6. Rizici i rubni slučajevi

1. **Floor-division za negativne koordinate** — klasičan izvor "zrcaljenog"
   terena oko origina. `X >> 4` (aritmetički shift) ili `FMath::FloorToInt`;
   NIKAD integer `/`. Provjeriti i `GridToLocal` (`X & 15`, ne `X % 16`).
2. **Broj ISM komponenti**: visual ring r=6 → 13² = 169 chunkova × ~5 prisutnih
   tipova ≈ 850 komponenti. UE to podnosi, ali draw pozivi rastu s radijusom —
   komponente kreirati samo za tipove prisutne u chunku, radius držati
   konzervativno. Dugoročna opcija (izvan opsega): greedy mesh po chunku.
3. **Swap-remove bookkeeping** (PLAN_InstancedStaticMesh 5.1) sada vrijedi po
   chunk-komponenti — pravilo "indeks nikad ne čuvati izvan seta" ostaje.
4. **Promovirani actor na granici unloada** — unload chunka s fokusiranim
   blokom mora prvo demote-ati (ili uništiti actor bez dropa); inače visi
   actor bez podataka.
5. **Float preciznost noisea**: postojeći `NoiseOffset ±100000` + udaljenost
   ~10⁶ blokova → sample koordinate gube preciznost (MC-ov "Far Lands" efekt).
   Prihvaćeno — realan doseg igrača je redovi veličine manji. UE5 LWC (double
   world koordinate) pokriva pozicije actora.
6. **Zastarjeli worker rezultati** — bez rednog broja generacije, brzi
   odlazak-povratak može primijeniti svježe regenerirani chunk PREKO
   retained-modified podataka. Redoslijed provjere u applyu: retained > worker.
7. **`GetTerrainHeightAt` po workeru**: čita samo immutable parametre
   (offseti, scale) postavljene prije prvog taska — bez lockova, ali offsete
   postaviti u `BeginPlay` PRIJE ijednog worker taska.

## 7. Validacija

- **Headless** (CLAUDE.md "Headless Test Run"): `[PERF] Chunk (x,y) gen=Xms
  apply=Yms inst=N` markeri; skripta koja teleportira igrača i broji
  generirane/unloadane chunkove; usporedba block-count hasheva istog chunka
  kroz dva rana (determinizam).
- **Ručno u editoru**: kopanje/gradnja točno na granici dva chunka (sve 4
  strane + kut), highlight preko granice, stablo raspolovljeno granicom,
  odlazak 20+ chunkova i povratak na izmijenjeno mjesto, mob despawn,
  `stat scenerendering` (draw pozivi) i `stat unit` (štucanje) pri hodanju.
