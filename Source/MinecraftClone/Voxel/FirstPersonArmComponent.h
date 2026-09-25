#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ItemType.h"
#include "FirstPersonArmComponent.generated.h"

class UStaticMeshComponent;
class UProceduralMeshComponent;
class USoundBase;
class UTexture2D;

/**
 * Component that handles first-person arm rendering, animations, and sounds.
 * Provides Minecraft-style hand display with swing animation and bobbing.
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
	// Reference na native mesh komponente charactera (vidi AFirstPersonCharacter) -
	// ova komponenta ih konfigurira i upravlja njihovim stanjem

	/** Static mesh for the arm (simple box placeholder) */
	UPROPERTY(BlueprintReadOnly, Category = "Arm")
	UStaticMeshComponent* ArmMesh;

	/** Static mesh for held item (sword, etc.) */
	UPROPERTY(BlueprintReadOnly, Category = "Arm")
	UStaticMeshComponent* HeldItemMesh;

	/**
	 * Ekstrudirani mesh itema sa "sprite" prikazom (mac, alat...) - Minecraft
	 * stil: tekstura debljine 1 piksela s bocnim stranicama po rubovima
	 * piksela (FItemMeshExtruder). Fallback bez ekstruzije: flat quad.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Arm")
	UProceduralMeshComponent* HeldSpriteMesh;

	/**
	 * Scale of the arm cube while a block item is held. Uniform by default so
	 * the held block is a true cube, exactly like a world block (world blocks
	 * are 100 UU, so 0.25 = 25 UU cube).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arm|Held Item")
	FVector HeldBlockScale = FVector(0.25f, 0.25f, 0.25f);

	/** Offset of the sprite mesh from the arm, in Unreal units (X = distance from player) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arm|Held Item")
	FVector HeldSpriteOffset = FVector(11.2f, 14.4f, 9.5f);

	/**
	 * Rotation of the sprite mesh (rucno stimano u Editoru 2026-09-25;
	 * Details panel prikazuje Roll/Pitch/Yaw = -3.2 / 36.8 / 2.9).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arm|Held Item")
	FRotator HeldSpriteRotation = FRotator(36.8f, 2.9f, -3.2f);

	/** World-space scale of the sprite mesh (ploca je 100x100 UU at scale 1) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arm|Held Item")
	FVector HeldSpriteScale = FVector(0.35f, 0.35f, 0.35f);

	// === POSITIONING ===

	/** Base offset from camera (X = distance from player) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arm|Position")
	FVector ArmBaseOffset = FVector(40.0f, 20.0f, -25.0f);

	/** Base rotation of arm */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arm|Position")
	FRotator ArmBaseRotation = FRotator(0.0f, -10.0f, 0.0f);

	/** Scale of the arm mesh */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Arm|Position")
	FVector ArmScale = FVector(0.08f, 0.25f, 0.08f);

	// === SWING ANIMATION ===

	/** Play swing animation (called on attack) */
	UFUNCTION(BlueprintCallable, Category = "Arm|Animation")
	void PlaySwingAnimation(float Duration);

	/** Is swing animation currently playing */
	UFUNCTION(BlueprintPure, Category = "Arm|Animation")
	bool IsSwinging() const { return bIsSwinging; }

	/** Swing arc angle in degrees */
	UPROPERTY(EditDefaultsOnly, Category = "Arm|Animation")
	float SwingArcAngle = 70.0f;

	/** Additional forward movement during swing */
	UPROPERTY(EditDefaultsOnly, Category = "Arm|Animation")
	float SwingForwardDistance = 15.0f;

	// === BOBBING ===

	/** Enable hand bobbing while moving */
	UPROPERTY(EditDefaultsOnly, Category = "Arm|Bobbing")
	bool bEnableBobbing = true;

	/** Bobbing amplitude (how much the hand moves) */
	UPROPERTY(EditDefaultsOnly, Category = "Arm|Bobbing")
	float BobAmplitude = 1.5f;

	/** Bobbing speed multiplier */
	UPROPERTY(EditDefaultsOnly, Category = "Arm|Bobbing")
	float BobSpeed = 12.0f;

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

	// === MATERIALS ===

	/** Material for the arm (skin color) */
	UPROPERTY(EditDefaultsOnly, Category = "Arm|Appearance")
	UMaterialInterface* ArmMaterial;

	/** Material for wooden sword */
	UPROPERTY(EditDefaultsOnly, Category = "Arm|Appearance")
	UMaterialInterface* WoodenSwordMaterial;

	/** Material for stone sword */
	UPROPERTY(EditDefaultsOnly, Category = "Arm|Appearance")
	UMaterialInterface* StoneSwordMaterial;

	/** Material for iron sword */
	UPROPERTY(EditDefaultsOnly, Category = "Arm|Appearance")
	UMaterialInterface* IronSwordMaterial;

	/** Material for diamond sword */
	UPROPERTY(EditDefaultsOnly, Category = "Arm|Appearance")
	UMaterialInterface* DiamondSwordMaterial;

protected:
	virtual void BeginPlay() override;

private:
	// Swing state
	bool bIsSwinging = false;
	float SwingProgress = 0.0f;
	float SwingDuration = 0.3f;

	// Bobbing state
	float BobTime = 0.0f;

	// Owner reference
	UPROPERTY()
	class AFirstPersonCharacter* OwnerCharacter;

	// Camera reference
	UPROPERTY()
	class UCameraComponent* OwnerCamera;

	// Current held item type
	EItemType CurrentHeldItem = EItemType::None;

	// Item ciji je mesh trenutno izgraden u HeldSpriteMesh sekciji -
	// CreateMeshSection nije jeftin, a SetHeldItem se zove svaki tick
	EItemType BuiltSpriteMeshItem = EItemType::None;

	// Material the arm cube shows when no block material applies (weapons, fallback)
	UPROPERTY()
	UMaterialInterface* DefaultArmMaterial;

	// Dynamic material instance for the sprite quad (parent: M_ItemSprite)
	UPROPERTY()
	class UMaterialInstanceDynamic* HeldSpriteMID;

	// Update swing animation
	void UpdateSwing(float DeltaTime);

	// Update bobbing
	void UpdateBobbing(float DeltaTime);

	// Calculate bob offset based on movement
	FVector CalculateBobOffset() const;

	// Configure the character's native arm mesh (asset, transform, material)
	void SetupArmMesh();

	// Configure the character's native held item mesh
	void SetupHeldItemMesh();

	// Configure the sprite mesh + create its dynamic material
	void SetupHeldSpriteMesh();

	// Izgradi ekstrudirani mesh (ili flat quad fallback) za item u HeldSpriteMesh
	void BuildHeldSpriteMesh(EItemType ItemType, UTexture2D* SpriteTexture);

	// Apply HeldSpriteOffset/Rotation/Scale to the sprite mesh
	void ApplyHeldSpriteTransform();

	// Update sword mesh appearance based on type
	void UpdateSwordAppearance(EItemType SwordType);
};
