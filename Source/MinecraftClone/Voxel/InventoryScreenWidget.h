#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "InventorySlotWidget.h"   // EInventoryClickType
#include "InventoryScreenWidget.generated.h"

class UCanvasPanel;
class UCraftingPanel;
class UPlayerInventoryPanel;
class UInventorySlotWidget;
class UInventoryComponent;
class AFirstPersonCharacter;

/**
 * Bazna klasa svih inventory UI ekrana. Ekran se u WBP-u SASTAVLJA od
 * ponovno iskoristivih panela (UCraftingPanel, UPlayerInventoryPanel...),
 * a ova klasa daje zajednicku logiku: cursor slot koji prati mis,
 * pickup/place na klik i osvjezavanje svih panela.
 *
 * Ekrani: WBP_FullInventory (CraftingPanel GridSize=2 + InventoryPanel),
 * WBP_CraftingTable (CraftingPanel GridSize=3 + InventoryPanel). Buduci
 * ekrani (furnace, chest) dodaju svoje panele; paneli koji ne postoje u
 * WBP-u se preskacu (BindWidgetOptional).
 *
 * BindWidget elementi:
 * - CanvasPanel (UCanvasPanel, obavezan) - korijen za cursor slot
 * - CraftingPanel (UCraftingPanel, opcionalan)
 * - InventoryPanel (UPlayerInventoryPanel, opcionalan)
 */
UCLASS()
class MINECRAFTCLONE_API UInventoryScreenWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UCanvasPanel> CanvasPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UCraftingPanel> CraftingPanel;

	UPROPERTY(meta = (BindWidgetOptional))
	TObjectPtr<UPlayerInventoryPanel> InventoryPanel;

	/** Klasa widgeta za cursor slot (WBP_InventorySlot) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Screen|Config")
	TSubclassOf<UInventorySlotWidget> CursorSlotWidgetClass;

	/** Veličina cursor slota */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Screen|Config")
	FVector2D CursorSlotSize = FVector2D(64.0f, 64.0f);

	/** Offset za centriranje cursor slota na miš */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Screen|Config")
	FVector2D CursorOffset = FVector2D(32.0f, 32.0f);

	/** Kreira slotove u svim panelima + cursor slot. Idempotentno. */
	UFUNCTION(BlueprintCallable, Category = "Screen")
	void InitializeScreen();

	/** Osvjezi sve panele i cursor slot iz inventara */
	UFUNCTION(BlueprintCallable, Category = "Screen")
	void RefreshAll();

	/** Vraća referencu na inventory komponentu vlasnika */
	UFUNCTION(BlueprintPure, Category = "Screen")
	UInventoryComponent* GetInventoryComponent() const { return InventoryComp; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	/** Pickup/place/quick-move na klik slota, pa refresh - zajednicko svim ekranima */
	UFUNCTION()
	void HandleSlotInteracted(int32 SlotIndex, EInventoryClickType ClickType);

	/** Ažurira prikaz cursor slota (vidljivost + sadrzaj held itema) */
	void UpdateCursorSlot();

	UPROPERTY(BlueprintReadOnly, Category = "Screen")
	TObjectPtr<UInventoryComponent> InventoryComp;

	UPROPERTY()
	TObjectPtr<UInventorySlotWidget> CursorSlot;

private:
	bool bInitialized = false;

	void ResolveInventoryComponent();
	void CreateCursorSlot();
};
