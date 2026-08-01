# PLAN: Sheep (Friendly Mob) Implementation

## Pregled

Implementacija passive/friendly moba "Sheep" koji luta mapom, bježi od igrača kad primi štetu, i ispušta Wool kad ugine.

## Arhitektura

Nova hijerarhija klasa sa zajedničkom baznom klasom `ACreatureBase`:

```
ACharacter (UE5)
└── ACreatureBase (NOVO - zajednička bazna klasa)
    │   Health, Movement, AI
    │
    ├── AEnemyBase (refaktorirano - combat + detection)
    │   └── AZombie
    │
    └── AMobBase (NOVO - flee behavior)
        └── ASheep
```

### Prednosti ACreatureBase pristupa

1. **DRY princip** - nema duplikacije zajedničkih propertija (Health, Speed, Wander, BehaviorTree)
2. **BTTask_FindWanderLocation** može castati na `ACreatureBase` umjesto specifično na `AEnemyBase`
3. **Lakše dodavanje novih tipova** - npr. `ASpider` (enemy), `ACow` (passive)
4. **Konzistentnost** - svi mobovi/neprijatelji imaju isti interface za health/death

---

## C++ Klase

### 1. ACreatureBase (Voxel/CreatureBase.h/.cpp) - NOVO

Apstraktna bazna klasa za SVE creature (hostile i passive).

**Properties:**
```cpp
// Stats
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature|Stats")
float MaxHealth = 20.0f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature|Stats")
float CurrentHealth = 20.0f;

// Movement
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature|Movement")
float WalkSpeed = 200.0f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature|Movement")
float WanderRadius = 500.0f;

// AI
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Creature|AI")
UBehaviorTree* BehaviorTree;
```

**Metode:**
```cpp
UFUNCTION(BlueprintCallable, Category = "Creature|AI")
UBehaviorTree* GetBehaviorTree() const { return BehaviorTree; }

UFUNCTION(BlueprintCallable, Category = "Creature|Movement")
float GetWanderRadius() const { return WanderRadius; }

UFUNCTION(BlueprintCallable, Category = "Creature|Stats")
virtual void OnDeath();

protected:
virtual void BeginPlay() override;
```

**Konstruktor:**
- Konfigurira CharacterMovementComponent (MaxWalkSpeed, bOrientRotationToMovement, RotationRate)
- Disable controller rotation (bUseControllerRotationYaw = false)

**BeginPlay:**
- Primjenjuje WalkSpeed na movement component
- Inicijalizira CurrentHealth = MaxHealth

---

### 2. AEnemyBase (Voxel/EnemyBase.h/.cpp) - REFAKTORIRANO

Sada nasljeđuje `ACreatureBase`. Sadrži samo combat i detection specifične propertije.

**Promjene:**
```cpp
// PRIJE:
class AEnemyBase : public ACharacter

// POSLIJE:
#include "CreatureBase.h"
class AEnemyBase : public ACreatureBase
```

**UKLONITI iz EnemyBase (prebačeno u CreatureBase):**
- `MaxHealth`, `CurrentHealth`
- `WalkSpeed`, `WanderRadius`
- `BehaviorTree`, `GetBehaviorTree()`

**ZADRŽATI u EnemyBase (enemy-specific):**
```cpp
// Combat
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Stats")
float AttackDamage = 3.0f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Stats")
float AttackRange = 150.0f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Stats")
float AttackCooldown = 2.0f;

// Detection
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Detection")
float DetectionRange = 3500.0f;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Enemy|Detection")
float LoseTargetRange = 4000.0f;

// Combat methods
bool CanAttack() const;
virtual void PerformAttack();
```

---

### 3. AMobBase (Voxel/MobBase.h/.cpp) - NOVO

Apstraktna bazna klasa za passive mobove. Nasljeđuje `ACreatureBase`.

**Properties:**
```cpp
// Flee behavior
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mob|Flee")
float FleeDistance = 800.0f;        // 8 blokova - koliko daleko bježi

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mob|Flee")
float ThreatDetectionRange = 300.0f; // 3 bloka - kad igrač priđe preblizu
```

**Metode:**
```cpp
UFUNCTION(BlueprintCallable, Category = "Mob|Flee")
float GetFleeDistance() const { return FleeDistance; }

UFUNCTION(BlueprintCallable, Category = "Mob|Flee")
float GetThreatDetectionRange() const { return ThreatDetectionRange; }
```

**Default vrijednosti:**
- MaxHealth = 10.0f (nasljeđeno, override)
- WalkSpeed = 150.0f (sporije od Zombija)
- WanderRadius = 400.0f

---

### 4. AMobAIController (Voxel/MobAIController.h/.cpp) - NOVO

AI Controller za passive mobove. Slična struktura kao EnemyAIController.

**Komponente:**
```cpp
UPROPERTY()
UBehaviorTreeComponent* BehaviorTreeComponent;

UPROPERTY()
UBlackboardComponent* BlackboardComponent;
```

**Metode:**
```cpp
virtual void OnPossess(APawn* InPawn) override;
virtual void OnUnPossess() override;

UFUNCTION(BlueprintCallable, Category = "AI")
AMobBase* GetControlledMob() const;

// Blackboard helpers
void SetThreatActor(AActor* Threat);
void ClearThreatActor();
```

---

### 5. ASheep (Voxel/Sheep.h/.cpp) - NOVO

Konkretna implementacija ovce.

**Properties:**
```cpp
UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sheep|Loot")
int32 WoolDropMin = 1;

UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Sheep|Loot")
int32 WoolDropMax = 3;
```

**Metode:**
```cpp
virtual void OnDeath() override;  // Spawna wool drops
void DropWool();                  // Helper za spawn
```

**Default vrijednosti (Minecraft-accurate):**
- MaxHealth = 8.0f (4 srca)
- WalkSpeed = 140.0f
- WanderRadius = 500.0f

---

### 6. BTService_DetectThreat (Voxel/AI/BTService_DetectThreat.h/.cpp) - NOVO

BT Service koji detektira prijetnju (igrač preblizu ili nakon primljene štete).

**Properties:**
```cpp
UPROPERTY(EditAnywhere, Category = "Blackboard")
FBlackboardKeySelector ThreatActorKey;

UPROPERTY(EditAnywhere, Category = "Blackboard")
FBlackboardKeySelector ThreatLocationKey;
```

**Logika:**
1. Provjerava udaljenost do igrača
2. Ako igrač unutar ThreatDetectionRange -> postavlja ThreatActor
3. Ako igrač izvan FleeDistance -> briše ThreatActor
4. Također reagira na damage events

---

### 7. BTTask_FindFleeLocation (Voxel/AI/BTTask_FindFleeLocation.h/.cpp) - NOVO

BT Task koji pronalazi lokaciju suprotno od prijetnje.

**Properties:**
```cpp
UPROPERTY(EditAnywhere, Category = "Blackboard")
FBlackboardKeySelector ThreatActorKey;

UPROPERTY(EditAnywhere, Category = "Blackboard")
FBlackboardKeySelector FleeLocationKey;
```

**Logika:**
1. Dohvati ThreatActor iz Blackboarda
2. Izračunaj smjer suprotan od prijetnje
3. Nađi točku na NavMeshu u tom smjeru (FleeDistance)
4. Spremi u FleeLocationKey

---

## Potrebne izmjene postojećeg koda

### 1. BTTask_FindWanderLocation.cpp

Promijeniti cast s `AEnemyBase` na `ACreatureBase`:

```cpp
// PRIJE:
#include "../EnemyBase.h"
...
AEnemyBase* Enemy = Cast<AEnemyBase>(AIController->GetPawn());
...
NavSys->GetRandomReachablePointInRadius(Origin, Enemy->WanderRadius, RandomLocation);

// POSLIJE:
#include "../CreatureBase.h"
...
ACreatureBase* Creature = Cast<ACreatureBase>(AIController->GetPawn());
...
NavSys->GetRandomReachablePointInRadius(Origin, Creature->GetWanderRadius(), RandomLocation);
```

### 2. EItemType enum (Block.h)

Dodati novi item type:
```cpp
UENUM(BlueprintType)
enum class EItemType : uint8
{
    None,
    Dirt,
    Stone,
    Grass,
    OakLog,
    BirchLog,
    OakSapling,
    BirchSapling,
    OakPlanks,
    Wool,        // NOVO
    Mutton       // NOVO (opcionalno)
};
```

### 3. MinecraftClone.Build.cs

Već ima potrebne module (AIModule, GameplayTasks, NavigationSystem) - NEMA IZMJENA.

---

## Content (Editor)

### BB_Mob (Blackboard Asset)

Lokacija: `Content/Blueprints/Mobs/BB_Mob`

| Key | Type | Opis |
|-----|------|------|
| WanderLocation | Vector | Ciljna lokacija za lutanje |
| ThreatActor | Object (Actor) | Trenutna prijetnja (igrač) |
| FleeLocation | Vector | Lokacija za bijeg |

### BT_Sheep (Behavior Tree)

Lokacija: `Content/Blueprints/Mobs/BT_Sheep`

```
Root
└── Selector
    │
    ├── Sequence [Flee from threat]
    │   ├── Decorator: Blackboard - ThreatActor IS SET
    │   ├── BTTask_FindFleeLocation
    │   ├── BTTask_RotateToTarget (prema FleeLocation) [postojeći]
    │   └── Move To (FleeLocation)
    │
    └── Sequence [Idle wander]
        ├── BTTask_FindWanderLocation [sada radi s ACreatureBase]
        ├── Wait (Random 2-5 sec)
        └── Move To (WanderLocation)

Service na Root:
└── BTService_DetectThreat (Interval: 0.25s)
```

### BP_Sheep (Blueprint)

Lokacija: `Content/Blueprints/Mobs/BP_Sheep`

**Setup:**
1. Parent Class: ASheep
2. Skeletal Mesh: (placeholder kocka ili UE5 Mannequin)
3. Capsule Collision: Height=90, Radius=40
4. AI Controller Class: AMobAIController
5. BehaviorTree: BT_Sheep

---

## Struktura datoteka

```
Source/MinecraftClone/Voxel/
├── CreatureBase.h          <- NOVO
├── CreatureBase.cpp         <- NOVO
├── EnemyBase.h              <- IZMJENA (parent class)
├── EnemyBase.cpp            <- IZMJENA (pojednostavljenje)
├── MobBase.h                <- NOVO
├── MobBase.cpp              <- NOVO
├── MobAIController.h        <- NOVO
├── MobAIController.cpp      <- NOVO
├── Sheep.h                  <- NOVO
├── Sheep.cpp                <- NOVO
└── AI/
    ├── BTTask_FindWanderLocation.cpp  <- IZMJENA (cast na ACreatureBase)
    ├── BTService_DetectThreat.h       <- NOVO
    ├── BTService_DetectThreat.cpp     <- NOVO
    ├── BTTask_FindFleeLocation.h      <- NOVO
    └── BTTask_FindFleeLocation.cpp    <- NOVO

Content/Blueprints/Mobs/
├── BB_Mob.uasset
├── BT_Sheep.uasset
└── BP_Sheep.uasset
```

---

## Plan implementacije

### Faza 1: ACreatureBase bazna klasa
- [ ] Kreirati CreatureBase.h/.cpp
- [ ] Prebaciti zajedničke propertije iz EnemyBase

### Faza 2: Refaktorirati AEnemyBase
- [ ] Promijeniti parent class na ACreatureBase
- [ ] Ukloniti duplirane propertije
- [ ] Testirati da Zombie i dalje radi

### Faza 3: Ažurirati BTTask_FindWanderLocation
- [ ] Promijeniti cast na ACreatureBase
- [ ] Testirati wandering ponašanje

### Faza 4: AMobBase i ASheep
- [ ] Kreirati MobBase.h/.cpp
- [ ] Kreirati Sheep.h/.cpp
- [ ] Dodati Wool u EItemType
- [ ] Implementirati OnDeath() s wool dropom

### Faza 5: MobAIController
- [ ] Kreirati MobAIController.h/.cpp
- [ ] Implementirati OnPossess/OnUnPossess

### Faza 6: BT komponente za Sheep
- [ ] Kreirati BTService_DetectThreat
- [ ] Kreirati BTTask_FindFleeLocation

### Faza 7: Editor setup
- [ ] Kreirati BB_Mob Blackboard
- [ ] Kreirati BT_Sheep Behavior Tree
- [ ] Kreirati BP_Sheep Blueprint

### Faza 8: Testiranje
- [ ] Postaviti NavMesh na mapu
- [ ] Spawn Sheep
- [ ] Testirati wander ponašanje
- [ ] Testirati flee ponašanje
- [ ] Testirati wool drop

---

## Datoteke koje NE treba mijenjati

| Datoteka | Razlog |
|----------|--------|
| Zombie.h/.cpp | Nasljeđuje AEnemyBase - radi automatski |
| EnemyAIController.h/.cpp | Koristi AEnemyBase metode - OK |
| BTService_FindPlayer.cpp | Koristi DetectionRange/LoseTargetRange - ostaje u AEnemyBase |
| BTTask_EnemyAttack.cpp | Koristi AttackRange/CanAttack/PerformAttack - ostaje u AEnemyBase |
| EnemySpawnPoint.h/.cpp | Template TSubclassOf<AEnemyBase> - radi automatski |

---

## Minecraft reference

| Svojstvo | Minecraft vrijednost | Naša implementacija |
|----------|---------------------|---------------------|
| Health | 8 (4 srca) | 8.0f |
| Speed | Sporo | 140 UU/s |
| Drops | 1 Wool, 1-2 Mutton (raw) | 1-3 Wool |
| Behavior | Passive, wanders, flees when hit | Isto |
| Detection | Bježi kad primi štetu | + bježi kad igrač preblizu |
