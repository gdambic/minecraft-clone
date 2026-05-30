# Zombie Navigation Setup

Dokumentacija potrebnih postavki da bi se Zombie AI mogao kretati pomocu NavMesh sustava.

## 1. NavMesh Bounds Volume

- Rucno postaviti `NavMeshBoundsVolume` u editoru
- Mora pokrivati cijeli teren gdje ce se AI kretati
- Skalirati dovoljno veliko (npr. 5000 po svim osima)

## 2. Project Settings

**Navigation System:**
- Runtime Generation = **Dynamic**

## 3. Zombie C++ kod (Zombie.cpp)

### Capsule Component postavke

```cpp
GetCapsuleComponent()->InitCapsuleSize(34.0f, 88.0f);
GetCapsuleComponent()->SetCanEverAffectNavigation(false);
GetCapsuleComponent()->bDynamicObstacle = false;
```

### CubeMesh postavke

```cpp
CubeMesh->SetCanEverAffectNavigation(false);
```

### Spawn pozicija

Zombi se spawna s offsetom koji odgovara half-height kapsule:
```cpp
WorldPos.Z += 88.0f;  // half-height kapsule
```

## 4. BP_Zombie Blueprint postavke

U editoru na CapsuleComponent:
- **Can Ever Affect Navigation** = false
- **Is Dynamic Obstacle** = false (BITNO: Blueprint override-a C++ vrijednosti!)

## 5. AI Controller

- `AIControllerClass` mora biti postavljen na `AEnemyAIController`
- `AutoPossessAI` = Placed in World or Spawned

## 6. Behavior Tree

- BehaviorTree asset mora biti postavljen na zombiju
- Blackboard mora imati `TargetActor` key (Object type)
- MoveTo task koristi `TargetActor` kao cilj

## 7. Cesti problemi

### Zombie ne pada na tlo
- Spawn Z pozicija je preniska/previsoka
- Provjeriti `MovementMode` (treba biti 1=Walking ili 3=Falling)

### "Pawn on NavMesh: NO"
- Capsule utjece na NavMesh (provjeriti CanEverAffectNavigation)
- bDynamicObstacle je upaljen
- Spawn pozicija nije na walkable povrsini

### MoveTo FAILED
- NavMesh nije generiran na lokaciji zombija ili igraca
- Prevelika vertikalna udaljenost od NavMesh povrsine
- Runtime Generation nije Dynamic

### Rupa u NavMeshu oko zombija
- `Is Dynamic Obstacle` upaljen u Blueprintu
- `Can Ever Affect Navigation` upaljen

## 8. Debug tipke

| Tipka | Funkcija |
|-------|----------|
| P | Prikazi/sakrij NavMesh |
| ' (apostrof) | AI debug overlay |
| Numpad 0 | Toggle NavMesh prikaz |
| Numpad 1 | AI info |
| Numpad 2 | Behavior Tree debug |
