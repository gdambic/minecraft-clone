#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ItemType.h"
#include "FirstPersonArmComponent.generated.h"

class UStaticMeshComponent;
class USoundBase;

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

	/** Static mesh for the arm (simple box placeholder) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arm")
	UStaticMeshComponent* ArmMesh;

	/** Static mesh for held item (sword, etc.) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Arm")
	UStaticMeshComponent* HeldItemMesh;

	// === POSITIONING ===

	/** Base offset from camera (lower right corner) */
	UPROPERTY(EditDefaultsOnly, Category = "Arm|Position")
	FVector ArmBaseOffset = FVector(40.0f, 20.0f, -25.0f);

	/** Base rotation of arm */
	UPROPERTY(EditDefaultsOnly, Category = "Arm|Position")
	FRotator ArmBaseRotation = FRotator(0.0f, -10.0f, 0.0f);

	/** Scale of the arm mesh */
	UPROPERTY(EditDefaultsOnly, Category = "Arm|Position")
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

	// Update swing animation
	void UpdateSwing(float DeltaTime);

	// Update bobbing
	void UpdateBobbing(float DeltaTime);

	// Calculate bob offset based on movement
	FVector CalculateBobOffset() const;

	// Create the arm mesh component
	void CreateArmMesh();

	// Create the held item mesh component
	void CreateHeldItemMesh();

	// Update sword mesh appearance based on type
	void UpdateSwordAppearance(EItemType SwordType);
};
