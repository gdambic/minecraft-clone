# PLAN: Ubrzanje pokretanja igre (spawn svijeta)

## 1. Problem

Pokretanje igre s `BP_VoxelWorld` dimenzija 100×100 traje **~4 sekunde**.

Sa `SurfaceLevel = 3` generiraju se 4 puna sloja terena:

```
100 × 100 × 4 = 40.000 blokova (+ random Dirt + stabla)
```

Svaki blok je zaseban `AActor` (`ABlock`) s vlastitim `UStaticMeshComponent`-om.
`SpawnActor` u UE košta ~0.1 ms po aktoru (registracija komponenti, collision,
render proxy, BeginPlay...):

```
40.000 × ~0.1 ms ≈ 4 s  →  poklapa se s izmjerenim vremenom
```

## 2. Mjerenje

`AVoxelWorld::GenerateWorld()` sadrži `[PERF]` dijagnostiku koja u log ispisuje
trajanje svake faze i ukupno vrijeme:

| Faza | Što mjeri | Skalira s veličinom svijeta? |
|------|-----------|------------------------------|
| 1/4 Assets | Sinkroni load svih mesh/materijal asseta iz registry-a u cache (`BuildBlockAssetCache`) | NE (fiksni trošak) |
| 2/4 Teren | Spawn slojeva terena + random Dirt blokova | DA |
| 3/4 Stabla | `FTreeGenerator::GenerateRandomTrees` | DA (s brojem stabala) |
| 4/4 Mobovi | Spawn zombija i ovaca | DA (s brojem mobova) |

Primjer izlaza u Output Logu (filter: `[PERF]`):

```
[PERF] ================================================
[PERF] GenerateWorld  100x100, 4 slojeva (SurfaceLevel=3)
[PERF] 1/4 Assets          xxx.x ms       21 ucitano, 0 neuspjelo (10 definicija)
[PERF] 2/4 Teren          xxxx.x ms    40020 blokova   0.xxxx ms/blok
[PERF] 3/4 Stabla          xxx.x ms      xxx blokova   0.xxxx ms/blok
[PERF] 4/4 Mobovi           xx.x ms
[PERF] ------------------------------------------------
[PERF] UKUPNO             xxxx.x ms    xxxxx blokova
[PERF]   fiksni (assets)     xxx.x ms  (xx%)
[PERF]   skalirajuci (spawn) xxxx.x ms  (xx%)
[PERF] ================================================
```

## 3. Uzroci sporosti

1. **30.000 od 40.000 blokova je nevidljivo.** Donja 3 sloja kamena su potpuno
   zakopana — nikad se ne vide dok ih igrač ne otkopa, a plaćamo pun trošak
   spawna, collisiona i renderiranja za svaki.
2. **`SpawnBlock` po svakom bloku zove `UBlockRegistry::Get(this)`**
   (`VoxelWorld.cpp`) — lanac GetWorld → GameInstance → GetSubsystem,
   40.000 puta, iako se registry ne mijenja tijekom generiranja.
3. **`InitializeFromRegistry` radi 3× `TryLoad()` po bloku** (`Block.cpp`) —
   120.000 resolva soft putanja. Asseti su nakon prvog puta u memoriji, ali
   `TryLoad` i dalje svaki put radi lookup po stringu putanje.
4. **`SetCanEverAffectNavigation(true)` na svih 40.000 komponenti**
   (`Block.cpp`, konstruktor) — svaki blok se registrira u nav octree, što
   uzrokuje velik rebuild navigacije pri pokretanju, a mobovima navigacija
   treba samo na površini.
5. **Collision je `QueryAndPhysics`** iako blokovima fizika ne treba —
   `QueryOnly` je dovoljan za raycast i hodanje.

## 4. Prijedlozi (po omjeru dobitka i truda)

### 4.1. Ne spawnaj zakopane blokove — lazy spawn (~4× ubrzanje) ⭐ quick win

> ✅ IMPLEMENTIRANO — detalji u [PLAN_LazySpawn.md](PLAN_LazySpawn.md)

- Tipove blokova držati u laganoj mapi `TMap<FIntVector, EBlockType>`
  (podaci, ne actori).
- `ABlock` actor spawnati samo za blokove s barem jednom izloženom stranom —
  pri ravnom terenu to je samo gornji sloj: **10.000 umjesto 40.000 actora**.
- Kad igrač uništi blok, lazy-spawnati susjede iz mape.
- `GetBlock` / `SetBlockType` / raycast logika treba prilagodbu da prvo
  konzultira mapu podataka.
- Očekivano: 4 s → ~1 s, plus manje memorije i bolji FPS.

### 4.2. Mikro-optimizacije unutar postojećeg dizajna (jeftino, zbrojivo s 4.1)

- ✅ IMPLEMENTIRANO: U `GenerateWorld` dohvatiti registry i **jednom** unaprijed
  razriješiti mesh/materijale, pa u petlji prosljeđivati gotove `UStaticMesh*` /
  `UMaterialInterface*` pointere (bez `UBlockRegistry::Get` i `TryLoad`
  po bloku). — `BuildBlockAssetCache()` puni `BlockAssetCache` (mapa
  `EBlockType → FBlockAssets`), a `SpawnBlock`/`PlaceBlockAt` prosljeđuju
  gotove pointere u `ABlock::InitializeFromRegistry`.
- `SetCanEverAffectNavigation(false)` za sve osim površinskog sloja.
- Collision `QueryOnly` umjesto `QueryAndPhysics`.
- `SpawnParams.SpawnCollisionHandlingOverride = AlwaysSpawn` da se preskoče
  overlap testovi pri spawnu.

### 4.3. Instanced Static Mesh — pravo dugoročno rješenje (~10-50×)

> ✅ IMPLEMENTIRANO — detalji u [PLAN_InstancedStaticMesh.md](PLAN_InstancedStaticMesh.md)

- Svi blokovi istog tipa postaju instance jednog
  `UInstancedStaticMeshComponent`-a na `AVoxelWorld` (jedan po tipu bloka).
- Dodavanje 40.000 instanci traje desetke milisekundi; rendering pada s
  40.000 komponenti na ~8 draw poziva.
- Veći refactor jer `ABlock` nosi stanje (destrukcija, highlight, drop).
  Uhodan hibridni uzorak: **svijet je ISM, a pravi `ABlock` actor se spawna
  samo za blok koji igrač trenutno gleda/kopa** — instanca se sakrije, actor
  preuzme highlight i destrukciju, po završetku se vrati instanca ili
  spawna drop.
- Ovim bi i 300×300 svijet bio praktički trenutan.

### 4.4. Raspodjela spawna kroz više frameova (ako ostaje actor-pristup)

- Batch od npr. 2.000 blokova po frameu preko timera.
- Ukupno vrijeme ostaje isto, ali nema smrzavanja — igra se odmah pokrene,
  a teren "izraste" u pola sekunde.
- Kombinira se s 4.1.

## 5. Preporuka

1. **Odmah:** 4.1 + 4.2 (nekoliko sati posla, 4 s → ispod sekunde).
2. **Sljedeći veći korak:** 4.3 — rješava i FPS/memoriju, ne samo startup,
   i preduvjet je za veće svjetove.
