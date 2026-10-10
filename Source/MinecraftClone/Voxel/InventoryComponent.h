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

	/**
	 * Konstante za crafting slotove. Storage je uvijek max 3x3; aktivna
	 * velicina grida (2 = osobni inventar, 3 = crafting table) je runtime
	 * stanje (ActiveCraftingGridSize). Aktivni grid je kompaktan row-major
	 * po aktivnoj sirini (2x2 koristi array indekse 0-3, 3x3 indekse 0-8).
	 */
	static constexpr int32 MaxCraftingGridSize = 3;
	static constexpr int32 MaxCraftingInputSize = 9;      // 3x3
	static constexpr int32 CraftingInputStartIndex = 36;  // 36..44
	static constexpr int32 CraftingOutputIndex = 45;
	static constexpr int32 TotalCraftingSlots = 10;       // 9 input + 1 output

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

	/** Provjeri je li slot index AKTIVAN crafting input slot (ovisi o aktivnoj velicini grida) */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool IsCraftingInputSlot(int32 SlotIndex) const;

	/** Provjeri je li slot index crafting output slot (45) */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	bool IsCraftingOutputSlot(int32 SlotIndex) const;

	/** Ažuriraj crafting output na temelju inputa (kopira prvi pronađeni item) */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Crafting")
	void UpdateCraftingOutput();

	/** Trenutno aktivna velicina crafting grida (2 ili 3) */
	UFUNCTION(BlueprintPure, Category = "Inventory|Crafting")
	int32 GetActiveCraftingGridSize() const { return ActiveCraftingGridSize; }

	/** Broj aktivnih crafting input slotova (4 ili 9) */
	UFUNCTION(BlueprintPure, Category = "Inventory|Crafting")
	int32 GetActiveCraftingInputCount() const { return ActiveCraftingGridSize * ActiveCraftingGridSize; }

	/** Postavi aktivnu velicinu grida (2 ili 3). Prije promjene vraca iteme iz grida u inventar. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Crafting")
	void SetActiveCraftingGridSize(int32 NewSize);

	/** Vrati sve iteme iz crafting inputa u inventar (visak dropa kod igraca) i ocisti output. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|Crafting")
	void ReturnCraftingInputToInventory();

	/** Pronađi prvi prazan slot (vraća -1 ako nema) */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	int32 FindFirstEmptySlot() const;

	/** Pronađi slot s određenim itemom (vraća -1 ako nema) */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	int32 FindSlotWithItem(EItemType Type) const;

	// ==================== Utility Functions ====================

	/** Dodaj item u inventar (pronalazi slobodan slot ili stacka). Vraca visak koji nije stao (0 = sve stalo). */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	int32 AddItem(EItemType Type, int32 Amount = 1);

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

	/** Maksimalna velicina stacka za item (iz Items.json preko BlockRegistry-ja; fallback 64) */
	UFUNCTION(BlueprintPure, Category = "Inventory")
	int32 GetMaxStackSize(EItemType ItemType) const;

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

	/** Uzmi POLOVICU stacka (ceil) u "ruku"; ostatak ostaje u slotu. Desni klik praznom rukom. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|HeldItem")
	bool PickUpHalf(int32 SlotIndex);

	/** Spusti JEDAN item iz "ruke" u slot (prazan ili istog tipa); ostatak ostaje u ruci. Desni klik punom rukom. */
	UFUNCTION(BlueprintCallable, Category = "Inventory|HeldItem")
	bool PlaceOne(int32 SlotIndex);

	/** Quick-move (shift+klik): main<->hotbar; crafting input/output ide u inventar. */
	UFUNCTION(BlueprintCallable, Category = "Inventory")
	bool QuickMove(int32 SlotIndex);

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

	/** Array od 10 crafting slotova: 0-8 = input (max 3x3), 9 = output */
	UPROPERTY(VisibleAnywhere, Category = "Inventory")
	TArray<FInventorySlot> CraftingSlots;

	/** Aktivna velicina crafting grida: 2 (osobni inventar) ili 3 (crafting table) */
	UPROPERTY(VisibleAnywhere, Category = "Inventory")
	int32 ActiveCraftingGridSize = 2;

	/** Pretvori crafting slot index (36-45) u array index (0-9) */
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

	/**
	 * Dodaj item samo u raspon slotova [RangeStart, RangeEndExclusive):
	 * merge u prvi slot istog tipa, inace prvi prazan. Vraca visak (0 ili Amount - atomarno).
	 */
	int32 AddItemInRange(EItemType Type, int32 Amount, int32 RangeStart, int32 RangeEndExclusive);

	/** Dohvati crafting grid kao array EItemType vrijednosti */
	TArray<EItemType> GetCraftingGridAsArray() const;

	/** Potroši ingredijente prema trenutnom receptu */
	void ConsumeRecipeIngredients();
};
