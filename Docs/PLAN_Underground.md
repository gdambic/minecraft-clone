# PLAN: Podzemlje (špilje, slojevi kamena, rude)

Status: sve tri faze implementirane i izmjerene headless 2026-11-27. Špilje su u editoru
potvrđeno mračne. Ostaje ručni pregled novih blokova i ruda u editoru. Ništa od ovoga još nije u kodu.
Datum: 2026-11-27

## 1. Što igrač dobiva

Danas je ispod trave i zemlje samo puni kamen do dna svijeta. Nakon ovog plana
igrač koji kopa prema dolje nailazi na špilje: velike dvorane sa stupovima,
duge tunele koji ih povezuju i uske prolaze. Na površini se mjestimice vide
otvori kroz koje se u špilje može ući pješice. Kamen više nije jednoličan: u
njemu su nakupine granita, diorita, andezita, zemlje i šljunka, dublje počinje tamniji
kamen, a samo dno svijeta je sloj koji se ne može probiti. U kamenu su i rude,
svaka na svojoj dubini.

Špilje su mračne. Svijet ostaje beskonačan i dalje se računa chunk po chunk.

## 2. Kako to radi u Minecraftu (provjereno u 1.21.11 jaru)

Brojke su pročitane iz datoteka `data/minecraft/worldgen/` u jaru, ne iz
wikija. Minecraftova visina Y i naša visina Z razlikuju se za 63: razina mora
je kod njih na 63, kod nas na 0. Njihovo dno svijeta je 127 blokova ispod mora,
naše 128, pa se sve dubine mogu prenijeti gotovo jedan na jedan. U nastavku su
dubine već preračunate u naše koordinate.

### Špilje

Minecraft za svaki blok pod zemljom odlučuje hoće li ondje biti kamen ili
prazan prostor. Odluku donosi pomoću šuma. Šum je funkcija koja svakom mjestu u
svijetu dodijeli broj između otprilike −1 i 1, tako da susjedna mjesta imaju
slične brojeve. Može se zamisliti kao karta brda i dolina, samo u tri
dimenzije. Minecraft ima više takvih karata, svaku za drugu namjenu.

Postoje tri vrste špilja.

Dvorane su velike šupljine. Igra pogleda jednu kartu šuma i isprazni svaki blok
na kojem je broj ispod zadanog praga. Brojevi se na toj karti mijenjaju sporo,
pa su ispražnjena područja velika i međusobno udaljena, otprilike 250 blokova.
Druga karta u dvoranama ostavlja kamene stupove od poda do stropa.

Tuneli su duge zavojite cijevi. Igra uzme dvije karte šuma i isprazni samo one
blokove na kojima su obje istodobno vrlo blizu nule. Takva mjesta nisu mrlje
nego tanke niti koje vijugaju kroz kamen, i to su tuneli. Treća karta određuje
koliko je tunel na kojem mjestu širok.

Uski prolazi nastaju isto kao tuneli, samo je dopušteno odstupanje od nule
manje, pa su široki jedan do dva bloka. Ima ih samo u otprilike pola svijeta.

Kad bi se ova tri pravila primjenjivala sve do površine, špilje bi probijale
tlo na svakom mjestu gdje mu se približe i teren bi bio pun rupa. Zato u sloju
tik ispod površine ne vrijede. Ondje Minecraft primjenjuje zasebno, strože
pravilo, koje isprazni vrlo malo blokova. Ono što isprazni su ulazi: rijetki
otvori kroz koje se s površine može sići u špilje. Što je teren niži, ulaza je
manje.

Pri dnu svijeta špilja također nema: u donja 24 bloka kamen je sve puniji što
se ide dublje.

Minecraft uz to ima i stariji način izrade špilja, "carvere". Carver krene iz
jedne točke i kopa crvoliki tunel ili kanjon, do osam chunkova daleko. Tunel
kreće iz 15 % chunkova, kanjon iz 1 %.

### Voda i lava u špiljama

Špilje nisu sve suhe. Minecraft svijet dijeli na područja od nekoliko desetaka
blokova i svakom dodjeljuje vlastitu razinu vode, pa neke špilje imaju jezera,
a susjedne su suhe. Na dubini ispod −117 sve šupljine do te visine pune su
lave.

### Slojevi kamena

Dno svijeta čini neprobojni kamen (bedrock): najdonji sloj je uvijek pun, a
sljedeća četiri sve rjeđe. Ispod dubine −63 običan kamen zamjenjuje tamniji
(deepslate), s prijelazom razvučenim kroz osam blokova da granica ne bude ravna
crta.

Raznolikost kamena dolazi od nakupina. Svaki chunk pokuša postaviti zadani broj
nakupina svake vrste na nasumičnoj visini unutar zadanog raspona. Nakupina
zamjenjuje samo kamen, nikad zrak.

| Nakupina | Pokušaja po chunku | Veličina | Dubina (naše koordinate) |
|---|---|---|---|
| Zemlja | 7 | do 33 bloka | od −63 naviše |
| Šljunak | 14 | do 33 bloka | cijela visina |
| Granit, diorit, andezit | po 2 | do 64 bloka | −63 do −3 |
| Granit, diorit, andezit | 1 u svakom šestom chunku | do 64 bloka | 1 do 65 |
| Tuf | 2 | do 64 bloka | ispod −63 |

### Rude

Rude su iste takve nakupine, samo manje. Raspodjela po visini je ili
ravnomjerna ili trokutasta (najgušće u sredini raspona, prema rubovima sve
rjeđe).

| Ruda | Pokušaja po chunku | Veličina | Dubina (naše koordinate) |
|---|---|---|---|
| Ugljen | 20 | do 17 | −63 do 127, najgušće oko 33 |
| Ugljen, planinski | 30 | do 17 | iznad 73 |
| Željezo | 10 | do 9 | −87 do −7, najgušće oko −47 |
| Željezo, sitno | 10 | do 4 | ispod 9 |
| Željezo, planinsko | 90 | do 9 | iznad 17, najgušće pri vrhu svijeta |
| Bakar | 16 | do 10 | −79 do 49, najgušće oko −15 |
| Zlato | 4 | do 9 | −127 do −31, najgušće oko −79 |
| Redstone | 4 + 8 | do 8 | ispod −48, gušće prema dnu |
| Lapis | 2 | do 7 | −95 do −31, najgušće oko −63 |
| Dijamant | 7 | do 4 | ispod −47, sve gušće prema dnu |

Dijamant, zlato i dubinski ugljen imaju dodatno pravilo: nakupina koja bi dodirivala zrak
špilje u pola slučajeva se odbacuje, pa ih se rjeđe vidi na zidovima.

Minecraft ima još nekoliko stvari u podzemlju koje ovaj plan ne obuhvaća:
velike žile bakra i željeza, podzemna jezera lave, izvore vode i lave na
zidovima, tamnice s čudovištima, geode i napuštene rudnike.

## 3. Kako to uvodimo u našu igru

### Špilje

Naš teren nije trodimenzionalna gustoća nego visina po stupcu, i to ostaje.
Špilje dodajemo kao drugi korak: nakon što se stupac napuni kamenom, za svaku
ćeliju pitamo novu funkciju je li izdubljena. Ta funkcija ovisi samo o seedu i
koordinatama, kao i visina terena, pa je svaki chunk i dalje može izračunati
sam, a isti seed uvijek daje iste špilje.

U nju prenosimo Minecraftove formule za dvorane sa stupovima, tunele, uske
prolaze i ulaze, s istim pragovima i istim mjerilima šuma. Naš šum nije isti
kao Minecraftov, pa svijet neće biti identičan nijednom Minecraft seedu, ali će
oblici i veličine špilja biti isti. Carvere (crvolike tunele i kanjone) u prvoj
fazi ne radimo.

Minecraft sloj "tik ispod površine" određuje na način koji ovisi o njegovu
računanju terena, a naše je drukčije. Kod nas će to jednostavno biti zadani
broj blokova ispod površine: u tom sloju nastaju samo ulazi, a ispod njega sve
tri vrste špilja.

Računanje nekoliko trodimenzionalnih šumova za svaku od šezdesetak tisuća
ćelija chunka bilo bi presporo. Minecraft to rješava tako da gustoću računa
samo u čvorovima rijetke rešetke (svaka 4 bloka vodoravno i 8 okomito), a
između čvorova vrijednost procjenjuje iz susjednih. Radimo isto.

### More

Naša voda nije blok nego ploha, a more je po dogovoru svaka prazna ćelija iznad
dna do razine mora. Špilja koja bi probila morsko dno ostala bi suha rupa pod
vodom. Zato u prvoj fazi vrijedi pravilo: ispod mora i uz obalu ostaje netaknuta
kora od nekoliko blokova, a ulazi u špilje otvaraju se samo ondje gdje je
površina iznad razine mora. Špilje smiju prolaziti duboko ispod mora, ali ga ne
dodiruju. Sve su špilje u prvoj fazi suhe.

### Što se još mora prilagoditi

Stabla, spawn igrača i mobova te teleport danas pretpostavljaju da je na vrhu
stupca čvrsto tlo. Nakon špilja to ne vrijedi na mjestu ulaza, pa svako od tih
mjesta mora dodatno provjeriti da tlo nije izdubljeno.

Teren se crta tako da se prikazuju samo blokovi koji dodiruju zrak. Špilje će
povećati broj takvih blokova, a time i trajanje izgradnje chunka. Koliko, ne
znam dok se ne izmjeri; mjerenje je dio prve faze.

### Blokovi

Uvodimo prave Minecraftove blokove, s teksturama iz jara. Što koji blok ispušta
pročitano je iz `loot_table/blocks` u jaru.

Novi blokovi kamena su Cobblestone, Deepslate, Cobbled Deepslate, Bedrock,
Granite, Diorite i Tuff. Andesite, Dirt i Gravel već imamo. Stone se mijenja:
kad se iskopa, više ne ispušta sam sebe nego Cobblestone, a Deepslate jednako
tako ispušta Cobbled Deepslate. Gravel u 10 % slučajeva umjesto sebe ispusti
Flint. Bedrock se ne može iskopati. Ostali ispuštaju sami sebe.

Ruda je osam, a svaka postoji kao dva bloka, jedan u običnom kamenu i jedan u
deepslateu, pa je to 16 novih blokova. Obje inačice ispuštaju isto.

| Ruda | Što ispusti |
|---|---|
| Coal Ore | 1 Coal |
| Iron Ore | 1 Raw Iron |
| Copper Ore | 2–5 Raw Copper |
| Lapis Ore | 4–9 Lapis Lazuli |
| Gold Ore | 1 Raw Gold |
| Redstone Ore | 4–5 Redstone |
| Diamond Ore | 1 Diamond |
| Emerald Ore | 1 Emerald |

Emerald Ore u Minecraftu nastaje samo u planinskim biomima, pa je kod nas samo
u Stony Peaksu.

Novi predmeti su Cobblestone, Cobbled Deepslate, Flint, Coal, Raw Iron, Raw
Copper, Raw Gold, Redstone, Lapis Lazuli, Diamond i Emerald, te predmeti za
Deepslate, Granite, Diorite i Tuff da se ti blokovi mogu nositi i postaviti.

Minecraft za dio ruda traži određenu krampu; bez nje se blok razbije, ali ne
ispusti ništa. Iron, Copper i Lapis traže kamenu krampu ili bolju, a Gold,
Redstone, Diamond i Emerald željeznu ili bolju (`tags/block/needs_stone_tool`
i `needs_iron_tool` u jaru). Coal traži bilo koju krampu. To pravilo uvodimo
odmah i u cijelosti. Kamene i željezne krampe još nemamo, pa se te rude zasad
ne mogu dobiti; postat će dostupne kad krampe dođu, bez ikakve izmjene ruda.
Vrijeme kopanja svakog bloka čita se iz jara (tvrdoća bloka u klasi `Blocks`).

Blokovi Basalt, Limestone i Calcite ostaju u igri, ali ih podzemlje ne koristi.

### Slojevi kamena

Najdonjih pet slojeva svijeta postaje Bedrock po Minecraftovom pravilu: najdonji
sloj je pun, a sljedeća četiri sve rjeđa. Ispod dubine −63 Stone zamjenjuje
Deepslate, s prijelazom razvučenim kroz osam blokova.

### Nakupine i rude

Nakupine kamena i rude postavljaju se istim postupkom kojim već postavljamo
stabla preko granice chunka: svaki chunk izračuna nakupine za sebe i svojih osam
susjeda, a upiše samo blokove koji padaju u njega. Tako se nakupina na granici
sastavi jednako bez obzira koji se chunk prvi generirao.

Nakupina zamjenjuje samo kamen, nikad zrak špilje. Ruda koja padne u Deepslate
ili Tuff upisuje se kao svoja deepslate inačica.

### Konfiguracija u JSON-u

Sve što se tiče podzemlja čita se iz nove datoteke
`Content/Data/WorldGen/Underground.json`, tako da se nova ruda ili drukčija
gustoća špilja uvodi bez mijenjanja koda.

Datoteka ima tri dijela. Prvi su postavke špilja: je li koja vrsta špilja
uključena, njezin prag i mjerilo šuma, te debljina sloja ispod površine u kojem
nastaju samo ulazi. Drugi su slojevi: dubina na kojoj počinje Deepslate, širina
prijelaza i broj slojeva Bedrocka.

Treći dio je popis nakupina, i on je najvažniji za proširivanje. Svaka nakupina
je jedan unos koji navodi ime, blok koji se postavlja, njegovu deepslate
inačicu (ako je ima), koje blokove smije zamijeniti, koliko puta po chunku se
pokušava (ili u svakom kojem chunku, za rijetke), najveću veličinu, najnižu i
najvišu dubinu, je li raspodjela po dubini ravnomjerna ili trokutasta, koliko
se često odbacuje ako dodiruje zrak, te po želji popis bioma u kojima se
pojavljuje. Nova ruda se uvodi tako da se doda blok u `Blocks.json`, predmet u
`Items.json` i jedan takav unos ovdje.

Početni sadržaj datoteke su Minecraftove vrijednosti iz točke 2, preračunate u
naše dubine.

### Svjetlo

Špilje trebaju biti mračne. Baklje dolaze naknadno i nisu dio ovog plana. Kako
špilja doista izgleda iznutra vidi se tek u editoru nakon prve faze; ako ne
bude mračna, popravak ulazi u prvu fazu.

## 4. Redoslijed rada

Prva faza su suhe špilje i datoteka s konfiguracijom. Ne traži nijedan novi
blok ni teksturu, a mijenja najviše: generaciju, pravilo za more, stabla i
spawn. U njoj nastaju i komande `voxel.slice` i `voxel.cavetest`. Završava
mjerenjem i ručnim obilaskom u editoru.

Druga faza su blokovi kamena: teksture iz jara, novi blokovi i predmeti,
Bedrock na dnu, Deepslate u dubini, nakupine i novi dropovi za Stone i Gravel.

Treća faza su rude: 16 blokova, njihovi predmeti i unosi u konfiguraciji.

Odgođeno, za zaseban plan: voda i lava u špiljama, baklje, kamena i željezna
krampa, peć i taljenje, mobovi u špiljama,
carveri, podzemne građevine.

## 5. Kako provjeravamo

Špilje se ne mogu ocijeniti iz loga, a u headless runu nema slike. Zato dodajem
komandu `voxel.slice`, koja okomiti presjek svijeta kroz položaj igrača snimi
kao sliku (svaki blok jedan piksel u boji svog tipa). Tu sliku mogu pregledati
sam nakon headless runa, a vi je možete otvoriti bez pokretanja editora. Na njoj
se odmah vidi jesu li dvorane prevelike, ima li tunela i probija li što morsko
dno.

Druga komanda, `voxel.cavetest`, ispisuje u log linije `[CAVE]`: udio šupljih
ćelija po pojasevima dubine, broj otvora na površini, broj ćelija mora koje
dodiruju zrak špilje (mora biti 0) i broj blokova svake rude po chunku u
usporedbi s očekivanim iz tablice.

Postojeće provjere moraju i dalje prolaziti: `-VoxelSoakTest` (isti seed daje
isti svijet), `[WATER] ... OK`, `voxel.flytest` (izgradnja chunkova ne kasni za
igračem u sprint-letu) i `[PERF]` vrijeme generacije prije i poslije.

## 6. Stanje prve faze (2026-11-27)

Špilje su u `FCaveGenerator` (`CaveGenerator.h`), a `FChunkGenerator` ih primjenjuje
nakon što napuni stupce. Šum je vlastita izvedba Minecraftovog `NormalNoise` (formule
faktora pročitane iz bytecodea), pa pragovi iz jara vrijede bez prilagodbe.

Postavke u `Content/Data/WorldGen/Underground.json`, dio `caves`: `enabled` gasi sve
špilje; `chambers`, `tunnels`, `noodles` i `entrances` imaju svaki svoj `enabled`;
`chambers.offset` (0.27) i `entrances.offset` (0.37) su Minecraftovi pragovi, veći broj
znači manje dvorana odnosno ulaza; `chambers.pillars` gasi stupove; `sizeScale` množi
veličinu svih špilja; `surfaceFactor` (5.47) određuje debljinu sloja ispod površine u
kojem nastaju samo ulazi, a ona iznosi 50 podijeljeno s tim brojem, dakle oko 9
blokova; `seaCrustDepth` i `seaGuardRadius` su debljina kore ispod morskog dna i
udaljenost od mora na kojoj zaštita vrijedi.

Izmjereno headless na seedu 1337, na dva mjesta po 9×9 chunkova: šupljine čine oko
1.5 % terena, najviše između Z −96 i −33 (2 do 4 %), a u gornjih tridesetak blokova
ispod površine manje od 0.5 %. Otvoreno je 0.03 do 0.16 % stupaca kopna. Dodira mora i
špilje nema, stabala nad rupom nema, a upit za jednu točku daje isto što i generacija
chunka. Soak test daje isti svijet nakon regeneracije (FAIL=0), a ploha mora ima isti
broj stupaca sa špiljama i bez njih.

Cijena: generacija chunka traje 6 do 8 ms umjesto 0.4 ms (na worker threadu, sprint-let
i dalje prolazi `voxel.flytest`). Chunkovi sa špiljama imaju više vidljivih blokova
(11540 instanci umjesto 6655 u početnih 25 chunkova), pa je najsporija izgradnja
vizuala jednog chunka u soak testu porasla s 13 ms na 31 ms. To je jedan dulji frame
pri učitavanju takvog chunka. Popravak bi bio da se vizuali jednog chunka grade kroz
više frameova; nije dio ove faze.

Komande `voxel.cavetest` i `voxel.slice` opisane su u `CLAUDE.md`.

## 7. Stanje druge i treće faze (2026-11-27)

Slojevi i nakupine su u `FUndergroundGenerator` (`UndergroundGenerator.h`). Postavljanje
nakupine je prijenos Minecraftove klase `OreFeature` pročitane iz bytecodea: nakupina je
niz kugli duž kratke dužine. Jedina razlika je pravilo "odbaci ako dodiruje zrak":
Minecraft za njega vuče brojeve redom iz jednog niza, a mi broj vežemo uz ćeliju, jer
inače chunk i njegov susjed ne bi dobili isti rezultat.

`Underground.json` je dobio dva nova dijela. U `layers` su Bedrock (broj slojeva) i
Deepslate (koji blok zamjenjuje, do koje je dubine pun i gdje nestaje). U `features` je
26 nakupina iz jara, redom kojim ih Minecraft postavlja. Te dijelove upisuje
`python Scripts/build_underground_config.py`; skripta ih pregazi, pa se nakon ručnih
izmjena više ne pokreće. Emerald je ograničen na biom StonyPeaks.

Nova ruda se dodaje ovako: vrijednost na kraj `EBlockType` (i `EItemType` za drop), unos
u `Blocks.json` i `Items.json`, tekstura, pa unos u `features`. Enum traži ponovni build;
sve ostalo su podaci.

Blokovi i predmeti imaju teksture iz jara. Uvozi ih Tools > MinecraftClone > Import
Underground Blocks, što ujedno izgradi materijale i ikone.

Pravila kopanja su u `Blocks.json`: svi kameni blokovi i rude traže krampu
(`requiresTool`), a rude koje traže bolju krampu imaju `requiredToolTier` (Iron, Copper i
Lapis kamenu; Gold, Redstone, Diamond i Emerald željeznu). Drvena krampa je jedina koja
postoji, pa se od ruda može dobiti samo Coal. Stone sada ispušta Cobblestone i, kao u
Minecraftu, bez krampe ne ispušta ništa; isto vrijedi za Andesite. Gravel u 10 %
slučajeva ispusti Flint. Bedrock se ne može iskopati.

Izmjereno na seedu 1337 oko ishodišta, prosjek po chunku: CoalOre 103, IronOre 57 i
DeepslateIronOre 26, CopperOre 84, DeepslateGoldOre 24, DeepslateRedstoneOre 37,
LapisOre 13 i DeepslateLapisOre 15, DeepslateDiamondOre 27. Najdonji sloj je svugdje
Bedrock, a ispod Z −63 nema nijednog Stonea. Generacija chunka traje 7.5 ms.

## 8. Vizuali podzemlja samo oko igrača (2026-11-27)

Pravilo "blok se prikazuje ako dodiruje zrak" nastalo je dok je jedini zrak bio iznad
površine. Sa špiljama je svaki blok zida svake špilje dobivao vizual i koliziju, i 100
blokova ispod igrača: 726 blokova po chunku umjesto 262, a izgradnja vizuala chunka
13.8 ms umjesto 5.4 ms.

Sada se pri učitavanju chunka grade samo vizuali površine. Površina je sve do
`SurfaceVisualDepth` (8) blokova ispod najniže točke stupca i njegova četiri susjeda;
susjedi su u računu zato što je blok litice bočno izložen zraku nad nižim susjedom. Sve
ispod toga je podzemlje.

Podzemlje je podijeljeno na odsječke od 16 blokova po visini, pa je odsječak kocka
16×16×16. Odsječak dobiva vizuale tek kad je unutar `UndergroundVisualRadius` (2)
odsječaka od igrača, vodoravno i po visini, što znači da igrač uvijek ima izgrađeno
barem 32 bloka u svakom smjeru. Odsječak se uklanja kad je dalje od radijusa plus jedan.
Odsječci tik uz igrača grade se prije vizuala površine dalekih chunkova, da igrač koji
ulazi u špilju ne propadne kroz zid bez kolizije.

Izmjereno soak testom (890 chunkova): izgradnja vizuala chunka traje u prosjeku 7.2 ms
uz 269 blokova, najsporiji chunk 14.5 ms. Odsječak podzemlja traje u prosjeku 0.6 ms.
`voxel.deeptest` spusti igrača u špilju i provjeri da svaki izloženi blok u krugu 24
bloka ima vizual (2632 bloka, nijedan bez vizuala), pa ga vrati na površinu i provjeri da
je odsječak špilje uklonjen.

Posljedice koje ostaju otvorene su u `BACKLOG.md`, točka 0e.

## 9. Odluke (2026-11-27)

Svijet ostaje beskonačan. Špilje su mračne, baklje dolaze naknadno. Uvode se
pravi Minecraftovi blokovi i rude s podacima iz jara, a generacija podzemlja
konfigurira se JSON datotekom. Pravilo koja ruda traži koju krampu vrijedi od
početka; same krampe dolaze naknadno, pa se dio ruda dotad ne može dobiti.
