# PLAN: Treći biom — Stony Peaks (kameni vrhovi)

> **STATUS (2026-11-23): PLAN**, ništa još nije implementirano.

## Što je Stony Peaks u Minecraftu

Stony Peaks je topliji planinski biom koji se generira na najvišim točkama okolnog
krajolika. Nema sloj zemlje ni trave: površina je goli kamen, kroz koji se provlače
vodoravne trake kalcita, a dio kamena zamijenjen je mrljama šljunka. U Javi se na
vrhovima još pojavljuju andezit i granit. Na njemu ne raste drveće, nema snijega
(temperatura 1.0), a izdvaja se po smaragdnoj rudi i po tome što su željezo i ugljen
češće izloženi na površini. Pasivni mobovi se ne spawnaju, hostile mobovi da.
Boje bioma su grass `#9ABE4B` i foliage `#82AC1E`. Najčešće graniči sa šumom,
savanom i ravnicom, a u hladnijim krajevima prelazi u Jagged/Frozen Peaks.
(Izvor: minecraft.wiki/w/Stony_Peaks.)

Za našu igru iz toga je bitno troje: biom mora biti **visok**, mora biti **od kamena
umjesto trave** i mora nastati **tamo gdje je teren najviši**, a ne kao slučajna mrlja.

## Što naša igra danas ima

Biom danas utječe samo na boju. `FBiomeGenerator` dijeli jedan niskofrekventni noise
na jednake pragove (Plains, Desert), a `AVoxelWorld` iz tog indeksa uzima tint trave.
Generacija chunka (`FChunkGenerator`) za biom uopće ne zna: svaki stupac dobije Grass
na vrhu, tri sloja Dirta i Stone ispod, a stabla se postavljaju jednako svuda. Polja
`treeDensity`, `surfaceBlock` i `heightAmplitudeScale` u `Biomes.json` postoje, ali ih
nitko ne čita.

Svijet je bio visok 48 blokova (Z od −24 do 23), uz samo 17 blokova zraka do stropa,
što nije dovoljno za planinu. Zato je prvo povišen na 256 blokova (vidi Odluke).

Blokovi Stone i Andesite već postoje. Calcite i Gravel ne postoje. Teksture se mogu
izvući iz Minecraft jara istim putem kojim to već radi `import_block_cracks.py`.

## Odluke

**Stony Peaks se ne bira kao treći jednaki prag postojećeg noisea.** Kad bi se biom
samo dodao u `Biomes.json`, dobio bi trećinu svijeta kao ravnu zelenu površinu, jer
generacija biom ne čita. Čak i kad bi čitala, prag bi na granici odrezao teren u
vertikalnu stijenu, a s tri jednaka pojasa Plains i Peaks se nikad ne bi dodirivali.
Umjesto toga uvodi se poseban, još niže frekventni **noise planina**. Njegova vrijednost
podiže teren postupno: što je veća, to je stupac viši. Iznad zadanog praga stupac
pripada Stony Peaksu, ispod njega vrijedi današnji odabir Plains/Desert. Granica bioma
tako postaje izohipsa na padini planine, a podnožje ostaje travnato, što odgovara
Minecraftu gdje vrhovi nastaju na najvišem terenu.

**Prijelaz visine mora biti gladak.** Podizanje počinje ispod praga bioma (smoothstep
preko jednog raspona noise vrijednosti), pa na granici nema skoka. Teren planine je
zbroj današnje visine i podizanja, tako da sitne neravnine ostaju i na padini.

**Svijet je povišen s 48 na 256 blokova (2026-11-23, IMPLEMENTIRANO).** Z ide od −128
do 127, tlo je i dalje oko Z=0. Chunk je narastao s 12 KB na 64 KB. Soak test prolazi
(`FAIL=0`, `RETAINED OK`), mobovi su na navmeshu. Izgradnja vizuala za 49 bootstrap
chunkova narasla je na 528 ms jer `IsBlockExposed` za svaki zakopani blok radi šest
lookupova kroz mapu chunkova. `ApplyChunkVisuals` zato sada koristi
`IsBlockExposedInChunk`, koji susjede unutar chunka čita izravno iz arraya. S tim je
izmjereno 178–306 ms (3,6–6,2 ms po chunku, ispod streaming budžeta od 8 ms), uz
baseline na 48 slojeva od 62–237 ms. Visina NavMesh volumena u levelu još nije
provjerena za planine.

**Visine planina.** Tlo ravnice je na Z=0 uz njihaj ±6 (`SurfaceLevel` 0,
`HeightAmplitude` 6), a strop je na Z=127. Najveće podizanje planine postavlja se na
oko 70 blokova, pa vrhovi dosežu približno Z=76, uz više od 50 blokova zraka za
gradnju. Granica Stony Peaksa počinje na podizanju od oko 30 blokova, tako da donja
polovica padine ostaje travnata. To odgovara omjeru u Minecraftu, gdje vrhovi stoje
60–130 blokova iznad mora. Obje brojke su polazne vrijednosti i ulaze kao properties
na `AVoxelWorld`, a konačno se podešavaju iz `[BIOME]` ispisa.

**Biom ulazi u generaciju.** `FChunkGenParams` dobiva biome offsete, scale, parametre
noisea planina i kopiju potrebnih polja svih bioma (worker ne smije čitati registry).
Odabir bioma i visina prelaze u jednu čistu funkciju koju zovu i generacija i
`AVoxelWorld::GetBiomeAt`. To je nužno: ako tint i generacija ne bi koristili isti kod,
trava bi na granici dobila boju krivog bioma.

**Površina se čita iz JSON-a.** Biom definira površinski blok, blok ispod njega i
dubinu tog sloja. Plains i Desert ostaju Grass/Dirt/3, Stony Peaks dobiva Stone/Stone/0.
Biom uz to može navesti **blok traka** (Calcite) i **blok mrlja** (Gravel). Trake su
vodoravni slojevi po Z, blago pomaknuti 2D noiseom da ne izgledaju kao ravnalo. Mrlje
su 2D noise koji na najgornjem bloku zamijeni kamen šljunkom. Andezit se može ubaciti
kao drugi blok traka, ali to je opcionalno.

**Stabla čitaju `treeDensity`.** Kandidat stabla odbacuje se ako je vjerojatnost bioma
na njegovoj bazi nula. Stony Peaks ima 0. Odluka ne smije mijenjati broj izvlačenja iz
streama, kako ne bi pomaknula stabla u ostalim biomima.

**Gravel je za sada obični blok.** Minecraftov šljunak pada kad ispod njega nema
oslonca. Igra nema sustav padajućih blokova, pa to ide u backlog.

## Izvan opsega

Smaragdna ruda i rude općenito, goat, snijeg te Jagged i Frozen Peaks nisu dio ovog
plana. Spawn mobova po biomu ovisi o otvorenom per-chunk spawnu (checkpoint E). Kad on
postoji, Stony Peaks dobiva samo hostile mobove. Do tada treba provjeriti gdje
današnji spawn postavlja ovce i svinje i, ako ih stavlja na vrhove, isključiti biome
s `treeDensity` nula ili dodati zasebno polje `passiveMobs`.

## Redoslijed implementacije

1. **Novi blokovi.** Calcite i Gravel se dodaju na kraj `EBlockType` i `EItemType`, u
   `Blocks.json` i `Items.json` (Calcite: krampa, sound grupa Stone; Gravel: lopata,
   sound grupa Gravel). Teksture `calcite.png` i `gravel.png` se izvlače iz jara pa
   `build_block_materials.py` i `generate_item_sprites.py` naprave materijale i ikone.
   Provjera: oba bloka se pojave u showcase redu. Odluka 2026-11-23: šljunak ne
   ispušta kremen i ne pada, a kalcit koristi zvukove grupe Stone. Enumi i JSON
   zapisi su dodani, teksture čekaju import u editoru.
2. **Viši svijet — GOTOVO.** `WorldHeight` je 256 (Z −128..127), soak prolazi.
   Otvoreno: visina NavMesh volumena u levelu.
3. **Biom u generaciji.** Parametri bioma ulaze u `FChunkGenParams`, a odabir bioma
   postaje zajednička funkcija za generaciju i tint. Teren se još ne mijenja.
   Provjera: isti seed daje isti raspored boja kao prije.
   **GOTOVO (2026-11-23).** Visina i biom imaju po jednu formulu u `FChunkGenerator`
   (`GetTerrainHeight`, `GetBiomeIndex`); `AVoxelWorld` ih samo poziva. Generator
   uz blokove zapisuje biom svakog stupca u chunk (`FVoxelChunk::Biomes`, 256 B),
   a tint ga čita odatle. Izmijenjeni chunk iz memorije dobiva biome ponovno
   izračunate. `[BIOME]` u logu (ispis prije i poslije bootstrapa) dao je isti
   rezultat kao stari kod: Plains=35628, Desert=29908, CRC stupaca `FDCCB0A5`,
   CRC terena `8DD5368A`, 0 razlika između spremljenog bioma i formule. Soak
   prolazi (`FAIL=0`, `RETAINED OK`).
4. **Noise planina i visina.** Dodaje se noise planina sa smoothstep podizanjem i
   pragom za Stony Peaks. Biom se upisuje u `Biomes.json` (grass `#9ABE4B`, foliage
   `#82AC1E`, `treeDensity` 0). Privremeni log `[BIOME]` ispisuje broj stupaca po
   biomu i najvišu visinu u učitanom području.
5. **Površina, trake i mrlje.** Generacija čita površinski blok, sloj ispod njega,
   dubinu sloja, blok traka i blok mrlja iz definicije bioma.
6. **Stabla po biomu.** Kandidati stabala čitaju `treeDensity`.
7. **Test.** Prvo headless run, u kojem `[BIOME]` mora pokazati sva tri bioma, a soak
   test mora i dalje prolaziti. Zatim ručna provjera u editoru komandom `voxel.tp`:
   padina je bez stijene na granici, vrh je kamen s trakama kalcita, na vrhovima nema
   stabala i kopanje radi na novoj visini. Ako komanda za skok do najbližeg vrha
   zatreba, opisat ću je, a dodaješ je ti.

## Rizici

Povisivanje svijeta dira sve što pretpostavlja 48 blokova: navmesh, spawn igrača na
površini, soak CRC-ove i budžet primjene chunka (više instanci po chunku na planinama).
Zato je to zaseban korak s vlastitom provjerom prije ikakve promjene terena.

Drugi rizik je veličina planina. Noise planina s preniskom frekvencijom daje rijetke
planine koje igrač nikad ne vidi, a s previsokom daje kamene točkice. Scale i prag se
zato podešavaju iz `[BIOME]` brojki, a ne odokativno.
