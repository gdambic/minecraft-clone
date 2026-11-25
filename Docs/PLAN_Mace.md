# PLAN: Mace (buzdovan s kuglom na lancu)

Status: implementirano 2026-11-25, headless test prolazi. Ručni test u editoru još nije napravljen.

## Što Terraria radi

Mace je u Terrariji "thrown flail". Dok igrač drži tipku, kugla se vrti oko njega. Kad je pusti, kugla poleti prema kursoru i prolazi kroz sve neprijatelje koje dotakne. Ako igrač nakon bacanja ponovno drži tipku, kugla padne na tlo i ondje leži dok je tipka pritisnuta. Kad je igrač pusti, kugla se vraća kroz blokove. Mace ima 18 štete, 4,6 knockbacka i vrijeme korištenja 45 tickova. Izvor ponašanja je Terraria wiki (stranice Mace i Flails).

Wiki ne navodi brojke za domet, brzinu i vraćanje. Zato je izvor algoritma tModLoader `ExampleAdvancedFlailProjectile.cs` (grana 1.4.4). Ta datoteka je preslika vanilla `Projectile.AI_015_Flails` za Ball O' Hurt. Posebne vrijednosti za Mace (projektil 947) nisu pronađene ni u jednom izvoru, pa se koriste vanilla zadane vrijednosti iz te datoteke.

## Kako je preneseno

Simulacija radi u fiksnom Terraria ticku od 60 Hz, a prikaz interpolira između dva ticka. Jedinice su pretvorene doslovno, 1 tile = 16 px = 1 blok = 100 UU, i to vrijedi i za gravitaciju. Brzina leta je 14 px/tick, odnosno 87,5 UU po ticku. Let traje 15 tickova, pa je domet 1312 UU, oko 13 blokova. Lanac se može rastegnuti najviše 5000 UU. Kugla koja leži na tlu vraća se sama kad je dalje od 2312 UU, a nestaje kad je dalje od 5625 UU.

Stanja su ista kao u vanilli: vrtnja, let, vraćanje, prisilno vraćanje, odbijanje od bloka i ležanje. Udarac u blok tijekom leta odbije kuglu s 40 % brzine i prebaci je u odbijanje. Iz odbijanja kugla prelazi u ležanje, a ako tipka nije držana, odmah se prisilno vraća. Kugla koja deset puta udari u blok dok se vraća prelazi u prisilno vraćanje i prolazi kroz blokove.

Terraria vrti kuglu u ravnini ekrana. U 3D-u je to okomita ravnina smjera gledanja, pa kugla prelazi preko glave i spušta se ispred igrača. Vrtnja pogađa sve unutar elipse od 344 UU oko igrača, ali samo ono što igrač vidi, i ne nanosi štetu prvih 12 tickova. Bacanje polazi iz sredine kapsule prema točki u koju gleda nišan.

Osnovna šteta flaila je pola iznosa s tooltipa. U letu i pri vraćanju udvostručuje se, a u vrtnji se množi s 1,2. Mace u WeaponData ima 6 štete, pa udara sa 6 u letu, 3,6 u vrtnji i 3 dok leži ili se prisilno vraća. Knockback koristi vanilla Minecraft jačinu iz `UCombatLibrary` i uvijek gura od igrača. U vrtnji je 35 % jačine, prema wikiju za 1.4.4, a dok kugla leži 50 %. Svaka meta ima vlastiti hit cooldown od 10 tickova, odnosno 20 u vrtnji. Uz to vrijedi i prozor nepovredivosti od 0,5 s koji igra koristi za svu štetu.

Ovo je namjerno drukčije od Terrarije. Kolizijska kutija kugle je 50 UU, a ne 24 px (150 UU), jer kugla od 1,5 bloka ne bi prolazila kroz otvore od jednog bloka. Veličina se mijenja s `voxel.mace.radius`. Kad je Mace u ruci, a kugla nije bačena, kugla visi smanjena ispod vrha drške. U Terrariji se u tom trenutku ne prikazuje ništa. Promjena itema poništi kuglu, dok Terraria ne dopušta promjenu itema dok je flail vani.

## Dijelovi

Drška je obični sprite item. Tekstura `T_Item_Mace` je privremena i nacrtana je kodom (`Scripts/generate_mace_texture.py`). Ušica na vrhu je piksel (13, 2), i tamo je sidro lanca `ChainAnchorLocal`. Nova tekstura mora imati ušicu na istom pikselu, inače treba pomaknuti sidro.

Kugla je proceduralni mesh: kugla promjera 40 UU i 14 stožaca. Zamijenit će je pravi model kad stigne. Lanac je ravna linija karika jedne `UInstancedStaticMeshComponent`, od vrha drške do sredine kugle, kao u Terrariji.

Sve vodi `UMaceComponent` na characteru. Kad je Mace u ruci, lijeva tipka ne kopa i ne udara melee. Za testiranje je Mace u startnom hotbaru na poziciji 6.

## Gdje su asseti danas

Tekstura drške je `Content/Items/Textures/T_Item_Mace.uasset`, odnosno `/Game/Items/Textures/T_Item_Mace` u editoru. Njezin izvorni PNG je `Saved/MaceTexture/T_Item_Mace.png`. `Saved/` je u .gitignore, pa ga `Scripts/generate_mace_texture.py` iznova crta i uvozi. Kugla nema asset, nego se gradi u `MaceComponent.cpp` u funkciji `BuildBallMesh`. Lanac koristi engineovu kocku `/Engine/BasicShapes/Cube`. Kugla i lanac imaju materijal `/Engine/BasicShapes/BasicShapeMaterial`, obojan kroz parametar `Color`.

## Plan: pravi model kugle i tekstura drške

Model kugle dolazi kao FBX ili OBJ. Pivot mu treba biti u središtu kugle, jer je to točka na koju se veže lanac i oko koje se kugla kotrlja. Kugla bez šiljaka treba imati promjer oko 40 UU, kao današnji placeholder. Ako model dođe u drugoj veličini, skalira se pri uvozu, a ne u kodu.

Model se uvozi u `Content/Weapons/Mace/` kao `SM_MaceBall`, a njegov materijal i teksture idu u istu mapu. Uvoz radi nova skripta `Scripts/import_mace_ball.py`, po uzoru na postojeće import skripte, i dobiva stavku u izborniku Tools > MinecraftClone. Ponovni uvoz nakon izmjene modela tako je jedan klik i ne ovisi o ručnim postavkama.

U kodu `UMaceComponent` dobiva static mesh komponentu za kuglu i učitava `/Game/Weapons/Mace/SM_MaceBall` po putanji. Ako asset ne postoji, ostaje današnja proceduralna kugla, isto pravilo kao sivi fallback za blokove bez definicije. Polumjer za kotrljanje računa se iz bounds mesha, a ne iz konstante. Kolizijska kutija ostaje na `voxel.mace.radius` i uskladi se s veličinom novog modela nakon prvog pogleda u editoru.

Nova tekstura drške zamjenjuje PNG koji skripta danas crta. Skripta tada samo uvozi zadani PNG i ne crta ga. Ako ušica nije na pikselu (13, 2), pomakne se `ChainAnchorLocal` na sredinu novog piksela ušice.

Provjera je ista kao danas. `voxel.macetest` mora i dalje završiti s `OK`, a u editoru se pogleda veličina kugle u ruci, u vrtnji i na tlu.

## Provjera

`voxel.macetest` sam pritišće i pušta tipku. Najprije baci kuglu prema stvorenju postavljenom 5 blokova ispred igrača. Zatim baci kuglu pa je spusti na tlo i pusti. Prolaz je `[MACE] test GOTOVO: ... -> OK`. Uvjeti su da se oba puta vrati, da udaljenost ne prijeđe 5000 UU i da padne barem jedan pogodak. `voxel.mace.log 1` ispisuje promjene stanja i pogotke u igri.

Prvi izmjereni run: pogodak u letu za 6,0, pogodak vrtnjom za 3,6, a kugla je na tlu ležala na Z=375, što je vrh bloka plus polumjer kutije.

U editoru još treba provjeriti izgled: spoj lanca s drškom, veličinu kugle u mirovanju (`voxel.mace.idlescale`, `voxel.mace.idledrop`) i vidi li se vrtnja dobro u prvom licu.
