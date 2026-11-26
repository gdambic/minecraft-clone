# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MinecraftClone is an **Unreal Engine 5.6** voxel game recreating Minecraft mechanics.

## Build Commands

```bash
# Build via Visual Studio 2022
# 1. Open MinecraftClone.sln
# 2. Configuration: Development Editor | Win64
# 3. Build solution

# Run: Open MinecraftClone.uproject with UE5.6 Editor
# In World Settings: GameMode Override → BP_FirstPersonGameMode
```

No automated test framework is configured. Testing is manual through the editor.

### Headless Test Run (bez editora)

Igra se može pokrenuti headless (puna game logika — BeginPlay, fizika, AI,
navmesh — ali bez renderiranja) i dijagnosticirati čitanjem loga:

```powershell
# Pokreni igru bez renderera, pusti da radi 45 s, pa ugasi proces
$p = Start-Process -FilePath "C:\Program Files\Epic Games\UE_5.6\Engine\Binaries\Win64\UnrealEditor-Cmd.exe" `
  -ArgumentList '"C:\RVS\C++GameDev\MinecraftClone\MinecraftClone.uproject"',
    '-game','-nullrhi','-unattended','-nosound','-nosplash','-log',
    '-ABSLOG="C:\putanja\do\test.log"' -PassThru
Start-Sleep -Seconds 45
if (!$p.HasExited) { Stop-Process -Id $p.Id -Force }
```

Ključni argumenti:
- `-game` — pokreće GameDefaultMap iz DefaultEngine.ini kao igru (ne editor)
- `-nullrhi` — bez GPU-a/renderiranja; game thread, fizika, AI i navmesh rade normalno
- `-unattended` — bez modalnih dijaloga koji bi blokirali proces
- `-ABSLOG="..."` — log u zadanu datoteku (inače `Saved/Logs/MinecraftClone.log`)

Zatim log pretražiti za relevantne markere, npr.:

```powershell
Select-String -Path test.log -Pattern "\[PERF\]|DEBUG|LogNavigation|Error"
```

Postojeći markeri u logu: `[PERF]` (mjerenje GenerateWorld faza),
`DEBUG:` (EnemyAIController nav dijagnostika ~2 s nakon starta:
`Pawn on NavMesh`, `MoveToActor result`). `[BIOME]` (stupci po biomu, raspon visine i
CRC visina/bioma/terena oko (0,0), ispis prije i poslije bootstrapa; isti seed
mora dati isti CRC; `more (SeaLevel=0)` = udio stupaca pod vodom i lokvi,
`kontinentalnost` = percentili za ugađanje pragova, `najblizi ocean` s `voxel.tp`). `[WATER]` (ploha mora: stupci pod plohom vs. stupci
s vrhom ispod mora u podacima, `OK` = prolaz; ispis na startu i na svakom soak hopu; `pogled ispod vode UKLJUCEN/iskljucen` kad se kamera spusti pod vrh vala nad morem ili izađe; `kutija vode UKLJUCENA/iskljucena` i `igrac PLIVA/vise ne pliva ... vZ=` za plivanje, vZ ~520 = skok na obalu). Brojanje ponavljajućih grešaka
(npr. wander failova) kroz vrijeme dobro otkriva radi li nešto trajno ili
se samo sporo inicijalizira.

Ograničenja: nema vizualne provjere (highlight, animacije) ni input
interakcije — to ostaje ručni test u editoru.

Dodatni argument `-VoxelSoakTest` pokreće automatski streaming test: teleport
hopovi (uklj. negativne koordinate) + CRC provjera determinizma regeneracije +
retained-modified provjera. Markeri u logu: `[SOAK]` (`GOTOVO: rehash OK=N,
FAIL=0` i `RETAINED OK` = prolaz). Za ručni test streaminga u editoru:
konzolna komanda `voxel.tp <gridX> <gridY>`.

Kopanje i pukotine mjeri `voxel.cracktest [item...]` (default `None
WoodenPickaxe`): kopa blok ispod igrača simuliranim frameovima i za svaku fazu
ispiše `[CRACK] blok=... faza=N`, a na kraju `[CRACK] lom t=... (ocekivano ...)`.
U headless runu se proces nakon testova sam ugasi.

Pickup dropa mjeri `voxel.pickuptest`: spawna drop u pickup zoni igraca i jedan
izvan nje; `[PICKUP] ... pokupljen nakon 0.50 s` i `let gotov nakon 0.150 s` =
prolaz (dalji drop se ne smije pokupiti). Proces se nakon testa sam ugasi.

Plivanje mjeri `voxel.swimtest`: baci igrača u najbliže more dublje od 8
blokova, pusti ga da tone, drži razmak do površine i na njoj, pa ga pusti;
`[WATER] swim t=... mod=... sredina-povrsina=... vZ=...` svakih 0.1 s
(tonjenje ~160 UU/s, dizanje ~190 UU/s). Baci i Dirt drop u more:
`[WATER] drop ... izronio nakon ~1.5 s`. Proces se nakon testa sam ugasi.

Mobove uz more provjerava `voxel.mobwatertest`: ovce na obali bježe od igrača prema
moru, jedna ovca je stvorena u moru; `[WATER] mob test GOTOVO: ... najvise 0 ... -> OK`
= prolaz. `voxel.mobs.avoidwater 0` isključuje popravak za usporedbu. Svakih 10 s
log ispiše `[WATER] stvorenja u moru: N od M`.

Mace mjeri `voxel.macetest`: vrtnja i bacanje u stvorenje 5 blokova ispred igraca,
pa bacanje s ponovnim pritiskom (kugla padne i lezi) i pustanje; `[MACE] t=... A -> B`
su promjene stanja, `[MACE] test GOTOVO: ... -> OK` = prolaz. `voxel.mace.log 1`
ispisuje stanja i pogotke u igri. Proces se nakon testa sam ugasi.

Zombija mjeri `voxel.zombietest`: prati zombija koji lovi igrača 14 s, pa ga
ozlijedi i ubije. `[ZOMBIE] test: ... mesh=SK_Zombie anim=ABP_Zombie_C` potvrđuje
assete; linije `lov/ozljeda/smrt` daju walkSpeed, agresivnost, napad, tint i kutove
kostiju od rest poze (GetAngle 0..360, pa 360-x = x na drugu stranu): ruka ~120
dok ima metu, noga do walk*80, root do 90 pri smrti; `GOTOVO: ... uklonjen -> OK`
= prolaz. Proces se nakon testa sam ugasi.

Knockback se mjeri komandom `voxel.kbtest [jacina...]` (default `0.4 0.9`
blokova/tick): odbaci stvorenje najbliže igraču i u log ispiše `[KB]` linije s
jačinom, brzinom i dometom pri doskoku. U headless runu (`-ExecCmds="voxel.kbtest"`)
proces se nakon testova sam ugasi da se log flusha.

## Architecture

### Coordinate System
- **X** = Width, **Y** = Length, **Z** = Height (Z is UP, unlike Minecraft's Y-up)
- Block Size = 100 Unreal Units

### Core Classes (Source/MinecraftClone/Voxel/)

| Class | Purpose |
|-------|---------|
| `AVoxelWorld` | World manager: chunk streaming (beskonačan svijet), per-chunk ISM vizuali, async generacija |
| `FVoxelChunk` / `VoxelGrid` | Chunk 16×16×256, Z −128..127 (flat array, Air=0) + koordinatni helperi (`VoxelChunk.h`) |
| `FChunkGenerator` | Čista, thread-safe generacija podataka chunka (teren+stabla+showcase, `ChunkGenerator.h`) |
| `ABlock` | Individual voxel actor with mesh, destruction system, drop spawning |
| `AItemDrop` | Physics-enabled dropped items with auto-pickup |
| `UInventoryComponent` | Slot-based player inventory (36 slots: 27 main + 9 hotbar) |
| `AFirstPersonCharacter` | Player with camera, input handling, block raycast |
| `FTreeGenerator` | Static utility for procedural Oak/Birch tree generation |

### Key Enums

```cpp
// EBlockType: Air, Dirt, Stone, Grass, OakLog, BirchLog, OakLeaves, BirchLeaves
// EItemType: None, Dirt, Stone, Grass, OakLog, BirchLog, OakSapling, BirchSapling
```

### Block Interaction Flow
1. Character raycasts from camera (500 units = 5 blocks range)
2. LMB: Destroy block → spawns AItemDrop → auto-pickup adds to inventory
3. RMB: Place block from inventory at adjacent grid position
4. Scroll: Cycle through inventory items

### Leaf Decay System
Leaves without log connection within 6 blocks (Manhattan distance via BFS) decay and may drop saplings (5% chance).

## Source Structure

```
Source/MinecraftClone/
├── MinecraftClone.h/.cpp     # Module definition
├── MinecraftClone.Build.cs   # Build configuration
└── Voxel/                    # Core voxel system
```

## Content Organization

- `Content/Data/Blocks.json`, `Items.json` - definicije blokova/itema; učitava ih `UBlockRegistry` (enum vrijednost bez JSON unosa dobije fallback: siva kocka + Error u logu)
- `Content/Data/Recipes/` - crafting recepti (JSON, file-per-recipe)
- `Content/Data/BlockSounds.json` - sampleovi po sound grupi; assetovi u `Content/Audio/Blocks/` (uvozi ih `Scripts/import_block_sounds.py`)
- `Content/Blueprints/Blocks/` - Block blueprints (BP_Dirt, BP_Stone, etc.)
- `Content/Input/Actions/` - Enhanced Input actions
- `Content/FirstPerson/Blueprints/` - Character and controller BPs

## Module Dependencies

Configured in `MinecraftClone.Build.cs`:
- Core, CoreUObject, Engine, InputCore
- EnhancedInput (modern input system)
- UMG, Slate (UI)

## Key APIs

```cpp
// VoxelWorld (beskonačan svijet: chunkovi 16×16×256 streamaju se oko igrača,
// generacija na worker threadu; teren = ISM instance PO CHUNKU; ABlock actor
// postoji samo za fokusirani blok; izmjene prežive unload u RAM-u, ne restart.
// Streaming properties: VisualRadius (6), ChunkApplyBudgetMs, BootstrapRadius.
// Vidi Docs/PLAN_Chunks.md.)
EBlockType GetBlockTypeAt(FIntVector GridPosition);   // tip na poziciji (izvor istine)
ABlock* GetBlock(int32 X, int32 Y, int32 Z);          // SAMO promovirani (fokusirani) blok
void SetBlockType(int32 X, int32 Y, int32 Z, EBlockType Type);
bool PlaceBlockAt(FIntVector GridPosition, EBlockType Type);
FVector GridToWorld(int32 X, int32 Y, int32 Z);
bool ResolveHitToGrid(const FHitResult& Hit, FIntVector& OutPos, EBlockType& OutType);
ABlock* PromoteToActor(FIntVector Pos);               // instanca -> actor (fokus)
void DemoteToInstance(FIntVector Pos);                // actor -> instanca (fokus otisao)
void DestroyBlockAt(FIntVector Pos);                  // unisti + drop, bez actora (vidi bDropItems dolje)
FBox GetBlockOutlineBox(EBlockType Type);             // oblik obruba (mesh bounds)

// Obrub fokusiranog bloka (Minecraft zicani okvir; Docs/PLAN_BlockOutline.md).
// UBlockOutlineComponent zivi na characteru s apsolutnim transformom i crta 12
// bridova kao screen-space translucentne linije kroz vlastiti scene proxy -
// bez materijala i bez asseta. Stimanje u igri: voxel.outline.px (2.5),
// voxel.outline.alpha (0.4), voxel.outline.inflate (0.3), voxel.outline.depthbias.
void UBlockOutlineComponent::SetOutlineBox(const FBox& LocalBox);

// Zvukovi blokova (sound grupa po materijalu; Docs/PLAN_BlockSounds.md).
// Blok u Blocks.json navodi "soundGroup", BlockSounds.json mapira grupu na
// sampleove u Content/Audio/Blocks. Zvuk NE ovisi o alatu - alat mijenja samo
// broj ponavljanja prije loma. Hit svira ABlock::AddDestroyProgress svakih
// 0.2 s, break AVoxelWorld::NotifyBlockDestroyed, place PlaceBlockAt.
static void UBlockSoundLibrary::PlayBlockSound(const UObject* Ctx, EBlockType Type,
                                               EBlockSoundEvent Event, const FVector& Location);
static float UBlockSoundLibrary::GetHitSoundInterval();   // voxel.sound.hitinterval
USoundBase* UBlockRegistry::GetRandomBlockSound(EBlockType Type, EBlockSoundEvent Event);
// Stimanje: voxel.sound.hitvolume/breakvolume/placevolume, *pitch,
// voxel.sound.pitchjitter, voxel.sound.hitfadeout, voxel.sound.maxperframe.
// Provjera: voxel.sndtest.

// Kopanje i pukotine (Docs/PLAN_BlockDamage.md). UBlockMiningComponent na
// characteru odlucuje (progres, alat, zvuk udarca, lom kroz DestroyBlockAt);
// UBlockCrackComponent na svijetu crta pukotinu kao malo vecu kocku s
// M_BlockCrack. Asseti: Tools > MinecraftClone > Import Block Cracks.
// Stimanje: voxel.crack.opacity, voxel.crack.inflate, voxel.crack.stage N.
void AVoxelWorld::SetBlockCrack(uint32 BreakerId, FIntVector Pos, int32 Stage);  // -1 brise
void AVoxelWorld::DestroyBlockAt(FIntVector Pos, bool bDropItems = true);

// Knockback (vanilla formula, jacina u blokovima/tick; Docs/PLAN_Knockback.md)
static bool UCombatLibrary::ApplyKnockback(ACharacter* Victim, FVector HorizontalDir,
                                           float StrengthBlocksPerTick, float KnockbackResistance);
void ACreatureBase::ReceiveKnockback(FVector HorizontalDir, float StrengthBlocksPerTick);
bool ACreatureBase::CanBeHurt() const;   // prozor nepovredivosti 0.5 s
// Per-mob: KnockbackResistance (0..1), AttackKnockback, HurtImmunityTime

// InventoryComponent - Slot API
FInventorySlot GetSlot(int32 SlotIndex);
void SetSlot(int32 SlotIndex, EItemType Type, int32 Quantity);
bool SwapSlots(int32 SlotA, int32 SlotB);
const TArray<FInventorySlot>& GetAllSlots();

// InventoryComponent - Utility
void AddItem(EItemType Type, int32 Amount = 1);  // Smart add for pickup
int32 RemoveItem(EItemType Type, int32 Amount = 1);
int32 GetItemCount(EItemType Type);
static EBlockType ItemTypeToBlockType(EItemType ItemType);
static EItemType BlockTypeToItemType(EBlockType BlockType);
```

## Documentation

Detailed docs in Croatian available in `Docs/`:
- `BACKLOG.md` - Poznati dugovi i odgođena poboljšanja
- `PLAN_BlockRegistry.md` - Data-driven block registry design
- `PLAN_Performance.md` - Rendering/performance analysis
- `PLAN_Knockback.md` - Knockback pri melee udarcu (vanilla formula, sprint-hit, per-mob otpor)
- `PLAN_Biomes.md` - Biomi i biome tint za Grass/lišće (Biomes.json + PerInstanceCustomData)
- `PLAN_StonyPeaks.md` - Treći biom Stony Peaks (noise planina, viši svijet, kamena površina s kalcitom, plan)
- `PLAN_Oceans.md` - Razina mora (Z=0) i noise kontinentalnosti: oceani, obala, dno mora, spawn na kopnu (implementirano)
- `PLAN_Water.md` - Realistična voda: vlastita ploha po chunku sa Single Layer Water materijalom, valovi, pogled ispod vode, plivanje, plutanje dropova, mobovi izvan mora (zatvoreno; ostatak odgođen)
- `PLAN_Chunks.md` - Chunk sustav i beskonačan svijet (streaming oko igrača, async generacija)
- `PLAN_BlockOutline.md` - Minecraft obrub fokusiranog bloka (screen-space linije u vlastitom scene proxyju)
- `PLAN_BlockSounds.md` - Zvukovi kopanja i lomljenja blokova (sound grupe po materijalu, plan)
- `PLAN_BlockDamage.md` - Pukotine pri kopanju (destroy_stage 0-9 iz Minecraft jara, kopanje odvojeno od prikaza, implementirano)
- `PLAN_ItemNameHighlight.md` - Ime odabranog itema iznad hotbara (2 s + fade, `UHotbarWidget`, `[ITEMNAME]` u logu, implementirano)
- `PLAN_Mace.md` - Mace: doslovni Terraria thrown flail (vrtnja, bacanje, drop) s drškom u ruci, kugla i lanac u svijetu (implementirano)
- `PLAN_FirstPersonHand.md` - First-person hand/viewmodel system
- `PLAN_HoldingDisplay.md` - Prikaz odabranog itema u ruci (sprite, implementirano)
- `PLAN_ItemExtrusion.md` - 3D ekstrudirani item mesh iz 16x16 teksture (Minecraft stil, implementirano)
- `IMPLEMENTACIJA_ItemExtrusion.md` - Detaljno objašnjenje izvedbe item ekstruzije, korak po korak
- `BLOCK_TEXTURES_AND_MATERIALS.md` - Teksture i materijali blokova, rotacija
- `TEXTURE_SPEC.md` - Popis PNG datoteka koje treba nacrtati
- `PLAN_Zombie.md` - Minecraft zombi: model/tekstura/animacije iz vanilla formula kao asseti (Blender skripta + import), logika u C++
- `sheep.md` - Sheep passive mob implementation
- `Blokovi_Itemi_Minecraft.md` - Block/item reference table
