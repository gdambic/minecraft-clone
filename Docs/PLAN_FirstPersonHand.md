# Plan: First Person Hand System (100% C++)

## Cilj
Vidljiva ruka igrača u donjem desnom kutu ekrana s idle i attack animacijom, plus zvučni efekti.

---

## Minecraft Referenca

### Pozicija Ruke
- Donji desni kut ekrana
- Vidljiva samo desna ruka (ili held item)
- Blagi "bob" pri hodanju

### Swing Animacija
- **Default trajanje**: 6 ticks = 0.3 sekunde (20 ticks/sec)
- **Tipovi swinga**: "whack" (default), "stab", "none"
- Swing se pokreće na LMB bez obzira pogodi li se nešto

### Idle State
- Ruka miruje, blagi breathing/idle movement
- Held item se prikazuje umjesto prazne ruke

---

## Trenutno Stanje Projekta

### Što IMAMO:
- `FirstPersonCharacter` s kamerom (bez vidljive ruke)
- Melee combat sustav (`PerformMeleeAttack()`)
- Weapon data sustav (`FWeaponData`)
- Mannequin skeletal mesh (`SK_Mannequin`)
- Attack animacije (`MM_Attack_01/02/03`)
- Inventory s weapon slotovima

### Što NEDOSTAJE:
- First-person arm mesh component
- Swing animacija sustav
- Hand bobbing pri kretanju
- Zvučni efekti (swing, hit)
- Held item prikaz (sword mesh)

---

## Arhitektura (C++)

```
┌─────────────────────────────────────────────────────────────┐
│  UFirstPersonArmComponent (novi ActorComponent)             │
│  ─────────────────────────────────────────────────────────  │
│  • USkeletalMeshComponent za ruku                           │
│  • UStaticMeshComponent za held item (sword)                │
│  • Swing animation state machine                            │
│  • Hand bobbing logic                                       │
│  • Sound playback                                           │
└─────────────────────────────────────────────────────────────┘
                          │
                          ▼
┌─────────────────────────────────────────────────────────────┐
│  AFirstPersonCharacter                                      │
│  ─────────────────────────────────────────────────────────  │
│  • Posjeduje UFirstPersonArmComponent                       │
│  • Poziva PlaySwingAnimation() iz PerformMeleeAttack()     │
│  • Update held item kad se promijeni hotbar slot            │
└─────────────────────────────────────────────────────────────┘
```

---

## Faze Implementacije

```
FAZA 1: Arm Mesh Setup
├── UFirstPersonArmComponent klasa
├── USkeletalMeshComponent za ruku (attach na kameru)
├── Pozicija: donji desni kut (offset od kamere)
├── Samo owner vidi (SetOnlyOwnerSee)
└── Test: Vidljiva ruka u igri

FAZA 2: Swing Animation
├── PlaySwingAnimation() funkcija
├── Animation Timeline (C++ FTimeline)
├── Rotacija ruke: idle → swing arc → idle
├── Swing duration ovisi o weapon attack speed
└── Test: LMB pokreće swing

FAZA 3: Hand Bobbing
├── Tick-based bobbing kad igrač hoda
├── Sin/Cos wave za X/Y offset
├── Povezano s character velocity
└── Test: Hodanje = ruka se njiše

FAZA 4: Sound Effects
├── USoundBase* za swing i hit zvukove
├── PlaySound u PerformMeleeAttack()
├── Swing sound uvijek, hit sound samo na pogodak
└── Test: Zvuk pri napadu

FAZA 5: Held Item Display
├── UStaticMeshComponent za sword/item
├── Socket attachment na ruku
├── Update mesh kad se promijeni hotbar slot
├── Različiti meshevi za svaki sword tip
└── Test: Sword vidljiv u ruci
```

---

## FAZA 1: Arm Mesh Setup

### Nova datoteka: FirstPersonArmComponent.h

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ItemType.h"
#include "FirstPersonArmComponent.generated.h"

class USkeletalMeshComponent;
class UStaticMeshComponent;
class USoundBase;
class UAnimMontage;

/**
 * Component that handles first-person arm rendering, animations, and sounds.
 * Attach to FirstPersonCharacter for Minecraft-style hand display.
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class MINECRAFTCLONE_API UFirstPersonArmComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UFirstPersonArmComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    // === ARM MESH ===

    /** Skeletal mesh for the arm */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arm")
    USkeletalMeshComponent* ArmMesh;

    /** Static mesh for held item (sword, etc.) */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arm")
    UStaticMeshComponent* HeldItemMesh;

    // === POSITIONING ===

    /** Base offset from camera (lower right) */
    UPROPERTY(EditDefaultsOnly, Category = "Arm|Position")
    FVector ArmBaseOffset = FVector(50.0f, 25.0f, -30.0f);

    /** Base rotation of arm */
    UPROPERTY(EditDefaultsOnly, Category = "Arm|Position")
    FRotator ArmBaseRotation = FRotator(0.0f, 0.0f, 0.0f);

    // === SWING ANIMATION ===

    /** Play swing animation (called on attack) */
    UFUNCTION(BlueprintCallable, Category = "Arm|Animation")
    void PlaySwingAnimation(float Duration);

    /** Is swing animation currently playing */
    UFUNCTION(BlueprintPure, Category = "Arm|Animation")
    bool IsSwinging() const { return bIsSwinging; }

    /** Swing arc angle in degrees */
    UPROPERTY(EditDefaultsOnly, Category = "Arm|Animation")
    float SwingArcAngle = 60.0f;

    // === BOBBING ===

    /** Enable hand bobbing while moving */
    UPROPERTY(EditDefaultsOnly, Category = "Arm|Bobbing")
    bool bEnableBobbing = true;

    /** Bobbing amplitude */
    UPROPERTY(EditDefaultsOnly, Category = "Arm|Bobbing")
    float BobAmplitude = 2.0f;

    /** Bobbing speed multiplier */
    UPROPERTY(EditDefaultsOnly, Category = "Arm|Bobbing")
    float BobSpeed = 10.0f;

    // === SOUNDS ===

    /** Sound played on swing (always) */
    UPROPERTY(EditDefaultsOnly, Category = "Arm|Sound")
    USoundBase* SwingSound;

    /** Sound played on hit (only when damage dealt) */
    UPROPERTY(EditDefaultsOnly, Category = "Arm|Sound")
    USoundBase* HitSound;

    /** Play swing sound */
    UFUNCTION(BlueprintCallable, Category = "Arm|Sound")
    void PlaySwingSound();

    /** Play hit sound */
    UFUNCTION(BlueprintCallable, Category = "Arm|Sound")
    void PlayHitSound();

    // === HELD ITEM ===

    /** Update held item display based on item type */
    UFUNCTION(BlueprintCallable, Category = "Arm|Item")
    void SetHeldItem(EItemType ItemType);

    /** Clear held item (show empty hand) */
    UFUNCTION(BlueprintCallable, Category = "Arm|Item")
    void ClearHeldItem();

protected:
    virtual void BeginPlay() override;

private:
    // Swing state
    bool bIsSwinging = false;
    float SwingProgress = 0.0f;
    float SwingDuration = 0.3f;
    FRotator SwingStartRotation;

    // Bobbing state
    float BobTime = 0.0f;

    // Owner reference
    UPROPERTY()
    class AFirstPersonCharacter* OwnerCharacter;

    // Update swing animation
    void UpdateSwing(float DeltaTime);

    // Update bobbing
    void UpdateBobbing(float DeltaTime);

    // Calculate bob offset based on movement
    FVector CalculateBobOffset() const;
};
```

### Nova datoteka: FirstPersonArmComponent.cpp

```cpp
#include "FirstPersonArmComponent.h"
#include "FirstPersonCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"

UFirstPersonArmComponent::UFirstPersonArmComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UFirstPersonArmComponent::BeginPlay()
{
    Super::BeginPlay();

    OwnerCharacter = Cast<AFirstPersonCharacter>(GetOwner());
    if (!OwnerCharacter)
    {
        UE_LOG(LogTemp, Error, TEXT("FirstPersonArmComponent must be attached to AFirstPersonCharacter!"));
        return;
    }

    // Get camera component
    UCameraComponent* Camera = OwnerCharacter->GetFirstPersonCameraComponent();
    if (!Camera)
    {
        return;
    }

    // Create arm skeletal mesh
    ArmMesh = NewObject<USkeletalMeshComponent>(OwnerCharacter, TEXT("ArmMesh"));
    if (ArmMesh)
    {
        ArmMesh->SetupAttachment(Camera);
        ArmMesh->SetRelativeLocation(ArmBaseOffset);
        ArmMesh->SetRelativeRotation(ArmBaseRotation);
        ArmMesh->SetOnlyOwnerSee(true);  // Only local player sees
        ArmMesh->SetCastShadow(false);
        ArmMesh->RegisterComponent();
    }

    // Create held item static mesh
    HeldItemMesh = NewObject<UStaticMeshComponent>(OwnerCharacter, TEXT("HeldItemMesh"));
    if (HeldItemMesh && ArmMesh)
    {
        HeldItemMesh->SetupAttachment(ArmMesh, TEXT("hand_r"));  // Attach to hand socket
        HeldItemMesh->SetOnlyOwnerSee(true);
        HeldItemMesh->SetCastShadow(false);
        HeldItemMesh->SetVisibility(false);  // Hidden until item equipped
        HeldItemMesh->RegisterComponent();
    }

    SwingStartRotation = ArmBaseRotation;
}

void UFirstPersonArmComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!ArmMesh)
    {
        return;
    }

    // Update swing animation
    if (bIsSwinging)
    {
        UpdateSwing(DeltaTime);
    }

    // Update bobbing (only when not swinging)
    if (bEnableBobbing && !bIsSwinging)
    {
        UpdateBobbing(DeltaTime);
    }
}

void UFirstPersonArmComponent::PlaySwingAnimation(float Duration)
{
    if (bIsSwinging)
    {
        return;  // Already swinging
    }

    bIsSwinging = true;
    SwingProgress = 0.0f;
    SwingDuration = FMath::Max(Duration, 0.1f);
    SwingStartRotation = ArmMesh->GetRelativeRotation();

    PlaySwingSound();
}

void UFirstPersonArmComponent::UpdateSwing(float DeltaTime)
{
    SwingProgress += DeltaTime / SwingDuration;

    if (SwingProgress >= 1.0f)
    {
        // Animation complete
        bIsSwinging = false;
        SwingProgress = 0.0f;
        ArmMesh->SetRelativeRotation(ArmBaseRotation);
        return;
    }

    // Swing arc: 0->0.5 = swing down, 0.5->1.0 = return
    float SwingPhase;
    if (SwingProgress < 0.5f)
    {
        // Swing down (0 to max angle)
        SwingPhase = SwingProgress * 2.0f;  // 0 to 1
    }
    else
    {
        // Return (max angle to 0)
        SwingPhase = (1.0f - SwingProgress) * 2.0f;  // 1 to 0
    }

    // Ease in-out for natural motion
    float EasedPhase = FMath::InterpEaseInOut(0.0f, 1.0f, SwingPhase, 2.0f);

    // Apply rotation (swing down = negative pitch)
    FRotator SwingRotation = ArmBaseRotation;
    SwingRotation.Pitch -= SwingArcAngle * EasedPhase;

    // Add slight roll for more natural motion
    SwingRotation.Roll += (SwingArcAngle * 0.3f) * EasedPhase;

    ArmMesh->SetRelativeRotation(SwingRotation);
}

void UFirstPersonArmComponent::UpdateBobbing(float DeltaTime)
{
    if (!OwnerCharacter)
    {
        return;
    }

    // Get character velocity
    FVector Velocity = OwnerCharacter->GetVelocity();
    float Speed = Velocity.Size2D();  // Horizontal speed only

    if (Speed > 10.0f)  // Moving threshold
    {
        BobTime += DeltaTime * BobSpeed;

        FVector BobOffset = CalculateBobOffset();
        FVector NewLocation = ArmBaseOffset + BobOffset;
        ArmMesh->SetRelativeLocation(NewLocation);
    }
    else
    {
        // Return to base position when stationary
        FVector CurrentLoc = ArmMesh->GetRelativeLocation();
        FVector TargetLoc = ArmBaseOffset;
        FVector NewLoc = FMath::VInterpTo(CurrentLoc, TargetLoc, DeltaTime, 5.0f);
        ArmMesh->SetRelativeLocation(NewLoc);
    }
}

FVector UFirstPersonArmComponent::CalculateBobOffset() const
{
    // Minecraft-style bob: slight up/down and side-to-side
    float BobX = FMath::Sin(BobTime) * BobAmplitude * 0.5f;
    float BobY = FMath::Cos(BobTime * 0.5f) * BobAmplitude * 0.3f;
    float BobZ = FMath::Abs(FMath::Sin(BobTime)) * BobAmplitude;

    return FVector(BobX, BobY, BobZ);
}

void UFirstPersonArmComponent::PlaySwingSound()
{
    if (SwingSound && OwnerCharacter)
    {
        UGameplayStatics::PlaySoundAtLocation(
            GetWorld(),
            SwingSound,
            OwnerCharacter->GetActorLocation(),
            1.0f,  // Volume
            1.0f   // Pitch
        );
    }
}

void UFirstPersonArmComponent::PlayHitSound()
{
    if (HitSound && OwnerCharacter)
    {
        UGameplayStatics::PlaySoundAtLocation(
            GetWorld(),
            HitSound,
            OwnerCharacter->GetActorLocation(),
            1.0f,
            1.0f
        );
    }
}

void UFirstPersonArmComponent::SetHeldItem(EItemType ItemType)
{
    // TODO: Load appropriate mesh based on ItemType
    // For now, just toggle visibility
    if (HeldItemMesh)
    {
        bool bIsWeapon = (ItemType == EItemType::WoodenSword ||
                          ItemType == EItemType::StoneSword ||
                          ItemType == EItemType::IronSword ||
                          ItemType == EItemType::DiamondSword);

        HeldItemMesh->SetVisibility(bIsWeapon);
    }
}

void UFirstPersonArmComponent::ClearHeldItem()
{
    if (HeldItemMesh)
    {
        HeldItemMesh->SetVisibility(false);
    }
}
```

---

## FAZA 2: Integracija s FirstPersonCharacter

### Modifikacije FirstPersonCharacter.h

```cpp
// Dodaj forward declaration
class UFirstPersonArmComponent;

// Dodaj member varijablu
UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
UFirstPersonArmComponent* FirstPersonArmComponent;
```

### Modifikacije FirstPersonCharacter.cpp

```cpp
// U konstruktoru:
FirstPersonArmComponent = CreateDefaultSubobject<UFirstPersonArmComponent>(TEXT("FirstPersonArm"));

// U PerformMeleeAttack(), nakon LastAttackTime = CurrentTime:
if (FirstPersonArmComponent)
{
    // Swing duration matches weapon cooldown
    FirstPersonArmComponent->PlaySwingAnimation(Cooldown * 0.5f);
}

// Ako je hit uspješan (Target != nullptr), nakon TakeDamage():
if (FirstPersonArmComponent)
{
    FirstPersonArmComponent->PlayHitSound();
}

// U ScrollInventory(), nakon broadcast:
if (FirstPersonArmComponent)
{
    FirstPersonArmComponent->SetHeldItem(NewItemType);
}
```

---

## FAZA 3: Jednostavna Ruka (bez Skeletal Mesh)

Ako nemamo arm skeletal mesh, možemo koristiti **proceduralni pristup**:

### Alternativa: Cube kao Placeholder Ruka

```cpp
// Umjesto SkeletalMeshComponent, koristi StaticMeshComponent s kockom
ArmMesh = NewObject<UStaticMeshComponent>(OwnerCharacter, TEXT("ArmMesh"));
if (ArmMesh)
{
    // Učitaj jednostavan cube mesh
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(
        TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (CubeMesh.Succeeded())
    {
        ArmMesh->SetStaticMesh(CubeMesh.Object);
        ArmMesh->SetRelativeScale3D(FVector(0.1f, 0.3f, 0.15f));  // Arm shape
    }
    // ... rest of setup
}
```

### Minecraft-Style: Jednostavni Box Model

```
Minecraft ruka je zapravo samo pravokutnik (Steve arm):
- Širina: 4 piksela
- Visina: 12 piksela
- Dubina: 4 piksela

U UE jedinicama (1 block = 100 UU, 1 piksel = 6.25 UU):
- Scale: (0.25, 0.75, 0.25) * neki faktor za FP view
```

---

## Sažetak Datoteka

| Datoteka | Akcija | Opis |
|----------|--------|------|
| `Voxel/FirstPersonArmComponent.h` | **CREATE** | Nova komponenta za FP ruku |
| `Voxel/FirstPersonArmComponent.cpp` | **CREATE** | Implementacija swing, bob, sound |
| `Voxel/FirstPersonCharacter.h` | MODIFY | + FirstPersonArmComponent member |
| `Voxel/FirstPersonCharacter.cpp` | MODIFY | + komponenta kreacija, swing pozivi |

---

## Potrebni Assets

### Zvukovi (potrebno kreirati/nabaviti):
- `Content/Audio/SFX/Swing.wav` - Whoosh zvuk zamaha
- `Content/Audio/SFX/Hit.wav` - Punch/impact zvuk

### Meshevi (opcije):
1. **Placeholder**: Engine cube skaliran kao ruka
2. **Custom**: Kreirati jednostavan box mesh u Blenderu
3. **Asset**: Pronaći FP arms pack (Unreal Marketplace)

---

## Testiranje

```
1. Pokreni igru
2. Vidljiva "ruka" u donjem desnom kutu
3. Hodaj → ruka se njiše (bob)
4. LMB → ruka swing animacija
5. LMB na moba → dodatni hit zvuk
6. Scroll hotbar na sword → sword vidljiv (ako implementirano)
```

---

## Reference

- [Minecraft Wiki - Swing Duration](https://minecraft.wiki/w/Data_component_format/swing_animation)
- [Microsoft - Swing Duration Component](https://learn.microsoft.com/en-us/minecraft/creator/reference/content/itemreference/examples/itemcomponents/minecraft_swing_duration)
- [HandPosition Mod](https://modrinth.com/mod/handposition)
- [SpigotMC - Swing Timings](https://www.spigotmc.org/threads/player-arm-swing-animation-timings.184857/)
