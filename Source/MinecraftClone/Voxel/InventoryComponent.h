#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "ItemType.h"
#include "BlockType.h"
#include "ItemData.h"
#include "InventoryComponent.generated.h"

class UDataTable;
struct FCraftingRecipe;

/** Struktura za pojedinačni slot inventara */
USTRUCT(BlueprintType)
struct FInventorySlot
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	EItemType ItemType = EItemType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory")
	int32 Quantity = 0;

	bool IsEmpty() const { return ItemType == EItemType::None || Quantity <= 0; }

	void Clear()
	{
		ItemType = EItemType::None;
		Quantity = 0;
	}
};

/** Delegate za promjenu pojedinačnog slota */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnSlotChanged, int32, SlotIndex, FInventorySlot, NewSlot);

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class MINECRAFTCLONE_API UInventoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/** Konstante za veličinu inventara */
	static constexpr int32 MainInventorySize = 27;
	static constexpr int32 HotbarSize = 9;
	static constexpr int32 TotalSlots = 36;
	static constexpr int32 HotbarStartIndex = 27;

	/** Konstante za crafting slotove */
	static constexpr int32 CraftingInputSize = 4;
	static constexpr int32 CraftingInputStartIndex = 36;  // 36, 37, 38, 39
	static constexpr int32 CraftingOutputIndex = 40;
	static constexpr int32 TotalCraftingSlots = 5;  // 4 input + 1 output

	UInventoryComponent();

	// ==================== Slot-based API ====================

	/** Dohvati slot po indexu */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	FInventorySlot GetSlot(int32 SlotIndex) const;

	/** Postavi slot na određeni tip i količinu */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void SetSlot(int32 SlotIndex, EItemType Type, int32 Quantity);

	/** Premjesti item iz jednog slota u drugi (swap ako je destinacija zauzeta) */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool MoveItem(int32 FromSlot, int32 ToSlot);

	/** Zamijeni dva slota */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool SwapSlots(int32 SlotA, int32 SlotB);

	/** Dohvati sve slotove */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	const TArray<FInventorySlot>& GetAllSlots() const { return Slots; }

	/** Provjeri je li slot index validan (uključuje i crafting slotove) */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool IsValidSlotIndex(int32 SlotIndex) const;

	/** Provjeri je li slot index crafting slot */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool IsCraftingSlotIndex(int32 SlotIndex) const;

	/** Provjeri je li slot index crafting input slot (36-39) */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool IsCraftingInputSlot(int32 SlotIndex) const;

	/** Provjeri je li slot index crafting output slot (40) */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool IsCraftingOutputSlot(int32 SlotIndex) const;

	/** Ažuriraj crafting output na temelju inputa (kopira prvi pronađeni item) */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Crafting")
	void UpdateCraftingOutput();

	/** Pronađi prvi prazan slot (vraća -1 ako nema) */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	int32 FindFirstEmptySlot() const;

	/** Pronađi slot s određenim itemom (vraća -1 ako nema) */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	int32 FindSlotWithItem(EItemType Type) const;

	// ==================== Utility Functions ====================

	/** Dodaj item u inventar (pronalazi slobodan slot ili stacka) */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	void AddItem(EItemType Type, int32 Amount = 1);

	/** Makni item iz inventara, vraća koliko je stvarno maknuto */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	int32 RemoveItem(EItemType Type, int32 Amount = 1);

	/** Dohvati ukupnu količinu itema u svim slotovima */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetItemCount(EItemType Type) const;

	/** Provjeri ima li dovoljno itema */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool HasItem(EItemType Type, int32 Amount = 1) const;

	/** Konvertira EItemType u EBlockType (vraća Air ako nije moguće postaviti) */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	static EBlockType ItemTypeToBlockType(EItemType ItemType);

	/** Konvertira EBlockType u EItemType (vraća None ako nema odgovarajući item) */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	static EItemType BlockTypeToItemType(EBlockType BlockType);

	/** Provjerava može li se ItemType postaviti kao blok */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	static bool CanItemBePlaced(EItemType ItemType);

	/** Dohvati podatke o itemu iz Data Table */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	static FItemData GetItemData(EItemType ItemType, UDataTable* ItemDataTable);

	/** Event kada se pojedinačni slot promijeni */
	UPROPERTY(BlueprintAssignable, Category = "Inventory")
	FOnSlotChanged OnSlotChanged;

	// ==================== Held Item API (za UI drag & drop) ====================

	/** Uzmi item iz slota u "ruku". Vraća true ako uspješno. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|HeldItem")
	bool PickUpItem(int32 SlotIndex);

	/** Spusti item iz "ruke" u slot. Vraća true ako uspješno. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|HeldItem")
	bool PlaceItem(int32 SlotIndex);

	/** Dohvati item koji je trenutno u "ruci" */
	UFUNCTION(BlueprintPure, Category = "Inventory|HeldItem")
	FInventorySlot GetHeldItem() const { return HeldItem; }

	/** Provjeri drži li igrač item */
	UFUNCTION(BlueprintPure, Category = "Inventory|HeldItem")
	bool IsHoldingItem() const { return !HeldItem.IsEmpty(); }

	/** Dohvati index slota iz kojeg je item uzet (-1 ako ništa ne drži) */
	UFUNCTION(BlueprintPure, Category = "Inventory|HeldItem")
	int32 GetHeldItemSlotIndex() const { return HeldItemSlotIndex; }

	/** Vrati held item nazad u originalni slot (koristi se kad se zatvori inventory) */
	UFUNCTION(BlueprintCallable, Category = "Inventory|HeldItem")
	void ReturnHeldItem();

protected:
	virtual void BeginPlay() override;

private:
	/** Array od 36 slotova: 0-26 = main inventory, 27-35 = hotbar */
	UPROPERTY(VisibleAnywhere, Category = "Inventory")
	TArray<FInventorySlot> Slots;

	/** Array od 5 crafting slotova: 0-3 = input (2x2), 4 = output */
	UPROPERTY(VisibleAnywhere, Category = "Inventory")
	TArray<FInventorySlot> CraftingSlots;

	/** Pretvori crafting slot index (36-40) u array index (0-4) */
	int32 CraftingSlotToArrayIndex(int32 SlotIndex) const;

	/** Item koji igrač trenutno drži (za UI drag & drop) */
	UPROPERTY(VisibleAnywhere, Category = "Inventory")
	FInventorySlot HeldItem;

	/** Index slota iz kojeg je HeldItem uzet (-1 ako ništa ne drži) */
	UPROPERTY(VisibleAnywhere, Category = "Inventory")
	int32 HeldItemSlotIndex = -1;

	/** Trenutni aktivni recept (nullptr ako nema matcha) */
	const FCraftingRecipe* CurrentRecipe = nullptr;

	/** Emitira OnSlotChanged delegate */
	void BroadcastSlotChanged(int32 SlotIndex);

	/** Dohvati crafting grid kao array EItemType vrijednosti */
	TArray<EItemType> GetCraftingGridAsArray() const;

	/** Potroši ingredijente prema trenutnom receptu */
	void ConsumeRecipeIngredients();
};
