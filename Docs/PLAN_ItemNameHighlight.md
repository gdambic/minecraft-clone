# Plan: ime odabranog itema iznad hotbara

Referenca: `Docs/images/tmp.png` — natpis "Stone Pickaxe" iznad hotbara nakon
odabira krampe.

## Kako to radi Minecraft

U Minecraftu ovo nije tooltip nego "selected item name" (u kodu `Gui`, polja
`toolHighlightTimer` i `lastToolHighlight`). Svaki tick klijent usporedi item u
odabranom hotbar slotu s itemom iz prošlog ticka. Ako je odabrani slot prazan,
natpis odmah nestaje. Ako je item drugačiji (drugi tip ili drugo ime), timer se
postavi na 40 tickova, tj. 2 sekunde. Ako je isti, timer samo odbrojava.

Bitno je da se natpis ne pali samo na promjenu slota, nego i kad se promijeni
sadržaj odabranog slota: kad pokupiš item u prazan odabrani slot ili kad se
alat slomi i na njegovo mjesto dođe drugi item. Promjena količine istog itema
natpis ne pali ponovno. Promjena durabilityja ga je nekad palila, ali Mojang je
to proglasio bugom i ispravio (MC-122257), pa ni to ne smije paliti natpis.

Tekst je bijel, sa sjenom, vodoravno centriran, i stoji neposredno iznad srca i
hrane (oko 59 GUI piksela od dna ekrana u survivalu). Zadnjih pola sekunde
(10 tickova) alfa linearno pada do nule, pa natpis izblijedi umjesto da naglo
nestane. Rijetki i enchantani itemi imaju drugu boju, a preimenovani su u
kurzivu; to kod nas zasad ne postoji.

Brojke 40 tickova i 10 tickova fade-a potječu iz dekompiliranog `Gui.java` i
nisu potvrđene javnim izvorom. Prije finalnog štimanja treba ih izmjeriti na
videu iz Minecrafta (broj frameova od pojave do nestanka natpisa).

## Kako to napraviti kod nas

Logika ide u `UHotbarWidget`, jer on već sluša `OnSelectedItemChanged` s
charactera i `OnSlotChanged` s inventara, pa zna točno one dvije situacije koje
pale natpis. Widget pamti posljednji prikazani tip itema. Kad se promijeni
odabrani slot, ili kad stigne `OnSlotChanged` za slot koji je trenutno odabran,
widget usporedi novi tip s zapamćenim. Ako je slot prazan, sakrije natpis. Ako
je tip drugačiji, upiše `DisplayName` iz `UBlockRegistry` (isti izvor koji već
koristi `InventorySlotWidget` za svoj tooltip) i pokrene timer na 2 sekunde.
Ako je tip isti, ne radi ništa, pa promjena količine ne resetira prikaz.

Natpis je jedan `UTextBlock` s imenom `SelectedItemName`, dodan u Blueprint
hotbara iznad srca kao opcionalni `BindWidgetOptional`, tako da stari Blueprint
radi i bez njega. Odbrojavanje i fade rade se u `NativeTick` widgeta: dok je
timer iznad pola sekunde alfa je 1, a u zadnjih pola sekunde pada linearno do
nule, nakon čega se tekst skriva. Trajanje i fade su `UPROPERTY` vrijednosti s
defaultom 2.0 i 0.5 sekundi, da se mogu uskladiti s mjerenjem iz videa.

Za izgled treba bijela boja i crna sjena pomaknuta za jedan "piksel" fonta,
što `UTextBlock` ima ugrađeno (Shadow Offset, Shadow Color). Projekt trenutno
nema Minecraft pixel font; dok ga ne uvezemo, koristi se zadani font, a uvoz
fonta je zaseban korak koji vrijedi i za brojeve količine u slotovima.

Pri pokretanju igre natpis se ne prikazuje, jer Minecraft na ulasku u svijet
ne pokazuje ime itema koji je već u ruci. To znači da `RefreshAllSlots` samo
zapamti trenutni tip bez paljenja timera.

## Provjera

Ovo je čisti UI, pa headless run ne može provjeriti izgled, ali može
provjeriti logiku. Za to bi trebala konzolna komanda (npr. `voxel.nametest`)
koja redom simulira odabir slota s itemom, odabir istog itema u drugom slotu,
odabir praznog slota, dodavanje itema u odabrani slot i promjenu količine, te
za svaki korak u log ispiše `[ITEMNAME]` liniju s tekstom i preostalim
vremenom. Komandu dodaješ ti; ja ću reći gdje i što točno treba pozvati.
Izgled, poziciju i fade treba potvrditi ručno u editoru.

## Što ostaje za kasnije

Boja po rijetkosti, kurziv za preimenovane iteme i Minecraft pixel font. Ništa
od toga ne mijenja gore opisanu logiku; samo dodaje podatke u `Items.json` i
stil na `SelectedItemName`.
