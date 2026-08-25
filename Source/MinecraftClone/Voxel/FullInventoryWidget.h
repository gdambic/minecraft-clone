#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InventoryComponent.h"
#include "FullInventoryWidget.generated.h"

class UUniformGridPanel;
class UHorizontalBox;
class UCanvasPanel;
class UInventorySlotWidget;
class AFirstPersonCharacter;

/**
 * C++ bazna klasa za Full Inventory widget (inventory + crafting).
 * Blueprint widget nasljeđuje ovu klasu i definira layout.
 *
 * Potrebni BindWidget elementi u Blueprintu:
 * - MainInventoryGrid (UUniformGridPanel) - grid za 27 main slotova
 * - HotbarRow (UHorizontalBox) - red za 9 hotbar slotova
 * - CraftingGrid (UUniformGridPanel) - 2x2 grid za crafting input
 * - HorizontalBox_CraftingOutput (UHorizontalBox) - kontejner za output slot
 * - CanvasPanel (UCanvasPanel) - za cursor slot
 */
UCLASS()
class MINECRAFTCLONE_API UFullInventoryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==================== UI elementi (BindWidget) ====================

	/** Grid za main inventory (27 slotova, 3 reda x 9 stupaca) */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UUniformGridPanel> MainInventoryGrid;

	/** Red za hotbar (9 slotova) */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> HotbarRow;

	/** Grid za crafting input (4 slota, 2x2) */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UUniformGridPanel> CraftingGrid;

	/** Kontejner za crafting output slot */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UHorizontalBox> HorizontalBox_CraftingOutput;

	/** Canvas panel za cursor slot */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> CanvasPanel;

	// ==================== Konfiguracija ====================

	/** Klasa widgeta za slot (WBP_InventorySlot) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Full Inventory|Config")
	TSubclassOf<UInventorySlotWidget> InventorySlotWidgetClass;

	/** Veličina cursor slota */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Full Inventory|Config")
	FVector2D CursorSlotSize = FVector2D(64.0f, 64.0f);

	/** Offset za centriranje cursor slota na miš */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Full Inventory|Config")
	FVector2D CursorOffset = FVector2D(32.0f, 32.0f);

	// ==================== Glavne funkcije ====================

	/** Kreira 27 main inventory slotova */
	UFUNCTION(BlueprintCallable, Category = "Full Inventory")
	void CreateMainSlots();

	/** Kreira 9 hotbar slotova */
	UFUNCTION(BlueprintCallable, Category = "Full Inventory")
	void CreateHotbarSlots();

	/** Kreira 4 crafting input slota + 1 output slot */
	UFUNCTION(BlueprintCallable, Category = "Full Inventory")
	void CreateCraftingSlots();

	/** Kreira cursor slot za drag & drop */
	UFUNCTION(BlueprintCallable, Category = "Full Inventory")
	void CreateCursorSlot();

	/** Osvježava sve slotove iz inventara */
	UFUNCTION(BlueprintCallable, Category = "Full Inventory")
	void RefreshAllSlots();

	/** Obrađuje klik na slot */
	UFUNCTION(BlueprintCallable, Category = "Full Inventory")
	void HandleSlotClicked(int32 SlotIndex);

	/** Ažurira prikaz cursor slota */
	UFUNCTION(BlueprintCallable, Category = "Full Inventory")
	void UpdateCursorSlot();

	/** Inicijalizira sve (poziva Create funkcije i RefreshAllSlots) */
	UFUNCTION(BlueprintCallable, Category = "Full Inventory")
	void InitializeInventory();

	// ==================== Reference ====================

	/** Postavlja referencu na player charactera */
	UFUNCTION(BlueprintCallable, Category = "Full Inventory")
	void SetPlayerCharacter(AFirstPersonCharacter* Character);

	/** Vraća referencu na inventory komponentu */
	UFUNCTION(BlueprintPure, Category = "Full Inventory")
	UInventoryComponent* GetInventoryComponent() const { return InventoryComp; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// ==================== Slot Widgeti ====================

	/** Array main slot widgeta (0-26) */
	UPROPERTY(BlueprintReadOnly, Category = "Full Inventory|Slots")
	TArray<TObjectPtr<UInventorySlotWidget>> MainSlotWidgets;

	/** Array hotbar slot widgeta (27-35) */
	UPROPERTY(BlueprintReadOnly, Category = "Full Inventory|Slots")
	TArray<TObjectPtr<UInventorySlotWidget>> HotbarSlotWidgets;

	/** Array crafting input slot widgeta (36-39) */
	UPROPERTY(BlueprintReadOnly, Category = "Full Inventory|Slots")
	TArray<TObjectPtr<UInventorySlotWidget>> CraftingSlotWidgets;

	/** Output slot widget (40) */
	UPROPERTY(BlueprintReadOnly, Category = "Full Inventory|Slots")
	TObjectPtr<UInventorySlotWidget> OutputSlotWidget;

	/** Cursor slot za prikaz held itema */
	UPROPERTY(BlueprintReadOnly, Category = "Full Inventory|Slots")
	TObjectPtr<UInventorySlotWidget> CursorSlot;

	// ==================== Reference ====================

	UPROPERTY(BlueprintReadOnly, Category = "Full Inventory|References")
	TObjectPtr<AFirstPersonCharacter> PlayerCharacter;

	UPROPERTY(BlueprintReadOnly, Category = "Full Inventory|References")
	TObjectPtr<UInventoryComponent> InventoryComp;

private:
	/** Konstante */
	static constexpr int32 MainSlotCount = 27;
	static constexpr int32 HotbarSlotCount = 9;
	static constexpr int32 HotbarStartIndex = 27;
	static constexpr int32 CraftingInputCount = 4;
	static constexpr int32 CraftingStartIndex = 36;
	static constexpr int32 CraftingOutputIndex = 40;

	/** Kreira slot widget s osnovnim postavkama */
	UInventorySlotWidget* CreateSlotWidget(int32 SlotIndex);

	/** Handler za OnSlotClicked event */
	UFUNCTION()
	void OnSlotClickedHandler(int32 SlotIndex);
};
