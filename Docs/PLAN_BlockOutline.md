# PLAN: Obrub (outline) bloka u koji igrač gleda

Status: koraci 1-3 i 6 implementirani, build prolazi, soak test prolazi
(rehash OK=200, FAIL=0, RETAINED OK). Pivot konvencija izmjerena headless runom:
`/Engine/BasicShapes/Cube` ima bounds (-50,-50,-50)-(50,50,50), pa obrub uzima
bounds mesha. Ostaju mjerenja 7.1-7.5 (ručna, u editoru) i brisanje asseta
`M_BlockHighlight` nakon provjere referenci.
Datum: 2026-10-25

## 1. Što je sada

Fokusirani blok se **presvuče drugim materijalom**. Tok je:

1. `AFirstPersonCharacter::UpdateBlockLookAt()` (`FirstPersonCharacter.cpp:323-378`)
   raycasta, prevede hit u grid poziciju i promovira blok u `ABlock` actor.
2. `HitBlock->SetHighlighted(true)` (`FirstPersonCharacter.cpp:373`).
3. `ABlock::SetHighlighted()` (`Block.cpp:63-80`) zamijeni materijal slota 0
   s `HighlightMaterial`, a na gašenju vrati `OriginalMaterial`.
4. Materijal dolazi iz registryja: po bloku iz `Blocks.json` ili default
   `/Game/Blueprints/Materials/M_BlockHighlight` (`BlockRegistry.cpp:17,136-138`).

Tri posljedice koje želimo ukloniti:

- blok izgubi svoju teksturu/biome tint dok je fokusiran (vidi se kao "svjetliji
  blok", ne kao obrub),
- highlight je vezan na promote/demote mehaniku (vizual ovisi o tome je li blok
  actor), dok je obrub čista stvar prikaza,
- oblik nije izražen: kad jednom dođu ploče/stepenice, swap materijala ne može
  pokazati stvarni oblik bloka.

## 2. Kako to radi Minecraft

Vanilla ne mijenja materijal bloka. Crta **žičani okvir (wireframe) oko oblika
targetiranog bloka**: `LevelRenderer.renderHitOutline` → `renderShape`, što
prolazi kroz bridove `VoxelShape`-a targetiranog bloka i za svaki emitira liniju.
Bitne karakteristike tog prikaza:

1. **Bridovi, ne silueta.** Vidi se 9 bridova kocke (6 siluete + 3 iz najbližeg
   kuta), jer su linije depth-testirane protiv svijeta, a stražnje brid-linije
   skriva sam blok.
2. **Crna linija s alphom ≈ 0.4** (102/255), blend po source alpha.
3. **Depth test da, depth write ne** — obrub se skriva za blokovima između
   igrača i cilja, ali ne "ukrućuje" depth buffer.
4. **Konstantna širina na ekranu** (linija ostaje ista debljina na 1 i na 5
   blokova udaljenosti), jer je to GL linija, ne geometrija u svijetu.
5. **Blago napuhan okvir**, da se linija na šavu sa susjednim blokom ne gubi u
   z-fightingu.

Zato **post-process outline preko Custom Depth/Stencil nije rješenje**: on daje
siluetu objekta, pa bi se dobio šesterokutni obris bez tri unutarnja brida —
prepoznatljivo *ne*-Minecraft.

## 3. Odabrani pristup: vlastiti scene proxy sa screen-space linijama

Crtamo 12 bridova kutije kao **translucentne linije konstantne piksel-debljine**
iz vlastite `UPrimitiveComponent`. Sve je C++; nema materijala, nema mesha, nema
asseta — u editoru ostaje samo vizualna provjera i kalibracija.

To je moguće jer UE 5.6 ima točno te semantike u batched-element putu (provjereno
u engine kodu, ne pretpostavljeno):

| Što nam treba | Dokaz u engine kodu |
|---|---|
| Linija sa zadanom debljinom i screen-space skaliranjem | `PDI->DrawTranslucentLine(Start, End, Color, DPG, Thickness, DepthBias, bScreenSpace)` — `Engine/Public/PrimitiveDrawInterface.h:67-75` |
| `bScreenSpace=true` → debljina konstantna na ekranu | `ScalingStart = W / ViewportSizeX`, offset po `CameraX/CameraY` — `Private/BatchedElements.cpp:1060-1102` |
| Alpha se čuva | `AddTranslucentLine` prenosi `Color` kakav je; `AddLine` ga forsira na `A=1` — `BatchedElements.cpp:129-196` |
| Depth test bez depth writea | translucent batched elementi (SDPG_World) crtaju se u glavnom translucency passu s `TStaticDepthStencilState<false, CF_DepthNearOrEqual>` — `Private/TranslucentRendering.cpp:1393-1396` |
| Depth bias protiv z-fightinga na šavu | `DepthBias` ulazi u rasterizer state thick-line batcha — `BatchedElements.cpp:1010` |

Dakle točka 2. iz vanilla liste (alpha), 3. (depth) i 4. (konstantna širina)
dobivamo gratis, bez ijednog shadera.

### 3.1 Piksel-debljina izvedena iz projekcije

Iz `BatchedElements.cpp:1061-1102`: svijetski offset je
`2 · Thickness · W / ViewportSizeX` po kameri, a projekcija vrati
`NDC_x = view_x · P00 / W` gdje je `P00 = 1 / tan(FOVx/2)`. Množenjem izlazi

```
širina_u_pikselima = Thickness · P00 = Thickness / tan(FOVx/2)
```

tj. **neovisna o rezoluciji i screen percentage**. Zato parametar izlažemo u
pikselima i pretvaramo ga u proxyju po viewu:

```cpp
const float TanHalfFovX = 1.0f / View->ViewMatrices.GetProjectionMatrix().M[0][0];
const float Thickness   = ThicknessPixels * TanHalfFovX;
```

Time obrub automatski ostaje jednake debljine i kad se mijenja FOV (zoom,
spyglass kasnije). Formula je izvedena iz koda, ali **mora se provjeriti
mjerenjem** (korak 7) — ne uzimamo je na vjeru.

### 3.2 Zašto ne druge tri opcije

- **`ULineBatchComponent` / `DrawDebugBox`**: njegov proxy zove
  `DrawTranslucentLine` bez `bScreenSpace` (`LineBatchComponent.cpp:78`), pa je
  debljina u svijetu — na pola bloka udaljenosti linija je ~10x deblja nego na
  5 blokova. U dev buildovima ide i na debug PDI (`LineBatchComponent.cpp:70-75`),
  a header sam kaže "not very performant for runtime use".
- **ProceduralMesh kavez + materijal s World Position Offsetom**: radi, ali
  traži materijal-graf u editoru i ručno računanje screen-space širine. Držimo ga
  kao **plan B** (korak 8, rizik TSR), jer ima jednu prednost: translucentni
  materijal može uključiti Responsive AA.
- **Post-process Custom Depth/Stencil**: daje siluetu, ne bridove (vidi 2.).

## 4. Nove datoteke i API

### `Source/MinecraftClone/Voxel/BlockOutlineComponent.h/.cpp`

```cpp
UCLASS(ClassGroup = (Voxel), meta = (BlueprintSpawnableComponent))
class MINECRAFTCLONE_API UBlockOutlineComponent : public UPrimitiveComponent
{
    GENERATED_BODY()
public:
    UBlockOutlineComponent();

    /** Oblik koji se obrubljuje, u lokalnom prostoru komponente.
     *  Default je puna kocka (±50 uu). No-op ako je kutija ista. */
    void SetOutlineBox(const FBox& InLocalBox);

    virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
    virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

private:
    FBox LocalBox = FBox(FVector(-50.0f), FVector(50.0f));
};
```

Proxy je cijela logika crtanja:

```cpp
void FBlockOutlineSceneProxy::GetDynamicMeshElements(
    const TArray<const FSceneView*>& Views, const FSceneViewFamily& ViewFamily,
    uint32 VisibilityMap, FMeshElementCollector& Collector) const
{
    // Live-tunable iz konzole (vidi 7.)
    const float Inflate = CVarOutlineInflate.GetValueOnRenderThread();
    const float Px      = CVarOutlinePixels.GetValueOnRenderThread();
    const float Alpha   = CVarOutlineAlpha.GetValueOnRenderThread();
    const float Bias    = CVarOutlineDepthBias.GetValueOnRenderThread();

    const FBox Box = LocalBox.ExpandBy(Inflate);
    const FMatrix& LocalToWorld = GetLocalToWorld();

    FVector C[8];
    for (int32 i = 0; i < 8; ++i)
    {
        C[i] = LocalToWorld.TransformPosition(FVector(
            (i & 1) ? Box.Max.X : Box.Min.X,
            (i & 2) ? Box.Max.Y : Box.Min.Y,
            (i & 4) ? Box.Max.Z : Box.Min.Z));
    }

    // 12 bridova kutije: 4 po X, 4 po Y, 4 po Z
    static constexpr int32 EdgeIdx[12][2] = {
        {0,1},{2,3},{4,5},{6,7},
        {0,2},{1,3},{4,6},{5,7},
        {0,4},{1,5},{2,6},{3,7}
    };

    const FLinearColor Color(0.0f, 0.0f, 0.0f, Alpha);

    for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
    {
        if (!(VisibilityMap & (1 << ViewIndex))) { continue; }

        const FSceneView* View = Views[ViewIndex];
        const float TanHalfFovX = 1.0f / View->ViewMatrices.GetProjectionMatrix().M[0][0];
        const float Thickness = Px * TanHalfFovX;

        FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
        for (const int32 (&E)[2] : EdgeIdx)
        {
            PDI->DrawTranslucentLine(C[E[0]], C[E[1]], Color,
                SDPG_World, Thickness, Bias, /*bScreenSpace=*/true);
        }
    }
}
```

Obavezni detalji proxyja:

- `GetViewRelevance`: `bDrawRelevance = IsShown(View)`, `bDynamicRelevance = true`,
  `bNormalTranslucency = true`, `bShadowRelevance = false`.
- `CanBeOccluded()` → `false`. Obrub je namjerno per-pixel depth testiran; HZB
  occlusion bi mogao odbaciti cijeli primitiv kad je blok u ravnini sa susjedima.
- `bWillEverBeLit = false`, `GetTypeHash()`, `GetMemoryFootprint()` kao u
  `FLineBatcherSceneProxy` (`LineBatchComponent.cpp:47-58,130-133`).
- `CalcBounds`: `LocalBox` proširen fiksnom marginom (npr. 2 uu) pa transformiran
  — margina je zato da promjena `voxel.outline.inflate` iz konzole ne traži
  ponovni izračun boundsa.

Postavke komponente u konstruktoru: `NoCollision`, `SetCastShadow(false)`,
`bUseAsOccluder = false`, `SetGenerateOverlapEvents(false)`,
`SetCanEverAffectNavigation(false)`, `Mobility = Movable`,
`PrimaryComponentTick.bCanEverTick = false`, `SetVisibility(false)`.

### `AVoxelWorld` — oblik obruba iz podataka

```cpp
/** Kutija obruba u lokalnom prostoru bloka (ishodište = GridToWorld te pozicije). */
FBox GetBlockOutlineBox(EBlockType Type);
```

Implementirano tako da vraća **bounds samog mesha** iz asset cachea, pa obrub
prati vizual bez pretpostavke o pivotu; bez mesha vraća punu kocku. Mjerenje
headless runom pokazalo je da `/Engine/BasicShapes/Cube` ima bounds
(-50,-50,-50)-(50,50,50), a instance i actori stoje točno na `GridToWorld(Pos)`
(`VoxelWorld.cpp:1004-1005,875`) — kocka je dakle centrirana na toj poziciji.
Kad dođu ploče i stepenice, mijenja se samo ova funkcija.

### `AFirstPersonCharacter` — upravljanje obrubom

```cpp
UPROPERTY(VisibleAnywhere, Category = "Block|Outline")
UBlockOutlineComponent* BlockOutlineComponent;

FIntVector OutlineGridPos = FIntVector(MIN_int32);   // sentinel "ništa"
```

Konstruktor:

```cpp
BlockOutlineComponent = CreateDefaultSubobject<UBlockOutlineComponent>(TEXT("BlockOutline"));
BlockOutlineComponent->SetupAttachment(RootComponent);
BlockOutlineComponent->SetUsingAbsoluteLocation(true);   // stoji u svijetu, ne na igraču
BlockOutlineComponent->SetUsingAbsoluteRotation(true);
BlockOutlineComponent->SetUsingAbsoluteScale(true);
```

## 5. Integracija u `UpdateBlockLookAt`

Ključno: **obrub se vodi po grid poziciji, ne po `ABlock` pointeru.** Time je
prikaz odvojen od promote/demote mehanike (koja ostaje jer nosi mining progres).

```cpp
    FIntVector HitGridPos;
    EBlockType HitType;
    bool bHasTarget = false;

    if (bHit && VoxelWorld && VoxelWorld->ResolveHitToGrid(HitResult, HitGridPos, HitType))
    {
        bHasTarget = true;
        HitBlock = VoxelWorld->PromoteToActor(HitGridPos);
    }

    // --- obrub ---
    if (bHasTarget)
    {
        if (HitGridPos != OutlineGridPos)
        {
            OutlineGridPos = HitGridPos;
            BlockOutlineComponent->SetOutlineBox(AVoxelWorld::GetBlockOutlineBox(HitType));
            BlockOutlineComponent->SetWorldLocation(
                VoxelWorld->GridToWorld(HitGridPos.X, HitGridPos.Y, HitGridPos.Z));
            BlockOutlineComponent->SetVisibility(true);
        }
    }
    else if (OutlineGridPos.X != MIN_int32)
    {
        OutlineGridPos = FIntVector(MIN_int32);
        BlockOutlineComponent->SetVisibility(false);
    }
```

Guard na promjenu pozicije znači da u tipičnom kadru nema nikakvog rada na
render-thread strani; transform se dira samo kad se meta promijeni.

Dodatno treba sakriti obrub kad se blok uništi dok se u njega gleda
(`AddDestroyProgress` vrati `true` u `Tick`, `FirstPersonCharacter.cpp:316-319`):
tamo resetirati `OutlineGridPos` na sentinel da se sljedeći kadar ponovno
evaluira.

## 6. Brisanje starog highlighta

Nakon što obrub radi, stari put ide van u cijelosti (inače ostaje mrtav kod koji
i dalje učitava materijal po bloku):

1. `Block.h:47-57` (`HighlightMaterial`, `SetHighlighted`, `IsHighlighted`),
   `Block.h:119` (`bIsHighlighted`), `Block.h:92` (parametar u
   `InitializeFromRegistry`).
2. `Block.cpp:43-47` (`OriginalMaterial` u `BeginPlay`), `Block.cpp:63-80`,
   `Block.cpp:205-209`.
3. `FirstPersonCharacter.cpp:370-374` (poziv `SetHighlighted`).
4. `BlockDefinition.h:78-80` i `:113` (`HighlightMaterial` u definiciji i
   razriješenim assetima).
5. `BlockRegistry.cpp:17` (`GDefaultHighlightPath`), `:136-138`, `:246`.
6. `VoxelWorld.cpp:904` (argument), `:1400-1405` (provjera učitavanja), `:1442`.
7. **Editor korak prije brisanja materijala:** Find in Blueprints → `SetHighlighted`,
   i Reference Viewer na `M_BlockHighlight`. Blokove spawna `ABlock::StaticClass()`
   (`VoxelWorld.cpp:882`), pa BP_Block vjerojatno nije u igri, ali to se ne može
   provjeriti iz `.uasset` datoteka izvan editora — provjeri pa briši.
8. `Blocks.json` nema ni jedan `highlightMaterial` unos (provjereno), pa tamo
   nema što čistiti.

Stanje: koraci 1-6 odrađeni u kodu, `grep` po `Source/` više ne nalazi
`SetHighlighted`, `HighlightMaterial`, `bIsHighlighted` ni `OriginalMaterial`.
Ostaje editor provjera referenci i brisanje `M_BlockHighlight.uasset`.

## 7. Parametri, konzola i mjerenje

CVar-ovi (čitaju se na render threadu, pa se mijenjaju uživo bez rebuilda):

| CVar | Default | Značenje |
|---|---|---|
| `voxel.outline.px` | 2.5 | širina linije u pikselima |
| `voxel.outline.alpha` | 0.4 | alpha crne linije (vanilla 102/255) |
| `voxel.outline.inflate` | 0.3 | napuhavanje kutije u uu (vanilla 0.002 bloka = 0.2 uu) |
| `voxel.outline.depthbias` | 0.0 | rezerva ako napuhavanje nije dovoljno |

Mjerenja koja zatvaraju plan (sve ručno u editoru, jer `-nullrhi` ne renderira):

1. **Piksel-debljina.** Screenshot na 1920 i na 2560 širine, isti FOV: izmjeri
   širinu linije u pikselima. Očekivano: ista vrijednost, jednaka
   `voxel.outline.px`. Ako je faktor 2 ili tan-faktor drugačiji, formula iz 3.1
   se korigira prema mjerenju.
2. **Konstantnost s udaljenosti.** Isti blok iz 0.5 i iz 5 blokova: debljina se
   ne smije promijeniti.
3. **Okluzija.** Blok iza drugog bloka: obrub nevidljiv. Blok u zidu (susjedi sa
   svih strana osim jedne): svih 9 vidljivih bridova prisutno, bez treperenja —
   to je test za `inflate`.
4. **Tint i tekstura ostaju.** Fokusirani blok trave zadržava biome tint
   (prije je swap materijala to gubio).
5. **Anti-aliasing.** Pogledaj obrub dok se kamera polako rotira: ako linija
   "pliva"/treperi, to je TSR na tankoj translucentnoj geometriji → rizik 8.1.

## 8. Rizici

**8.1 TSR/TAA razmazivanje tanke linije.** Batched-element linije ne mogu
uključiti Responsive AA (to je flag na materijalu). Ako mjerenje 7.5 pokaže
treperenje: prvo probati `voxel.outline.px` 3–4, a ako ne pomogne, preći na plan
B — `UProceduralMeshComponent` kavez od 12 tankih quadova + unlit translucentni
materijal s Responsive AA i WPO proširenjem po kameri. Geometrija i vertex
podaci (tangenta brida u vertex boji, znak širenja u alphi) ostaju C++; u editoru
bi se tada radio materijal-graf. `ProceduralMeshComponent` je već u
`MinecraftClone.Build.cs:25`, pa dependency ne treba dodavati.

**8.2 Cijena.** 12 linija = jedan thick-line batch po kadru, bez vertex buffera
u stalnom vlasništvu. Zanemarivo; `GetDynamicMeshElements` se izvodi samo dok je
komponenta vidljiva.

**8.3 Pivot konvencija.** Obrub je centriran na `GridToWorld(Pos)` jer su mesh i
ISM instance tamo centrirani. `WorldToGrid` (`VoxelWorld.cpp:427-431`) radi
`floor`, dakle tretira istu poziciju kao **minimalni kut** ćelije — te dvije
konvencije se razlikuju za pola bloka. Za obrub je relevantna samo vizualna
(centar), ali ako se konvencija ikad poravna, `GetBlockOutlineBox` je jedino
mjesto koje se mijenja.

**8.4 Ne-kockasti oblici.** Dok ne dođu ploče/stepenice, `GetBlockOutlineBox`
vraća punu kocku. API je spreman, ali više kutija po bloku (npr. ograda) tražilo
bi `TArray<FBox>` — sitna izmjena proxyja.

**8.5 Headless test ne pokriva ovo.** `-nullrhi` nema renderer, pa `[SOAK]`/
`voxel.kbtest` stil automatske provjere ovdje ne postoji. Korak 7 je ručni.

## 9. Redoslijed implementacije

1. `BlockOutlineComponent.h/.cpp` + 4 CVar-a. Prvo bez integracije, testirati
   fiksnim transformom na jednom bloku.
2. `AVoxelWorld::GetBlockOutlineBox`.
3. Komponenta na `AFirstPersonCharacter` + integracija u `UpdateBlockLookAt` i
   reset pri uništenju bloka.
4. Mjerenja 7.1–7.4 i kalibracija defaultova; defaulte upisati kao konstante.
5. Mjerenje 7.5; ako treperi → plan B (8.1).
6. Brisanje starog highlight puta (korak 6), uključujući editor provjeru
   referenci, pa brisanje `M_BlockHighlight`.
7. `CLAUDE.md`: dopuniti popis dokumenata i `Key APIs` s obrubom.

Prirodni nastavak (izvan ovog plana): overlay napredovanja kopanja (vanilla
`destroy_stage_0..9`) — isti "prikaz fokusiranog bloka" problem, pa bi mu ista
komponenta mogla biti dom.

## 10. Izvori

Engine kod (UE 5.6, lokalna instalacija):
`Engine/Source/Runtime/Engine/Public/PrimitiveDrawInterface.h`,
`Engine/Source/Runtime/Engine/Private/BatchedElements.cpp`,
`Engine/Source/Runtime/Engine/Private/Components/LineBatchComponent.cpp`,
`Engine/Source/Runtime/Renderer/Private/TranslucentRendering.cpp`.

Minecraft referenca:
[LevelRenderer javadoc (renderHitOutline / renderVoxelShape / renderShape)](https://lexxie.dev/forge/1.20.1/net/minecraft/client/renderer/LevelRenderer.html),
[opis vanilla outline rendera (crna, alpha 102/255, depth test, source-alpha blend)](https://github.com/ludovic111/horizon/pull/609),
[minecraft:selection_box (oblik obruba dolazi iz oblika bloka)](https://learn.microsoft.com/en-us/minecraft/creator/reference/content/blockreference/examples/blockcomponents/minecraftblock_selection_box).

Plan B reference:
[World Position Offset material functions (SplineThicken)](https://dev.epicgames.com/documentation/en-us/unreal-engine/world-position-offset-material-functions-in-unreal-engine),
[Wireframe material constant thickness (forum)](https://forums.unrealengine.com/t/wireframe-material-constant-thickness/353486).
