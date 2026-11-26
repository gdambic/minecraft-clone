# PLAN: Minecraft zombi

## Cilj

Postojeći zombi je Mixamo lik: kostur od 24 kosti s laktovima i koljenima, izdijeljene kutije (310 vrhova po udu) koje se pri savijanju gnječe, tekstura povećana na 4096×4096 sa šest materijala i animacije ljudskog zombija koji tetura i udara jednom rukom. Ništa od toga ne izgleda ni ne miče se kao Minecraftov zombi.

Novi zombi ima model, teksturu, animacije i Animation Blueprint kao assete u editoru, a svu logiku (brzinu hoda, napad, ozljedu, smrt, pogled) računa C++ i samo ih predaje ABP-u i materijalu.

## Izvor podataka

Sve brojke u ovom planu pročitane su iz klijentskog jara Minecrafta 1.21.11. Klase su rastavljene javapom i povezane sa službenim Mojang mappingsima, pa nijedna vrijednost nije pogođena ni prepisana s wikija.

## Tekstura i materijal

Tekstura je originalni `entity/zombie/zombie.png` veličine 64×64, uvezen s filtriranjem Nearest, bez kompresije i bez mipmapa. Područje šešira (texOffs 32,0) u toj je teksturi potpuno prozirno, pa sloj šešira nije u modelu.

Minecraft crta humanoide s render tipom entityCutoutNoCull, pa je `M_Zombie` maskiran i dvostran. Kad je stvorenje ozlijeđeno ili umire, Minecraft boju miješa s crvenom iz OverlayTexture (0xB2FF0000), tj. rezultat je 70 % izvorne boje i 30 % crvene, u prikaznom prostoru. Materijal zato boju prebaci u gamma prostor, pomiješa je s crvenom prema parametru `HurtAmount` i vrati je u linearni. Kod postavlja `HurtAmount` na 1 dok traje ozljeda (10 tickova) i cijelo vrijeme smrti.

## Model i kostur

Geometrija je `HumanoidModel.createMesh` s teksturom 64×64: glava 8×8×8, tijelo 8×12×4, ruke i noge 4×12×4 piksela. Lijeva ruka i lijeva noga koriste zrcaljeni UV desne, kao u Minecraftu. UV svake plohe kopira `ModelPart.Cube`, uključujući redoslijed vrhova i zrcaljenje. Jedan piksel je 6.25 UU, pa je zombi visok 200 UU.

Kostur ima sedam kostiju: `root` na tlu te `head`, `body`, `right_arm`, `left_arm`, `right_leg` i `left_leg` kao njegova djeca. Pivoti su na Minecraftovim mjestima: glava i tijelo na vrhu tijela (24 px iznad tla), ramena 5 px lijevo i desno i 2 px niže, kukovi 1.9 px lijevo i desno na 12 px. Svaka kutija je stopostotno vezana na svoju kost, pa se dijelovi okreću kao kruti blokovi. Kosti su ravne, a ne lanac, jer i u Minecraftu glava ne prati okret trupa.

U Unrealu mesh gleda prema +Y, kao mannequin, pa ga u blueprintu treba zakrenuti za −90° i spustiti na dno kapsule.

## Animacije

Sve animacije uzorkovane su iz Minecraftovih formula na 60 fps.

Hod je `HumanoidModel.setupAnim`: noga se njiše kao cos(walkPos × 0.6662) × 1.4 × walkSpeed, a druga noga je u protufazi. `A_Zombie_Walk` sadrži jedan puni period na walkSpeed = 1 i traje 0.5 s. U Minecraftu walkSpeed svaki tick ide 40 % prema min(pomak u blokovima × 4, 1), a walkPos raste za walkSpeed. C++ zato računa walkPos i walkSpeed, ABP iz walkPos postavlja vrijeme animacije hoda, a walkSpeed je težina miješanja s mirovanjem.

Ruke su `AnimationUtils.animateZombieArms`: obje su ispružene naprijed za π/2.25 (80°), a dok zombi ima metu (isAggressive) za π/1.5 (120°). Uz to se lagano njišu (`bobArms`: zRot oko 0.05 rad s periodom 70 tickova, xRot ±0.05 rad s periodom 94 tickova). `A_Zombie_Idle` i `A_Zombie_IdleAggressive` su petlje od 14 s u koje stane cijeli broj perioda njihanja; frekvencije su zato pomaknute za manje od 0.5 %.

Napad je zamah tipa WHACK od 6 tickova (0.3 s, `A_Zombie_Attack`). Trup se zakrene za sin(√t × 2π) × 0.2, ramena idu s njim, a ruke se iz agresivne poze spuste i opet dignu po formuli iz `animateZombieArms`. Tijekom zamaha nema njihanja ruku, što je razlika od najviše 0.05 rad.

Smrt je `LivingEntityRenderer.setupRotations`: cijeli lik se oko osi pogleda prevrne na svoj lijevi bok za min(1, √((deathTime − 1) / 20 × 1.6)) × 90°. Na tlu leži nakon 13.5 tickova, a nestaje na 20 tickova (`A_Zombie_Death`, 1 s). Ruke ostaju ispružene jer Minecraft i tijekom smrti računa pozu ruku.

Vanilla zombi nema animaciju ozljede, samo crveni tint iz materijala.

## Animation Blueprint

ABP je asset koji čita varijable iz C++ anim instance. Noge dolaze iz miješanja mirovanja i hoda prema walkSpeed. Ruke se kroz Layered Blend per Bone uzimaju iz mirne ili agresivne petlje, ovisno o tome ima li zombi metu. Napad je montage u slotu koji pokriva trup i ruke, pa noge nastavljaju hodati. Glavu okreće Transform (Modify) Bone iz `UHeadLookComponent`, kao kod ovce. Smrt je montage preko cijelog tijela.

## Kod

`AZombie` dobiva Minecraftov hitbox (0.6 × 1.95 bloka, tj. radijus kapsule 30 i polovica visine 97.5), računa walkPos i walkSpeed, pokreće montage napada i smrti, postavlja `HurtAmount` i nakon animacije smrti uklanja actora. Mesh nema physics asset jer pogotke hvata kapsula.

## Koraci

1. Tekstura, model i animacije: `Scripts/build_zombie_blender.py` gradi sve u Blenderu, a `Scripts/import_zombie.py` (Tools > MinecraftClone > Import Zombie) izvlači teksturu iz jara, pokreće Blender i uvozi assete u `/Game/Blueprints/MOBS/Zombie`.
2. C++ anim instance i logika u `AZombie`.
3. `ABP_Zombie` u editoru i novi mesh u `BP_Zombie`.
4. Headless test `voxel.zombietest` s `[ZOMBIE]` linijama u logu, pa ručni test u editoru.
5. Brisanje starih Mixamo asseta iz `Content/Blueprints/Characters/Zombie` i `Resources/`.
