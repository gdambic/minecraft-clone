# PLAN: Realistična voda

> **STATUS (2026-11-25): ZATVORENO ZA SADA.** Faze 1-4 i faza 5 (plutanje dropova, mobovi
> izvan mora) gotove i provjerene. Sve ostalo oko vode je odgođeno za budućnost, vidi
> "Odgođeno" na kraju i `BACKLOG.md`. Preduvjet (razina mora i oceani) je u `PLAN_Oceans.md`.

## Cilj

Udubine ispod razine mora dobivaju vodu kakva je u GTA-u, a ne Minecraftovu. Površina
se njiše u valovima, odražava nebo, plitko je prozirna, a dublje tamnoplava. Uz obalu
ima pjene, a pod vodom je slika zamućena i obojena. Igrač može plivati, a ispušteni
predmeti plutaju. Voda nije blok i ne teče. Ona je ploha na razini mora iznad svakog
stupca za koji generator kaže da je more.

## Gdje voda postoji

Odluku već donosi generator. `FChunkGenerator::IsSeaWater` kaže da je more svaka
prazna ćelija iznad generirane površine stupca, do razine mora. Za vodu je dovoljno
pitati to za sloj na razini mora: ako je taj sloj more, stupac ima vodu.

Iz toga slijede dvije posljedice, i obje su željene. Rupa koju igrač iskopa na
kopnu ostaje suha jer pravilo gleda generirani teren, a ne trenutni. Blok koji igrač
postavi u more stoji u vodi, a voda kroz njega nije vidljiva jer je blok neproziran.
Voda se ne razlijeva u iskopane rupe ni u druge udubine, i to je svjesno
pojednostavljenje prve verzije.

## Zašto vlastita ploha, a ne Unrealov Water plugin

Unreal ima Water plugin s oceanom, valovima i plutanjem. Pregled njegovog koda ipak
pokazuje da nam ne odgovara. Plugin je označen kao Experimental i građen je za
Landscape, a mi Landscape nemamo. Ocean ima konačan opseg, a naš svijet je
beskonačan. Najveći je problem što bi ploha oceana bila svuda, pa bi se pojavila i
u svakoj rupi na kopnu ispod razine mora. Plivanje plugin ionako ne donosi.

Izgled koji tražimo zapravo ne dolazi iz plugina, nego iz Unrealovog materijala sa
shading modelom **Single Layer Water**, a on je dio enginea (`MSM_SingleLayerWater`).
Taj model sam računa kako svjetlo slabi i mijenja boju s dubinom vode, lomi sliku
dna i radi odsjaje. Projekt već koristi Lumen za odsjaje i globalno osvjetljenje, a
s njim taj materijal izgleda najbolje.

Zato radimo vlastitu plohu vode po chunku, s tim materijalom.

## Kako se ploha gradi

Kad chunk dobije vizuale, za njega se izgradi i mesh vode. Mesh je mreža kvadrata
na vrhu sloja razine mora, ali samo iznad stupaca koji imaju vodu. Chunk bez mora
nema ni mesh vode. Mesh nastaje kroz `ProceduralMeshComponent`, koji je u projektu
već uključen. Mreža ima više točaka po bloku, jer valovi pomiču točke i treba ih
dovoljno da val bude gladak. Svaka točka uz obalu dobiva oznaku koja kaže
materijalu da tamo stiša valove, kako se između vode i obale ne bi otvorila
pukotina. Kad se chunk ukloni iz memorije, nestaje i njegova voda.

## Izgled

Materijal vode radi pet stvari. Valovi pomiču točke mreže zbrojem nekoliko
Gerstnerovih valova, a to je ista vrsta valova koju koristi Unrealov plugin.
Boju po dubini, lom svjetla i odsjaje radi sam Single Layer Water. Pjena se pojavljuje
gdje je razmak između površine i dna malen, dakle uz obalu i oko blokova u vodi.
Sitne nabore na površini daje normal mapa koja klizi. Daleko od kamere valovi se
smiruju, da voda na rubu vidljivog područja ne treperi.

Istu formulu valova imamo i u C++. Igra tako zna gdje je površina u svakom
trenutku, a to trebaju pogled ispod vode, plivanje i plutanje.

## Ispod vode

Kad je kamera ispod površine vala, uključuje se post-process koji sliku boji u
plavozeleno, zamućuje je i dodaje maglu koja raste s udaljenošću. Granica se računa
prema valu, a ne prema ravnoj razini mora. Zato se pri plivanju na površini slika
ispravno dijeli valom.

## Plivanje i plutanje

Unrealov `CharacterMovementComponent` igrača stavlja u plivanje kad se on nalazi u
physics volumenu označenom kao voda (`bWaterVolume`). Igra zato ima jedan takav
volumen koji prati igrača i uključen je samo kad je igrač iznad stupca s morem.
Gornja ploha volumena je razina mora. Mobovi dobivaju isto ponašanje.

Ispušteni predmeti (`AItemDrop`) već imaju fiziku. Pod vodom na njih djeluje sila
prema gore i otpor, pa izrone i plutaju na valu. Mobovi pri lutanju izbjegavaju
vodu. Ulazak u vodu i izlazak iz nje imaju zvuk pljuska.

## Faze

**Faza 1, ploha vode po chunku.** Mesh nad stupcima s morem, zasad s jednostavnim
plavim prozirnim materijalom i bez valova. Headless log dobiva `[WATER]` liniju s
brojem stupaca s vodom u bootstrap chunkovima, i taj broj mora odgovarati broju koji
daje `IsSeaWater`. Vizualno se provjerava u editoru: voda je samo nad udubinama i
nema je u rupama na kopnu.

**Faza 2, izgled.** Single Layer Water materijal s valovima, pjenom i normal mapom.
Materijal radiš u editoru prema mom opisu, jer je to vizualan posao. Provjera je
ručna.

**Faza 3, ispod vode.** Post-process ispod površine vala. Provjera je ručna.

**Faza 4, plivanje.** Vodeni volumen uz igrača. Test komanda baca igrača u more, a
`[WATER]` linija u logu ispisuje način kretanja (`Swimming`) i visinu prema razini
mora. To se može provjeriti headless.

**Faza 5, plutanje, mobovi i zvuk.** Uzgon za dropove, s `[WATER]` linijom koja
mjeri vrijeme dok drop ne izroni. Mobovi izbjegavaju vodu. Zvuk pljuska.

## Rizici

Prvi rizik je cijena. Svaki chunk s morem dobiva dodatni mesh, pa izgradnju vode
mjerimo u istom `[PERF]` budžetu od 8 ms po chunku kao i ostale vizuale chunka.
Drugi je rub vidljivog svijeta. Voda završava tamo gdje završavaju učitani
chunkovi, otprilike 100 blokova od igrača. Na pučini bi se taj rub mogao vidjeti, pa
će možda trebati jedna jeftina daleka ploha ili magla prema horizontu. To odlučujemo
kad vidimo kako izgleda.

## Rezultat faze 1 (2026-11-23)

`AVoxelWorld::BuildChunkWater` gradi plohu na kraju `ApplyChunkVisuals`, a
`UnloadChunk` je uklanja. Ploha je na 87.5 % visine bloka razine mora (Z = 37.5 UU,
kao Minecraftova voda od 14/16), s dva kvadrata po rubu bloka. Nema koliziju, ne
utječe na navigaciju i ne baca sjenu. Točke uz kopno imaju vertex color R = 1 za
stišavanje valova u fazi 2. Privremeni materijal `M_WaterPreview` (prozirna plava,
two sided) gradi `Scripts/build_water_material.py`, a pokreće se iz Tools >
MinecraftClone > Build Water Material.

Provjera je `[WATER]` linija na startu i na svakom soak hopu. Ona broji stupce pod
plohom i uspoređuje ih sa stupcima čiji je najviši blok u podacima chunka ispod
razine mora. Na oceanu kod hopa 2 to je 40127 stupaca u 163 chunka, s ishodom `OK`
na svim hopovima. Izgradnja plohe traje u prosjeku 0.22 ms po chunku, najsporiji
vizual chunka je 5.54 ms (budžet 8 ms), a soak prolazi (`FAIL=0`, `RETAINED OK`).

## Faza 2: materijal (2026-11-24)

Umjesto ručnog slaganja u editoru (dogovor s korisnikom) materijal gradi
`Scripts/build_water_material.py`, kao i sve ostale materijale u projektu. Nastaju
`M_Water` (Single Layer Water, opaque, two sided, normala u world spaceu) i
`MI_Water`, na kojoj se svi parametri štimaju uživo. Ponovno pokretanje skripte
gradi graf iznova, ali ne dira vrijednosti u `MI_Water`. `AVoxelWorld` po defaultu
koristi `MI_Water`. Privremeni `M_WaterPreview` je ostao u `Content/Water`, ali ga
više ništa ne koristi.

Valove računaju dva Custom čvora s istom formulom: jedan pomiče točke (World Position
Offset), a drugi daje normalu. Svaki od četiri vala ima smjer, valnu duljinu i
amplitudu u parametru `WaveN`, a brzina slijedi iz valne duljine. Valovi se stišaju
na točkama uz obalu (vertex color iz faze 1) i s udaljenošću od kamere. Na normalu se
dodaju dvije normal mape koje klize, engineova `water_n`. Pjena nastaje gdje je dno
blizu površine, preko `SceneDepthWithoutWater`. Boju vode daju koeficijenti
apsorpcije i raspršenja (jedinica 1/cm), a polazne vrijednosti su procjena za
štimanje, ne izmjerena voda.

Headless: skripta prolazi bez greške spajanja, soak prolazi (`FAIL=0`, svi `[WATER]`
`OK`). Najsporiji vizual chunka bio je ovaj dan 11–13 ms, ali isti je bio i s
`M_WaterPreview` (11.45 ms, A/B run). Usporenje dakle ne dolazi od materijala.
Najsporiji chunkovi nisu imali vodu, a izgradnja plohe traje 0.37–0.42 ms. To se
nadovezuje na otvorenu varijabilnost chunk applya iz Stony Peaksa. Kompajliranje
shadera i izgled headless se ne mogu provjeriti.

## Faza 3: ispod vode (2026-11-24)

Post-process `M_Underwater` s instancom `MI_Underwater` gradi ista skripta kao i vodu.
`AVoxelWorld` drži jedan unbound `UPostProcessComponent` s tim materijalom. Svaki frame
provjeri je li kamera nad stupcem mora i niža od najvišeg mogućeg vrha vala (zbroj
amplituda iz `MI_Water`). Samo tada post-process uključi i u njega prepiše valove i
brzinu iz `MI_Water`, pa štimanje valova vrijedi i za pogled ispod vode.

Gdje je točno crta vode na ekranu, odlučuje materijal za svaki piksel. Uzme točku na
near planeu kamere kroz taj piksel i usporedi njezinu visinu s visinom vala na tom
mjestu. Kad igrač pliva na površini, slika se zato dijeli po valu. Ispod crte slika se
njiše i zamućuje (pet uzoraka), oboji se s `Tint` i s udaljenošću tone u `FogColor`
(`FogDensity`). Visina vala u maski zanemaruje vodoravni pomak Gerstnerovih valova i
stišavanje uz obalu, pa uz obalu crta može malo odstupati od plohe.

Headless: soak prolazi (`FAIL=0`, `RETAINED OK`, svi `[WATER]` `OK`). Na oceanskom hopu
log ispiše `[WATER] pogled ispod vode UKLJUCEN` s kamerom na dnu (Z = −1336), a na
sljedećem kopnenom hopu `iskljucen`. Izgled i prevođenje shadera provjeravaju se u
editoru, jer headless commandlet shadere ne prevodi.

## Faza 4: plivanje (2026-11-25)

Plivanje koristi Unrealov `MOVE_Swimming`, a do njega vodi jedan vodeni volumen.
`AVoxelWaterVolume` je `APhysicsVolume` s `bWaterVolume`. Oblik mu daje `UBoxComponent`,
a ne brush. `AVoxelWorld` svaki frame pomiče tu kutiju za igračem. Kutija je široka 3×3
bloka, vrh joj je na površini vode, a dno na dnu svijeta. Uključena je samo kad je stupac
ispod sredine igrača more po `FChunkGenerator::IsSeaWater`. Kapsula lika volumen traži u
svom popisu preklapanja, a tamo ulaze samo komponente s overlap eventima, pa ih kutija
mora imati uključene. Kako se kapsula ne miče uvijek, `UpdatePhysicsVolume` zovemo svaki
frame dok je kutija aktivna.

Brzine su preuzete iz Minecrafta. Voda koči 0.8 po ticku, tj. 4.46 /s, a to daje
`FluidFriction` 8.9. Bez inputa Unreal u vodi koči još jednom, preko `BrakingFrictionFactor`.
Zato ga lik u vodi postavlja na 0 (`OnMovementModeChanged`). Prvo mjerenje je bez toga
dalo tonjenje 54 UU/s. `Buoyancy` 0.27 daje tonjenje 160 UU/s (1.6 bloka/s). Razmak u vodi
dodaje input prema gore, jer Unrealov skok traži tlo, pa se igrač diže oko 190 UU/s.

Na površini sredina lika stoji na razini vode i lik brzo prelazi između plivanja i
padanja. Izlaz na obalu je Unrealov odskok iz vode (`CheckWaterJump`). Uvjet pogleda
prema gore je uklonjen (`JumpOutOfWaterPitch` −90), a `OutofWaterZ` je 520, pa razmak + W
prema obali u razini vode izbaci igrača na kopno. Obala viša od jednog bloka ne može se
preskočiti.

`voxel.swimtest` baci igrača u najbliže more dublje od 8 blokova i svakih 0.1 s ispiše
`[WATER] swim` s načinom kretanja, visinom prema površini i brzinom. Izmjereno je
tonjenje 160.8 UU/s, dizanje 191 UU/s i plutanje na površini ±1 UU. Izlaz na obalu
potvrđen je ručno u editoru. Plivanje je "dovoljno dobro". Otvoreno je trzanje na
površini i to da stvorenja u kutiji oko igrača također plivaju.

## Faza 5a: plutanje dropova (2026-11-25)

`AItemDrop` pita `AVoxelWorld::GetSeaSurfaceAt` je li ispod njega more. To je isto
pravilo kao za plohu. Dok mu je sredina ispod plohe, okomitu brzinu mijenjaju uzgon
(`WaterBuoyancy` 300 UU/s²) i kočenje vode (`WaterDrag` 4.46 /s, isto kao kod igrača). Dno
mora ga tada ne drži. Iznad plohe ga gravitacija vraća dolje, pa se smiri oko plohe. Na
kopnu drop i dalje pada stalnom brzinom `FallSpeed`. Ovo nije Minecraftovo ponašanje,
nego uvjerljiv plovak.

`voxel.swimtest` uz igrača baci i Dirt drop. Izmjereno je: ulaz 397 UU/s, zaron 59 UU,
izron nakon 1.53 s pri 67 UU/s, a nakon 2 s na plohi ±0.3 UU. To se slaže s izračunom.
`voxel.pickuptest` i dalje prolazi. Usput je ispravljeno uništavanje dropa ispod
Z=−1000, jer svijet sada ide do `WorldMinZ`. Drop pluta na ravnoj plohi, a ne na valu.
U editoru je potvrđeno: "drop pliva".

## Faza 5b: mobovi ne ulaze u more (2026-11-25)

Navmesh vodu ne vidi, jer je dno mora za njega obično tlo. Zato `AVoxelWorld::BuildChunkWaterNav`
u svakom chunku s morem stupce mora prekrije s što manje pravokutnih `UBoxComponent`
kutija. Kutije se protežu od dna mora do plohe. Svaka je dinamička prepreka
(`bDynamicObstacle`) koja u navmesh upiše područje `UNavArea_Water` (`VoxelNavigation.h`).
Kutije žive i nestaju zajedno s plohom chunka.

Kontroleri mobova (`AMobAIController`, `AEnemyAIController`) svaki tick biraju filter.
Na kopnu koriste `UNavFilter_NoWater`, koji vodu potpuno isključuje. Zato nijedan put ne
ulazi u more: ni lutanje, ni bijeg, ni potjera iz behavior treeja. Zombi se zato zaustavi
na obali kad igrač pliva. Mob koji se ipak nađe nad morem, na primjer od knockbacka, dobije
default filter. Tamo je voda dopuštena, ali skupa (cijena 20), pa izađe najkraćim putem.
Lutanje i bijeg uz to odbacuju cilj nad morem (do 10 pokušaja).

`voxel.mobwatertest` stavi igrača na najbližu obalu s plitkim morem. Kraj njega stvori
6 ovaca, koje od igrača bježe prema moru, i jednu ovcu u moru. Test traje 60 s, a
`[WATER] mob test GOTOVO ... -> OK` je prolaz. Kontrolni run s isključenim popravkom
(`voxel.mobs.avoidwater 0`) daje FAIL: ovca ode u more sa stopalima 85 UU ispod plohe.
S popravkom je rezultat 0 ovaca u moru, a ovca iz mora izađe nakon 3–5 s.

## Odgođeno (za budućnost)

Ovo se radi tek kad se odluči vratiti vodi:

- **Zvuk pljuska:** kad igrač, mob ili drop uđe u vodu.
- **Plivanje na površini:** lik brzo prelazi između plivanja i padanja, pa se malo trza.
- **Stvorenja u kutiji vode:** mobovi u kutiji oko igrača također plivaju.
- **Drop na valu:** drop pluta na ravnoj plohi, a ne prati val.
- **Linija vode ispod površine:** maska pogleda ispod vode zanemaruje vodoravni pomak
  Gerstnerovih valova i stišavanje uz obalu.
- **Bacanje predmeta (Q):** ne postoji, a omogućilo bi da se drop baci u vodu.
- **Mob u dubokom moru:** mob kojeg knockback baci u duboko more hoda po dnu do obale,
  jer ne pliva.
