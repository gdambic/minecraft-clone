# Priča o maču: od 16x16 slike do 3D modela u ruci

Ovaj dokument prati put mača kroz igru, od trenutka kad programer nacrta
teksturu do trenutka kad igrač njime zamahne. Napisan je kao priča, redom
kojim se stvari zaista događaju.

## Prije pokretanja igre

Sve počinje s običnom slikom. Programer nacrta mač kao PNG od 16x16 piksela
i importa ga u editor, gdje postane tekstura `T_Item_WoodenSword` u mapi
`Content/Items/Textures`. U `Content/Data/Items.json` mač ima zapis
`"display": { "type": "sprite", "texture": "WoodenSword" }` — time igri
kaže: "moj izgled je ova tekstura".

Zatim programer u editoru pokrene **Tools > MinecraftClone > Build Block
Materials**. Ta Python skripta (`Scripts/build_block_materials.py`) prođe
kroz sve teksture itema i namjesti ih tako da ostatak sustava može računati
na njih: isključi zamućivanje (Nearest filter, da pikseli ostanu oštri),
isključi kompresiju (tekstura ostaje sirovi BGRA format, da joj C++ kod
kasnije može čitati piksele) i isključi streaming (da su pikseli uvijek u
memoriji). Ista skripta izgradi i materijal `M_ItemSprite` — jednostavan
materijal koji prima jednu teksturu i ne crta njene prozirne piksele.

To je sva priprema. Nigdje se unaprijed ne gradi nikakav 3D model mača —
model će nastati tek u igri, iz same teksture.

## Pokretanje igre

Kad igra krene, `UBlockRegistry` (subsystem koji živi cijelu sesiju) učita
`Items.json` i sad zna da WoodenSword postoji i da se prikazuje kao sprite.
Model mača još uvijek ne postoji.

Model nastaje prvi put kad ga netko zatraži. U našoj igri to se dogodi
odmah: `AFirstPersonCharacter::BeginPlay` spawna mač kao drop tri bloka
ispred igrača, a drop pri inicijalizaciji pita registry:
`GetItemExtrudedMesh(WoodenSword)` — "daj mi 3D model ovog itema".
-- ZAŠTO TU, A NE U VoxelWorld?

Registry tada prvi (i jedini) put pokrene pravu tvornicu modela: klasu
`FItemMeshExtruder` (datoteka `Voxel/ItemMeshExtruder.cpp`). Ona kao input
uzme teksturu `T_Item_WoodenSword`, a kao output vrati gole liste geometrije
— vertekse, trokute, normale i UV koordinate. Radi to ovako: pročita svih
16x16 piksela i za svaki zapamti samo je li proziran ili nije. Od
neprozirnih piksela složi model kao sendvič — jedna velika prednja ploča,
jedna velika stražnja ploča (obje s teksturom mača), a između njih debljina
od točno jednog piksela. Onda prođe po rubovima: gdje god neproziran piksel
graniči s prozirnim, tu model treba bočnu stranicu, inače bi mač gledan sa
strane bio šupalj. Svaka bočna stranica je mali pravokutnik čije UV
koordinate pokazuju točno u taj rubni piksel teksture — pa stranica poprimi
njegovu boju. Zato mač izgleda kao da je sazidan od malih kockica, jednako
kao u Minecraftu (Minecraft ovo isto radi automatski, klasom koja se zove
ItemModelGenerator).

Registry gotovu geometriju spremi u svoj cache. Svaki sljedeći upit za
WoodenSword — bio to novi drop ili prikaz u ruci — dobije isti, već
izračunati model. Tvornica za taj item više nikad ne radi.

## Mač na podu

Drop (`AItemDrop`) uzme geometriju iz registryja i ugradi je u svoju
`ProceduralMeshComponent` komponentu — to je Unrealova komponenta koja
prima gole liste verteksa i trokuta i od njih napravi vidljiv mesh. Na nju
stavi materijal `M_ItemSprite` s teksturom mača i smanji je na četvrtinu
(mač na podu je visok 25 cm u Unreal jedinicama). Svaki frame drop se
polako vrti oko vertikalne osi i pada dok ne sleti na tlo — kao itemi u
Minecraftu.

Kad igrač priđe, overlap sfera dropa ga primijeti, mač uđe u inventory i
drop se uništi.

## Mač u ruci

Kad igrač u hotbaru odabere slot s mačem, character javi komponenti
`UFirstPersonArmComponent`: `SetHeldItem(WoodenSword)`. Ta komponenta
odlučuje što se vidi u ruci, i to uvijek istim redoslijedom pitanja: je li
slot prazan? (sakrij sve) — je li item blok? (pokaži kocku s materijalom
terena) — ima li item sprite teksturu? Mač ima, pa slijedi:

Komponenta pita registry za isti onaj cache-irani model i ugradi ga u
`HeldSpriteMesh` — `ProceduralMeshComponent` koja živi na characteru i
prikvačena je na ruku, a ruka na kameru. Zbog tog lanca prikvačivanja mač
automatski prati pogled igrača i nasljeđuje animaciju zamaha i njihanje pri
hodu, bez ijedne linije dodatnog koda. Gdje točno mač stoji u ruci određuju
tri parametra vidljiva u editoru na komponenti (kategorija Arm > Held
Item): `HeldSpriteOffset` (pozicija), `HeldSpriteRotation` (nagib —
zadano dijagonalno kao u Minecraftu) i `HeldSpriteScale` (veličina).

Jedna važna sitnica: `SetHeldItem` se poziva svaki frame, a ugradnja
geometrije nije besplatna. Zato komponenta pamti za koji je item mesh
zadnji put izgrađen i gradi ponovno samo kad igrač stvarno promijeni item.

## Kad nešto pođe po zlu

Ako tvornica ne uspije pročitati piksele (npr. netko doda novu teksturu, a
zaboravi pokrenuti Build Block Materials, pa je tekstura komprimirana), u
log ode `Error` s uputom što pokrenuti, a mač se prikaže kao običan ravan
kvadrat s teksturom — stari izgled prije ove nadogradnje. Igra dakle nikad
ne ostane bez prikaza; samo izgubi 3D efekt dok se tekstura ne popravi.

## Sudionici priče, ukratko

`build_block_materials.py` priprema teksture i materijal prije igre.
`UBlockRegistry` na zahtjev naruči model i čuva ga u cacheu.
`FItemMeshExtruder` je tvornica: tekstura ulazi, geometrija izlazi.
`AItemDrop` i `UFirstPersonArmComponent` su dva potrošača te geometrije —
jedan je vrti na podu, drugi je drži u ruci.
