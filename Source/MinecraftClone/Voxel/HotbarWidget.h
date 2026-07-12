#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InventoryComponent.h"
#include "HotbarWidget.generated.h"

class UHorizontalBox;
class UInventorySlotWidget;
class AFirstPersonCharacter;
class UDataTable;

/**
 * C++ bazna klasa za Hotbar widget.
 * Blueprint widget nasljeđuje ovu klasu i definira layout.
 *
 * Potrebni BindWidget elementi u Blueprintu:
 * - SlotContainer (UHorizontalBox) - kontejner za slot widgete
 */
UCLASS()
class MINECRAFTCLONE_API UHotbarWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==================== UI elementi (BindWidget) ====================

	/** Kontejner za hotbar slotove - mora postojati u Blueprint widgetu s imenom "SlotContainer" */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> SlotContainer;

	// ==================== Konfiguracija ====================

	/** Klasa widgeta za slot (WBP_InventorySlot) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hotbar|Config")
	TSubclassOf<UInventorySlotWidget> InventorySlotWidgetClass;

	/** DataTable s podacima o itemima */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Hotbar|Config")
	TObjectPtr<UDataTable> ItemDataTable;

	// ==================== Glavne funkcije ====================

	/**
	 * Kreira 9 hotbar slotova i dodaje ih u SlotContainer.
	 * Poziva se iz Event Construct u Blueprintu.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hotbar")
	void CreateHotbarSlots();

	/**
	 * Obrađuje promjenu slota u inventaru.
	 * Ažurira vizual ako je slot u hotbar rasponu (27-35).
	 * @param SlotIndex - index slota koji se promijenio
	 * @param NewSlot - novi sadržaj slota
	 */
	UFUNCTION(BlueprintCallable, Category = "Hotbar")
	void HandleSlotChanged(int32 SlotIndex, FInventorySlot NewSlot);

	/**
	 * Obrađuje promjenu selektiranog itema.
	 * Mijenja highlight s prethodnog na novi slot.
	 * @param NewIndex - novi selektirani index (0-8)
	 * @param NewItemType - tip novog itema (za informaciju)
	 */
	UFUNCTION(BlueprintCallable, Category = "Hotbar")
	void SelectedItemChanged(int32 NewIndex, EItemType NewItemType);

	/**
	 * Osvježava sve slotove iz inventara.
	 * Koristi se za inicijalnu sinkronizaciju.
	 */
	UFUNCTION(BlueprintCallable, Category = "Hotbar")
	void RefreshAllSlots();

	// ==================== Stanje ====================

	/** Vraća trenutno selektirani index */
	UFUNCTION(BlueprintPure, Category = "Hotbar")
	int32 GetCurrentSelectedIndex() const { return CurrentSelectedIndex; }

	/** Vraća broj slotova */
	UFUNCTION(BlueprintPure, Category = "Hotbar")
	int32 GetSlotCount() const { return SlotWidgets.Num(); }

	/** Vraća slot widget po indexu */
	UFUNCTION(BlueprintPure, Category = "Hotbar")
	UInventorySlotWidget* GetSlotWidget(int32 Index) const;

	// ==================== Reference ====================

	/** Postavlja referencu na player charactera */
	UFUNCTION(BlueprintCallable, Category = "Hotbar")
	void SetPlayerCharacter(AFirstPersonCharacter* Character);

	/** Vraća referencu na player charactera */
	UFUNCTION(BlueprintPure, Category = "Hotbar")
	AFirstPersonCharacter* GetPlayerCharacter() const { return PlayerCharacter; }

	/** Vraća referencu na inventory komponentu */
	UFUNCTION(BlueprintPure, Category = "Hotbar")
	UInventoryComponent* GetInventoryComponent() const;

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Array kreeiranih slot widgeta */
	UPROPERTY(BlueprintReadOnly, Category = "Hotbar|State")
	TArray<TObjectPtr<UInventorySlotWidget>> SlotWidgets;

	/** Trenutno selektirani index (0-8) */
	UPROPERTY(BlueprintReadOnly, Category = "Hotbar|State")
	int32 CurrentSelectedIndex = 0;

	/** Referenca na player charactera */
	UPROPERTY(BlueprintReadOnly, Category = "Hotbar|References")
	TObjectPtr<AFirstPersonCharacter> PlayerCharacter;

private:
	/** Konstante */
	static constexpr int32 HotbarSlotCount = 9;
	static constexpr int32 HotbarStartIndex = 27;

	/** Provjeri je li index validan za hotbar */
	bool IsValidHotbarIndex(int32 Index) const;

	/** Konvertira inventory slot index (27-35) u hotbar index (0-8) */
	int32 InventoryToHotbarIndex(int32 InventorySlotIndex) const;

	/** Delegate handleri */
	UFUNCTION()
	void OnSlotChangedHandler(int32 SlotIndex, FInventorySlot NewSlot);

	UFUNCTION()
	void OnSelectedItemChangedHandler(int32 NewIndex, EItemType NewItemType);
};
