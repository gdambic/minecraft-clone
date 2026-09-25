# PLAN — 3D ekstrudirani item mesh iz 16x16 teksture (Minecraft stil)

> **Status: implementirano** (2026-09-23). Nadogradnja na
> PLAN_HoldingDisplay.md — flat sprite quad u ruci i u item dropu zamijenjen
> "ekstrudiranim" 3D meshom, kako to radi Minecraft.
>
> Odstupanje od plana: čitanje piksela ima dva puta — `PlatformData` (za
> cooked/packaged build; traži TC_EDITOR_ICON + NeverStream) i
> `Texture->Source` fallback (editor build; pod `-nullrhi` se platform data
> uopće ne izgradi, što je headless test odmah otkrio). Modul `ImageCore`
> dodan u Build.cs za `FImage`. Verificirano headless:
> `ItemMeshExtruder: T_Item_WoodenSword (16x16) -> 248 verteksa, 124 trokuta`,
> bez Error logova.

## Kako Minecraft koristi 2D teksturu mača (rezultat istraživanja)

Minecraft **nema ručno modeliran 3D model mača**. Model se generira
automatski iz 16x16 teksture (`ItemModelGenerator`, JSON parent
`builtin/generated` / `item/generated`):

1. **Geometrija**: model je 16x16 piksela širok/visok i **1 piksel debeo**
   (1/16 bloka). Sastoji se od:
   - **prednjeg i stražnjeg quada** preko cijele 16x16 površine, s teksturom
     i alpha cutoutom (prozirni pikseli se ne crtaju);
   - **bočnih quadova po rubovima piksela**: na svakoj granici između
     neprozirnog i prozirnog piksela (ili ruba teksture) generira se quad
     debljine 1 piksel, čiji UV pokazuje u taj rubni piksel — pa bočna
     stranica ima boju tog piksela. Rezultat: mač izgleda kao da je svaki
     piksel mala kockica (izvor: dekompajlirani vanilla
     `ItemModelGenerator`; potvrda mehanizma: greyminecraftcoder blog,
     minecraft.wiki `Model`).
2. **Isti model, različiti transformi po kontekstu** (`display` blok u
   modelu). Za alate/mačeve (parent `item/handheld`, vrijednosti provjerene
   u vanilla assetima, misode/mcmeta):
   - `firstperson_righthand`: rotation `[0, -90, 25]`, translation
     `[1.13, 3.2, 1.13]` (u 1/16 bloka), scale `0.68`
   - `thirdperson_righthand`: rotation `[0, -90, 55]`, translation
     `[0, 4, 0.5]`, scale `0.85`
   - **GUI/inventory**: ostaje čista 2D ikona (bez ekstruzije)
   - **ground (drop)**: isti ekstrudirani model, umanjen, rotira se
3. **Rendering**: nearest filtering (oštri pikseli), alpha cutout (masked),
   bez sjaja — što naš `M_ItemSprite` već radi.

Zaključak za nas: treba **jedan generator mesha iz teksture** (ekvivalent
`ItemModelGenerator`) + postojeći `M_ItemSprite` materijal, a mesh se koristi
na dva mjesta s različitim transformima: ruka (HOLDING) i `AItemDrop`.
Inventory/hotbar ikone ostaju 2D kao i u Minecraftu — tu se ništa ne mijenja.

## Postojeće stanje (na što se plan naslanja)

- `UFirstPersonArmComponent`: mač se prikazuje kao flat quad `HeldSpriteMesh`
  s `M_ItemSprite` MID-om (`SetHeldItem`, `FirstPersonArmComponent.cpp:400`).
- `AItemDrop`: sprite itemi su uspravni rotirajući quad.
- `UBlockRegistry::GetItemIconTexture()` vraća `UTexture2D` za item;
  `GetItemSpriteMaterial()` vraća `M_ItemSprite` (Masked, Two Sided, Fully
  Rough, Nearest kroz postavke teksture).
- Tekstura `T_Item_WoodenSword`: `build_block_materials.py` postavlja
  `TF_NEAREST` + `TC_EDITOR_ICON` (= nekomprimirani BGRA8 — **pogodno za
  CPU čitanje piksela**).

## Koraci

### 1. `FItemMeshExtruder` — generator geometrije iz teksture

Nova statička utility klasa (`Voxel/ItemMeshExtruder.h/.cpp`), po uzoru na
`FTreeGenerator`:

- Ulaz: `UTexture2D*`; izlaz: `FItemExtrudedMesh` struct s
  `TArray<FVector> Vertices; TArray<int32> Triangles; TArray<FVector2D> UVs;
  TArray<FVector> Normals;` (jedna mesh sekcija, materijal `M_ItemSprite`).
- Čitanje piksela: `PlatformData->Mips[0].BulkData.LockReadOnly()` kao
  `FColor` (BGRA8). Ako lock ne uspije ili format nije B8G8R8A8 → `Error` u
  log i vrati prazan mesh (pozivatelj tada zadržava postojeći flat quad —
  siguran fallback).
- Alpha prag: piksel je "neprozirn" ako `A >= 128` (Minecraft cutout).
- Geometrija (dimenzije u pikselima; 1 px = `PixelSize` UU, parametar):
  - prednji + stražnji full quad (16x16), suprotnih normala, UV 0..1;
  - bočni quadovi: scan po redovima i stupcima; na svakom prijelazu
    opaque↔transparent (i na rubu teksture uz opaque piksel) emitiraj quad
    debljine 1 px. Uzastopne prijelaze u istom redu/stupcu spojiti u jedan
    quad (greedy run) — UV se proteže duž runa, a okomita UV koordinata
    je prikvačena na sredinu rubnog piksela, pa uz nearest filtering svaki
    fragment dobije boju "svog" piksela.
- Origin mesha u centru 16x16 ploče (lakše rotiranje u ruci i u dropu).

### 2. Cache u `UBlockRegistry`

- `const FItemExtrudedMesh* GetItemExtrudedMesh(EItemType)` — generira
  jednom po itemu (samo za iteme s display type `"sprite"`), sprema u
  `TMap<EItemType, FItemExtrudedMesh>`. Vraća `nullptr` ako item nema
  sprite teksturu ili je ekstruzija neuspjela.

### 3. Prikaz u ruci — `UFirstPersonArmComponent`

- `HeldSpriteMesh` (UStaticMeshComponent s plane assetom) zamijeniti
  `UProceduralMeshComponent`-om `HeldSpriteMesh` (isto ime, ista attach
  logika na `ArmMesh` → nasljeđuje swing/bobbing; ista pravila
  OnlyOwnerSee/NoCollision/NoShadow).
- `SetHeldItem`: umjesto `SetTextureParameterValue` na quadu →
  `Registry->GetItemExtrudedMesh(ItemType)`; ako postoji,
  `CreateMeshSection` + `M_ItemSprite` MID s tom teksturom; ako ne postoji
  mesh a postoji tekstura → stari flat quad put (fallback, jedan plane
  section).
- Default transform po uzoru na Minecraft `firstperson_righthand`
  (`[0,-90,25]`, scale 0.68): dijagonalno, vrh mača gore-naprijed.
  Postojeći `HeldSpriteOffset/Rotation/Scale` (EditAnywhere) ostaju kanal
  za fino podešavanje u Editoru; u planu samo promijeniti defaulte
  (npr. Rotation ≈ pitch 25° roll -90° ekvivalent u našem prostoru — točne
  vrijednosti se štimaju vizualno u Editoru).
- `MinecraftClone.Build.cs`: dodati `"ProceduralMeshComponent"` u
  `PublicDependencyModuleNames`.

### 4. `AItemDrop` — ekstrudirani mesh umjesto quada

- Za sprite iteme: rotirajući uspravni quad zamijeniti istim
  `UProceduralMeshComponent` + `GetItemExtrudedMesh` (postojeća rotacija i
  bobbing ostaju). Fallback bez mesha: postojeći quad.
- Skala manja nego u ruci (Minecraft ground display), parametar
  `EditAnywhere`.

### 5. Python skripta — streaming off za item teksture

- U `build_block_materials.py` za `T_Item_*` teksture postaviti i
  `never_stream = True`: garantira da je mip 0 dostupan za CPU čitanje i u
  cooked/packaged buildu (u editoru radi i bez toga).

### 6. Build i verifikacija

- Build Development Editor (VS 2022).
- Headless run (`-nullrhi`): nema `Error` logova iz `ItemMeshExtruder` /
  `BlockRegistry` (ekstruzija se pokreće kad se mač spawna kao drop u
  `BeginPlay`).
- Ručno u Editoru:
  - pokupiti WoodenSword s poda → u ruci 3D "pixel" mač (bočne stranice u
    boji rubnih piksela, ne sivo), dijagonalno kao u Minecraftu;
  - swing animacija i bobbing rade kao prije (attach nepromijenjen);
  - drop na podu: ekstrudirani mač rotira;
  - blokovi u ruci i inventory ikone nepromijenjeni.

## Izvan opsega

- Third-person prikaz (igra je first-person; drop pokriva "ground" prikaz).
- Višeslojne teksture (Minecraft layer0-4) — naši itemi imaju jednu teksturu.
- Enchant glint / tint efekti.
