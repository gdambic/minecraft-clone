# Pukotine pri kopanju bloka

Status: **implementirano** (2026-11-21), čeka ručnu provjeru izgleda u editoru.

## Što igrač vidi

Dok igrač drži lijevu tipku na bloku, preko bloka se pojavljuju pukotine koje
rastu u deset faza, iste kao u Minecraftu. Kad pukotine pokriju cijeli blok,
blok pukne. Ako igrač pusti tipku, pogleda drugi blok ili promijeni item u
ruci, pukotine nestaju i kopanje kreće ispočetka. Trajanje kopanja je isto kao
prije, samo se sada zaokružuje na cijele Minecraftove tickove (1/20 s).

## Kako Minecraft crta pukotine

Minecraft ima jedan set od deset slika pukotina (`destroy_stage_0` do
`destroy_stage_9`, 16×16 piksela) i koristi ga za sve blokove. Kad se blok
kopa, igra njegovu geometriju nacrta još jednom, ovaj put sa slikom trenutne
faze. Iz klijentskog jara (verzija 1.21.11) pročitali smo kako se taj drugi
crtež miješa s blokom: konačna boja je dvostruki umnožak boje pukotine i boje
bloka. Siva boja od 50 % zato ne mijenja blok, tamni pikseli pukotine ga
zatamne, a svijetli posvijetle. Pikseli slike koji su gotovo prozirni se ne
crtaju.

## Kako smo to napravili

Kopanje i crtanje pukotina su odvojeni, jer će u multiplayeru o kopanju
odlučivati server, a pukotine će crtati svaki klijent.

O kopanju odlučuje nova komponenta na igraču, `UBlockMiningComponent`. Ona
zna u koji blok igrač gleda, drži li tipku i koji item ima u ruci. Iz
registryja blokova izračuna koliko kopanje traje, broji vrijeme i kad je
blok gotov, ruši ga preko `AVoxelWorld::DestroyBlockAt`. Ako blok zahtijeva
alat koji igrač nema, blok se ruši bez dropa. Toj je komponenti predana i
logika koja je prije bila u `ABlock`-u, uključujući zvuk udarca, pa promovirani
actor više ne sudjeluje u kopanju.

Pri svakoj promjeni faze komponenta javi svijetu koju fazu treba prikazati na
kojem bloku. Svijet to prosljeđuje komponenti `UBlockCrackComponent`, koja
na mjesto bloka postavi kocku malo veću od samog bloka i na nju stavi sliku
faze. Kocka je veća za desetinku jedinice po strani, da se ne bori s plohama
bloka za isti piksel. Kad je blok uništen ili mu se chunk unloada, pukotina
se automatski briše. Svaki igrač ima svoju pukotinu, pa će isto raditi i
kad više igrača kopa istovremeno.

Slike pukotina i materijal uvozi skripta `Scripts/import_block_cracks.py`
(Tools > MinecraftClone > Import Block Cracks). Ona slike izvuče izravno iz
Minecraft jara na računalu, uveze ih u `Content/Blocks/Crack` i izgradi
materijal `M_BlockCrack`, koji ponavlja Minecraftovo miješanje boja. Jedina
razlika je u tome što Unreal množi boje u linearnom prostoru, a Minecraft u
prikaznom, pa materijal faktor pukotine diže na potenciju 2,2 da izgled
ostane isti.

## Provjera

Komanda `voxel.cracktest` kopa blok ispod igrača simuliranim frameovima i za
svaku fazu ispiše liniju `[CRACK]` u log. U headless runu
(`-ExecCmds="voxel.cracktest None WoodenPickaxe"`) faze idu od 0 do 9 redom,
a trajanja odgovaraju `Blocks.json`: trava i zemlja 1,5 s, kamen golom rukom
3,0 s, kamen drvenom krampom 1,5 s. Soak test streaminga i dalje prolazi.

Izgled se ne može provjeriti headless i ostaje ručni test u editoru:
pukotine moraju biti poravnate s pikselima teksture na svih šest strana, ne
smiju treperiti izbliza ni iz daljine i trebaju izgledati jednako jako kao u
Minecraftu. Za usporedbu služe `voxel.crack.opacity` (jačina, 0 do 1),
`voxel.crack.inflate` (koliko je kocka veća od bloka) i `voxel.crack.stage N`,
koja na bloku u koji igrač gleda prikaže fazu N bez kopanja.

## Što još nije napravljeno

Čestice pri udarcu i lomu, pauza od 0,3 s između lomljenja dva bloka, sporije
kopanje u zraku i pod vodom, tvrdoća (`hardness`) umjesto sekundi u
`Blocks.json` i oznaka da kamen zahtijeva krampu. Za multiplayer treba još
pokretati komponentu za kopanje na serveru i slati faze pukotina klijentima;
struktura za to je spremna.
