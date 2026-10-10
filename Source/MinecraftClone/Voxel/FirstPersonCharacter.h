#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BlockType.h"
#include "ItemType.h"
#include "WeaponData.h"
#include "InventoryComponent.h"
#include "FirstPersonCharacter.generated.h"

class UCameraComponent;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class UInputAction;
class ABlock;
class AVoxelWorld;
class UInventoryComponent;
class UInventoryScreenWidget;
class ACreatureBase;
class UFirstPersonArmComponent;
struct FInputActionValue;

/** Delegate koji se broadcasta kada se promijeni odabrani item */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSelectedItemChanged, int32, NewIndex, EItemType, NewItemType);

/** Delegate koji se broadcasta kada se otvori/zatvori neki inventory UI ekran */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInventoryToggled, bool, bIsOpen);

/** Koji je UI ekran trenutno otvoren (tocno jedan moze biti otvoren) */
UENUM(BlueprintType)
enum class EOpenScreen : uint8
{
	None,
	Inventory,
	CraftingTable
};

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

	/** First person arm component for hand visualization and swing animation */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UFirstPersonArmComponent* FirstPersonArmComponent;

	/**
	 * Meshevi ruke - native komponente (kao i kamera) da prežive PIE izmjene
	 * svojstava koje uništavaju runtime-kreirane komponente. Konfigurira ih
	 * i njima upravlja UFirstPersonArmComponent.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* ArmMeshComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UStaticMeshComponent* HeldItemMeshComponent;

	/** Ekstrudirani sprite itema (mac, alat...) - proceduralni mesh iz teksture */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UProceduralMeshComponent* HeldSpriteMeshComponent;

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

	/** Event koji se poziva kada se otvori/zatvori inventory UI (bilo koji ekran) */
	UPROPERTY(BlueprintAssignable, Category = "UI")
	FOnInventoryToggled OnInventoryToggled;

	/** Je li neki UI ekran trenutno otvoren (sinkroniziran s CurrentOpenScreen, radi BP kompatibilnosti) */
	UPROPERTY(BlueprintReadWrite, Category = "UI")
	bool bIsInventoryOpen = false;

	/**
	 * Klase ekrana - postavljaju se u BP_FirstPersonCharacter Class Defaults.
	 * Ekrane kreira i vidljivoscu upravlja C++ (OpenScreen/CloseCurrentScreen);
	 * BP vise ne radi CreateWidget za njih.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UInventoryScreenWidget> InventoryScreenClass;

	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<UInventoryScreenWidget> CraftingTableScreenClass;

	/** Trenutno otvoreni UI ekran */
	UPROPERTY(BlueprintReadOnly, Category = "UI")
	EOpenScreen CurrentOpenScreen = EOpenScreen::None;

	/** Toggle inventory prikaz (E tipka): otvara inventar ili zatvara trenutno otvoreni ekran */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void ToggleInventory();

	/** Otvori zadani ekran (zatvara prethodni ako je neki otvoren) */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void OpenScreen(EOpenScreen Screen);

	/** Zatvori trenutno otvoreni ekran (vraca held item i crafting grid u inventar) */
	UFUNCTION(BlueprintCallable, Category = "UI")
	void CloseCurrentScreen();

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

	// === MELEE COMBAT ===

	/** Range for melee attacks (3 blocks = 300 UU) */
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	float MeleeRange = 300.0f;

	/** Base knockback force applied to hit targets */
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	float AttackKnockback = 400.0f;

	/** Vertical knockback component */
	UPROPERTY(EditDefaultsOnly, Category = "Combat")
	float KnockbackVertical = 150.0f;

	/** Time of last attack for cooldown tracking */
	float LastAttackTime = 0.0f;

	/** Perform raycast to find creature in melee range */
	ACreatureBase* MeleeTrace() const;

	/** Execute melee attack on target creature */
	void PerformMeleeAttack();

	/** Get weapon data for currently equipped item */
	FWeaponData GetEquippedWeaponData() const;

	/** Calculate damage multiplier based on cooldown progress (Minecraft formula) */
	float CalculateDamageMultiplier(float CooldownProgress) const;

	/** Raycast za detekciju bloka */
	void UpdateBlockLookAt();

	/** Attack input handlers */
	void StartAttack();
	void StopAttack();

	/** Place block on looked-at face */
	void PlaceBlock();

	/** Pokusaj interakciju s fokusiranim blokom (desni klik). Vraca true ako je interakcija potrosila input. */
	bool TryInteractWithFocusedBlock();

	/** Zajednicki kursor/IMC/input-mode setup za otvaranje/zatvaranje ekrana */
	void ApplyScreenInputMode(bool bUIMode);

	/** Auto-close provjere dok je blok ekran otvoren (stol unisten / igrac predaleko) */
	void TickOpenScreenSafety();

	/** Grid pozicija stola nad kojim je crafting table ekran otvoren */
	FIntVector OpenCraftingTablePos = FIntVector::ZeroValue;

	/** Instance ekrana (kreirane u BeginPlay iz *ScreenClass, skrivene dok se ne otvore) */
	UPROPERTY()
	TObjectPtr<UInventoryScreenWidget> InventoryScreen;

	UPROPERTY()
	TObjectPtr<UInventoryScreenWidget> CraftingTableScreen;

	/** Kreiraj i pripremi instancu ekrana (CreateWidget + AddToViewport + Init) */
	UInventoryScreenWidget* CreateScreen(TSubclassOf<UInventoryScreenWidget> ScreenClass);

	/** Widget trenutno otvorenog ekrana (nullptr ako nista nije otvoreno) */
	UInventoryScreenWidget* GetScreenWidget(EOpenScreen Screen) const;

	/** Udaljenost od stola nakon koje se blok ekran automatski zatvara */
	UPROPERTY(EditAnywhere, Category = "Block Interaction")
	float ScreenAutoCloseDistance = 500.0f;

	/** Scroll inventory selection */
	void ScrollInventory(const FInputActionValue& Value);

	/**
	 * Handler za promjenu sadržaja slota - osvježi prikaz itema u ruci ako je
	 * promijenjen odabrani hotbar slot (npr. postavljen zadnji blok iz ruke,
	 * pickup u odabrani prazan slot, drag-drop u inventoryju).
	 */
	UFUNCTION()
	void OnInventorySlotChanged(int32 SlotIndex, FInventorySlot NewSlot);

	/**
	 * Stvori startni WoodenSword drop 2 bloka desno od igrača.
	 * Poziva se timerom malo nakon BeginPlay - teren se generira u
	 * VoxelWorld::BeginPlay pa u characterovom BeginPlay još ne postoji,
	 * a drop treba tlo na koje će pasti.
	 */
	void SpawnStartingSwordDrop();

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

	/** Meshevi ruke (native komponente kojima upravlja UFirstPersonArmComponent) */
	UStaticMeshComponent* GetArmMeshComponent() const { return ArmMeshComponent; }
	UStaticMeshComponent* GetHeldItemMeshComponent() const { return HeldItemMeshComponent; }
	UProceduralMeshComponent* GetHeldSpriteMeshComponent() const { return HeldSpriteMeshComponent; }
};
