# Plan: Player Melee Combat (100% C++)

## Cilj
Igrač udara mobove rukama (1 dmg) i mačem (4-7 dmg). Sva logika u C++.

---

## Minecraft Referenca

| Oružje | Damage | Cooldown |
|--------|--------|----------|
| Ruka | 1 | 0.25s |
| Wooden Sword | 4 | 0.625s |
| Stone Sword | 5 | 0.625s |
| Iron Sword | 6 | 0.625s |
| Diamond Sword | 7 | 0.625s |

**Damage Multiplier**: `0.2 + (progress²) * 0.8` - brzo klikanje = manje štete

---

## Trenutno Stanje

**IMAMO:**
- `ACreatureBase::TakeDamage(float)` - mobovi primaju štetu
- `AMobBase::OnDamageTaken(AActor*)` - flee trigger
- `FirstPersonCharacter` raycast (500 UU za blokove)
- `InventoryComponent` + hotbar
- `IA_Attack` input

**NEDOSTAJE:**
- Entity raycast
- Melee attack logika
- Weapon tipovi i data

---

## Faze Implementacije

```
FAZA 1: Udaranje Rukama (C++)
├── MeleeTrace() - raycast za ACreatureBase
├── PerformMeleeAttack() - damage + knockback
├── StartAttack() modifikacija - prioritet mob > block
└── Testiranje: LMB na Sheep = 1 dmg, bježi, 8 udaraca = smrt

FAZA 2: Weapon System (C++)
├── EItemType enum += WoodenSword, StoneSword, IronSword, DiamondSword
├── FWeaponData USTRUCT
├── UWeaponDataLibrary - static helper klasa
└── Testiranje: Registry vraća ispravne podatke

FAZA 3: Sword Combat (C++)
├── GetEquippedWeaponData() - čita iz inventory slota
├── Cooldown tracking + damage multiplier
├── PerformMeleeAttack() koristi weapon data
└── Testiranje: Sword radi 4-7x više štete
```

---

## FAZA 1: Udaranje Rukama

### Datoteka: FirstPersonCharacter.h

```cpp
// Dodaj u POSTOJEĆI header, sekcija Combat

protected:
    // === MELEE COMBAT ===

    /** Range for melee attacks (3 blocks = 300 UU) */
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float MeleeRange = 300.0f;

    /** Base unarmed damage */
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float UnarmedDamage = 1.0f;

    /** Unarmed attack cooldown in seconds */
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float UnarmedCooldown = 0.25f;

    /** Knockback force applied to hit targets */
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float AttackKnockback = 400.0f;

    /** Vertical knockback component */
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    float KnockbackVertical = 150.0f;

    /** Time of last attack for cooldown tracking */
    float LastAttackTime = 0.0f;

    /** Perform raycast to find creature in melee range */
    class ACreatureBase* MeleeTrace() const;

    /** Execute melee attack on target creature */
    void PerformMeleeAttack();
```

### Datoteka: FirstPersonCharacter.cpp

```cpp
#include "CreatureBase.h"
#include "MobBase.h"

ACreatureBase* AFirstPersonCharacter::MeleeTrace() const
{
    if (!CameraComponent)
    {
        return nullptr;
    }

    FVector Start = CameraComponent->GetComponentLocation();
    FVector End = Start + CameraComponent->GetForwardVector() * MeleeRange;

    FHitResult HitResult;
    FCollisionQueryParams Params;
    Params.AddIgnoredActor(this);

    if (GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, ECC_Pawn, Params))
    {
        return Cast<ACreatureBase>(HitResult.GetActor());
    }

    return nullptr;
}

void AFirstPersonCharacter::PerformMeleeAttack()
{
    const float CurrentTime = GetWorld()->GetTimeSeconds();

    // Cooldown check
    if (CurrentTime - LastAttackTime < UnarmedCooldown)
    {
        return;
    }

    // Find target
    ACreatureBase* Target = MeleeTrace();
    if (!Target)
    {
        return;
    }

    // Update attack time
    LastAttackTime = CurrentTime;

    // Apply damage
    Target->TakeDamage(UnarmedDamage);

    // Calculate knockback direction
    FVector KnockbackDir = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal();
    FVector KnockbackForce = KnockbackDir * AttackKnockback + FVector(0.0f, 0.0f, KnockbackVertical);

    // Apply knockback
    if (ACharacter* TargetCharacter = Cast<ACharacter>(Target))
    {
        TargetCharacter->LaunchCharacter(KnockbackForce, true, true);
    }

    // Notify passive mobs to trigger flee behavior
    if (AMobBase* Mob = Cast<AMobBase>(Target))
    {
        Mob->OnDamageTaken(this);
    }

    UE_LOG(LogTemp, Log, TEXT("Melee hit %s for %.1f damage"), *Target->GetName(), UnarmedDamage);
}
```

### Modifikacija: StartAttack()

```cpp
void AFirstPersonCharacter::StartAttack()
{
    // PRIORITY 1: Attack creature if in range
    if (MeleeTrace() != nullptr)
    {
        PerformMeleeAttack();
        return;
    }

    // PRIORITY 2: Block destruction (existing logic)
    bIsAttacking = true;
}
```

---

## FAZA 2: Weapon System

### Datoteka: Block.h - Proširi EItemType

```cpp
UENUM(BlueprintType)
enum class EItemType : uint8
{
    None,

    // Blocks
    Dirt,
    Stone,
    Grass,
    OakLog,
    BirchLog,
    OakSapling,
    BirchSapling,
    OakPlanks,
    Wool,

    // Weapons
    WoodenSword,
    StoneSword,
    IronSword,
    DiamondSword
};
```

### Nova datoteka: WeaponData.h

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Block.h"
#include "WeaponData.generated.h"

/**
 * Data structure holding weapon combat statistics
 */
USTRUCT(BlueprintType)
struct FWeaponData
{
    GENERATED_BODY()

    /** Damage dealt per hit */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    float Damage = 1.0f;

    /** Attacks per second (determines cooldown) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    float AttackSpeed = 4.0f;

    /** Knockback force multiplier */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    float KnockbackMultiplier = 1.0f;

    /** Whether this is a weapon (false = fist) */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
    bool bIsWeapon = false;

    /** Calculate cooldown from attack speed */
    float GetCooldown() const
    {
        return AttackSpeed > 0.0f ? 1.0f / AttackSpeed : 0.25f;
    }

    /** Default constructor (fist) */
    FWeaponData()
        : Damage(1.0f)
        , AttackSpeed(4.0f)
        , KnockbackMultiplier(1.0f)
        , bIsWeapon(false)
    {}

    /** Parameterized constructor */
    FWeaponData(float InDamage, float InAttackSpeed, float InKnockback, bool InIsWeapon)
        : Damage(InDamage)
        , AttackSpeed(InAttackSpeed)
        , KnockbackMultiplier(InKnockback)
        , bIsWeapon(InIsWeapon)
    {}
};


/**
 * Static library for weapon data lookup
 */
UCLASS()
class MINECRAFTCLONE_API UWeaponDataLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()

public:
    /** Get weapon data for an item type */
    UFUNCTION(BlueprintPure, Category = "Weapons")
    static FWeaponData GetWeaponData(EItemType ItemType);

    /** Check if item type is a weapon */
    UFUNCTION(BlueprintPure, Category = "Weapons")
    static bool IsWeapon(EItemType ItemType);

private:
    /** Initialize weapon registry (called once) */
    static const TMap<EItemType, FWeaponData>& GetWeaponRegistry();
};
```

### Nova datoteka: WeaponData.cpp

```cpp
#include "WeaponData.h"

const TMap<EItemType, FWeaponData>& UWeaponDataLibrary::GetWeaponRegistry()
{
    // Static registry initialized once
    static TMap<EItemType, FWeaponData> Registry;

    if (Registry.Num() == 0)
    {
        //                          Damage  Speed  Knockback  IsWeapon
        Registry.Add(EItemType::None,         FWeaponData(1.0f, 4.0f, 1.0f, false));  // Fist
        Registry.Add(EItemType::WoodenSword,  FWeaponData(4.0f, 1.6f, 1.2f, true));
        Registry.Add(EItemType::StoneSword,   FWeaponData(5.0f, 1.6f, 1.2f, true));
        Registry.Add(EItemType::IronSword,    FWeaponData(6.0f, 1.6f, 1.2f, true));
        Registry.Add(EItemType::DiamondSword, FWeaponData(7.0f, 1.6f, 1.2f, true));
    }

    return Registry;
}

FWeaponData UWeaponDataLibrary::GetWeaponData(EItemType ItemType)
{
    const TMap<EItemType, FWeaponData>& Registry = GetWeaponRegistry();

    if (const FWeaponData* Data = Registry.Find(ItemType))
    {
        return *Data;
    }

    // Default to fist
    return FWeaponData();
}

bool UWeaponDataLibrary::IsWeapon(EItemType ItemType)
{
    return GetWeaponData(ItemType).bIsWeapon;
}
```

---

## FAZA 3: Sword Combat

### Datoteka: FirstPersonCharacter.h - Dodaci

```cpp
protected:
    // === WEAPON COMBAT ===

    /** Get weapon data for currently equipped item */
    FWeaponData GetEquippedWeaponData() const;

    /** Calculate damage multiplier based on cooldown progress (Minecraft formula) */
    float CalculateDamageMultiplier(float CooldownProgress) const;
```

### Datoteka: FirstPersonCharacter.cpp - Implementacija

```cpp
#include "WeaponData.h"

FWeaponData AFirstPersonCharacter::GetEquippedWeaponData() const
{
    if (!InventoryComponent)
    {
        return FWeaponData();  // Fist default
    }

    FInventorySlot CurrentSlot = InventoryComponent->GetSlot(CurrentHotbarIndex);
    return UWeaponDataLibrary::GetWeaponData(CurrentSlot.ItemType);
}

float AFirstPersonCharacter::CalculateDamageMultiplier(float CooldownProgress) const
{
    // Minecraft formula: 0.2 + (progress^2) * 0.8
    // At 0% progress: 0.2 (20% damage)
    // At 50% progress: 0.4 (40% damage)
    // At 100% progress: 1.0 (100% damage)
    float ClampedProgress = FMath::Clamp(CooldownProgress, 0.0f, 1.0f);
    return 0.2f + FMath::Square(ClampedProgress) * 0.8f;
}
```

### Ažuriran PerformMeleeAttack() - Koristi Weapon Data

```cpp
void AFirstPersonCharacter::PerformMeleeAttack()
{
    const float CurrentTime = GetWorld()->GetTimeSeconds();

    // Get equipped weapon data
    FWeaponData Weapon = GetEquippedWeaponData();
    const float Cooldown = Weapon.GetCooldown();

    // Calculate time since last attack
    const float TimeSinceAttack = CurrentTime - LastAttackTime;

    // Cooldown check - prevent attack if not ready
    if (TimeSinceAttack < Cooldown)
    {
        return;
    }

    // Find target
    ACreatureBase* Target = MeleeTrace();
    if (!Target)
    {
        // Update time even on miss (shorter cooldown on miss like Minecraft)
        LastAttackTime = CurrentTime;
        return;
    }

    // Calculate damage multiplier based on cooldown progress
    const float CooldownProgress = FMath::Clamp(TimeSinceAttack / Cooldown, 0.0f, 1.0f);
    const float DamageMultiplier = CalculateDamageMultiplier(CooldownProgress);

    // Calculate final damage
    const float FinalDamage = Weapon.Damage * DamageMultiplier;

    // Update attack time
    LastAttackTime = CurrentTime;

    // Apply damage
    Target->TakeDamage(FinalDamage);

    // Calculate knockback
    FVector KnockbackDir = (Target->GetActorLocation() - GetActorLocation()).GetSafeNormal();
    float FinalKnockback = AttackKnockback * Weapon.KnockbackMultiplier;
    FVector KnockbackForce = KnockbackDir * FinalKnockback + FVector(0.0f, 0.0f, KnockbackVertical);

    // Apply knockback
    if (ACharacter* TargetCharacter = Cast<ACharacter>(Target))
    {
        TargetCharacter->LaunchCharacter(KnockbackForce, true, true);
    }

    // Notify passive mobs to trigger flee behavior
    if (AMobBase* Mob = Cast<AMobBase>(Target))
    {
        Mob->OnDamageTaken(this);
    }

    UE_LOG(LogTemp, Log, TEXT("Melee hit %s for %.1f damage (weapon: %s, multiplier: %.2f)"),
        *Target->GetName(),
        FinalDamage,
        Weapon.bIsWeapon ? TEXT("Sword") : TEXT("Fist"),
        DamageMultiplier);
}
```

---

## Sažetak Datoteka

| Datoteka | Akcija | Opis |
|----------|--------|------|
| `FirstPersonCharacter.h` | MODIFY | + combat varijable, MeleeTrace(), PerformMeleeAttack(), GetEquippedWeaponData() |
| `FirstPersonCharacter.cpp` | MODIFY | + implementacije gore navedenih funkcija |
| `Block.h` | MODIFY | + EItemType sword tipovi |
| `Voxel/WeaponData.h` | **CREATE** | FWeaponData struct + UWeaponDataLibrary |
| `Voxel/WeaponData.cpp` | **CREATE** | Weapon registry implementacija |
| `MinecraftClone.Build.cs` | CHECK | Već ima sve potrebne module |

---

## Testiranje

### Faza 1:
```
1. Pokreni igru
2. Približi se Sheep-u (< 3 bloka)
3. LMB → Sheep prima 1 damage, odleti, bježi
4. Ponovi 8x → Sheep umire, drop wool
5. Provjeri da block destruction i dalje radi kad nema moba
```

### Faza 2-3:
```
1. Debug: Dodaj WoodenSword u hotbar slot 0
   InventoryComponent->SetSlot(0, EItemType::WoodenSword, 1);
2. Selektiraj slot 0
3. Udari Sheep → Prima 4 damage (umjesto 1)
4. 2 udarca = Sheep mrtav (8 HP / 4 dmg = 2 hits)
5. Brzo klikanje → Log pokazuje multiplier < 1.0
```
