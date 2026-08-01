# Blokovi i Itemi u Minecraftu

Ovaj dokument sadrži istraživanje Minecraft sustava blokova i itema kao referenca za implementaciju u našem projektu.

---

## Odnos Block ↔ Item

**Ključni koncept:** Block i Item su **dvije forme istog objekta**.

```
┌─────────────────┐         Place          ┌─────────────────┐
│     ITEM        │ ──────────────────────▶│     BLOCK       │
│  (u inventoryu) │                        │   (u svijetu)   │
│                 │ ◀────────────────────  │                 │
└─────────────────┘         Break/Drop     └─────────────────┘
```

- **Item** - postoji samo u inventory-u, ruci igrača, item frame-u
- **Block** - postoji u game worldu na grid poziciji

Kada se registrira block, Minecraft **automatski kreira odgovarajući item** (BlockItem) koji:
- Ima isti identifier kao block
- Koristi block model za prikaz u inventoryu
- Ima `block_placer` komponentu koja postavlja block

---

## Kategorije Blokova

### 1. Strukturni (Structural)
- **Drvo/Lumber**: trupci (logs), daske (planks), bamboo, stabljike gljiva (stems, hyphae)
- **Kamen**: stone, deepslate, cobblestone, bricks
- **Minerali**: quartz, copper, iron, gold, diamond, emerald, coal, lapis, netherite
- **Nether**: netherrack, nether bricks, blackstone, basalt
- **End**: end stone, purpur

### 2. Dekorativni (Ornamental)
- **Parcijalni blokovi**: stepenice (stairs), ploče (slabs), ograde (fences), zidovi (walls)
- **Obojivi**: vuna (wool), terracotta, beton (concrete), staklo (glass)
- **Rasvjeta**: svijeće (candles), baklje (torches), fenjeri (lanterns), glowstone, froglight

### 3. Prirodni (Natural)
- **Tlo**: dirt, grass, sand, gravel, clay, mud
- **Rude (Ores)**: coal ore, iron ore, diamond ore, ancient debris
- **Biljke**: saplings, leaves, flowers, crops, vines
- **Gljive**: mushrooms, nether wart, sculk
- **Tekućine**: water, lava, ice, snow

### 4. Funkcionalni (Utility)
- **Interaktivni**: crafting table, furnace, chest, anvil, enchanting table
- **Redstone**: pistons, hoppers, buttons, pressure plates, doors

### 5. Kreativni/Naredbeni (Creative/Commands Only)
- Barrier, command blocks, light blocks, structure blocks

---

## Podaci Koji Opisuju Block

| Svojstvo | Tip | Opis | Primjeri |
|----------|-----|------|----------|
| **Hardness** | float | Tvrdoća - određuje vrijeme razbijanja | 0.5 (dirt), 1.5 (stone), 50 (obsidian) |
| **Blast Resistance** | float | Otpornost na eksplozije | 6 (cobblestone), 1200 (obsidian) |
| **Harvest Level** | int 0-4 | Minimalni tier alata potreban za drop | 0=wood, 1=stone, 2=iron, 3=diamond, 4=netherite |
| **Preferred Tool** | enum | Vrsta alata za brže kopanje | pickaxe, axe, shovel, hoe, none |
| **Flammable** | bool + params | Može li gorjeti | šansa zapaljenja i uništenja |
| **Transparent** | bool | Propušta li svjetlo | glass=true, stone=false |
| **Solid/Collidable** | bool | Ima li fizičku koliziju | stone=true, water=false |
| **Gravity** | bool | Pada li pod utjecajem gravitacije | sand=true, gravel=true |
| **Light Level** | int 0-15 | Emitira li svjetlo i koliko | glowstone=15, torch=14 |
| **Sound Type** | enum | Zvučna grupa za zvukove | wood, stone, glass, grass, metal, gravel |
| **Drops** | array | Što ispada kad se razbije | stone → cobblestone, diamond_ore → diamond |
| **Stack Size** | int | Max količina u stacku | 64, 16, ili 1 |
| **Replaceable** | bool | Može li se zamijeniti postavljanjem drugog bloka | air, water, tall_grass |

---

## Mehanika Razbijanja Blokova

### Formula za Vrijeme Razbijanja
- **1.5× hardness** ako je korišten ispravan alat
- **5× hardness** ako je korišten pogrešan alat

### Množitelji Brzine po Tieru Alata

| Tier | Brzina | Trajnost |
|------|--------|----------|
| Wooden | 2× | 59 |
| Stone | 4× | 131 |
| Iron | 6× | 250 |
| Diamond | 8× | 1561 |
| Netherite | 9× | 2031 |
| Gold | 12× | 32 |

### Harvest Levels (Razine Alata)

| Level | Tier | Primjeri Blokova |
|-------|------|------------------|
| 0 | Wood/Gold | Dirt, Sand, Wood |
| 1 | Stone | Stone, Coal Ore, Iron Ore |
| 2 | Iron | Gold Ore, Diamond Ore, Redstone Ore |
| 3 | Diamond | Obsidian, Ancient Debris |
| 4 | Netherite | (trenutno nekorišteno) |

### Penali
- **Kopanje pod vodom** bez Aqua Affinity: 5× sporije
- **Kopanje u zraku** (lebdenje): 5× sporije
- **Kombinacija**: 25× sporije

---

## Zvučne Grupe (Sound Types)

| Grupa | Blokovi | Napomena |
|-------|---------|----------|
| **Wood** | Daske, vrata, drveni predmeti | - |
| **Stone** | Kamen, rude, cigle | - |
| **Grass** | Trava, cvijeće, lišće, gljive | Lakši zvuk |
| **Gravel** | Šljunak, clay, dirt, farmland | Zrnasti zvuk |
| **Glass** | Staklo, led, glowstone | Kristalni zvuk |
| **Metal** | Željezni blokovi, rails, redstone | - |
| **Anvil** | Nakovanj, zvono | Volume: 0.3 |
| **Amethyst** | Ametist blokovi | Kristalni zvuk |
| **Sand** | Pijesak | - |
| **Wool** | Vuna | Mekani zvuk |
| **Snow** | Snijeg | - |

Svaka grupa ima 5 zvukova:
- **break** - kad se blok razbije
- **place** - kad se blok postavi
- **step** - kad igrač hoda po bloku
- **hit** - kad se blok udara (bez razbijanja)
- **fall** - kad igrač padne na blok

---

## Kategorije Itema

### 1. Block Items (ItemBlock)
Itemi koji postavljaju blokove kad se koriste:
- Dirt, Stone, Wood, Ores...
- Dekorativni blokovi, redstone komponente

### 2. Tools (Alati)

| Vrsta | Namjena | Ključna Svojstva |
|-------|---------|------------------|
| Pickaxe | Kopanje kamena/ruda | durability, efficiency, harvest level |
| Axe | Sječa drva | durability, efficiency, damage |
| Shovel | Kopanje zemlje/pijeska | durability, efficiency |
| Hoe | Oranje farmlanda | durability |

### 3. Weapons (Oružja)
- **Sword** - melee damage, sweep attack
- **Bow/Crossbow** - ranged, projectile shooter
- **Trident** - melee + throwable
- **Mace** - smash damage (novije verzije)

### 4. Armor (Oklop)
- Helmet, Chestplate, Leggings, Boots
- Svojstva: protection, durability, armor toughness, knockback resistance

### 5. Food (Hrana)
- Obnavlja hunger + saturation
- Neki imaju specijalne efekte (golden apple, rotten flesh)

### 6. Materials (Materijali za Crafting)
- **Ingots**: Iron, Gold, Copper, Netherite
- **Gems**: Diamond, Emerald, Amethyst
- **Resources**: Stick, String, Leather, Redstone

### 7. Utility Items
- Bucket (prazna/s tekućinom)
- Flint and Steel, Shears, Lead, Name Tag
- Maps, Compass, Clock, Spyglass

### 8. Entity Placers
- Spawn Eggs
- Boats, Minecarts
- Armor Stands, Item Frames

### 9. Projectiles/Throwables
- Snowball, Egg, Ender Pearl
- Splash/Lingering Potions

---

## Podaci Koji Opisuju Item

| Komponenta | Tip | Opis |
|------------|-----|------|
| **max_stack_size** | int (1-64) | Koliko stane u stack (tools=1, eggs=16, većina=64) |
| **durability** | int | Max trajnost prije pucanja (samo tools/armor) |
| **damage** | float | Dodatna šteta u napadu |
| **enchantable** | bool + slots | Može li se enchantati i koje vrste |
| **rarity** | enum | Common/Uncommon/Rare/Epic (određuje boju teksta) |
| **food** | nutrition + saturation | Koliko hrani igrača |
| **cooldown** | float (sec) | Vrijeme čekanja između korištenja |
| **fuel** | int (ticks) | Koliko dugo gori u furnace-u |
| **wearable** | slot enum | Gdje se nosi (head/chest/legs/feet) |
| **fire_resistant** | bool | Preživljava vatru/lavu |
| **block_placer** | block_id | Koji block postavlja (za BlockItems) |
| **entity_placer** | entity_id | Koju entitetu spawna |
| **icon** | texture path | 2D ikona u inventoryu |
| **glint** | bool | Ima li enchantment sjaj |

---

## Primjeri Konkretnih Blokova

### Stone
```
Type: Stone
Hardness: 1.5
BlastResistance: 6.0
HarvestLevel: 1 (stone pickaxe minimum)
PreferredTool: Pickaxe
SoundType: Stone
LightLevel: 0
Transparent: false
Gravity: false
Drops: [Cobblestone]  // Ne dropa sebe!
```

### Oak Log
```
Type: OakLog
Hardness: 2.0
BlastResistance: 2.0
HarvestLevel: 0 (bilo koji alat)
PreferredTool: Axe
SoundType: Wood
LightLevel: 0
Transparent: false
Gravity: false
Drops: [OakLog]
```

### Obsidian
```
Type: Obsidian
Hardness: 50.0
BlastResistance: 1200.0
HarvestLevel: 3 (diamond pickaxe minimum)
PreferredTool: Pickaxe
SoundType: Stone
LightLevel: 0
Transparent: false
Gravity: false
Drops: [Obsidian]
```

### Glowstone
```
Type: Glowstone
Hardness: 0.3
BlastResistance: 0.3
HarvestLevel: 0
PreferredTool: None (bilo što)
SoundType: Glass
LightLevel: 15
Transparent: true
Gravity: false
Drops: [GlowstoneDust, 2-4]
```

### Sand
```
Type: Sand
Hardness: 0.5
BlastResistance: 0.5
HarvestLevel: 0
PreferredTool: Shovel
SoundType: Sand
LightLevel: 0
Transparent: false
Gravity: true  // Pada!
Drops: [Sand]
```

---

## Izvori

- [Minecraft Wiki - Category:Blocks](https://minecraft.wiki/w/Category:Blocks)
- [Minecraft Wiki - Breaking](https://minecraft.wiki/w/Breaking)
- [Minecraft Wiki - Tiers](https://minecraft.wiki/w/Tiers)
- [Minecraft Wiki - Block components](https://minecraft.wiki/w/Block_components)
- [Minecraft Wiki - Block sound type](https://minecraft.wiki/w/Block_sound_type)
- [Minecraft Wiki - Item](https://minecraft.wiki/w/Item)
- [Minecraft Wiki - Item components](https://minecraft.wiki/w/Item_components)
- [Bedrock Wiki - Blocks as Items](https://wiki.bedrock.dev/blocks/blocks-as-items)
