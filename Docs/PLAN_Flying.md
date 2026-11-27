# PLAN: Letenje (Minecraft creative let, bez creative moda)

Status: implementirano i izmjereno headless (`voxel.flytest` OK); ostaje ručni test u editoru.
Datum: 2026-11-27

## 1. Što igrač dobiva

Igrač dvaput brzo pritisne razmaknicu i poleti. U zraku lebdi na mjestu kad ne
pritišće ništa, razmaknicom se diže, Shiftom spušta, a WASD ga nosi vodoravno
dvostruko brže od trčanja. Uz Ctrl leti još dvostruko brže. Let završava kad
igrač ponovno dvaput pritisne razmaknicu (tada pada) ili kad se spusti na tlo.
Ulazak u more također prekida let i igrač počinje plivati.

Creative mod ne radimo sada. Zato letenje dobiva samo jednu sklopku "smije
letjeti", uključenu po defaultu i promjenjivu konzolnom komandom
`voxel.fly.allow 0/1`. Kad kasnije dođe creative mod, on će samo postavljati tu
sklopku, a survival će je gasiti.

## 2. Kako to radi u Minecraftu (provjereno u 1.21.11 jaru)

Brojke ispod pročitane su iz bytecodea klasa `LocalPlayer`, `Player`,
`Abilities` i `AbstractClientPlayer` preko Mojang mappingsa, ne iz wikija.

Uključivanje: kad igrač pritisne skok, igra pokrene odbrojavanje od 7 tickova
(0.35 s). Ako unutar tog prozora stigne drugi pritisak, let se prebaci (uključi
ili isključi). Ako je igrač u tom trenutku na tlu, igra ga odmah i odgurne u
skok da se odvoji od tla. Prebacivanje ne radi dok igrač pliva.

Vodoravno kretanje: u letu je osnovna brzina leta 0.05 bloka po ticku, a uz
sprint dvostruko, 0.1. Svaki tick se brzini doda ta vrijednost u smjeru inputa
(input je pomnožen s 0.98), a zatim se cijela vodoravna brzina pomnoži s 0.91
(trenje zraka). Iz toga izlazi najveća brzina od 10.9 blokova u sekundi, odnosno
21.8 uz sprint. Kad igrač pusti tipke, brzina za 0.53 s padne na 37 %, a za
otprilike sekundu i pol na 5 %, pa se vidi kratko klizanje.

Okomito kretanje: dok se drži skok, svaki tick se okomitoj brzini doda
trostruka brzina leta (0.15 bloka), a dok se drži Shift isto toliko se oduzme;
oba zajedno se poništavaju. Nakon pomaka okomita brzina se pomnoži s 0.6, a
gravitacija u letu ne djeluje. Najveća okomita brzina je 7.5 blokova u sekundi
i postiže se gotovo trenutno (oko 0.1 s). Sprint ne ubrzava dizanje ni
spuštanje.

Slijetanje: kad lik u letu dotakne tlo, let se gasi. Tlo se dotiče samo
kretanjem prema dolje, pa let tik iznad tla ne prekida let.

Ostalo: dok se leti, vidno polje se proširi za 10 % (FOV množitelj 1.1).
Kopanje izvan tla je u Minecraftu pet puta sporije, ali to vrijedi i za skok
pa ga ovaj plan ne uvodi (vidi točku 6).

## 3. Kako to uvodimo u našu igru

Stanje leta pripada igraču. Lik pamti smije li letjeti i leti li trenutno, a
let se ostvaruje ugrađenim Unreal načinom kretanja "Flying", koji već ne
primjenjuje gravitaciju i već klizi uz zidove.

Ugrađeni Unreal let ubrzava i koči drukčije od Minecrafta, pa uvodimo vlastitu
podklasu komponente za kretanje lika. Ona u letu računa brzinu točno po vanilla
formuli iz točke 2, preračunatoj s tickova na sekunde, tako da ne ovisi o broju
frameova. Svi ostali načini kretanja (hod, pad, plivanje) ostaju nepromijenjeni.
Ista komponenta nakon svakog pomaka u letu provjerava je li lik pri spuštanju
udario u tlo; ako jest, prebacuje ga u hod i gasi let. Zamjena komponente radi
se u konstruktoru lika, pa blueprint lika zadržava sve postojeće postavke
kretanja.

Dvostruki pritisak razmaknice prepoznaje lik u istoj funkciji koja već pamti
drži li se skok (za plivanje). Prvi pritisak radi kao običan skok, drugi unutar
0.35 s pali ili gasi let. U vodi se pritisak ne broji.

Tipke: po dogovoru raspored prati Minecraft. Sprint prelazi sa Shifta na Ctrl i
na tlu i u letu; ponašanje sprinta ostaje isto (radi dok se tipka drži) i dalje
vrijedi za sprint-udarac i knockback. Shift u letu spušta lika, a na tlu za
sada ne radi ništa (kasnije postaje šuljanje). Ctrl trenutno nije zauzet ni u
jednoj ulaznoj mapi ni u kodu.

Voda: kad lik u letu uđe u more, Unreal ga sam prebaci u plivanje. Mi tada
samo ugasimo stanje leta. U Minecraftu se kroz vodu može letjeti, ali to bi
tražilo gašenje ugrađenog prijelaza u plivanje i drugo trenje, pa je ovo
svjesno odstupanje.

Prikaz: ruka u prvom licu sada se njiše prema vodoravnoj brzini lika bez obzira
na to je li na tlu, pa bi se u letu od 11 blokova u sekundi tresla jače nego
pri trčanju. U letu ćemo njihanje ugasiti. FOV se u letu glatko proširi za 10 %
i vrati pri slijetanju.

Postojeći swimtest koristi način "Flying" da lik lebdi iznad mora dok se
učita dno. Vlastita formula leta i slijetanje rade samo dok je naše stanje leta
uključeno, pa taj test ostaje kakav jest.

## 4. Redoslijed rada

Prvo uvodimo podklasu komponente kretanja s vanilla formulom leta i slijetanjem,
zajedno sa stanjem leta i komandom `voxel.fly.allow`. Odmah zatim dodajemo
dvostruki pritisak razmaknice i premještanje sprinta na Ctrl te Shift za
spuštanje. Nakon toga dolaze prekid leta u vodi, gašenje njihanja ruke i FOV.
Na kraju test komanda, upis u CLAUDE.md i ručni test u editoru.

## 5. Kako provjeravamo

Nova komanda `voxel.flytest` radi u headless runu i sama ugasi proces. Simulira
dvostruki pritisak skoka i provjerava da je lik u letu, pa redom drži skok,
pušta sve, leti naprijed, leti naprijed sa sprintom, pušta tipke i na kraju
drži Shift iznad kopna dok ne sleti. U log ispisuje linije `[FLY]` s brzinama i
načinom kretanja. Očekujemo dizanje oko 750 UU/s, vodoravno oko 1090 UU/s,
sprint oko 2180 UU/s, pad brzine na otprilike 37 % pola sekunde nakon puštanja i
prelazak u hod pri dodiru tla. Drugi dio testa spušta lika u more i očekuje
prelazak u plivanje uz ugašen let.

Treći dio provjerava streaming chunkova: lik 20 sekundi leti sprintom u jednom
smjeru (oko 440 blokova, preko dvadeset chunkova), a test svake sekunde ispiše
je li chunk ispod lika učitan i koliko je chunkova u redu čekanja. Ako streaming
ne stigne, to se mora vidjeti u logu prije nego što krenemo na ručni test.

Postojeći testovi (`voxel.swimtest`, `voxel.kbtest`, `-VoxelSoakTest`) moraju i
dalje prolaziti. Ručno u editoru ostaje provjeriti osjećaj leta, njihanje ruke i
FOV.

## 6. Što namjerno ne radimo

Kopanje izvan tla u Minecraftu je pet puta sporije, a naša igra to ne radi ni
za skok. To je zasebno survival pravilo i ide u BACKLOG. Također ne radimo
sprint FOV na tlu (Minecraft ga ima, mi ga nemamo ni bez leta), let kroz vodu,
spremanje stanja leta i creative mod.

## 7. Rezultat (2026-11-27)

Headless `voxel.flytest` prolazi. Dvostruki skok uključuje let, dizanje je
750 UU/s, lik nakon puštanja skoka lebdi (vZ 5), vodoravni let je 1085 UU/s,
sprint-let 2174 UU/s, a pola sekunde nakon puštanja ostaje 0.36 brzine (vanilla
0.37). Shift spusti lika na tlo za 2.5 s i let se ugasi; Shift iznad mora
prebaci lika u plivanje uz ugašen let.

Test je otkrio da streaming ne prati sprint-let. Vizuali chunkova gradili su se
samo u streaming timeru (svakih 0.25 s, budžet 8 ms), što daje oko 8 chunkova u
sekundi, a sprint-let traži oko 18. Do 107 od 169 chunkova oko igrača ostajalo
je bez prikaza. Izgradnja vizuala sada ide svaki frame s budžetom od 3 ms, a
timer i dalje vodi zahtjeve za podatke i unload. Nakon toga je najviše 13
chunkova nedovršeno, što je točno novi rub ispred igrača, i on se izgradi unutar
sekunde. Soak test (`rehash FAIL=0`, `RETAINED OK`), swimtest i kbtest prolaze
kao i prije.
