#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BlockType.h"
#include "ItemType.h"
#include "FirstPersonCharacter.generated.h"

class UCameraComponent;
class UInputAction;
class ABlock;
class AVoxelWorld;
class UInventoryComponent;
struct FInputActionValue;

/** Delegate koji se broadcasta kada se promijeni odabrani item */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSelectedItemChanged, int32, NewIndex, EItemType, NewItemType);

/** Delegate koji se broadcasta kada se otvori/zatvori inventory */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInventoryToggled, bool, bIsOpen);

/**
 * A basic first person character (without visible arms)
 */
UCLASS(abstract)
class MINECRAFTCLONE_API AFirstPersonCharacter : public ACharacter
{
	GENERATED_BODY()

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;

	/** Inventory component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UInventoryComponent* InventoryComponent;

protected:
	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MouseLookAction;

	/** Attack/Destroy Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* AttackAction;

	/** Interact/Place Block Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* InteractAction;

	/** Scroll Input Action for inventory selection */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* ScrollAction;

	/** Toggle Inventory Input Action (E tipka) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* ToggleInventoryAction;

public:
	AFirstPersonCharacter();

	virtual void Tick(float DeltaTime) override;

	/** Doseg za interakciju s blokovima (u UE units) - postavlja se u BP */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block Interaction")
	float BlockInteractionRange;

	/** Index trenutno selektiranog itema u listi */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Inventory")
	int32 SelectedItemIndex;

	/** Vraća trenutno selektirani tip itema */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	EItemType GetSelectedItemType() const;

	/** Vraća trenutno selektirani tip bloka (konvertira iz ItemType) */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	EBlockType GetSelectedBlockType() const;

	/** Event koji se poziva kada se promijeni odabrani item */
	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FOnSelectedItemChanged OnSelectedItemChanged;

	/** Event koji se poziva kada se otvori/zatvori inventory */
	UPROPERTY(BlueprintAssignable, Category = "UI")
	FOnInventoryToggled OnInventoryToggled;

	/** Je li inventory trenutno otvoren */
	UPROPERTY(BlueprintReadWrite, Category = "UI")
	bool bIsInventoryOpen = false;

	/** Toggle inventory prikaz (E tipka) */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void ToggleInventory();

	/** Vraća InventoryComponent */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	UInventoryComponent* GetInventoryComponent() const { return InventoryComponent; }

protected:
	virtual void BeginPlay() override;

	/** Blok u koji igrač trenutno gleda */
	UPROPERTY(BlueprintReadOnly, Category = "Block Interaction")
	ABlock* CurrentlyLookedAtBlock;

	/** Normala pogođene plohe */
	UPROPERTY(BlueprintReadOnly, Category = "Block Interaction")
	FVector CurrentHitNormal;

	/** Referenca na VoxelWorld (pronađe se automatski) */
	UPROPERTY(BlueprintReadOnly, Category = "Block Interaction")
	AVoxelWorld* VoxelWorld;

	/** Je li igrač trenutno drži attack tipku */
	bool bIsAttacking = false;

	/** Raycast za detekciju bloka */
	void UpdateBlockLookAt();

	/** Attack input handlers */
	void StartAttack();
	void StopAttack();

	/** Place block on looked-at face */
	void PlaceBlock();

	/** Scroll inventory selection */
	void ScrollInventory(const FInputActionValue& Value);

protected:
	/** Called from Input Actions for movement input */
	void MoveInput(const FInputActionValue& Value);

	/** Called from Input Actions for looking input */
	void LookInput(const FInputActionValue& Value);

	/** Handles aim inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoAim(float Yaw, float Pitch);

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles jump start inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoJumpStart();

	/** Handles jump end inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoJumpEnd();

protected:
	/** Set up input action bindings */
	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;

public:
	/** Returns first person camera component */
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }
};
