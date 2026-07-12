#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ItemType.h"
#include "ItemData.h"
#include "InventorySlotWidget.generated.h"

class UImage;
class UTextBlock;
class UBorder;
class UButton;
class UDataTable;

/** Delegate za klik na slot */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnSlotClicked, int32, SlotIndex);

/**
 * C++ bazna klasa za inventory slot widget.
 * Blueprint widget nasljeđuje ovu klasu i definira layout.
 *
 * Potrebni BindWidget elementi u Blueprintu:
 * - ItemIcon (UImage) - ikona itema
 * - QuantityText (UTextBlock) - broj itema
 * - SlotBorder (UBorder) - obrub za selekciju
 */
UCLASS()
class MINECRAFTCLONE_API UInventorySlotWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==================== UI elementi (BindWidget) ====================

	/** Ikona itema - mora postojati u Blueprint widgetu s imenom "ItemIcon" */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UImage> ItemIcon;

	/** Tekst za količinu - mora postojati u Blueprint widgetu s imenom "QuantityText" */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> QuantityText;

	/** Obrub slota za highlight selekcije - mora postojati s imenom "SlotBorder" */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UBorder> SlotBorder;

	/** Opcionalni button za detekciju klika - ako postoji s imenom "ClickButton" */
	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UButton> ClickButton;

	// ==================== Konfiguracija stila ====================

	/** Boja obruba kada je slot selektiran */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Inventory Slot|Style")
	FLinearColor SelectedBorderColor = FLinearColor(1.0f, 0.8f, 0.4f, 0.5f);

	/** Boja obruba kada slot nije selektiran */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Inventory Slot|Style")
	FLinearColor DefaultBorderColor = FLinearColor(0.2f, 0.2f, 0.2f, 1.0f);

	/** Boja pozadine kada je slot prazan */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Inventory Slot|Style")
	FLinearColor EmptySlotColor = FLinearColor(0.1f, 0.1f, 0.1f, 0.8f);

	// ==================== Data Table ====================

	/** DataTable s podacima o itemima (ikone, nazivi, stack size) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Inventory Slot|Data")
	TObjectPtr<UDataTable> ItemDataTable;

	// ==================== Slot identifikacija ====================

	/** Index slota u inventaru (-1 = cursor slot, 0-26 = main, 27-35 = hotbar, 36-39 = crafting input, 40 = crafting output) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Inventory Slot|Data")
	int32 SlotIndex = -1;

	// ==================== Events ====================

	/** Event koji se broadcasta kada se klikne na slot */
	UPROPERTY(BlueprintAssignable, BlueprintCallable, Category = "Inventory Slot|Events")
	FOnSlotClicked OnSlotClicked;

	// ==================== Glavne funkcije ====================

	/**
	 * Postavlja vizualni highlight selekcije na slotu.
	 * Mijenja boju obruba ovisno o stanju selekcije.
	 * @param bSelected - true za selektirani izgled, false za normalni
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory Slot")
	void SetSelected(bool bSelected);

	/**
	 * Ažurira prikaz slota s novim podacima.
	 * Postavlja ikonu, količinu i tooltip.
	 * @param ItemType - tip itema za prikaz (None = prazan slot)
	 * @param Quantity - količina itema
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory Slot")
	void SetSlotData(EItemType ItemType, int32 Quantity);

	/**
	 * Dohvaća teksturu ikone za zadani tip itema.
	 * @param ItemType - tip itema
	 * @return Tekstura ikone ili nullptr ako ne postoji
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory Slot")
	UTexture2D* GetItemIcon(EItemType ItemType) const;

	/**
	 * Dohvaća podatke o itemu iz DataTable.
	 * @param ItemType - tip itema
	 * @return FItemData struktura s podacima
	 */
	UFUNCTION(BlueprintCallable, Category = "Inventory Slot")
	FItemData GetItemData(EItemType ItemType) const;

	// ==================== Stanje slota ====================

	/** Vraća true ako je slot prazan */
	UFUNCTION(BlueprintPure, Category = "Inventory Slot")
	bool IsSlotEmpty() const { return bIsEmpty; }

	/** Vraća true ako je slot trenutno selektiran */
	UFUNCTION(BlueprintPure, Category = "Inventory Slot")
	bool IsSlotSelected() const { return bIsSelected; }

	/** Vraća trenutni tip itema u slotu */
	UFUNCTION(BlueprintPure, Category = "Inventory Slot")
	EItemType GetCurrentItemType() const { return CurrentItemType; }

	/** Vraća trenutnu količinu u slotu */
	UFUNCTION(BlueprintPure, Category = "Inventory Slot")
	int32 GetCurrentQuantity() const { return CurrentQuantity; }

protected:
	virtual void NativeConstruct() override;
	virtual FReply NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent) override;

private:
	/** Handler za klik na button */
	UFUNCTION()
	void OnClickButtonClicked();

protected:
	/** Je li slot prazan */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory Slot|State")
	bool bIsEmpty = true;

	/** Je li slot selektiran */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory Slot|State")
	bool bIsSelected = false;

	/** Trenutni tip itema */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory Slot|State")
	EItemType CurrentItemType = EItemType::None;

	/** Trenutna količina */
	UPROPERTY(BlueprintReadOnly, Category = "Inventory Slot|State")
	int32 CurrentQuantity = 0;

private:
	/** Ažurira vizual ikone */
	void UpdateIconVisual(const FItemData& Data);

	/** Ažurira vizual količine */
	void UpdateQuantityVisual(int32 Quantity);

	/** Ažurira tooltip */
	void UpdateTooltip(const FItemData& Data);

	/** Postavlja slot u prazno stanje */
	void ClearSlotVisual();
};
