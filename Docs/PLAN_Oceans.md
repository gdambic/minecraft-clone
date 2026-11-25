# PLAN: Razina mora i noise kontinentalnosti

> **STATUS (2026-11-23): IMPLEMENTIRANO** u generaciji i headless provjereno; izgled
> obale još nije ručno pogledan u editoru, a premještanje spawna s oceana na kopno
> nije pokrenuto jer ishodište sa seedom 1337 leži na kopnu. Rezultati su na kraju.

## Kako to radi Minecraft

Minecraft ima fiksnu razinu mora, Y=63, i pravilo da svaki prazan blok na otvorenom
ispod nje postaje voda. Ocean zato nije poseban objekt nego samo teren koji je dovoljno
nizak. Koliko je teren nizak odlučuje niskofrekventni 2D noise kontinentalnosti s
vrijednostima od otprilike −1.2 do 1. Ispod −0.455 je duboki ocean, od −0.455 do −0.19
ocean, od −0.19 do −0.11 obala, a iznad toga kopno koje se udaljava od mora. Isti broj
spušta teren i bira biom, pa se ta dva nikad ne razilaze. Dno plitkog mora je zemlja,
a dno dubokog šljunak pa kamen. Planine nastaju u unutrašnjosti, ne uz obalu.
(Izvor: minecraft.wiki, stranice World generation, Ocean, Terrain features.)

## Što naša igra danas ima

Visinu stupca daje jedan noise oko `SurfaceLevel = 0` s amplitudom 6, pa oko ishodišta
teren ide od −3 do 6. Na to se u Stony Peaksu dodaje podizanje planina. Biom se bira
iz zasebnog noisea na jednake pragove (Plains, Desert), a Stony Peaks iz podizanja
planina. Razina mora ne postoji. Od blokova za morsko dno imamo Dirt i Gravel, a
Sand nemamo.

## Razina mora je 0, ali kopno se podiže

Razina mora bit će Z = 0. Naš teren je od početka građen oko nule, pa nula kod nas
igra ulogu Minecraftove Y=63. To ne znači da teren ostaje isti. Danas je nula sredina
ravnice, a ne njezino dno. Kad bi se samo uvela razina mora na 0, svaki stupac ispod
nule, a oko ishodišta ih ima do −3, postao bi lokva usred ravnice. U Minecraftu je
ravnica malo iznad mora, pa i kod nas kopno mora dobiti pomak prema gore. Visina
kopna zato više ne polazi od `SurfaceLevel`, nego od razine mora plus visine koju
daje kontinentalnost. Uz obalu je to jedan do dva bloka, a u unutrašnjosti četiri do
osam, s istim sitnim neravninama kao danas. `SurfaceLevel` time prestaje postojati
kao zasebna postavka i zamjenjuje ga `SeaLevel`.

## Kontinentalnost

Uvodi se novi noise s vlastitim pomakom iz seeda, kao što ga već imaju planine i
biomi, i s nižom frekvencijom od noisea bioma, tako da oceani budu veći od jednog
bioma. Njegova vrijednost prolazi kroz krivulju koja je zadana s nekoliko točaka, a
između njih se linearno interpolira. Duboki ocean daje dno dvadesetak blokova ispod
mora, ocean desetak, obala se penje do jednog bloka iznad mora, a unutrašnjost do
osam. Krivulja je glatka, pa obala ispada kao blaga plaža, a ne zid. Pragovi i točke
krivulje idu u postavke `AVoxelWorld`a, da se mogu ugađati bez prevođenja.

Minecraftove pragove preuzimamo kao početnu točku, ali ne kao gotove brojeve. Naš
`FractalNoise2D` nema istu raspodjelu vrijednosti kao Minecraftov noise, pa isti
pragovi kod nas ne daju isti udio oceana. Pragovi se zato ugađaju prema izmjerenom
udjelu oceana u logu.

Sitne neravnine pod morem se smanjuju, da dno bude mirnije od kopna, a podizanje
planina množi se faktorom unutrašnjosti. Tako planina ne može izrasti iz oceana ni
s obale, nego samo dublje u kopnu, kao u Minecraftu.

## Biomi i dno mora

Gdje je kontinentalnost ispod praga oceana, stupac dobiva novi biom Ocean iz
`Biomes.json`, bez stabala. Na obali i u unutrašnjosti odabir ostaje kao danas:
Plains ili Desert, a u planinama Stony Peaks. Površina ne ovisi samo o biomu nego i
o dubini, isto kao u Minecraftu. U plitkom moru dno je Dirt, a u dubokom Gravel na
kamenu. Pijesak i plaže ostaju za kasnije, kad se doda blok Sand.

Stabla se ne sade na stupce čija je površina ispod razine mora, jer bi stajala u
vodi.

## Spawn i mobovi

Ishodište sada može pasti u ocean. Minecraft igrača u tom slučaju stavlja na najbliže
kopno, pa i mi radimo isto. Pri pokretanju svijeta traži se najbliži stupac kopna od
PlayerStarta, istim spiralnim pretraživanjem kojim `[BIOME]` danas nalazi najbliži
vrh, i tamo se stvara igrač. Mobovi se stvaraju samo na stupcima iznad razine mora.

## Što se vidi prije vode

Ovaj plan ne crta vodu. Oceani će se u igri vidjeti kao prazne udubine do dvadeset
blokova duboke, s ravnom crtom obale na visini 0. Pravilo "zrak ispod razine mora na
otvorenom je voda" ostaje zapisano kao jedna funkcija u generatoru. Na nju će se
oslanjati prikaz vode iz `PLAN_Water.md`, i tako se ta odluka ne mora donositi na
dva mjesta.

## Provjera

`[BIOME]` log dobiva tri nova podatka: udio oceana u području 2048×2048, raspon
visine dna mora i najbliži ocean od ishodišta s gotovom naredbom `voxel.tp`, kao što
danas ispisuje najbliži vrh. Pragove ugađamo dok udio oceana ne bude oko trećine
svijeta. Ta brojka je naš izbor, ne Minecraftova vrijednost. CRC visine i bioma se
promijeni jednom, što je očekivano, a nakon toga isti seed mora opet davati isti
CRC. Soak test mora i dalje prolaziti s `FAIL=0` i `RETAINED OK`, a igrač se mora
pojaviti na kopnu. Izgled obale i dna pogledamo ručno u editoru preko `voxel.tp`.

## Rezultat implementacije (2026-11-23)

Prvo mjerenje s Minecraftovim pragovima pokazalo je da naš `FractalNoise2D` s četiri
oktave daje kontinentalnost samo od −0.41 do 0.40, pa je oceana bilo 12.5 %, a
dubokog oceana nije bilo. Noise se zato množi s `ContinentNoiseGain = 2.3`, čime
raspon postaje −0.94 do 0.93, blizu Minecraftovog, i pragovi ostaju Minecraftovi.
Drugo, krivulja je u prvoj verziji imala obalu (−0.19 do −0.11) još pod vodom, pa je
10.4 % svih stupaca bilo lokva u kopnenom biomu. Crta obale je zato pomaknuta na prag
oceana: krivulja na −0.19 daje −1, a na −0.11 daje +2.

S tim postavkama `[BIOME]` na području 2048×2048 izmjeri 32.7 % oceana, 34.0 %
stupaca pod vodom, od čega su lokve u kopnenim biomima 1.3 %, i najdublje dno na
Z = −23. Najbliži ocean od ishodišta je na (−56, −72). CRC je isti u oba ispisa istog
runa, a soak test prolazi (`rehash OK=284, FAIL=0`, `RETAINED OK`), uz najsporiji
vizual chunka 6.81 ms unutar budžeta od 8 ms. Mobovi se stvaraju na kopnu, a igrač je
na navmeshu.
