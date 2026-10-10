#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PlayerInventoryPanel.h"   // FOnPanelSlotInteracted
#include "CraftingPanel.generated.h"

class UUniformGridPanel;
class UHorizontalBox;
class UInventorySlotWidget;
class UInventoryComponent;

/**
 * Crafting dio ekrana: NxN input grid + output slot. GridSize se postavlja
 * PO INSTANCI u designeru ekrana (osobni inventar 2, crafting table 3) -
 * isti panel widget sluzi oba ekrana, razlikuje ih samo ta postavka.
 *
 * Potrebni BindWidget elementi u WBP_CraftingPanel:
 * - CraftingGrid (UUniformGridPanel) - NxN input slotova
 * - OutputBox (UHorizontalBox) - output slot
 */
UCLASS()
class MINECRAFTCLONE_API UCraftingPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UUniformGridPanel> CraftingGrid;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> OutputBox;

	/** Klasa widgeta za slot (WBP_InventorySlot) - postavlja se u WBP Class Defaults */
	UPROPERTY(EditDefaultsOnly, Category = "Panel|Config")
	TSubclassOf<UInventorySlotWidget> SlotWidgetClass;

	/** Velicina grida (2 = osobni inventar, 3 = crafting table) - postavlja se po instanci u designeru ekrana */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Panel|Config", meta = (ClampMin = "2", ClampMax = "3"))
	int32 GridSize = 2;

	/** Klik na bilo koji slot panela - ekran odlucuje sto s njim (pickup/place) */
	UPROPERTY(BlueprintAssignable, Category = "Panel")
	FOnPanelSlotInteracted OnSlotInteracted;

	/** Kreira sve slot widgete (poziva ekran pri inicijalizaciji) */
	void CreateSlots();

	/** Osvjezi prikaz svih slotova iz inventara */
	void Refresh(const UInventoryComponent* InventoryComp);

private:
	UPROPERTY()
	TArray<TObjectPtr<UInventorySlotWidget>> InputSlotWidgets;

	UPROPERTY()
	TObjectPtr<UInventorySlotWidget> OutputSlotWidget;

	UInventorySlotWidget* CreateSlotWidget(int32 SlotIndex);

	UFUNCTION()
	void HandleSlotInteracted(int32 SlotIndex, EInventoryClickType ClickType);
};
