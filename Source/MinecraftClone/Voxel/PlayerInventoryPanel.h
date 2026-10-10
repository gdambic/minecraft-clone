#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PlayerInventoryPanel.generated.h"

class UUniformGridPanel;
class UHorizontalBox;
class UInventorySlotWidget;
class UInventoryComponent;

/** Delegate kojim panel prosljeduje klik na slot ekranu koji ga sadrzi */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnPanelSlotClicked, int32, SlotIndex);

/**
 * Donji dio svakog inventory ekrana: 27 main slotova + 9 hotbar slotova.
 * Samostalan, ponovno iskoristiv panel - isti se ugraduje u osobni inventar,
 * crafting table i buduce ekrane (furnace, chest...).
 *
 * Potrebni BindWidget elementi u WBP_PlayerInventoryPanel:
 * - MainInventoryGrid (UUniformGridPanel) - 27 slotova, 3x9
 * - HotbarRow (UHorizontalBox) - 9 slotova
 */
UCLASS()
class MINECRAFTCLONE_API UPlayerInventoryPanel : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UUniformGridPanel> MainInventoryGrid;

	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> HotbarRow;

	/** Klasa widgeta za slot (WBP_InventorySlot) - postavlja se u WBP Class Defaults */
	UPROPERTY(EditDefaultsOnly, Category = "Panel|Config")
	TSubclassOf<UInventorySlotWidget> SlotWidgetClass;

	/** Klik na bilo koji slot panela - ekran odlucuje sto s njim (pickup/place) */
	UPROPERTY(BlueprintAssignable, Category = "Panel")
	FOnPanelSlotClicked OnSlotClicked;

	/** Kreira sve slot widgete (poziva ekran pri inicijalizaciji) */
	void CreateSlots();

	/** Osvjezi prikaz svih slotova iz inventara */
	void Refresh(const UInventoryComponent* InventoryComp);

private:
	UPROPERTY()
	TArray<TObjectPtr<UInventorySlotWidget>> MainSlotWidgets;

	UPROPERTY()
	TArray<TObjectPtr<UInventorySlotWidget>> HotbarSlotWidgets;

	UInventorySlotWidget* CreateSlotWidget(int32 SlotIndex);

	UFUNCTION()
	void HandleSlotClicked(int32 SlotIndex);
};
