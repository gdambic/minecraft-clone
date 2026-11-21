# PLAN: Zvukovi kopanja i lomljenja blokova

Status: implementirano (koraci 1-7). Headless provjereno: svih 13 blokova
razrjesava grupu i assetove, nula fallbackova (`voxel.sndtest` -> `[SND]
GOTOVO: provjereno=13, bez zvuka=0`). Ostaje rucni test u editoru - je li
zvuk cujan i kako je ustiman.
Datum: 2026-10-27

## 1. Cilj

Dok igrac drzi LMB na bloku, cuje se ponavljajuci zvuk kopanja koji ovisi o
materijalu bloka; kad blok pukne, cuje se jednokratni zvuk loma. Isti sustav
pokriva i leaf decay (lisce koje nestane samo) te kasnije postavljanje bloka.

Zvuk **ne ovisi o alatu** — vanilla nema tool-specificne sample. Alat mijenja
samo brzinu kopanja, pa se zvuk kopanja ponovi manje puta prije loma (krampa na
kamenu: 2-3 ponavljanja umjesto 15-ak golom rukom).

## 2. Vanilla referenca

Svaki blok pripada *sound grupi* (`SoundType`: stone, wood, gravel, grass, sand,
basalt...). Grupa definira evente `hit`, `break`, `place`, `step`, `fall`.

| Pojam | Vanilla ponasanje |
|---|---|
| Zvuk tijekom kopanja | `block.<grupa>.hit`, svakih **4 ticka = 0.2 s** |
| Zvuk pri lomu | `block.<grupa>.break`, jednom |
| Sample za `hit` | iste datoteke kao korak po tom materijalu (`step/*.ogg`) |
| Sample za `break` | `dig/*.ogg` (odnosno `block/<grupa>/break*.ogg`) |
| Domet | ~16 blokova |

`hit` je tisi i nizeg pitcha od `break`-a. Tocne vanilla formule i nase
vrijednosti su u poglavlju 10.

Izvori: [Block sound type](https://minecraft.wiki/w/Block_sound_type),
[sounds.json](https://minecraft.wiki/w/Sounds.json),
[ViaVersion #5120 (level event 2020, 4-tick kadenca)](https://github.com/ViaVersion/ViaVersion/issues/5120).

## 3. Assetovi

Originalni `.ogg` sampleovi su vec izvuceni iz lokalne instalacije u
`Saved/MinecraftSounds/` (1070 datoteka, `manifest.csv` mapira event -> datoteke).
Projekt se ne distribuira (ucenje), pa ih koristimo direktno.

UE 5.6 uvozi `.ogg` bez konverzije ([Importing Audio Files](https://dev.epicgames.com/documentation/en-us/unreal-engine/importing-audio-files)),
interno ga svejedno pretvara u 16-bit WAV.

Treba nam 5 grupa za 13 postojecih blokova:

| Grupa | hit sampleovi | break sampleovi |
|---|---|---|
| Stone | `step/stone1..6.ogg` | `dig/stone1..4.ogg` |
| Wood | `step/wood1..6.ogg` | `dig/wood1..4.ogg` |
| Gravel | `step/gravel1..4.ogg` | `dig/gravel1..4.ogg` |
| Grass | `step/grass1..6.ogg` | `dig/grass1..4.ogg` |
| Basalt | `block/basalt/step1..6.ogg` | `block/basalt/break1..5.ogg` |

Ciljna struktura u projektu (`Content/Audio/Blocks/<Grupa>/`):
`Hit_1.uasset`...`Hit_6.uasset`, `Break_1.uasset`...`Break_4.uasset`.

Dodjela grupa nasim blokovima (vanilla mapiranje gdje postoji):

- **Gravel**: Dirt
- **Grass**: Grass, OakLeaves, BirchLeaves
- **Stone**: Stone, Andesite, Limestone
- **Basalt**: Basalt
- **Wood**: OakLog, BirchLog, OakPlanks, BirchPlanks, CraftingTable

## 4. Podatkovni model

### 4.1 Novi enum i polje u definiciji bloka

`BlockSoundTypes.h` (novi):

```cpp
UENUM(BlueprintType)
enum class EBlockSoundGroup : uint8
{
    None, Stone, Wood, Gravel, Grass, Basalt
};
```

`BlockDefinition.h` — novo polje uz `ToolClass`:

```cpp
/** Sound grupa bloka (JSON "soundGroup": "stone"/"wood"/...). */
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
EBlockSoundGroup SoundGroup = EBlockSoundGroup::None;
```

`Blocks.json` dobiva `"soundGroup": "stone"` po unosu.
`FJsonObjectConverter::JsonObjectToUStruct` (BlockRegistry.cpp:104) pretvara
string u enum isto kao sto vec radi za `toolClass` — bez dodatnog koda za parsing.

### 4.2 Mapa grupa -> sampleovi

Novi `Content/Data/BlockSounds.json`, isti stil kao Blocks.json (eksplicitne
putanje do assetova, bez magicne konvencije imena):

```json
[
  {
    "group": "Stone",
    "hit":   ["/Game/Audio/Blocks/Stone/Hit_1", "...", "/Game/Audio/Blocks/Stone/Hit_6"],
    "break": ["/Game/Audio/Blocks/Stone/Break_1", "...", "/Game/Audio/Blocks/Stone/Break_4"],
    "place": []
  }
]
```

Ucitava ga `UBlockRegistry::LoadBlockSoundsFromJson()` u
`TMap<EBlockSoundGroup, FBlockSoundSet>`. `USoundBase` se loada lazy
(`FSoftObjectPath::TryLoad`) i kesira, isto kao `ItemBlockMaterialCache`
(BlockRegistry.h:188).

Fallback: blok bez `soundGroup` ili grupa bez unosa u JSON-u dobije **Stone**
grupu + `UE_LOG(Error)` jednom po tipu bloka — ista filozofija kao sivi fallback
blok (BlockRegistry.cpp:159).

## 5. Reprodukcija

Novi `BlockSoundLibrary.h/.cpp` (`UBlockSoundLibrary : UBlueprintFunctionLibrary`),
jedino mjesto koje zna za glasnocu, pitch i limite:

```cpp
static void PlayBlockHit(const UObject* Ctx, EBlockType Type, const FVector& Loc);
static void PlayBlockBreak(const UObject* Ctx, EBlockType Type, const FVector& Loc);
static void PlayBlockPlace(const UObject* Ctx, EBlockType Type, const FVector& Loc);
```

Svaka bira nasumicni sample iz grupe, zove
`UGameplayStatics::PlaySoundAtLocation` u sredistu bloka i primjenjuje:

- glasnoca: `voxel.sound.hitvolume` (0.6) / `voxel.sound.breakvolume` (1.0)
- pitch: bazni (0.5 hit / 0.8 break) × nasumicno ±5 % da se ponavljanje ne cuje
  kao petlja
- prethodni zvuk kopanja se gasi kad krene novi (`voxel.sound.hitfadeout`,
  0.06 s) - vidi poglavlje 10
- attenuation: jedan `SA_BlockSounds` asset (sferni falloff do 1600 uu = 16
  blokova), putanja je konstanta u `BlockRegistry.cpp`; asset radi
  `Scripts/import_block_sounds.py`, a ako fali, zvuk svira bez prostornog pada
  uz Warning

**Limiter.** Leaf decay unisti desetke blokova u jednom tren — `PlayBlockBreak`
broji pozive po frameu i preko `voxel.sound.maxbreakperframe` (default 4)
preskace ostatak uz `[SND]` log. Bez toga jedno sruseno stablo zvuci kao eksplozija.

## 6. Tocke zakacenja

| Dogadaj | Mjesto | Sto se dodaje |
|---|---|---|
| Kopanje u tijeku | `ABlock::AddDestroyProgress` (Block.cpp:66) | brojac `HitSoundTimer`; svakih 0.2 s `PlayBlockHit` u sredistu bloka |
| Pocetak kopanja | `ABlock::ResetDestroyProgress` (Block.cpp:122) | `HitSoundTimer = 0` da prvi udarac zvuci odmah |
| Lom bloka | `AVoxelWorld::NotifyBlockDestroyed` (VoxelWorld.cpp:1203) | `PlayBlockBreak` prije brisanja podataka |
| Postavljanje | `AVoxelWorld::PlaceBlockAt` (VoxelWorld.cpp:1321) | `PlayBlockPlace` nakon uspjesnog postavljanja |

`NotifyBlockDestroyed` je jedina tocka kroz koju prolazi svako unistenje
(kopanje, `DestroyBlockAt`, leaf decay), a chunk unload ne ide kroz nju — zato
zvuk loma ide tamo, a ne u characteru.

Promjena alata tijekom kopanja vec resetira progres
(FirstPersonCharacter.cpp:319-323) i time ce resetirati i timer zvuka — isto
ponasanje kao u Minecraftu.

## 7. Koraci implementacije

1. **Import assetova** (editor): kopirati 5 grupa iz `Saved/MinecraftSounds/` u
   `Content/Audio/Blocks/<Grupa>/`, uvesti kao `USoundWave`, preimenovati u
   `Hit_N` / `Break_N`. Napraviti `SA_BlockSounds` attenuation asset.
2. **Enum + polje**: `BlockSoundTypes.h`, `FBlockDefinition::SoundGroup`,
   `"soundGroup"` u svih 13 unosa u `Blocks.json`.
3. **Registry**: `BlockSounds.json`, `FBlockSoundSet`, `LoadBlockSoundsFromJson()`,
   `GetBlockSounds(EBlockType)` + lazy load i kes, Stone fallback uz Error log.
4. **Library**: `BlockSoundLibrary` s tri funkcije, CVar-ovima i limiterom.
5. **Zakacenja**: `AddDestroyProgress`, `ResetDestroyProgress`,
   `NotifyBlockDestroyed` (koraci iz tablice gore; `PlaceBlockAt` moze i zasebno).
6. **Dijagnostika**: konzolna komanda `voxel.sndtest <BlockType>` — ispise u log
   `[SND]` koju grupu i koje assetove blok koristi i odsvira hit+break; sluzi i
   za headless provjeru da je mapiranje kompletno.
7. **Test**: headless run s `-ExecCmds="voxel.sndtest all"` (provjerava lookup i
   fallback bez renderiranja), pa rucni test u editoru za stvarni zvuk —
   kopanje kamena krampom i rukom, rusenje stabla (limiter), leaf decay.

## 7a. Sto je stvarno napravljeno

- `Scripts/extract_minecraft_sounds.py` - izvlacenje sampleova iz lokalne
  instalacije u `Saved/MinecraftSounds/` (ponovljivo, nije u repou).
- `Scripts/import_block_sounds.py` - uvoz 49 `.ogg` sampleova u
  `Content/Audio/Blocks/<Grupa>/` + `SA_BlockSounds`; registriran i u
  **Tools > MinecraftClone > Import Block Sounds**.
- `BlockSoundTypes.h` - `EBlockSoundGroup`, `EBlockSoundEvent`, `FBlockSoundSet`.
- `BlockDefinition.h` + `Blocks.json` - polje `soundGroup` na svih 13 blokova.
- `Content/Data/BlockSounds.json` + `UBlockRegistry::LoadBlockSoundsFromJson`,
  `GetBlockSoundGroup`, `GetRandomBlockSound`, `GetBlockSoundAttenuation`.
- `BlockSoundLibrary.h/.cpp` - sviranje, CVar-ovi, limiter po frameu,
  konzolna komanda `voxel.sndtest`.
- Zakacenja: `Block.cpp` (hit + reset timera), `VoxelWorld.cpp`
  (`NotifyBlockDestroyed` = break, `PlaceBlockAt` = place).

Napomena iz importa: headless commandlet pri uvozu ispise handled ensure
"Decoder for AudioFormat 'BINKA' not found" (nema audio mixera u `-nullrhi`
runu). Assetovi su svejedno nastali; ako se u editoru ne cuju, pokreni import
iz editora preko Tools izbornika.

## 8. Kriterij prihvacanja

- Kopanje svakog od 13 blokova daje ponavljajuci zvuk materijala svakih 0.2 s.
- Lom daje jednokratni glasniji zvuk iste grupe.
- Zvuk je isti bez obzira na alat; brza alatka = manje ponavljanja.
- Rusenje stabla ne proizvodi kakofoniju (limiter radi).
- `voxel.sndtest all` ne prijavljuje nijedan fallback.

## 9. Izvan opsega (za backlog)

- Koraci igraca po materijalu (`step` eventi) i zvuk pada.
- Zvukovi pickupa itema, craftanja, otvaranja inventara.
- Crack particle efekt uz zvuk kopanja.
- Sound grupe za buduce blokove (sand, glass, wool) — dodaju se u istu JSON mapu.

## 10. Usporedba s vanillom nakon prvog slusanja (2026-10-27)

Prvi test u editoru: zvuk kopanja pretih i razvucen. Provjera vanilla brojki u
izvoru pokazala je da su nam formule bile tocne:

| Dogadaj | Vanilla | Nasa prva verzija | Sada |
|---|---|---|---|
| Kopanje, glasnoca | `(volume + 1) / 8` = 0.25 | 0.25 | **0.6** |
| Kopanje, pitch | `pitch × 0.5` = 0.5 | 0.5 | 0.5 |
| Kopanje, razmak | 4 ticka = 0.2 s | 0.2 s | 0.2 s |
| Lom / postavljanje, glasnoca | `(volume + 1) / 2` = 1.0 | 1.0 | 1.0 |
| Lom / postavljanje, pitch | `pitch × 0.8` = 0.8 | 0.8 | 0.8 |

Izvori: [PlayerControllerMP.onPlayerDamageBlock](https://github.com/Marcelektro/MCP-919/blob/main/src/minecraft/net/minecraft/client/multiplayer/PlayerControllerMP.java)
(`(block.stepSound.getVolume() + 1.0F) / 8.0F`, `getFrequency() * 0.5F`, svaka 4
ticka), [ItemBlock](https://github.com/Marcelektro/MCP-919/blob/main/src/minecraft/net/minecraft/item/ItemBlock.java)
(`(volume + 1.0F) / 2.0F`, `getFrequency() * 0.8F`).

Dvije stvarne razlike prema vanilli:

1. **Preklapanje glasova.** Sampleovi traju 0.42-0.69 s, a na pitchu 0.5 i do
   1.4 s. Uz novi udarac svakih 0.2 s svira 5-7 glasova odjednom, sto se cuje
   kao jedan dugi drone. Rjesenje: `ABlock` pamti komponentu zadnjeg udarca i
   gasi je fade-outom od 0.06 s kad krene sljedeci
   (`voxel.sound.hitfadeout`, 0 = staro ponasanje).
2. **Glasnoca.** Vanilla 0.25 je racunata za Minecraftov mikser; u UE je
   pretiho, pa je default dignut na 0.6. Svjesno odstupanje, stima se CVar-om.
