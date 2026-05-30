# Plan: Zombie Enemy Implementation

## Sažetak
Implementacija prvog neprijatelja (zombie) za Minecraft Clone u UE5.6 koristeći Behavior Tree sustav i NavMesh navigaciju. Arhitektura je dizajnirana za skalabilnost - lako dodavanje novih tipova neprijatelja.

## Specifikacija (prema korisničkim odgovorima)

| Mehanika | Odluka |
|----------|--------|
| **Vizual** | Jednostavan cube |
| **Navigacija** | Navigation Invokers (dinamički NavMesh) |
| **Sunlight** | Ignorira se (nema gorenja) |
| **Player HP** | Nema - samo "HIT" poruka na ekranu |
| **Zombie HP** | Besmrtan (igrač ga ne može udarati) |
| **Spawning** | Editor spawn points |
| **Detection** | Instant chase |
| **Idle** | Random wander |
| **Attack Cooldown** | 2 sekunde |

---

## Arhitektura: Hybrid (Best Practice)

```
AEnemyBase : ACharacter (abstract base)
├── Shared: Health, Detection, Attack properties
├── Virtual: PerformAttack(), OnDeath(), GetBehaviorTree()
│
└── AZombie : AEnemyBase
    └── Melee attack, wander behavior

AEnemyAIController : AAIController (shared)
└── Generic - radi s bilo kojim AEnemyBase

BT Tasks (reusable):
├── BTTask_EnemyAttack
├── BTService_CheckForTarget
└── BTTask_FindWanderLocation
```

---

# IMPLEMENTACIJSKE FAZE

Svaka faza završava s testabilnom funkcionalnošću.

---

## Faza 1: Zombie Actor (Statičan Cube)

**Cilj:** Kreirati zombija kao vidljivog cube-a u svijetu.

**Datoteke:**

1. `MinecraftClone.Build.cs` - dodati module:
```cpp
"AIModule", "GameplayTasks", "NavigationSystem"
```

2. `Voxel/EnemyBase.h/.cpp`:
```
AEnemyBase : ACharacter
├── UPROPERTY EditAnywhere:
│   ├── float MaxHealth = 20.0f
│   ├── float AttackDamage = 3.0f
│   ├── float AttackRange = 150.0f
│   ├── float AttackCooldown = 2.0f
│   ├── float DetectionRange = 3500.0f
│   ├── float WalkSpeed = 230.0f
│   └── float WanderRadius = 500.0f
├── UBehaviorTree* BehaviorTree
└── virtual PerformAttack() {}  // prazna implementacija za sada
```

3. `Voxel/Zombie.h/.cpp`:
```
AZombie : AEnemyBase
├── UStaticMeshComponent* CubeMesh (zeleni cube)
└── Constructor: postavlja default vrijednosti
```

**Editor:**
- Kreiraj `BP_Zombie` (Parent: AZombie)
- Dodaj cube mesh, scale (0.8, 0.8, 1.8) za humanoid proporcije
- Postavi materijal na zelenu boju

### TEST 1: Statičan Zombie
```
1. Drag & drop BP_Zombie u level
2. Play
✓ Očekivano: Vidiš zeleni cube koji stoji na mjestu
✓ Cube ima collision i ne propada kroz pod
```

---

## Faza 2: NavMesh Setup (Navigation Invokers)

**Cilj:** Konfigurirati dinamičku navigaciju koja radi s proceduralno generiranim svijetom.

**Pristup:** Navigation Invokers - NavMesh se generira samo oko AI agenata u runtime-u.
Ovo je best practice za proceduralne/open-world igre jer:
- Radi s bilo kojom veličinom svijeta
- Nema potrebe za unaprijed definiranim granicama
- NavMesh se generira samo gdje je potrebno

**Izvor:** [Epic Games - Using Navigation Invokers](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-navigation-invokers-in-unreal-engine)

### Korak 1: Project Settings

1. Edit → Project Settings → **Navigation System**:
   - `Generate Navigation Only Around Navigation Invokers` = ✓ (UKLJUČI)

2. Edit → Project Settings → **Navigation Mesh**:
   - `Runtime Generation` = `Dynamic`
   - Agent Radius: 34
   - Agent Height: 192
   - Cell Size: 19

### Korak 2: NavMeshBoundsVolume (AUTOMATSKI U KODU)

NavMeshBoundsVolume definira MAKSIMALNO područje gdje se NavMesh MOŽE generirati.
**Kreira se automatski** u `VoxelWorld::SetupNavigation()` nakon generacije svijeta.

Pokrivenost:
- Automatski izračunava veličinu prema `WorldSizeX`, `WorldSizeY`, `WorldSizeZ`
- Dodaje 10% marginu
- Centrira se na svijet

**NEMA RUČNOG POSTAVLJANJA U EDITORU!**

### Korak 3: NavigationInvoker komponenta na BP_Zombie

1. Otvori `BP_Zombie`
2. Add Component → `NavigationInvoker`
3. Postavke komponente:
   - `Tile Generation Radius` = 3000 (30 blokova oko zombija)
   - `Tile Removal Radius` = 5000 (50 blokova - kad zombi ode, NavMesh se briše)

### TEST 2: Navigation Invokers
```
1. Play igru
2. Pritisni "P" za NavMesh vizualizaciju
✓ Očekivano: NavMesh se pojavljuje SAMO oko zombija
✓ NavMesh prati zombija kako se kreće
3. Hodaj daleko od zombija
✓ Očekivano: NavMesh postoji samo oko zombija, ne oko igrača
```

---

## Faza 3: AI Controller + Osnovno Praćenje

**Cilj:** Zombi uvijek prati igrača (bez detekcije).

**Datoteke:**

1. `Voxel/EnemyAIController.h/.cpp`:
```
AEnemyAIController : AAIController
├── OnPossess(): RunBehaviorTree(Enemy->BehaviorTree)
└── GetControlledEnemy() → AEnemyBase*
```

**Editor:**
1. Kreiraj `BB_Enemy` (Blackboard):
   - Key: `TargetActor` (Object, Base Class: Actor)
   - Key: `WanderLocation` (Vector)

2. Kreiraj `BT_Zombie_Phase3` (privremeni jednostavni BT):
```
Root
└── Sequence
    ├── Set Blackboard Value: TargetActor = Player (hardcoded)
    └── MoveTo (TargetActor)
```

3. Update `BP_Zombie`:
   - AI Controller Class: AEnemyAIController
   - Auto Possess AI: Placed in World or Spawned
   - Behavior Tree: BT_Zombie_Phase3

### TEST 3: Osnovno Praćenje
```
1. Postavi BP_Zombie u level
2. Play
✓ Očekivano: Zombi odmah počne pratiti igrača
✓ Zombi hoda oko prepreka (blokova)
✓ Zombi se zaustavi kad dođe do igrača
```

---

## Faza 4: Detekcija Igrača

**Cilj:** Zombi prati igrača samo kad je unutar DetectionRange.

**Datoteke:**

1. `Voxel/AI/BTService_CheckForTarget.h/.cpp`:
```
UBTService_CheckForTarget : UBTService
├── FBlackboardKeySelector TargetActorKey
└── TickNode():
    ├── Pronađi igrača u svijetu
    ├── Ako udaljenost < Enemy->DetectionRange
    │   └── Postavi TargetActorKey = Player
    └── Inače
        └── Očisti TargetActorKey
```

**Editor:**
1. Update `BT_Zombie` (puni BT):
```
Root
├── Service: BTService_CheckForTarget (Interval: 0.5s)
└── Selector
    └── Sequence (CHASE)
        ├── Decorator: Blackboard "TargetActor" Is Set
        └── MoveTo (TargetActor)
```

### TEST 4: Detekcija
```
1. Postavi BP_Zombie u level
2. Postavi se daleko od zombija (>35 blokova)
3. Play
✓ Očekivano: Zombi stoji na mjestu
4. Približi se zombiju (<35 blokova)
✓ Očekivano: Zombi počne pratiti
5. Udalji se (>40 blokova)
✓ Očekivano: Zombi prestane pratiti
6. U editoru promijeni DetectionRange na 500
✓ Očekivano: Zombi te sada vidi samo kad si vrlo blizu
```

---

## Faza 5: Napad

**Cilj:** Zombi napada kad je dovoljno blizu, prikazuje "HIT!" na ekranu.

**Datoteke:**

1. Update `Voxel/EnemyBase.h/.cpp`:
```cpp
// Dodaj:
float LastAttackTime = 0.0f;
bool CanAttack() const;
```

2. Update `Voxel/Zombie.cpp`:
```cpp
void AZombie::PerformAttack()
{
    LastAttackTime = GetWorld()->GetTimeSeconds();
    GEngine->AddOnScreenDebugMessage(-1, 1.0f, FColor::Red, TEXT("HIT!"));
}
```

3. `Voxel/AI/BTTask_EnemyAttack.h/.cpp`:
```
UBTTask_EnemyAttack : UBTTaskNode
└── ExecuteTask():
    ├── Dohvati udaljenost do cilja
    ├── Ako udaljenost > AttackRange → Failed
    ├── Ako !CanAttack() (cooldown) → Failed
    ├── Pozovi Enemy->PerformAttack()
    └── Return Succeeded
```

**Editor:**
Update `BT_Zombie`:
```
Root
├── Service: BTService_CheckForTarget
└── Selector
    └── Sequence (CHASE & ATTACK)
        ├── Decorator: Blackboard "TargetActor" Is Set
        ├── MoveTo (TargetActor, AcceptableRadius=100)
        └── BTTask_EnemyAttack    ← NOVO
```

### TEST 5: Napad
```
1. Postavi BP_Zombie u level
2. Play i dopusti zombiju da te stigne
✓ Očekivano: "HIT!" se pojavljuje na ekranu
✓ "HIT!" se pojavljuje svake 2 sekunde (cooldown)
3. Udalji se od zombija
✓ Očekivano: "HIT!" prestaje
4. U editoru promijeni AttackCooldown na 0.5
✓ Očekivano: "HIT!" se pojavljuje češće
```

---

## Faza 6: Lutanje (Wander)

**Cilj:** Zombi nasumično luta kad ne vidi igrača.

**Datoteke:**

1. `Voxel/AI/BTTask_FindWanderLocation.h/.cpp`:
```
UBTTask_FindWanderLocation : UBTTaskNode
├── FBlackboardKeySelector WanderLocationKey
└── ExecuteTask():
    ├── Dohvati Enemy->WanderRadius
    ├── Generiraj random točku u tom radijusu
    ├── Validiraj na NavMesh-u
    └── Postavi WanderLocationKey
```

**Editor:**
Update `BT_Zombie` (finalna verzija):
```
Root
├── Service: BTService_CheckForTarget
└── Selector
    ├── [1] Sequence (CHASE & ATTACK)
    │   ├── Decorator: Blackboard "TargetActor" Is Set
    │   ├── MoveTo (TargetActor, AcceptableRadius=100)
    │   └── BTTask_EnemyAttack
    │
    └── [2] Sequence (WANDER)    ← NOVO
        ├── BTTask_FindWanderLocation
        ├── MoveTo (WanderLocation)
        └── Wait (Random 2-5 sec)
```

### TEST 6: Lutanje
```
1. Postavi BP_Zombie u level
2. Postavi se daleko od zombija (>35 blokova)
3. Play
✓ Očekivano: Zombi nasumično luta
✓ Zombi se kreće, stane, pa opet kreće
4. Približi se
✓ Očekivano: Zombi prestane lutati i počne te pratiti
5. Udalji se
✓ Očekivano: Zombi se vrati lutanju
```

---

## Faza 7: Spawn Points

**Cilj:** Kreirati spawn point actor za jednostavno postavljanje zombija.

**Datoteke:**

1. `Voxel/EnemySpawnPoint.h/.cpp`:
```
AEnemySpawnPoint : AActor
├── UPROPERTY EditAnywhere:
│   ├── TSubclassOf<AEnemyBase> EnemyClass
│   └── bool bSpawnOnBeginPlay = true
├── UBillboardComponent* Sprite (za vizualizaciju u editoru)
└── BeginPlay(): SpawnEnemy()
```

**Editor:**
- Kreiraj `BP_EnemySpawnPoint` (Parent: AEnemySpawnPoint)
- Postavi EnemyClass default na BP_Zombie

### TEST 7: Spawn Points
```
1. Postavi BP_EnemySpawnPoint u level (ne BP_Zombie direktno)
2. U Details postavi EnemyClass = BP_Zombie
3. Play
✓ Očekivano: Zombi se spawna na poziciji spawn pointa
✓ Spawn point nije vidljiv u igri (samo editor)
4. Postavi više spawn pointova
✓ Očekivano: Više zombija se spawna
```

---

## Finalna Struktura Datoteka

```
Source/MinecraftClone/Voxel/
├── EnemyBase.h/.cpp           (Faza 1)
├── Zombie.h/.cpp              (Faza 1)
├── EnemyAIController.h/.cpp   (Faza 3)
├── EnemySpawnPoint.h/.cpp     (Faza 7)
└── AI/
    ├── BTService_CheckForTarget.h/.cpp   (Faza 4)
    ├── BTTask_EnemyAttack.h/.cpp         (Faza 5)
    └── BTTask_FindWanderLocation.h/.cpp  (Faza 6)

Content/AI/
├── BB_Enemy.uasset            (Faza 3)
└── BT_Zombie.uasset           (Faza 3, update u 4,5,6)

Content/Blueprints/Enemies/
├── BP_Zombie.uasset           (Faza 1)
└── BP_EnemySpawnPoint.uasset  (Faza 7)
```

---

## Sažetak Faza

| Faza | Što se implementira | Test |
|------|---------------------|------|
| 1 | EnemyBase + Zombie (cube) | Vidiš zeleni cube u igri |
| 2 | Navigation Invokers | NavMesh samo oko zombija |
| 3 | AIController + BT (chase) | Zombi uvijek prati |
| 4 | Detection service | Zombi prati samo kad si blizu |
| 5 | Attack task | "HIT!" na ekranu |
| 6 | Wander task | Zombi luta kad nema cilja |
| 7 | Spawn points | Zombi se spawna iz spawn pointa |

---

## Dodavanje Novog Neprijatelja (Buduće)

Za dodavanje npr. Skeleton-a:

1. **C++:** Kreiraj `ASkeleton : AEnemyBase`
   - Override `PerformAttack()` za ranged napad

2. **Editor:** Kreiraj `BT_Skeleton`
   - Koristi iste BT Tasks
   - Drugačija struktura

3. **Editor:** Kreiraj `BP_Skeleton`

**Nema potrebe za novim AI Controllerom ili BT Tasks!**

---

## Izvori

**Minecraft Behavior:**
- [Minecraft Wiki - Zombie](https://minecraft.wiki/w/Zombie)
- [Minecraft Fandom - Zombie](https://minecraft.fandom.com/wiki/Zombie)

**UE5 Architecture Best Practices:**
- [Epic Games - Inheritance, Composition, Interfaces](https://dev.epicgames.com/community/learning/tutorials/kBj8/)
- [Kolosdev - Base Classes & Architecture](https://kolosdev.com/2025/05/30/unreal-engine-base-classes-architecture/)

**UE5 Navigation (Procedural Worlds):**
- [Epic Games - Using Navigation Invokers](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-navigation-invokers-in-unreal-engine)
- [Epic Forums - NavMesh for Procedural Levels](https://forums.unrealengine.com/t/how-to-build-navmesh-for-dynamically-sized-procedural-level/290586)
