# PLAN: Optimizacija performansi

**Zadnje ažuriranje:** 2026-07-27
**Engine:** Unreal Engine 5.6 (`C:\Program Files\Epic Games\UE_5.6`)
**Verifikacija:** 2026-07-27 — sve reference na kod projekta i engine source ponovno
provjerene čitanjem datoteka. Nađene i ispravljene u ovoj verziji:
- ❌ stari Korak 5 (zastavica `bInitializedFromData`) — nemoguć, `BeginPlay` se izvršava
  PRIJE inicijalizacije (vidi 2.5, Korak 5b)
- ❌ staro pravilo `IsExposed` ("cijeli rub = bedrock") — proturječilo vlastitoj tablici
  i ostavljalo šuplje bočne stijenke (vidi 4.4)
- ⚠️ praznina: sekcija 4 nije pokrivala tok uništenja bloka (uništeni blokovi danas
  ostaju kao nevidljivi Air aktori i svijet o tome ne zna) — dopunjeno u 4.5
- ✅ nova potvrda: `SetMaterial` NEMA mobility ograničenje → `Mobility = Static` ne
  razbija highlight swap (vidi 3.4, Fix 2)

---

# 0. STATUS — pročitaj ovo prvo

## 0.1 Postoje DVA odvojena problema

Ne miješati ih — imaju različite uzroke, mjere se različitim alatima i imaju različite fixeve.

| | **Problem A: Startup** | **Problem B: Per-frame** |
|---|---|---|
| Simptom | 4.1 s zamrznuto pri pokretanju | render thread je usko grlo |
| Uzrok | 40 000 × `SpawnActor` na game threadu | 40 000 primitiva u `FScene` |
| Mjeri se | `[PERF]` log iz `GenerateWorld()` | `stat scenerendering` |
| Status | **izmjereno, dijagnoza gotova** | **izmjereno, dijagnoza gotova** |
| Sekcija | 2 | 3 |

## 0.2 Što je napravljeno u kodu

| Datoteka | Promjena | Status |
|---|---|---|
| `VoxelWorld.h` | deklaracija `MeasureAssetLoadCost()` | ✅ kompajlirano, radi |
| `VoxelWorld.cpp` | mjerenje faza u `GenerateWorld()` + `MeasureAssetLoadCost()` | ✅ kompajlirano, radi |

**Ništa drugo nije mijenjano.** Nijedna optimizacija još nije primijenjena — samo mjerenje.
Promjene nisu commitane.

## 0.3 Sljedeći korak

Problem A, Korak 1: **postaviti mesh na CDO** (sekcija 2.4). Detaljan plan u 2.5.

## 0.4 ⚠️ Hipoteze koje su OBORENE — ne vraćati se na njih

Tijekom analize je nekoliko pretpostavki opovrgnuto mjerenjem ili čitanjem engine sourcea.
Popisane su ovdje da ih se ne re-litigira:

| Oborena tvrdnja | Čime je oborena | Ispravno |
|---|---|---|
| "Sinkroni load Megascans materijala je velik fiksni trošak" | mjerenje: max 116 ms, 1% na 100×100 | assets nisu problem |
| "`SpawnActorDeferred` izbjegava registraciju komponenti" | `Actor.cpp:4291` vs `4313` | ne izbjegava; native klasa s native rootom registrira odmah |
| "Trošak je super-linearan (TMap rehash / navmesh)" | mjerenje: `ms/blok` konstantan | strogo linearan |
| "S6 zahtijeva prepravku raycasta" | raycast koristi `HitResult.GetActor()` | raycast radi nepromijenjeno |
| "`stat initviews` je najbolja naredba" | grupa se registrira lijeno, konzola je često odbije | koristiti `stat scenerendering` |

## 0.5 ⚠️ Otvorene hipoteze — NISU potvrđene

| Hipoteza | Kako potvrditi |
|---|---|
| `Mobility = Movable` uzrokuje 3 264 dynamic RT updatea po frameu | primijeni `Mobility = Static`, usporedi brojač |
| Koliko od 0.10 ms/blok otpada na sam `SpawnActor` | izmjeri nakon Koraka 1 |

---

# 1. Kontekst — što kod radi

`VoxelWorld.cpp` generira teren kao **zasebni aktor po bloku** (`SpawnBlock`).

| Parametar | C++ default | Stvarno u BP |
|---|---|---|
| WorldSizeX × WorldSizeY | 100 × 100 | **50 × 50** ✅ potvrđeno |
| Slojevi (Z = 0..SurfaceLevel) | 4 | 4 |
| Blokova | 40 000 | **10 649** |

✅ Potvrda da BP koristi 50×50: mjerenje javlja 10 649 blokova, a `stat scenerendering`
javlja 10 220 ray tracing instanci. `BP_VoxelWorld` override-a C++ default.

Svaki blok je `AActor` s `UStaticMeshComponent` (`Block.cpp:15-16`), s uključenom
navigacijom (`Block.cpp:19`).

---

# 2. PROBLEM A: Startup delay

## 2.1 Izmjereni baseline

Instrumentacija u `GenerateWorld()`, uz restart editora između mjerenja:

| Svijet | Blokova | Assets | Spawn | **ms/blok** | Mobovi | UKUPNO |
|---|---|---|---|---|---|---|
| 25×25 | 3 113 | 115.6 ms | 356.6 ms | **0.115** | 7.2 ms | 472.2 ms |
| 50×50 | 10 649 | 35.9 ms | 970.5 ms | **0.091** | 8.0 ms | 1 006.4 ms |
| 100×100 | 40 662 | 33.6 ms | 4 079.8 ms | **0.100** | 7.3 ms | 4 113.4 ms |

## 2.2 Zaključci iz podataka

**Skaliranje je linearno.** 50×50 → 100×100: blokova ×3.82, vremena ×4.20. Omjer 1.10 —
unutar šuma. `ms/blok` nema trenda (0.115 → 0.091 → 0.100).

**Assets nisu problem.** Max 116 ms, na najvećem svijetu 1% ukupnog. Skok 115.6 → 35.9 nije
nedosljednost — prvo mjerenje čita s diska, ostala iz OS file cachea.

**Mobovi nisu problem.** 7-8 ms konstantno, neovisno o veličini. Nema navmesh blow-upa.

**Dijagnoza: ~0.10 ms po bloku, linearno. Ništa drugo.**

0.10 ms = 100 µs po bloku. Sam `SpawnActor` za jednostavan aktor tipično košta 10-30 µs.
Ostatak je režija popisana u 2.3.

## 2.3 Što se izvršava po svakom bloku

| Operacija | Puta na 40k | Gdje |
|---|---|---|
| `UBlockRegistry::Get(this)` | **80 000** | `VoxelWorld.cpp` u `SpawnBlock` + `Block.cpp:180` — svaki put `GetWorld()` → `GetGameInstance()` → `GetSubsystem()` |
| `GetBlockDefinition(Type)` | 80 000 | ista dva mjesta |
| `SpawnActor<ABlock>` | 40 000 | UObject alokacija + registracija aktora + registracija komponente |
| `FSoftObjectPath::TryLoad()` | **120 000** | `Block.cpp:202,212,224` — mesh, materijal, highlight |
| `SetStaticMesh` na registriranoj komponenti | 40 000 | `Block.cpp:205` — vidi 2.3.1 |
| `SetMaterial` | 40 000 | `Block.cpp:215` |
| `UpdateVisibility()` | **80 000** | prvo iz `BeginPlay` (s još-default `BlockType = Air` → sakrije mesh i ugasi koliziju!), pa iz `InitializeFromRegistry` (vrati oboje) — dvostruki render/physics churn |
| `Blocks.Add` bez `Reserve` | 40 000 | ~17 rehashiranja cijele mape |

### 2.3.1 Zašto je `SetStaticMesh` po bloku skup

`StaticMeshComponent.cpp:2287-2331` — kad je komponenta **već registrirana**, `SetStaticMesh`
pokreće lanac:

| Linija | Operacija |
|---|---|
| 2300 | `PrecachePSOs()` |
| 2304-2314 | `MarkRenderStateDirty()` / `RecreateRenderState_Concurrent()` |
| **2317** | **`RecreatePhysicsState()`** |
| 2323 | `IStreamingManager::NotifyPrimitiveUpdated()` |
| 2326 | `UpdateBounds()` |
| **2328** | **`FNavigationSystem::UpdateComponentData()`** |
| 2331 | `MarkCachedMaterialParameterNameIndicesDirty()` |

Fizikalno tijelo se stvori pri registraciji komponente, pa odmah uništi i ponovno stvori.
Navigacija se ažurira po bloku. × 40 000.

**Ovo je najvjerojatnije najveći pojedinačni komad onih 0.10 ms.**

### 2.3.2 Ključno otkriće: svi blokovi dijele isti mesh

`BlockRegistry.cpp:50` definira `/Engine/BasicShapes/Cube.Cube`, i **svaka** definicija ga
koristi (`Def.Mesh = FSoftObjectPath(MeshPath)` na linijama 71, 86, 101, 116, 131, 146, 161,
176, 191, 206). Razlikuje se **samo materijal**.

Mesh se dakle može postaviti **jednom na CDO**, u konstruktoru — dok komponenta još nije
registrirana, pa nema lanca iz 2.3.1.

Postojeći poziv u `Block.cpp:205` tada postaje no-op zbog early-outa
(`StaticMeshComponent.cpp:2265-2271`):

```cpp
bool UStaticMeshComponent::SetStaticMesh(UStaticMesh* NewMesh)
{
	// Do nothing if we are already using the supplied static mesh
	if(NewMesh == GetStaticMesh())
	{
		return false;
	}
```

### 2.3.3 ⚠️ Mina: `Mobility = Static` sam po sebi razbija igru

`StaticMeshComponent.cpp:2273-2283`:

```cpp
	// Don't allow changing static meshes if "static" and registered
	if (World->HasBegunPlay() && !AreDynamicDataChangesAllowed() && Owner != nullptr)
	{
		FMessageLog("PIE").Warning(... "Calling SetStaticMesh on '{0}' but Mobility is Static.");
		return false;
	}
```

Blokovi se spawnaju iz `BeginPlay`, dakle `HasBegunPlay() == true`.

➡️ **Ako se `Mobility = Static` primijeni na TRENUTNI kod, `SetStaticMesh` vrati `false` i
blokovi ostanu bez mesha**, uz spam upozorenja u PIE logu.

➡️ **S meshom na CDO-u** early-out na 2268 se okine **prije** te provjere, pa oboje radi.
**Mobility = Static mora doći TEK NAKON promjene mesha na CDO.**

## 2.4 Rangirani fixevi za Problem A

| # | Fix | Trud | Očekivanje |
|---|---|---|---|
| **1** | **Mesh na CDO** (2.3.2) | minute | miče 7 pod-operacija po bloku |
| **2** | **Cache razriješenih asseta** | sat | miče 120k `TryLoad` + 80k subsystem lookupa |
| **3** | `Blocks.Reserve()` | 1 linija | miče ~17 rehashiranja |
| **4** | Ukloni dvostruki `UpdateVisibility` | minute | sitno, čisto |
| **5** | **Ne spawnaj zakopane blokove kao aktore** (sekcija 4) | dani | **3.6× manje spawnova** |
| **6** | Amortiziraj spawn kroz frameove | sat | kozmetika, skriva ostatak |
| ❌ | ~~Preload asseta~~ | — | **odbačeno** — assets su 1% |
| ❌ | ~~`SpawnActorDeferred`~~ | — | **odbačeno** — ne odgađa registraciju |
| ❌ | ~~`FNavigationLockContext`~~ | — | **odbačeno** — nema navmesh signature |

Realna procjena za 1+2+3+4: **0.100 → 0.04-0.06 ms/blok**, dakle 100×100 s 4.1 s na
~1.7-2.4 s. Uz fix 5: ~0.5 s.

⚠️ Procjena je gruba jer se ne zna koliko od 0.10 ms otpada na sam `SpawnActor`. To će
pokazati mjerenje nakon Koraka 1.

## 2.5 Detaljne upute za implementaciju (Koraci 1-6)

**Pravila za izvođača:**
- Koraci se rade TOČNO ovim redoslijedom. Nakon svakog koraka: build
  (Development Editor | Win64) → PIE → checkpoint. Ako checkpoint ne prolazi,
  stani i istraži — NE prelazi na sljedeći korak.
- **Ne dirati:** `InitializeFromRegistry` (koristi ga Blueprint kod),
  `BlockRegistry.cpp/h`, bilo koji `.uasset`.
- Prije početka: commitati trenutno stanje (mjerna instrumentacija u
  `VoxelWorld.h/cpp` još nije commitana) da se izmjene mogu diffati.

### Korak 1 — Mesh na CDO

**Datoteka: `Source/MinecraftClone/Voxel/Block.cpp`**

Na vrh dodaj include (ako već nije tu):
```cpp
#include "UObject/ConstructorHelpers.h"
```

U konstruktor `ABlock::ABlock()`, odmah iza `RootComponent = MeshComponent;`:
```cpp
	// Svi blokovi dijele isti mesh (BlockRegistry.cpp:50). Postavljamo ga ovdje
	// jer komponenta jos NIJE registrirana — nema lanca nuspojava iz
	// StaticMeshComponent.cpp:2287-2331. Kasniji SetStaticMesh s istim meshom
	// postaje no-op (early-out na liniji 2268).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		MeshComponent->SetStaticMesh(CubeMesh.Object);
	}
```

⚠️ `if (CubeMesh.Succeeded())` je obavezan — `FObjectFinder` na nepostojećoj putanji
ispisuje fatalan error pri startu editora.
⚠️ `static` je namjeran: `FObjectFinder` se smije koristiti samo u konstruktorskom
kontekstu, a `static` osigurava da se lookup radi jednom (pri kreiranju CDO-a na
startu), a ne pri svakom od 40 000 spawnova.

Postojeći `SetStaticMesh` u `InitializeFromRegistry` **ostaje** — samo prestane raditi išta.

**Checkpoint 1:** builda se bez grešaka; u PIE svijet izgleda IDENTIČNO (svi blokovi
imaju mesh i materijale); `[PERF]` `ms/blok` pao ispod baseline 0.100; nema novih
warninga u Output Logu.

### Korak 2 — Cache razriješenih asseta

#### 2a. Struct — datoteka `Source/MinecraftClone/Voxel/BlockDefinition.h`

⚠️ Struct ide u `BlockDefinition.h`, **NE** u `VoxelWorld.h` — `Block.h` treba taj tip
u potpisu metode, a include `VoxelWorld.h` iz `Block.h` stvorio bi kružnu ovisnost.

Iznad `FBlockDefinition` dodaj forward deklaracije:
```cpp
class UStaticMesh;
class UMaterialInterface;
```

Na KRAJ datoteke (iza `FItemDefinition`) dodaj:
```cpp
/**
 * Razrijesena (hard-pointer) verzija FBlockDefinition.
 * Gradi se jednom u AVoxelWorld::BuildAssetCache() da se izbjegne
 * 3x FSoftObjectPath::TryLoad() po svakom spawnu bloka.
 */
USTRUCT()
struct MINECRAFTCLONE_API FResolvedBlockAssets
{
	GENERATED_BODY()

	UPROPERTY()
	TObjectPtr<UStaticMesh> Mesh = nullptr;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> Material = nullptr;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> HighlightMaterial = nullptr;

	float TimeToDestroy = 1.5f;
	EItemType DropItemType = EItemType::None;
	float DropChance = 1.0f;
};
```

#### 2b. `VoxelWorld.h`

Dodaj include (iznad `#include "VoxelWorld.generated.h"`):
```cpp
#include "BlockDefinition.h"
```
(TMap treba potpun tip vrijednosti, forward deklaracija nije dovoljna.)

U private sekciji ZAMIJENI deklaraciju `void MeasureAssetLoadCost();` (zajedno s
njenim doc-komentarom) s:
```cpp
	/**
	 * Razrijesi sve FSoftObjectPath iz registry-a JEDNOM i spremi hard-pointere.
	 * Zamjenjuje 120 000 TryLoad poziva (3 po bloku na 100x100).
	 * Mjeri i logira trajanje ([PERF] 1/4 Assets) — nasljedjuje ulogu
	 * MeasureAssetLoadCost().
	 */
	void BuildAssetCache();

	/** Razrijeseni asseti po tipu bloka. Gradi se na pocetku GenerateWorld(). */
	UPROPERTY()
	TMap<EBlockType, FResolvedBlockAssets> AssetCache;
```

#### 2c. `VoxelWorld.cpp`

Dodaj includove (ako već nisu tu):
```cpp
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
```

U `GenerateWorld()` zamijeni poziv `MeasureAssetLoadCost();` s `BuildAssetCache();`.

Cijelu funkciju `MeasureAssetLoadCost()` ZAMIJENI s:
```cpp
void AVoxelWorld::BuildAssetCache()
{
	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	if (!Registry)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PERF] 1/4 Assets     -- registry nedostupan --"));
		return;
	}

	const double Start = FPlatformTime::Seconds();

	int32 Resolved = 0;
	int32 Failed = 0;
	AssetCache.Empty();

	for (const FBlockDefinition& Def : Registry->GetAllBlockDefinitions())
	{
		FResolvedBlockAssets Assets;
		Assets.TimeToDestroy = Def.TimeToDestroy;
		Assets.DropItemType = Def.DropItemType;
		Assets.DropChance = Def.DropChance;

		if (!Def.Mesh.IsNull())
		{
			Assets.Mesh = Cast<UStaticMesh>(Def.Mesh.TryLoad());
			if (Assets.Mesh) { Resolved++; } else { Failed++; }
		}
		if (!Def.Material.IsNull())
		{
			Assets.Material = Cast<UMaterialInterface>(Def.Material.TryLoad());
			if (Assets.Material) { Resolved++; } else { Failed++; }
		}
		if (!Def.HighlightMaterial.IsNull())
		{
			Assets.HighlightMaterial = Cast<UMaterialInterface>(Def.HighlightMaterial.TryLoad());
			if (Assets.HighlightMaterial) { Resolved++; } else { Failed++; }
		}

		AssetCache.Add(Def.BlockType, Assets);
	}

	const double ElapsedMs = (FPlatformTime::Seconds() - Start) * 1000.0;
	UE_LOG(LogTemp, Warning, TEXT("[PERF] 1/4 Assets     %8.1f ms   %6d ucitano, %d neuspjelo (%d definicija)"),
		ElapsedMs, Resolved, Failed, AssetCache.Num());
}
```

**Checkpoint 2:** builda se; PIE radi kao prije; `[PERF] 1/4 Assets` ispisuje isti broj
učitanih asseta kao prije (30 učitano, 0 neuspjelo, 10 definicija).

### Korak 3 — `ABlock::InitializeFromCache`

#### `Block.h`
Ispod postojećih forward deklaracija (`class AItemDrop;` itd.) dodaj:
```cpp
struct FResolvedBlockAssets;
```

U public sekciju, odmah ispod deklaracije `InitializeFromRegistry`:
```cpp
	/**
	 * Brza inicijalizacija iz vec razrijesenih asseta (AVoxelWorld::AssetCache).
	 * Nula subsystem lookupa, nula TryLoad poziva.
	 * NIJE UFUNCTION (samo C++) — zato je forward deklaracija tipa dovoljna.
	 */
	void InitializeFromCache(EBlockType Type, const FResolvedBlockAssets& Assets);
```

#### `Block.cpp`
Dodaj include `#include "BlockDefinition.h"` (dolazi i preko `BlockRegistry.h`, ali
eksplicitno je robusnije). Zatim dodaj:
```cpp
void ABlock::InitializeFromCache(EBlockType Type, const FResolvedBlockAssets& Assets)
{
	BlockType = Type;
	TimeToDestroy = Assets.TimeToDestroy;
	DropItemType = Assets.DropItemType;
	DropChance = Assets.DropChance;

	if (MeshComponent)
	{
		// No-op kad je mesh isti kao na CDO-u (early-out, StaticMeshComponent.cpp:2268)
		if (Assets.Mesh)
		{
			MeshComponent->SetStaticMesh(Assets.Mesh);
		}
		if (Assets.Material)
		{
			MeshComponent->SetMaterial(0, Assets.Material);
			// OBAVEZNO — bez ovoga highlight swap vrati krivi materijal
			OriginalMaterial = Assets.Material;
		}
	}

	if (Assets.HighlightMaterial)
	{
		HighlightMaterial = Assets.HighlightMaterial;
	}

	UpdateVisibility();
}
```

**Checkpoint 3:** builda se (metoda još nema pozivatelja — to je u redu).

### Korak 4 — Pozivatelji

**Datoteka: `VoxelWorld.cpp`.** Obje funkcije zamijeni u cijelosti.

```cpp
void AVoxelWorld::SpawnBlock(int32 X, int32 Y, int32 Z, EBlockType Type)
{
	FIntVector GridPos(X, Y, Z);

	// Provjeri postoji li vec blok na toj poziciji
	if (Blocks.Contains(GridPos))
	{
		return;
	}

	const FResolvedBlockAssets* Assets = AssetCache.Find(Type);
	if (!Assets)
	{
		// Fallback put (cache miss) — validiraj kroz registry kao prije
		UBlockRegistry* Registry = UBlockRegistry::Get(this);
		if (!Registry || !Registry->GetBlockDefinition(Type))
		{
			UE_LOG(LogTemp, Warning, TEXT("BlockRegistry: No definition for block type %d"), (int32)Type);
			return;
		}
		UE_LOG(LogTemp, Warning, TEXT("SpawnBlock: AssetCache miss za tip %d, fallback na registry"), (int32)Type);
	}

	FVector WorldPos = GridToWorld(X, Y, Z);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;

	ABlock* NewBlock = GetWorld()->SpawnActor<ABlock>(ABlock::StaticClass(), WorldPos, FRotator::ZeroRotator, SpawnParams);
	if (!NewBlock)
	{
		return;
	}

	NewBlock->SetGridPosition(GridPos);
	if (Assets)
	{
		NewBlock->InitializeFromCache(Type, *Assets);
	}
	else
	{
		NewBlock->InitializeFromRegistry(Type);
	}
	Blocks.Add(GridPos, NewBlock);
}
```

```cpp
ABlock* AVoxelWorld::PlaceBlockAt(FIntVector GridPosition, EBlockType Type)
{
	const FResolvedBlockAssets* Assets = AssetCache.Find(Type);
	if (!Assets)
	{
		// Fallback put (cache miss) — validiraj kroz registry kao prije
		UBlockRegistry* Registry = UBlockRegistry::Get(this);
		if (!Registry || !Registry->GetBlockDefinition(Type))
		{
			UE_LOG(LogTemp, Error, TEXT("PlaceBlockAt: No registry definition for type %d at (%d,%d,%d)"),
				(int32)Type, GridPosition.X, GridPosition.Y, GridPosition.Z);
			return nullptr;
		}
		UE_LOG(LogTemp, Warning, TEXT("PlaceBlockAt: AssetCache miss za tip %d, fallback na registry"), (int32)Type);
	}

	// Provjeri postoji li vec blok na toj poziciji
	if (Blocks.Contains(GridPosition))
	{
		// Ako postoji Air blok, unisti ga i spawnaj novi s pravim tipom
		ABlock* ExistingBlock = Blocks[GridPosition];
		if (ExistingBlock && ExistingBlock->BlockType == EBlockType::Air)
		{
			Blocks.Remove(GridPosition);
			ExistingBlock->Destroy();
		}
		else
		{
			return nullptr;
		}
	}

	FVector WorldPos = GridToWorld(GridPosition.X, GridPosition.Y, GridPosition.Z);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;

	ABlock* NewBlock = GetWorld()->SpawnActor<ABlock>(ABlock::StaticClass(), WorldPos, FRotator::ZeroRotator, SpawnParams);
	if (NewBlock)
	{
		NewBlock->SetGridPosition(GridPosition);
		if (Assets)
		{
			NewBlock->InitializeFromCache(Type, *Assets);
		}
		else
		{
			NewBlock->InitializeFromRegistry(Type);
		}
		Blocks.Add(GridPosition, NewBlock);
	}

	return NewBlock;
}
```

**Checkpoint 4:** PIE — svijet izgleda identično; **highlight radi** (pogledaj blok →
highlight; skreni pogled → vrati se originalni materijal); **kopanje radi** i dropovi
padaju; **postavljanje bloka radi** (RMB); stabla se generiraju; `[PERF]` `ms/blok`
osjetno pao. U logu NEMA "AssetCache miss" warninga.

### Korak 5 — Reserve + čišćenje `BeginPlay`

#### 5a. `VoxelWorld.cpp`, `GenerateWorld()` — odmah iza poziva `BuildAssetCache();`:
```cpp
	// ~66 blokova po stablu (deblo do 7 + krosnja ~60); procjena ne mora biti
	// tocna, samo dovoljna da izbjegne rehash tijekom generacije
	Blocks.Reserve(WorldSizeX * WorldSizeY * (SurfaceLevel + 1) + RandomBlockCount + TreeCount * 70);
```

#### 5b. `Block.cpp`, `BeginPlay()`

⚠️ **Ranija verzija plana ovdje je predlagala zastavicu `bInitializedFromData` — TO NE
MOŽE RADITI, ne pokušavati.** Redoslijed pri spawnu je: `SpawnActor` → (unutar tog
poziva) `BeginPlay` → tek onda `InitializeFromCache`. Zastavica postavljena u
`InitializeFromCache` ne može utjecati na `BeginPlay` koji se već izvršio.

Ispravan fix — izbaci `UpdateVisibility()` iz `BeginPlay`:
```cpp
void ABlock::BeginPlay()
{
	Super::BeginPlay();

	// Fallback za blokove koji se NE inicijaliziraju kroz registry/cache
	// (npr. rucno postavljen BP blok u levelu). Normalnim blokovima
	// InitializeFromCache/Registry ovo odmah pregazi ispravnom vrijednoscu.
	if (MeshComponent && !OriginalMaterial)
	{
		OriginalMaterial = MeshComponent->GetMaterial(0);
	}

	// NAMJERNO nema UpdateVisibility(): BeginPlay se izvrsava dok je BlockType
	// jos default (Air), pa bi sakrio mesh i ugasio koliziju koje
	// InitializeFromCache odmah zatim vraca — dvostruki physics/render churn.
	// Vidljivost postavljaju InitializeFromCache/InitializeFromRegistry.
}
```

Zašto je sigurno: jedina dva puta kojima blok nastaje su `SpawnBlock` i `PlaceBlockAt`,
i oba ODMAH nakon spawna zovu `InitializeFromCache` (ili fallback
`InitializeFromRegistry`), a obje metode završavaju s `UpdateVisibility()`.

**Checkpoint 5:** sve iz Checkpointa 4 i dalje radi; `ms/blok` opet pao.

### Korak 6 — Završno mjerenje

Postupak iz 2.6, sve tri veličine svijeta (25/50/100). Usporedi `ms/blok` s baselineom
**0.100** iz 2.1 i upiši rezultate u ovaj dokument (nova tablica ispod 2.1).
Očekivano: **0.04-0.06 ms/blok**. Ako je pad manji od 20%: provjeri da u logu nema
"AssetCache miss" i da je Korak 1 stvarno primijenjen (Checkpoint 1).

## 2.6 Kako ponoviti mjerenje

1. `BP_VoxelWorld` → postavi `WorldSizeX` i `WorldSizeY` (`SurfaceLevel` ostavi isti)
2. **Restartaj editor** (da assets budu hladni)
3. PIE, pa Output Log → filtriraj `[PERF]`
4. Ponovi za 25×25, 50×50, 100×100

Ključne brojke: **`ms/blok`** (linearnost) i **fiksni vs. skalirajući** postotak.

---

# 3. PROBLEM B: Per-frame rendering

## 3.1 Izmjereni baseline

`stat scenerendering` u PIE, svijet 50×50 (~10 649 blokova).
Screenshot: `Docs/stat_scenerendering.png`.

### Cycle counters — vremena render threada (ms)

| Faza | CallCount | InclusiveAvg | InclusiveMax | ExclusiveAvg |
|---|---|---|---|---|
| **RenderViewFamily** | 1 | **11.94** | **19.04** | 2.12 |
| **InitViews** | 1 | **3.87** | 9.35 | 0.11 |
| Dynamic shadow setup | 2 | 2.27 | 3.89 | 0.13 |
| OcclusionSubmittedFence Wait | 1 | 0.99 | 8.20 | 0.00 |
| BeginOcclusionTests | 1 | 0.41 | 0.82 | 0.41 |
| DeferredShadingSceneRenderer Lighting | 1 | 0.36 | 1.06 | 0.21 |
| Lighting drawing | 1 | 0.10 | 0.82 | 0.00 |
| Translucency drawing | 1 | 0.06 | 0.09 | 0.05 |
| Base pass drawing | 1 | 0.04 | 0.10 | 0.04 |
| Proj Shadow drawing | 1 | 0.03 | 0.04 | 0.03 |
| Depth drawing | 1 | 0.02 | 0.07 | 0.02 |

### Counters — količine

| Brojač | Average | Max |
|---|---|---|
| Ray tracing total instances | 10 220 | 10 220 |
| Ray tracing active instances | 10 220 | 10 220 |
| Ray tracing dynamic update primitives | 3 264 | 3 264 |
| **Mesh draw calls** | **72.58** | 142 |
| Lights in scene | — | 29 |
| Decals in scene | — | 0 |

## 3.2 Zaključci

### Odnos je 30:1

Faze koje **odlučuju što crtati**: `InitViews` 3.87 + `Dynamic shadow setup` 2.27 +
`OcclusionSubmittedFence Wait` 0.99 + `BeginOcclusionTests` 0.41 = **~7.5 ms**

Faze koje **stvarno crtaju**: `Lighting drawing` 0.10 + `Translucency` 0.06 +
`Base pass` 0.04 + `Proj Shadow` 0.03 + `Depth` 0.02 = **~0.25 ms**

`InitViews` sam je **~32%** od `RenderViewFamily`. Max 19.04 ms = spikeovi na ~52 FPS.

### Draw callovi NISU problem

**72.58 draw calla.** Auto-instancing (`r.MeshDrawCommands.DynamicInstancing`, default 1)
spaja identičan mesh+materijal, pa se deseci tisuća blokova sruše u ~72 poziva.

➡️ **Ne mjeriti draw callovima.** Da se gledalo samo `stat rhi`, zaključak bi bio da je sve
savršeno optimizirano — dok se render thread guši.

### Ray tracing

**10 220 RT instanci** — svaki blok ulazi u ray tracing scenu. **3 264 dynamic update
primitives** — toliko primitiva svaki frame osvježava svoju RT instancu.

Blokovi su statični i ne bi smjeli biti "dynamic" → **hipoteza** (nepotvrđena, vidi 0.5) da
je uzrok default `Mobility = Movable`.

**29 svjetala u sceni** — svako sjenčano svjetlo množi posao cullinga
(`Dynamic shadow setup` = 2.27 ms uz CallCount 2).

## 3.3 Rangirani fixevi za Problem B

| # | Fix | Trud | Očekivanje | Što mjeriti |
|---|---|---|---|---|
| **1** | Isključi hardware ray tracing / Lumen HW RT | minute | miče 10 220 RT instanci | `Ray tracing total instances` |
| **2** | `Mobility = Static` ⚠️ **tek nakon mesha na CDO** (2.3.3) | minute | testira hipotezu 3 264 → ~0 | `Ray tracing dynamic update primitives` |
| **3** | Smanji broj sjenčanih svjetala | minute | dio od 2.27 ms | `Dynamic shadow setup` |
| **4** | Ne renderiraj zakopane blokove (sekcija 4) | dani | ~3.6× manje primitiva | `InitViews` (3.87 ms baseline) |
| **5** | Instanced Static Mesh | dani | primitive → ~8 | `InitViews` |
| **6** | Chunking + greedy meshing | tjedni | trokuti → ~2 000 | sve |

## 3.4 Detaljne upute za fixeve 1-3

### Fix 1 — isključi hardware ray tracing

Edit → Project Settings → Engine → Rendering:
1. Sekcija *Hardware Ray Tracing* → isključi **Support Hardware Ray Tracing**
   (editor će tražiti restart).
2. Ako je pod *Global Illumination* → Lumen uključen *Use Hardware Ray Tracing when
   available*, isključi i to.

Za Minecraft-stil grafiku RT gotovo sigurno nije potreban. Alternativa ako se RT želi
zadržati globalno: u `ABlock` konstruktoru `MeshComponent->bVisibleInRayTracing = false;`
— miče samo blokove iz RT scene, bez restarta.

**Mjerenje:** `stat scenerendering` → `Ray tracing total instances` mora pasti na ~0.
Usporedi `RenderViewFamily` InclusiveAvg prije/poslije (baseline 11.94 ms).

### Fix 2 — `Mobility = Static`

⚠️ Smije se raditi **tek NAKON Koraka 1 iz 2.5** (razlog: 2.3.3).

U `ABlock` konstruktoru, iza postavljanja mesha:
```cpp
	MeshComponent->SetMobility(EComponentMobility::Static);
```

✅ **Verificirano da je sigurno za ovaj kod (2026-07-27, engine source):**
- `SetMaterial` NEMA mobility ograničenje (`MeshComponent.cpp:63-135` — u funkciji nema
  `AreDynamicDataChangesAllowed` provjere) → **highlight swap radi i uz Static**.
- `SetVisibility` i `SetCollisionEnabled` (koje zove `UpdateVisibility`) rade neovisno
  o mobilnosti → uništenje bloka (postane nevidljivi Air) radi.
- Blokovi se nikad ne pomiču nakon spawna.
- `SetStaticMesh` iz `InitializeFromCache` je no-op PRIJE mobility provjere
  (early-out na 2268 dolazi prije provjere na 2277).

Simptom da ipak nešto postavlja DRUGI mesh u runtimeu: spam
"Calling SetStaticMesh on ... but Mobility is Static" u PIE Message Logu →
stani i istraži, ne ignoriraj.

**Mjerenje:** `Ray tracing dynamic update primitives` 3 264 → ~0 (ovim se testira
hipoteza iz 0.5). Bonus koji se ne vidi u tom brojaču: draw naredbe za Static primitive
se cachiraju (6.3) umjesto da se grade svaki frame.

### Fix 3 — sjenčana svjetla

Outliner → u filter upiši "light" → identificiraj svih 29 svjetala. Očekivano potrebni:
1 DirectionalLight (sunce) + 1 SkyLight. Ostatak (najčešće ostaci template/example
contenta) je kandidat za uklanjanje. Za svako suvišno svjetlo: obriši ga, ili
Details → Light → **Cast Shadows** = off.

**Mjerenje:** `Dynamic shadow setup` (baseline 2.27 ms, CallCount 2).

---

# 4. Zajednički fix: ne spawnaj zakopane blokove kao aktore

Ovo rješava **oba problema odjednom** — zato je izdvojeno.

## 4.1 Ideja

Blok koji je sa svih šest strana okružen čvrstim blokovima nitko ne može ni vidjeti ni
dodirnuti. Takav blok ne treba biti **aktor** — dovoljno je da bude **broj u mapi**.

## 4.2 Podatkovni model

```cpp
// Svi blokovi — čisti podatak. 40 000 unosa ≈ 640 KB.
UPROPERTY()
TMap<FIntVector, EBlockType> BlockTypes;

// Samo izloženi blokovi — pravi aktori. ~11 000 unosa.
UPROPERTY()
TMap<FIntVector, ABlock*> BlockActors;
```

`BlockTypes` je izvor istine. `BlockActors` je renderirana projekcija tog stanja.

Nove metode:
```cpp
EBlockType GetBlockType(FIntVector Pos) const;   // uvijek radi
ABlock*    GetBlockActor(FIntVector Pos) const;  // može vratiti nullptr
```

## 4.3 Generacija u dva prolaza

1. **Popuni podatke** — trostruka petlja upisuje u `BlockTypes`. Nema `SpawnActor`, nema
   `TryLoad`. Čisti memory writes → **milisekunde za 40 000 blokova**.
2. **Materijaliziraj izložene** — prođi kroz `BlockTypes`, spawnaj aktor samo ako je barem
   jedan susjed zrak.

## 4.4 ⚠️ Zamka u pravilu "izložen"

Naivno pravilo "nema susjeda → izložen" čini **cijeli donji sloj izloženim** (ispod Z=0 nema
ničega), što pojede pola dobitka.

⚠️ **Ispravak (2026-07-27):** ranija verzija plana ovdje je predlagala "cijeli rub
svijeta = bedrock" (`if (!IsInsideWorld(N)) continue;` za svih 6 smjerova). To je
pogrešno iz dva razloga:
1. Bočni obod zakopanih slojeva tada se NE spawna → igrač koji padne preko ruba svijeta
   i pogleda natrag vidi šuplje bočne stijenke.
2. Brojka ~11 200 u tablici ispod RAČUNA bočni obod kao izložen — kod i tablica su si
   proturječili.

Ispravno pravilo: **bedrock je samo ISPOD svijeta (Z < 0); bočno i iznad, izvan svijeta
je zrak.**

```cpp
bool AVoxelWorld::IsExposed(const FIntVector& Pos) const
{
	static const FIntVector Dirs[6] = {
		{1,0,0}, {-1,0,0}, {0,1,0}, {0,-1,0}, {0,0,1}, {0,0,-1}
	};

	for (const FIntVector& D : Dirs)
	{
		const FIntVector N = Pos + D;
		if (N.Z < 0)
		{
			continue;   // ispod svijeta = bedrock, NE izlaze blok
		}
		const EBlockType* NeighbourType = BlockTypes.Find(N);
		if (!NeighbourType || *NeighbourType == EBlockType::Air)
		{
			return true;   // nema unosa (ukljucujuci bocno izvan svijeta) = zrak
		}
	}
	return false;
}
```

Razlika za 100×100×4:

| Pravilo | Izloženih | Dobitak |
|---|---|---|
| Naivno (i dno = zrak) | ~21 200 | 1.9× |
| **Bedrock ispod, zrak bočno** | **~11 200** | **3.6×** |

(gornji sloj 10 000 + bočni obod tri zakopana sloja 396 × 3 = 1 188)

## 4.5 Kad igrač kopa

⚠️ **Dopuna (2026-07-27) — praznina u ranijoj verziji plana: tok uništenja.**
U TRENUTNOM kodu uništeni blok NE nestaje: `ABlock::AddDestroyProgress`
(`Block.cpp:142`) samo postavi `BlockType = Air` i sakrije mesh, a aktor ostaje u
`Blocks` mapi zauvijek. `AVoxelWorld` uopće ne saznaje da je blok uništen (postoje samo
posebne notifikacije za log/lišće). Za sekciju 4 taj tok se MORA preusmjeriti:

1. U `ABlock::AddDestroyProgress`, umjesto `SetBlockType(EBlockType::Air)` na kraju,
   pozvati `World->SetBlockAt(GridPosition, EBlockType::Air)`.
2. VoxelWorld dohvaćati preko `Cast<AVoxelWorld>(GetOwner())` — `SpawnParams.Owner = this`
   se već postavlja u oba spawn puta. Usput time zamijeniti i postojeći spori lookup
   `GetAllActorsOfClass` (`Block.cpp:108-114`).
3. Spawn dropa i log/leaf notifikacije ostaju u `AddDestroyProgress`, PRIJE poziva
   `SetBlockAt` (dropu treba pozicija aktora dok još postoji).
4. Postojeća grana u `PlaceBlockAt` "postoji Air aktor na poziciji"
   (`VoxelWorld.cpp:191-204`) nestaje — u novom modelu Air pozicija NIKAD nema aktora.

**`SetBlockAt(P, Air)` — uništenje:**
1. `BlockTypes[P] = Air`
2. Ako postoji aktor na P: makni iz `BlockActors`, pozovi `Destroy()`
3. Za svakog od šest susjeda N: ako je `BlockTypes[N]` čvrst **i** nema aktora u
   `BlockActors` → **materijaliziraj** (spawnaj aktor postojećim spawn putem)

**`SetBlockAt(P, Type)` — postavljanje:**
1. `BlockTypes[P] = Type`
2. Spawnaj aktor na P

Nema koraka 3 — blok na koji je igrač mogao naciljati je po definiciji izložen. Susjedi koji
su možda postali zakopani zadržavaju aktor (čišćenje toga je opcionalno, preskočiti u v1).

Posao je **konstantan**, ne ovisi o veličini svijeta.

## 4.6 Što treba prilagoditi

✅ **Rade nepromijenjeno — traže samo tip bloka** (zamjena `GetBlock(...)->BlockType` →
`GetBlockType(...)`, mehanički):

| Mjesto | Što radi |
|---|---|
| `VoxelWorld.cpp:380` | `OnLogDestroyed` — je li susjed lišće |
| `VoxelWorld.cpp:408` | `OnLeafDecayed` — isto |
| `VoxelWorld.cpp:484` | `HasLogConnection` BFS — traži log |
| `TreeGenerator.cpp:97` | ima li već bloka na poziciji lista |
| `TreeGenerator.cpp:133,169` | je li tlo prikladno za stablo |

✅ **Rade nepromijenjeno — dobivaju aktor iz `HitResult`:**

| Mjesto | Zašto je sigurno |
|---|---|
| `FirstPersonCharacter.cpp:204` | `Cast<ABlock>(HitResult.GetActor())` — raycast može pogoditi samo blok s kolizijom, a to je točno skup koji ima aktor |
| `ItemDrop.cpp:90` | isto, fizikalni sudar |

⚠️ **Jedino mjesto koje traži pažnju:**

`VoxelWorld.cpp:435` — `ProcessLeafDecay` zove `LeafBlock->AddDestroyProgress(...)`, što
treba aktor (zbog spawna sapling dropa). Lišće je gotovo uvijek izloženo, ali treba
null-safe grana za lišće zazidano unutar guste krošnje.

## 4.7 Glavni rizik

**Desinkronizacija dviju mapa** — blok koji se vidi ali ne postoji, ili obrnuto. Podmukli
bugovi.

Obrana: **sav pristup ide kroz jednu metodu.** `SetBlockAt(Pos, Type)` je jedini ulaz i
interno održava oba stanja plus materijalizaciju susjeda. Ostatak koda samo čita.

Zato ovo raditi **nakon** fixeva 1-4 iz 2.4 — oni su zatvoreni i niskorizični, pa ako nešto
pukne nakon ovoga, zna se gdje tražiti.

---

# 5. Alati za mjerenje u UE 5.6

## 5.1 Startup — `[PERF]` log

Već instrumentirano. Output Log → filter `[PERF]`. Vidi 2.6.

## 5.2 Per-frame — `stat scenerendering`

Utipkati u **PIE konzoli** (tilda `~` dok igra radi), ne u editorskom Cmd polju izvan PIE-a.

Grupa nabraja faze render threada po imenu (`RenderCore/Public/RenderCore.h:17-46`):
`RenderViewFamily`, `InitViews`, `BeginOcclusionTests`, `Dynamic shadow setup`,
`Depth drawing`, `Base pass drawing`, `Proj Shadow drawing`, `Mesh draw calls`,
`Total GPU rendering`.

Kako se čita: **Inclusive** = faza + sve što zove; **Exclusive** = samo ta faza.
Veliki Inclusive uz mali Exclusive (`InitViews` 3.87 vs 0.11) = faza sama ne radi ništa, ali
su joj djeca skupa.

Prikaz je "(flat)" — za hijerarhiju: `stat scenerendering+` (s plusom,
`StatsCommand.cpp:1885`).

⚠️ Ovo su **CPU vremena render threada**, ne GPU vremena.

⚠️ Ekran reže popis (`[8 more stats...]`). Za pun popis: `stats.MaxPerGroup 50`.

## 5.3 ⚠️ Zašto `stat initviews` često "ne postoji"

Grupa **nije uklonjena** u 5.6:
- `Core/Public/Stats/GlobalStats.inl:28` — `DECLARE_STATS_GROUP(TEXT("Init Views"), STATGROUP_InitViews, STATCAT_Advanced)`
- `RenderCore/Public/RenderCore.h:78-79` — brojači `Processed primitives`, `Frustum Culled primitives`
- `Renderer/Private/SceneVisibility.cpp:4727-4729` — brojači se pune svaki frame

Ali `StatsCommand.cpp:2409-2410`:
```cpp
const FName MaybeGroupFName = FName(*(FString(TEXT("STATGROUP_")) + MaybeGroup));
bResult = FStatGroupGameThreadNotifier::Get().StatGroupNames.Contains(MaybeGroupFName);
```
Konzola prihvaća `stat <ime>` **samo ako je grupa već registrirana** u runtimeu, a grupe se
registriraju lijeno. Zato koristiti `stat scenerendering`.

Popis registriranih grupa: `stat group list` (`StatsCommand.cpp:1894`).

## 5.4 Ostale naredbe

```
stat unit          // Frame / Game / Draw / GPU / RHIT — prvi korak, nađi usko grlo
stat unitgraph     // isto kao graf kroz vrijeme
stat rhi           // draw calls + VRAM
stat gpu           // GPU vrijeme po passu
ProfileGPU         // ili Ctrl+Shift+,  — u 5.6 tablica podijeljena na Graphics/Compute
r.ShowMaterialDrawEvents 1   // razrada ProfileGPU po materijalu
FreezeRendering              // zamrzne culling — odletiš kamerom i vidiš što se STVARNO renderira
stats.MaxPerGroup 50
```

Viewport → **Lit** → **Optimization Viewmodes**: Quad Overdraw, Shader Complexity,
Light Complexity.

## 5.5 Unreal Insights (novo u 5.6)

```
UnrealEditor.exe MinecraftClone.uproject -game -trace=cpu,gpu,frame,bookmark
```
ili u runtimeu: `Trace.Start cpu,gpu,frame` / `Trace.Stop`.
Otvoriti `Engine/Binaries/Win64/UnrealInsights.exe` ili Tools → Unreal Insights.

Novo u 5.6: GPU timeline razdvojen na **Graphics** i **Compute**; klikom na stavku dobiješ
**strelice ovisnosti** (pokazuju na što taj posao čeka).

---

# 6. Pozadina: anatomija framea u UE terminologiji

*Referentni materijal — nije potreban za izvođenje plana, ali objašnjava zašto brojke iz
sekcije 3 izgledaju kako izgledaju.*

## 6.1 Četiri stupnja rade paralelno, svaki na drugom frameu

| Thread | Što radi | Frame |
|---|---|---|
| **Game Thread** | Tick, gameplay, fizika, animacija | N |
| **Render Thread** | Odlučuje što se vidi, gradi naredbe za crtanje | N-1 |
| **RHI Thread** | Prevodi naredbe u D3D12/Vulkan pozive | N-2 |
| **GPU** | Rasterizira | N-3 |

Frame traje koliko **najsporiji stupanj** — ostali čekaju. Zato `stat unit` daje pet brojeva.

## 6.2 Renderer nikad ne vidi `AActor`

`AActor` i `UActorComponent` žive isključivo na game threadu. Kad se `UStaticMeshComponent`
registrira, Unreal stvori odvojeni objekt na render threadu — **`FPrimitiveSceneProxy`** — i
ubaci ga u **`FScene`**.

➡️ 40 000 blokova **ne košta ništa na Ticku** (`Block.cpp:13` postavlja
`bCanEverTick = false`, ispravno). Cijena je **40 000 `FPrimitiveSceneProxy` u `FScene`**.

## 6.3 Što render thread radi svaki frame

**Korak 1: `InitViews`**
- **Frustum culling** — prolazi kroz cijeli niz primitiva, testira bounds protiv vidnog polja.
  Jeftino po komadu, ali **40 000 puta**. Zakopani blok se testira jednako kao onaj ispred nosa.
- **Occlusion culling** — HZB / hardverski queries. Rezultati kasne 1-2 framea. Kod ravnog
  terena odozgo pomaže malo.
- **View relevance** — u koje mesh passove primitiva ide.
- ⚠️ **Ponavlja se za svaki shadow view.** Directional light s 3-4 kaskade → scena se cullira
  4-5 puta po frameu → **~200 000 culling testova**.

**Korak 2: `GPUScene` upload** — `FPrimitiveSceneData` po primitivi (transform, bounds) mora
biti na GPU-u.

**Korak 3: Mesh Draw Commands**
- **Cached Mesh Draw Commands** — za `Mobility = Static` naredbe se grade jednom i pamte.
  Za `Movable` se grade **svaki frame ispočetka**.
- **Dynamic Instancing** — identičan mesh+materijal se spaja u jednu instanciranu naredbu.
  ⚠️ Spajanje se događa **tek nakon** cullinga i gatheringa — **cijena po primitivi ostaje
  puna**, samo je krajnji broj draw callova nizak.

**Korak 4:** sortiranje i submit na RHI thread.
**Korak 5:** GPU passovi — Prepass → Base Pass → Shadow Depth → Lumen → Translucency → Post.

## 6.4 Gdje odlazi nevidljivi blok

| Faza | Košta? |
|---|---|
| Game Thread Tick | ❌ ne |
| Frustum culling | ✅ **da, svaki frame** |
| Occlusion culling | ✅ da |
| Shadow view culling (×4-5) | ✅ **da, višestruko** |
| GPUScene memorija | ✅ da |
| Physics broadphase | ✅ da |
| Navmesh geometry gathering | ✅ da (`Block.cpp:19`) |
| Garbage Collection | ✅ da |
| Stvarno crtanje na GPU | ❌ ne (culliran) |

---

# 7. Provjerene činjenice iz engine sourcea

Sve provjereno u `C:\Program Files\Epic Games\UE_5.6\Engine\Source`.

| Datoteka:linija | Činjenica |
|---|---|
| `Runtime/Engine/Private/Actor.cpp:4291` | `RegisterAllComponents()` unutar `PostSpawnInitialize` |
| `Runtime/Engine/Private/Actor.cpp:4288` | registracija se odgađa samo ako `SceneRootComponent == nullptr` **i** klasa je `UBlueprintGeneratedClass` |
| `Runtime/Engine/Private/Actor.cpp:4313` | provjera `bDeferConstruction` dolazi **poslije** registracije |
| `.../Components/StaticMeshComponent.cpp:2265-2271` | `SetStaticMesh` rani izlaz ako je mesh isti |
| `.../Components/StaticMeshComponent.cpp:2273-2283` | `Mobility = Static` + `HasBegunPlay()` → warning i `return false` |
| `.../Components/StaticMeshComponent.cpp:2287-2331` | lanac nuspojava `SetStaticMesh` na registriranoj komponenti |
| `.../Components/MeshComponent.cpp:63-135` | `SetMaterial` NEMA mobility ograničenje → highlight swap radi i uz `Mobility = Static` |
| `Runtime/Core/Public/Stats/GlobalStats.inl:28` | `STATGROUP_InitViews` postoji u 5.6 |
| `Runtime/RenderCore/Public/RenderCore.h:17-46` | brojači grupe `SceneRendering` |
| `Runtime/RenderCore/Public/RenderCore.h:52-80` | brojači grupe `InitViews` |
| `Runtime/Renderer/Private/SceneVisibility.cpp:4727-4729` | `Processed/Culled/Occluded primitives` se pune svaki frame |
| `Runtime/Core/Private/Stats/StatsCommand.cpp:1885` | `stat groupname+` = hijerarhijski prikaz |
| `Runtime/Core/Private/Stats/StatsCommand.cpp:1894` | `stat group list` |
| `Runtime/Core/Private/Stats/StatsCommand.cpp:2409-2410` | grupa mora biti registrirana da je konzola prihvati |

Činjenice iz projekta:

| Datoteka:linija | Činjenica |
|---|---|
| `BlockRegistry.cpp:50` | svi blokovi koriste `/Engine/BasicShapes/Cube.Cube` |
| `BlockRegistry.cpp:71,86,101,116,131,146,161,176,191,206` | potvrda — svaka definicija |
| `Block.cpp:13` | `bCanEverTick = false` |
| `Block.cpp:19` | `SetCanEverAffectNavigation(true)` |
| `Block.cpp:180,187` | drugi `UBlockRegistry::Get` + `GetBlockDefinition` po bloku |
| `Block.cpp:202,212,224` | tri `TryLoad` po bloku |
| `VoxelWorld.cpp:147` + `Block.cpp:33` | `BeginPlay` bloka izvršava se UNUTAR `SpawnActor` poziva (svijet je u playu), dakle PRIJE `InitializeFromRegistry` — zato zastavica iz starog Koraka 5 nije mogla raditi |
| `Block.cpp:108-114` | `AddDestroyProgress` traži VoxelWorld sporim `GetAllActorsOfClass`, iako je `Owner` aktora već VoxelWorld (`VoxelWorld.cpp:144`) |
| `Block.cpp:142` + `VoxelWorld.cpp:191-204` | uništeni blok postaje nevidljivi Air aktor i ostaje u `Blocks` mapi; svijet nije obaviješten — bitno za sekciju 4 (vidi 4.5) |
| `FirstPersonCharacter.cpp:138` | karakter drži pokazivač na `VoxelWorld` (nađen u `BeginPlay`) |

---

# 8. Izvori (online)

- [Stat Commands in Unreal Engine — Epic Documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/stat-commands-in-unreal-engine)
- [Visibility and Occlusion Culling — Epic Documentation](https://dev.epicgames.com/documentation/en-us/unreal-engine/visibility-and-occlusion-culling-in-unreal-engine)
- [How to use ProfileGPU and GPU Insights in UE5.6 and later](https://zenn.dev/kta552/articles/ue-profile-2-0-how-to-use?locale=en)
- [New GPU Profiler and RHI Submission Pipeline — Epic Dev Community](https://dev.epicgames.com/community/learning/tutorials/7Ox6/unreal-engine-new-gpu-profiler-and-rhi-submission-pipeline)
- [Unreal Engine 5.6 Performance Highlights — Tom Looman](https://tomlooman.com/unreal-engine-5-6-performance-highlights/)
- [Unreal Engine Performance Guide — AMD GPUOpen](https://gpuopen.com/learn/unreal-engine-performance-guide/)
- [Measuring Performance — Unreal Art Optimization](https://unrealartoptimization.github.io/book/process/measuring-performance/)
- [Procedural Voxel Mesh Generation — Epic Dev Community Tutorial](https://dev.epicgames.com/community/learning/tutorials/k8am/unreal-engine-procedural-voxel-mesh-generation)
- [UE4 Voxel: Instancing vs Chunking — Zades Dev Blog](https://zadesio.wordpress.com/2020/07/31/ue4-voxel-instancing-vs-chunking/)
- [Optimize lots of instanced meshes for a voxel game — Epic Forums](https://forums.unrealengine.com/t/optimize-lots-of-instanced-meshes-for-a-voxel-game/449274)
