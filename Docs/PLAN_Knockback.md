# PLAN: Knockback (odbacivanje pri melee udarcu)

Status: koraci 1-6 implementirani i izmjereni headless; korak 7 (per-mob
vrijednosti) djelomicno (svi mobovi na 0 otpora), korak 8 (mob -> igrac)
odgoden do sustava zdravlja igraca. Ostaje rucni test u editoru.
Datum: 2026-10-25

## 1. Zašto udarac mačem sada ne odbacuje

Knockback **postoji** u kodu (`FirstPersonCharacter.cpp:761-770`), ali je prigušen na
četiri mjesta istovremeno:

1. **Prepolovljena jačina.** `AttackKnockback = 400` uu/s
   (`FirstPersonCharacter.h:199`) = 0.2 bloka/tick. Vanilla osnovni knockback je
   0.4 bloka/tick = **800 uu/s**.
2. **Vertikale praktički nema.** `KnockbackVertical = 150` uu/s = 0.075 bloka/tick
   umjesto vanilla 0.4. Pri UE gravitaciji 980 uu/s² to je vrh skoka od 11 uu
   (0.11 bloka) — mob se ne odvoji od tla, pa ga odmah guta trenje
   (`GroundFriction` 8, `BrakingDecelerationWalking` 2048) i zaustavi u ~0.2 s.
3. **AI odmah prepiše brzinu.** Nakon udarca Behavior Tree nastavlja `MoveTo`
   prema igraču; path following postavlja akceleraciju natrag u smjeru igrača u
   istom ili sljedećem ticku, pa i ono malo brzine ode u ništa.
4. **Nema sprint-napada.** U Minecraftu "veliki" knockback **ne dolazi od mača** —
   dolazi od sprint-hita (+0.5 bloka/tick, više od dvostrukog osnovnog). Igrač
   ovdje nema sprint (nema ga u `FirstPersonCharacter`), pa taj udarac ne postoji.

Dodatno, smjer se računa kao `(Target - Self).GetSafeNormal()` nad **3D** vektorom,
pa razlika u visini kapsula (igrač 96, zombi 88 half-height) oduzima dio
horizontalne komponente. Vanilla koristi samo XY.

## 2. Vanilla referenca (brojke)

Minecraft radi knockback kao promjenu brzine, ne kao silu:

| Pojam | Vanilla vrijednost |
|---|---|
| Osnovni knockback svakog pogotka | 0.4 bloka/tick |
| Sprint-hit (dodatno) | +0.5 bloka/tick (`sprint ? 1 : 0` × 0.5) |
| Knockback enchant | +0.5 × razina |
| Gornja granica vertikale | 0.4 bloka/tick |
| Trajanje nepovredivosti nakon pogotka | 10 tickova = 0.5 s |
| Sprint/crit zahtijeva napunjen cooldown | ≥ 84.8 % |

Redoslijed primjene (living entity):
1. jačina ×= (1 − knockback_resistance),
2. horizontalna brzina se **prepolovi**, pa se doda knockback brzina u smjeru udarca,
3. ako je na tlu: vertikalna brzina se prepolovi, doda se jačina, rezultat se
   kleše na 0.4 bloka/tick; ako je u zraku, vertikala se ne mijenja,
4. napadač koji sprinta dobije XZ brzinu ×0.6 i izgubi sprint.

Per-mob atributi (bitno jer dolaze drugi mobovi):
`knockback_resistance` 0 za većinu, 0.6 hoglin/zoglin, 0.75 ravager,
1.0 iron golem/warden; `attack_knockback` (koliko mob odbaci igrača) 1.0
hoglin/zoglin, 1.5 ravager/warden.

Vanilla **ne** mijenja knockback po oružju — mač, sjekira i gola ruka odbacuju
isto. Ako želimo da se oružja razlikuju, to je svjesno odstupanje; plan ostavlja
multiplikator u podacima, ali s defaultom 1.0, a "jak" udarac vezuje na sprint.

Izvori: [Knockback (mechanic)](https://minecraft.wiki/w/Knockback_(mechanic)),
[Melee attack](https://minecraft.wiki/w/Melee_attack),
[Attribute](https://minecraft.wiki/w/Attribute).

## 3. Pretvorba u Unreal jedinice

Blok = 100 uu, tick = 0.05 s → **1 blok/tick = 2000 uu/s**.

- osnovni knockback 0.4 b/t → **800 uu/s**
- sprint dodatak 0.5 b/t → **+1000 uu/s**
- granica vertikale 0.4 b/t → **800 uu/s**

Problem: Minecraft gravitacija je 0.08 b/t² ≈ **3200 uu/s²**, UE default je
**980 uu/s²**. S 800 uu/s vertikale i UE gravitacijom mob leti 1.63 s i diže se
3.3 bloka — lebdeći, ne Minecraft. Zato uz knockback ide i gravitacija:

- `ACreatureBase`: `GravityScale = 3.27` (≈3200 uu/s²) → vrh skoka 1 blok,
  vrijeme u zraku 0.5 s, kao vanilla.
- horizontalni zračni otpor: dok je knockback aktivan, svaki tick
  `Velocity.XY *= pow(0.91, DeltaTime / 0.05)` (vanilla horizontalni drag).

Očekivani domet: ~1.5–2.5 bloka za obični pogodak, ~4 bloka za sprint-hit.
Brojke su izvedene, ne mjerene — korak 6 ih mjeri i tada se, ako treba,
korigira samo `KnockbackBaseStrength`.

## 4. Arhitektura

Knockback mora raditi za **svaki** mob (zombi, ovca, svinja, budući) i u obje
smjerove (igrač → mob, mob → igrač), pa ide u dijeljeni sloj, ne u
`FirstPersonCharacter`.

```
UCombatLibrary (novi, Voxel/CombatLibrary.h/.cpp)
  static void ApplyKnockback(ACharacter* Victim, FVector HorizontalDir,
                             float StrengthBlocksPerTick, float Resistance)
      // vanilla formula: pola stare XY + jačina, vertikala samo ako je na tlu, kleš na 0.4 b/t

ACreatureBase (postojeći)
  float KnockbackResistance = 0.f;      // 0..1, per-mob, EditAnywhere
  float AttackKnockback = 0.f;          // dodatni knockback kad MOB udari (vanilla attack_knockback)
  float HurtImmunityTime = 0.5f;        // 10 tickova
  float LastHurtTime;                   // gate za TakeDamage
  bool  bInKnockback; float KnockbackEndTime;
  virtual void ReceiveKnockback(FVector Dir, float Strength);
  virtual void Tick(float) override;    // zračni otpor + kraj knockbacka
```

AI se ne smije boriti protiv knockbacka: u `ReceiveKnockback` →
`AIController->StopMovement()` + `BrainComponent->PauseLogic("Knockback")`, a na
kraju prozora (0.5 s ili doticanje tla) `ResumeLogic`. Time ne treba dirati BT
assete u Contentu.

Podaci o oružju ostaju u postojećem C++ registryju (`WeaponData.cpp`, isti uzorak
kao `ToolData.cpp`); dodaje se `KnockbackMultiplier` s vanilla defaultom 1.0 i
popunjavaju se sve klase alata, da gola ruka, krampa, lopata i sjekira imaju
definirane brojke umjesto fallbacka na pesnicu. Migracija u `Items.json` je
odvojen posao — u BACKLOG.

## 5. Koraci implementacije

1. **`Voxel/CombatLibrary.h/.cpp`** — `UCombatLibrary::ApplyKnockback` s vanilla
   formulom (XY pola + jačina, vertikala uz `IsMovingOnGround()`, kleš 800 uu/s,
   otpor `×(1−Resistance)`, izlaz kroz `LaunchCharacter(V, true, true)`).
   Smjer uvijek s `Z = 0` i renormaliziran.
2. **`CreatureBase.h/.cpp`** — polja iz §4, `GravityScale = 3.27`,
   `ReceiveKnockback` (otpor + pauza AI-a), `Tick` (zračni otpor 0.91/tick,
   `ResumeLogic` na kraju), te gate u `TakeDamage`: pogodak unutar
   `HurtImmunityTime` se ignorira.
3. **`WeaponData.h/.cpp`** — dopuniti registry: svi tieri mačeva (4/5/6/7/8 dmg,
   1.6 atk/s), sjekire (7/9/9/9/10, 0.8–1.0), krampe (2/3/4/5/6, 1.2), lopate
   (2.5/3.5/4.5/5.5/6.5, 1.0), pesnica (1, 4.0). `KnockbackMultiplier = 1.0`
   svugdje.
4. **Sprint + sprint-hit u `FirstPersonCharacter`** — novi `IA_Sprint`
   (Shift, Enhanced Input, `Content/Input/Actions/`), `MaxWalkSpeed` 1.3× dok
   traje (vanilla sprint), pa u `PerformMeleeAttack`:
   `Strength = 0.4 + (bSprinting && CooldownProgress >= 0.848 ? 0.5 : 0)`,
   nakon pogotka igračeva XY brzina ×0.6 i sprint se prekida.
5. **Zamjena postojećeg bloka knockbacka** u `PerformMeleeAttack` pozivom
   `Target->ReceiveKnockback(YawForwardXY, Strength * Weapon.KnockbackMultiplier)`;
   `AttackKnockback`/`KnockbackVertical` iz `FirstPersonCharacter.h` se brišu
   (jačina je sada u blokovima/tick, a ne u uu).
6. **Mjerenje i štimanje** — log `[KB]` u `ApplyKnockback` (jačina, početna
   brzina) i u `Tick` na dodir tla (prijeđena XY distanca u blokovima). Headless
   run (CLAUDE.md recept) + `Select-String -Pattern "\[KB\]"`; cilj ~1.5–2.5
   bloka obično, ~4 sprint. Korigirati samo konstantu iz §3.
7. **Per-mob vrijednosti** — `Zombie` 0 otpora; `Sheep`/`Pig` 0 (lagani mobovi
   svejedno odlete jednako — vanilla ne skalira po masi). Primjer za budućnost
   dokumentirati u ovom dokumentu: iron golem 1.0, ravager 0.75, hoglin 0.6.
8. **Mob → igrač (odgođeno, dizajn spreman)** — `AEnemyBase::PerformAttack`
   trenutno samo ispisuje "HIT!" (`Zombie.cpp:43-67`); igrač nema zdravlje. Kad
   zdravlje igrača stigne, isti poziv:
   `UCombatLibrary::ApplyKnockback(Player, DirFromMob, 0.4 + Enemy->AttackKnockback, 0)`.
   Ne implementirati prije sustava zdravlja.

## 5b. Izmjereno (headless, 2026-10-25)

Konzolna komanda `voxel.kbtest <jacina...>` (default `0.4 0.9`) nadje stvorenje
najblize igracu, pocaka da stoji na tlu i odbaci ga; `[KB]` linije u logu daju
jacinu, pocetnu/krajnju brzinu i domet pri doskoku. Headless run se nakon
zadnjeg testa sam gasi (force-kill ostavi zadnje sekunde loga u bufferu).

```
[KB] BP_Sheep_C_1  jacina=0.40 b/t  brzina V(52,-130) -> V(-748,-267,800)   doskok 2.88 bloka u 0.61 s
[KB] BP_Zombie_C_0 jacina=0.90 b/t  brzina V(0)       -> V(-1358,1182,800)  doskok 4.01 bloka u 0.29 s
```

Dakle obican pogodak ~2.9 bloka, sprint-hit ~4.0 bloka, vertikalni hop 800 uu/s
(1 blok pri gravitaciji 3200). AI se vraca u normalu: ovca nastavi wander
odmah nakon doskoka, zombi je 2.5 s kasnije opet kod igraca (`BTTask_EnemyAttack`
na 138 uu) - `PauseLogic`/`ResumeLogic` se ne zaglavljuje.

Napomena: wiki navodi "base knockback 1.552 bloka" za melee, a formula s vanilla
konstantama (0.4 b/t, drag 0.91/tick, gravitacija 0.08 b/t²) analiticki daje
~2.7 bloka, sto se poklapa s izmjerenim 2.88. Razlika prema wiki brojci nije
razrijesena; ako se domet zeli skratiti, jedini potreban zahvat je
`UCombatLibrary::BaseKnockbackStrength`.

## 5c. Odstupanja od plana pri izvedbi

1. **Sprint bez novog asseta.** `IA_Sprint` se ne može napraviti iz koda, pa
   `SprintAction` postoji kao opcionalni `UPROPERTY`; dok nije postavljen, C++
   veže Shift legacy putem (`BindKey`). Kad se napravi IA/IMC asset i postavi u
   BP, legacy grana se preskače.
2. **Tieri oružja.** Registry je popunjen samo za tipove koji postoje u
   `EItemType` (mačevi drvo→dijamant, drvena sjekira, drvena krampa 2 dmg).
   Kameni/željezni/dijamantni alati i lopate dolaze kad dobiju unos u enum i
   `Items.json` — bez toga bi item pao na sivi fallback.
3. **`KnockbackMultiplier` je 1.0 svugdje** (vanilla), umjesto prijašnjih 1.2 za
   mačeve. Razliku među oružjima nosi šteta i brzina udarca, a jak udarac sprint.
4. **Smrtni udarac ne odbacuje** — ako pogodak ubije moba, `ReceiveKnockback` se
   preskače (mob se ionako uništi u istom ticku).

## 6. Testiranje

- Headless (`-game -nullrhi`, `[KB]` markeri) pokriva brojke: jačinu, domet,
  trajanje nepovredivosti, da AI stoji tijekom prozora. **Odrađeno** — §5b.
- Ručno u editoru pokriva ono što log ne vidi i što headless ne može (nema
  inputa): da mob vidljivo odleti u smjeru pogleda pri udarcu mišem, da Shift
  sprint radi i da sprint-hit očito jače odbacuje, da mob ne propadne kroz blok
  pri doskoku. **Preostaje.**

## 7. Rizici

- `GravityScale = 3.27` mijenja i skokove/padove mobova (AI preko navmesha to ne
  osjeti, ali doskoci postaju brži) — ako smeta, alternativa je gravitaciju
  dirati samo tijekom knockback prozora.
- `PauseLogic` bez `ResumeLogic` (npr. mob umre u zraku) zaustavlja AI trajno →
  `ResumeLogic` mora ići i u `OnDeath` i uz tvrdi timeout, ne samo na dodir tla.
- Igrač i mobovi bi trebali dijeliti istu gravitaciju; igrač je zasad na 980
  (`FirstPersonCharacter.cpp:49-51`) → zabilježiti u BACKLOG kao nedosljednost.
