# Spec tekstura za blokove

Popis PNG datoteka koje treba nacrtati. Kad su gotove, ubaci ih u
`Content/Blocks/Textures/` i pokreni `Scripts/build_block_materials.py` — materijali
se generiraju sami.

## Pravilo imenovanja

```
T_<Blok>_Top.png
T_<Blok>_Side.png
T_<Blok>_Bottom.png
```

**Svaki blok treba sve tri datoteke.** Nema fallbacka i nema posuđivanja tekstura
između blokova — ime datoteke jednoznačno određuje kojem bloku i kojoj strani
pripada, pa se iz popisa datoteka vidi cijela istina o izgledu bloka.

Blok koji je sa svih strana isti (kamen, daske) svejedno treba sve tri. Izvezi isti
crtež tri puta.

Ako bloku nedostaje ijedna od tri, skripta ga **preskoči cijelog** i ispiše koja
datoteka fali. Blok ostaje siv. Nema polovično postavljenih materijala.

## Pravila za crtanje

- **16 x 16 px**, RGBA PNG. Ne miješaj rezolucije između blokova.
- **Rubovi se moraju bešavno spajati.** Blokovi stoje jedan do drugoga.
  `_Top` mora tileati u oba smjera, `_Side` vodoravno (susjedni blok) i
  okomito (stup zemlje). U Asepriteu uključi `View > Tiled Mode > Both Axes`
  i crtaj s time uključenim — šav odmah vidiš.
- **Bez sjena i bez usmjerenog svjetla** — to radi engine.
- **Bez tamnog okvira** oko pločice.
- 3–5 nijansi po materijalu je dovoljno.

## Popis — 10 blokova, 30 datoteka

### Dirt
```
T_Dirt_Top.png       smeđa zemlja
T_Dirt_Side.png      isto
T_Dirt_Bottom.png    isto
```

### Stone
```
T_Stone_Top.png      siva, blaga zrnatost
T_Stone_Side.png     isto
T_Stone_Bottom.png   isto
```

### Grass
```
T_Grass_Top.png      čisto zelena
T_Grass_Side.png     zemlja, gore 3-4 px zeleni rub s neravnim prijelazom
T_Grass_Bottom.png   zemlja, bez zelenog ruba
```

Zeleni rub na `_Side` mora biti na **gornjem** rubu slike. Ako se u editoru pokaže
na krivom bridu na nekoj od četiri strane, javi — to je poznat rizik s UV-ovima
engine kocke i rješavamo ga zasebno.

### OakLog
```
T_OakLog_Top.png     godovi, koncentrični krugovi
T_OakLog_Side.png    kora, okomite linije, mora tileati okomito
T_OakLog_Bottom.png  godovi
```

### BirchLog
```
T_BirchLog_Top.png     godovi, svjetliji
T_BirchLog_Side.png    bijela kora s crnim crticama
T_BirchLog_Bottom.png  godovi
```

### OakLeaves — s prozirnošću
```
T_OakLeaves_Top.png
T_OakLeaves_Side.png
T_OakLeaves_Bottom.png
```

### BirchLeaves — s prozirnošću
```
T_BirchLeaves_Top.png
T_BirchLeaves_Side.png
T_BirchLeaves_Bottom.png
```

Lišće ide kroz `M_VoxelBlock_Masked` (alpha cutout, two-sided). Alpha mora biti
**binarna** — piksel je ili pun ili potpuno proziran, bez polutonova. Cutoff je na
0.333. Probuši oko 15–25 % piksela, raspoređeno nepravilno.

### OakPlanks
```
T_OakPlanks_Top.png     daske, vidljive linije
T_OakPlanks_Side.png    isto
T_OakPlanks_Bottom.png  isto
```

### BirchPlanks
```
T_BirchPlanks_Top.png     daske, svjetlije od hrasta
T_BirchPlanks_Side.png    isto
T_BirchPlanks_Bottom.png  isto
```

### CraftingTable
```
T_CraftingTable_Top.png     mreža 3x3 na dasci
T_CraftingTable_Side.png    daske s alatom
T_CraftingTable_Bottom.png  obične daske
```

Napomena: pravi Minecraft ima različitu prednju i bočnu stranu. Naš materijal
razlikuje samo gore/bok/dolje, pa sve četiri strane izgledaju isto. Za prednju
stranu treba rotacija bloka — vidi `BLOCK_TEXTURES_AND_MATERIALS.md`.

## Postavke importa

Ne diraj ih ručno. Skripta ih postavlja sama na svakoj teksturi u mapi:

- `Filter = Nearest` — bez ovoga je 16x16 mutna mrlja
- `Compression = UserInterface2D (RGBA)` — BC kompresija radi u blokovima 4x4 px
  i vidljivo uništava 16x16 sliku
- `sRGB = on`
- `Mip Gen = FromTextureGroup`, `LOD Group = World`

Mipovi su namjerno uključeni. Bez njih blokovi u daljini titraju kroz TSR.
Ako želiš čisti Minecraft izgled bez mipova, prebaci `GENERATE_MIPS = False`
na vrhu skripte.

## Radni tijek — blok po blok

`BlockRegistry.cpp` već pokazuje na `MI_*` putanje. Blok čiji `MI_` još ne postoji
dobije **default sivi materijal** i pojavi se u logu kao `[PERF] NEUSPJEH`. To je
očekivano stanje, ne greška — svijet je siv dok ne stignu teksture.

Za svaki blok:

1. Nacrtaj sve tri PNG datoteke tog bloka i ubaci ih u `Content/Blocks/Textures/`
2. Pokreni skriptu (Output Log > Python):
   `exec(open(r"C:/RVS/C++GameDev/MinecraftClone/Scripts/build_block_materials.py").read())`
3. Skripta izgradi `MI_` samo za blokove koji imaju sve tri; ostale preskoči i ispiše
   koja datoteka fali
4. Pokreni igru — taj blok je otekstiran, ostali su i dalje sivi

Skripta se smije pokretati koliko god puta. Prepisuje samo ono što se promijenilo.

Nema koraka rekompilacije — putanje u kodu su fiksne, mijenjaju se samo asseti.

## Provjera da je blok stvarno prošao

U logu tražiš dvije stvari:

```
[BlockMaterials] MI_Stone  <-  Top=T_Stone_Top, Side=T_Stone_Side, Bottom=T_Stone_Bottom
```

i da `MI_Stone` **nestane** iz `[PERF] NEUSPJEH` linija pri pokretanju igre.
