# Plan: baklje

Status: faze 1 do 4 (stanje bloka, svojstva bloka, baklja, svjetlo blokova) implementirane
2026-11-28 i potvrđene u editoru (commit 801cc6b). Faza 6 (čestice) je implementirana i
potvrđena u editoru (commit 7ff97d6). Faza 5 (svjetlo na igraču, mobovima i dropovima)
je implementirana i izmjerena bez prikaza, a u editoru još nije pregledana; vidi točku 7.

Pravilo plana: uzima se Minecraftovo ponašanje i ponavlja se. Sve u točki 2 pročitano
je iz jara 1.21.11 (podaci iz JSON datoteka, ponašanje iz bytecodea klasa `TorchBlock`,
`WallTorchBlock`, `BaseTorchBlock`, `StandingAndWallBlockItem`, `BlockPlaceContext`,
`Blocks` i `LeavesBlock`).

## 1. Cilj

Igrač od ugljena i štapa napravi baklje, postavi ih na pod ili na zid i one osvijetle
špilju, sve onako kako to radi Minecraft.

## 2. Kako to radi Minecraft

### Blok i stanje bloka

Ćelija svijeta ne pamti samo vrstu bloka, nego stanje bloka. Stanje je vrsta bloka
zajedno s vrijednostima njegovih svojstava. Stojeća baklja (`torch`) nema svojstava,
pa ima jedno stanje. Zidna baklja (`wall_torch`) je druga vrsta bloka sa svojstvom
`facing`, koje ima četiri vrijednosti, pa ima četiri stanja. Isti mehanizam koriste
deblo (os uz koju leži), peć (strana i gori li) i lišće.

Svaka vrsta bloka pri registraciji dobiva i opća svojstva. Baklja ima ova: nema
koliziju, razbija se trenutno, svijetli razinom 14, zvuk joj je drvo.

### Oblik i oslonac

Baklja ima obris za ciljanje, a nema koliziju. Obris stojeće baklje je stupić širok
4 i visok 10 piksela na sredini dna ćelije. Obris zidne baklje je kvadar širok 5
piksela, od 3. do 13. piksela visine, koji iz zida strši 5 piksela.

Stojeća baklja opstaje ako blok ispod nje može nositi nešto na sredini svoje gornje
stranice. Zidna baklja opstaje ako je stranica bloka iza nje čvrsta. Lišće ima prazan
oblik za oslonac, pa na njemu baklja ne može stajati.

Kad se promijeni susjedni blok, baklja provjeri oslonac: stojeća gleda samo blok
ispod, a zidna samo blok iza sebe. Ako oslonca više nema, baklja nestaje i ispusti
predmet.

### Postavljanje

Predmet baklje zna za obje vrste bloka. Baklja se postavlja u ćeliju uz stranicu u
koju igrač cilja. Smjerovi se zatim slažu po tome koliko je igračev pogled blizu
svakome od njih, a smjer suprotan kliknutoj stranici stavlja se na prvo mjesto.

Igra prolazi tim popisom i preskače smjer prema gore. Za smjer prema dolje pokuša
stojeću baklju. Za vodoravni smjer pokuša zidnu baklju, a zidna baklja sama prolazi
istim popisom i uzima prvi vodoravni smjer u kojem ima čvrst zid. Postavlja se prvo
stanje koje ima oslonac.

Posljedice su ove. Klik na gornju stranicu bloka daje stojeću baklju. Klik na bočnu
stranicu daje zidnu baklju na tom zidu. Klik na strop ne postavlja baklju na strop,
nego na najbliži zid ili pod uz tu ćeliju, ako ga ima.

### Model, recept, drop

Model stojeće baklje je kvadar 2×2×10 piksela na sredini dna ćelije. Zidna baklja ima
vlastiti model: isti kvadar podignut 3,5 piksela, prislonjen uz zid i nagnut 22,5°.
Model se za četiri stanja okreće za 0°, 90°, 180° i 270°. Oba koriste teksturu
`torch.png` i crtaju se bez sjenčanja.

Recept je ugljen iznad štapa i daje 4 baklje; stane u mrežu 2×2. Razbijena baklja
uvijek ispusti jednu baklju.

### Čestice

Baklja povremeno ispusti česticu dima i česticu plamena. Kod stojeće nastaju na
sredini ćelije, 0,7 bloka iznad dna. Kod zidne su 0,22 bloka više i 0,27 bloka
pomaknute prema zidu.

### Svjetlo

Svaka ćelija ima razinu svjetla blokova od 0 do 15. Baklja svojoj ćeliji daje 14.
Svjetlo se širi u šest smjerova i svakim korakom slabi za jedan, a kroz pune blokove
ne prolazi. Zato baklja osvijetli hodnik iza ugla slabije nego ravno ispred sebe, i
ne osvijetli ništa iza zida. Stranica bloka je onoliko svijetla koliko je svijetla
ćelija ispred nje.

## 3. Što naša igra danas nema

Ćelija pamti samo vrstu bloka, bez stanja.

Svaki blok se smatra punom kockom: sakriva stranice susjeda, ima koliziju i računa se
kao tlo. Nema pojma obrisa različitog od kolizije.

Blok se može postaviti bilo gdje u prazan prostor i ne ovisi o osloncu.

Vrijeme kopanja nula danas znači "ne može se iskopati". U Minecraftu nula znači
trenutno, a neuništiv blok ima −1. Bedrock u našem `Blocks.json` već ima −1.

Svjetlo je Unrealovo: sunce, nebo i odbijeno svjetlo. Razine svjetla po ćeliji ne
postoje.

## 4. Rješenje

### Stanje bloka

Chunk uz vrstu bloka za svaku ćeliju pamti i stanje. Vrsta bloka u `Blocks.json`
navodi svoja svojstva i njihove vrijednosti, a igra iz toga pri pokretanju složi popis
svih stanja. Generirani teren koristi početno stanje svake vrste, pa se generator ne
mijenja.

Crtanje bloka ovisi o stanju: `Blocks.json` za svako stanje navodi zakret modela, kao
Minecraftova datoteka `blockstates`. Model je jedan po vrsti bloka; različit model za
različita stanja iste vrste još nije podržan, jer ga baklja ne treba.

Postojeći blokovi ostaju s jednim stanjem. Deblo položeno na bok i slični slučajevi
mogu se kasnije dodati kao podaci, bez nove izmjene chunka.

### Opća svojstva bloka

`Blocks.json` dobiva Minecraftova svojstva: ima li blok koliziju, koji mu je obris za
ciljanje, koje su mu stranice čvrste za oslonac i kojom razinom svijetli. Za pune
kocke vrijede dosadašnje vrijednosti. Lišće dobiva "nijedna stranica nije čvrsta".

Blok bez kolizije ne sakriva stranice susjeda, propušta igrača, mobove i dropove, ne
računa se kao tlo i pogađa ga samo zraka nišana. Obrub fokusiranog bloka crta se po
obrisu.

Pravilo vremena kopanja postaje Minecraftovo: nula je trenutno, negativno je
neuništivo.

### Ponašanje bloka

Tri Minecraftova pitanja postaju dio naše igre: koje stanje blok dobiva pri
postavljanju, može li opstati na svom mjestu, i što radi kad mu se promijeni susjed.
Odgovore daje kod vezan uz vrstu bloka, a `Blocks.json` navodi koje ponašanje blok
koristi. Sada postoje dva ponašanja, stojeća i zidna baklja; kasnije ih koriste
sadnice, cvijeće i peć.

Svijet nakon svake izmjene bloka obavijesti šest susjeda. To pokriva i lišće koje
istrune.

### Baklja

Dvije nove vrste bloka, `Torch` i `WallTorch`, i predmet `Torch` koji zna za obje.
Postavljanje ide Minecraftovim redom smjerova iz točke 2. Recept, drop, zvuk i oba
modela su iz jara; modele i teksturu uvozi skripta iz izbornika Tools.

### Svjetlo

Chunk za svaku ćeliju pamti razinu svjetla blokova. Kad se baklja postavi ili ukloni,
ili kad se postavi ili razbije blok u blizini svjetla, razine se preračunaju
Minecraftovim širenjem. Kad se chunk učita, svjetlo se izračuna iz baklji u njemu i u
susjednim chunkovima.

Svaka nacrtana instanca bloka dobiva šest brojeva, po jedan za svaku stranicu: razinu
svjetla ćelije ispred te stranice. Materijal blokova tu razinu pretvara u vlastiti sjaj
stranice. Sunce, nebo i sjene ostaju Unrealovi, a svjetlo baklji im se pribraja.

Ovaj način ne koristi Unrealova svjetla, pa broj baklji nije ograničen i svjetlo ne
prolazi kroz zidove. Krivulju kojom Minecraft razinu pretvara u svjetlinu pročitat ću
iz jara na početku te faze.

Dvije posljedice treba znati unaprijed. Prva: svjetlo je po stranici bloka, što
odgovara Minecraftu s isključenim "Smooth Lighting". Druga: igrač, mobovi, dropovi i
predmet u ruci nisu blokovi, pa ih baklja u prvoj izvedbi neće osvijetliti. Minecraft
ih osvjetljava razinom ćelije u kojoj stoje; to je zasebna faza.

### Čestice

Plamen i dim nastaju na Minecraftovim mjestima iz točke 2, s teksturama iz jara, samo
za baklje blizu igrača. Učestalost i gibanje čestica pročitat ću iz jara na početku te
faze.

## 5. Redoslijed rada

1. Stanje bloka u chunku i crtanje po stanju. Igra izgleda isto kao prije; postojeći
   testovi moraju prolaziti.
2. Opća svojstva bloka (kolizija, obris, oslonac) i novo pravilo vremena kopanja.
3. Baklja: dvije vrste bloka, predmet, postavljanje, oslonac, recept, modeli. Nakon
   ove faze baklja se može napraviti, postaviti i razbiti, ali ne svijetli.
4. Svjetlo blokova.
5. Svjetlo na igraču, mobovima i dropovima.
6. Čestice.
7. Pregled u editoru: izgled baklje, svjetlina, čestice.

## 6. Provjera

Nova komanda `voxel.torchtest` radi bez prikaza i rezultat ispisuje s oznakom
`[TORCH]`. Provjerava sljedeće:

- Klik na gornju stranicu daje stojeću baklju, klik na bočnu zidnu baklju pravog
  smjera, a baklja se ne može postaviti u zrak ni na lišće.
- Blokovi oko baklje i dalje su nacrtani i igrač kroz baklju prolazi.
- Kad se uništi blok oslonca, baklja nestane i ispusti jedan predmet.
- Recept daje 4 baklje.
- Razine svjetla: 14 u ćeliji baklje, 13 u susjednima, 0 iza zida, i sve se vrati na
  0 kad se baklja ukloni.

Postojeći testovi (soak, cavetest, deeptest, cracktest) moraju i dalje prolaziti.
Izgled svjetla i čestica provjerava se samo u editoru.

## 7. Stanje izvedbe (2026-11-28)

Chunk uz vrstu bloka za svaku ćeliju pamti stanje (`FVoxelChunk::States`) i razinu
svjetla (`BlockLight`). Stanje preživi unload izmijenjenog chunka, a svjetlo se pri
učitavanju računa iznova.

`Blocks.json` je dobio polja `collision`, `blocksLight`, `sturdy`, `lightLevel`,
`behavior`, `properties`, `variants`, `shape` i `model`. Polje `model` je Minecraftov
JSON modela prepisan bez preračunavanja; mesh iz njega gradi igra pri pokretanju
(`BlockModel.cpp`), pa baklja nema asset modela. `Items.json` je dobio `wallBlock`.
Lišće je označeno kao blok koji nije oslonac i propušta svjetlo.

Pravila oslonca, postavljanja i otpadanja su u `VoxelWorldBlockRules.cpp`, a svjetlo u
`VoxelWorldLight.cpp`. Svjetlinu stranice Minecraft računa kao `1.5 * l / (4 - 3l)`, gdje
je `l` razina podijeljena s 15, a boju svjetla iz te svjetline; oboje je pročitano iz
`lightmap.fsh` i `LightTexture` i preneseno u kod i u materijale.

Svaka instanca bloka nosi šest svjetlina stranica u custom data (indeksi 3 do 8).
Materijali `M_VoxelBlock`, `M_VoxelBlock_Masked` i novi `M_BlockModel` iz njih računaju
vlastiti sjaj. Jačinu za sve materijale odjednom daje `MPC_BlockLight`, a mijenja se u
igri konzolnom varijablom `voxel.light.intensity` (početno 1).

Teksturu baklje i materijale uvozi Tools > MinecraftClone > Import Torch.

Izložba blokova kod spawna sadrži baklju na podu (u redu s ostalim blokovima) i, iza
redova, kamen s po jednom bakljom na svakom od četiri boka, pa se svjetlo vidi odmah
nakon pokretanja.

Greška nađena pregledom u editoru 2026-11-28: gornje plohe blokova bez tinta bile su
crne. Materijal gornju plohu množi tintom iz podataka instance, a ti su podaci nakon
uvođenja svjetla postojali i za blokove bez tinta i bili nula. Sada se bijeli tint upisuje
svakoj instanci, a `voxel.torchtest` provjerava da nijedna instanca nema crni tint.

Izmjereno komandom `voxel.torchtest` (20 provjera, sve prolaze): klik na vrh bloka daje
stojeću baklju, klik na bok zidnu baklju pravog smjera, na strop bez oslonca i na lišće
se ne može postaviti. Razine svjetla su 14 u ćeliji baklje, 13 pokraj nje, 0 u kamenu i
6 ispod sredine poda 5×5, gdje svjetlo mora obići rub. Spremljene razine jednake su
neovisnom izračunu u tri trenutka: s dvije baklje, nakon ponovnog učitavanja chunka i
nakon uklanjanja baklji. Zraka igrača kroz baklju prolazi, zraka nišana je pogađa.
Baklja kojoj se uništi oslonac nestaje i ispušta jedan predmet. Recept daje 4 baklje.

Soak test nakon izmjene: rehash FAIL=0, izgradnja vizuala chunka u prosjeku 4.3 ms.
Testovi cavetest, deeptest, cracktest, flytest, mobwatertest, pickuptest i zombietest
prolaze. Materijali su prevedeni s uključenim prikazom (`-AllowCommandletRendering`) bez
ijedne greške u logu.

### Čestice (faza 6)

Pročitano iz jara: `ClientLevel.animateTick` svakog ticka 667 puta odabere po jednu
nasumičnu ćeliju u dometu 16 i u dometu 32 blokova oko igrača, a pomak po osi je razlika
dvaju nasumičnih brojeva, pa su ćelije bliže igraču češće na redu. Blok koji tako dođe
na red ispusti svoje čestice; baklja ispusti jedan dim i jedan plamen. Baklja u ćeliji
igrača dolazi na red oko 0,18 puta po ticku, to jest 3,7 puta u sekundi.

Plamen (`FlameParticle`, `RisingParticle`) nastaje do 0,05 bloka od mjesta baklje, gotovo
se ne miče, traje 12 do 44 ticka i pritom se smanji na polovicu. Dim (`SmokeParticle`,
`BaseAshSmokeParticle`) je siv, diže se, traje 8 do 40 tickova, izmjenjuje osam sličica i
zaustavlja se pod punim blokom.

Kod nas je to u `VoxelWorldParticles.cpp`: isto uzorkovanje ćelija i isto gibanje, u
Minecraftovim tickovima, s izglađivanjem između tickova. Čestica je kvadrat okrenut
kameri (dva ISM-a, plamen i dim) s materijalom `M_BlockParticle` bez sjenčanja. Teksture
uvozi Import Torch; osam sličica dima složi u jednu traku. `voxel.particles 0` gasi
čestice.

Razlika prema Minecraftu: Minecraft dim osvjetljava svjetlom ćelije u kojoj je, a naš dim
uvijek ima svoju sivu boju.

Izmjereno komandom `voxel.particletest` (7 provjera, sve prolaze): mjesto nastanka
(stojeća baklja 0,2 bloka iznad središta ćelije, zidna još 0,22 gore i 0,27 prema zidu),
trajanje plamena 12..43 i dima 8..39 tickova, dim se diže i staje pod stropom, učestalost
0,195 po ticku uz očekivanih 0,183.

### Svjetlo na onome što nije blok (faza 5)

Pročitano iz jara (`EntityRenderer.getPackedLightCoords`): stvorenje dobiva jednu razinu
svjetla, onu ćelije u kojoj su mu oči, i cijelo se oboji njome. Predmet u ruci dobiva
razinu igrača.

Kod nas to radi `UBlockLightReceiverComponent` na svakom stvorenju (`ACreatureBase`),
dropu (`AItemDrop`) i igraču. Deset puta u sekundi pročita svjetlinu ćelije u kojoj su
vlasniku oči (dropu: u kojoj leži) i upiše je u parametre `LightFallbackPos` i
`LightFallbackNeg` svih materijala vlasnikovih mesheva. To su isti parametri kojima
svjetlo prima blok pod nišanom. Materijal bez tih parametara ostaje neosvijetljen.

Materijali: `M_ItemSprite` (sličice predmeta) dobiva svjetlo u `build_block_materials.py`.
Nova skripta `build_entity_light.py` dodaje sjaj materijalima `M_Zombie` i `M_Sheep`, ne
dirajući ono što je spojeno na Base Color, i gradi `M_EntityColor`, jednobojni materijal
kojim su zamijenjeni Unrealovi materijali kocke na praznoj ruci i buzdovanu. Pokreće je
Import Torch, a i Import Zombie, jer on `M_Zombie` gradi iz nule.

Izmjereno komandom `voxel.entitylighttest` (11 provjera, sve prolaze): drop sličica, drop
blok, zombi i ovca uz baklju imaju u materijalu točno svjetlinu svoje ćelije (razine 13,
12, 10 i 9), a nakon uklanjanja baklje nulu.

Otvoreno:

- Izgled u editoru: osvijetljenost ruke, predmeta, dropova, zombija i ovce; jesu li prazna
  ruka i buzdovan s novim materijalom ostali iste boje; je li ovca na danjem svjetlu ista.
- Lik iz predloška (`FirstPersonMesh`, materijal `M_Mannequin`) ne prima svjetlo.
- Svinja nema blueprint ni materijal, pa nije obuhvaćena.
- `voxel.deeptest` je prije padao uz `UndergroundVisualRadius` veći od 4; sada igrača
  vraća vodoravno dovoljno daleko, pa prolazi uz bilo koju postavku.

## 8. Što nije u planu

Minecraftovo svjetlo neba (mrak u špiljama i dalje daje Unreal), glatko osvjetljenje,
drveni ugljen, pravila koja ovise o svjetlu (stvaranje čudovišta u mraku) i ispiranje
baklji vodom, jer voda u špiljama još ne postoji.
