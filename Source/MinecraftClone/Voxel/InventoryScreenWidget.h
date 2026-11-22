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

	// ==================== Drag-split (Minecraft povlacenje s itemom u ruci) ====================
	// Pritisak s itemom u ruci ne djeluje odmah: slotovi preko kojih mis prijede
	// skupljaju se dok se tipka ne otpusti. Desna = po 1 u svaki, lijeva = stack
	// ravnomjerno podijeljen. Samo jedan slot = obican klik na njega.

	bool bDragActive = false;
	bool bDragRight = false;
	int32 DragPressSlot = -1;
	TArray<int32> DragSlots;

	/** Slot moze primiti drag: aktivan, nije output, prazan ili isti tip koji nije pun */
	bool CanDragInto(int32 SlotIndex) const;

	/** Koliko svaki drag slot dobiva (paralelno s DragSlots) i koliko ostaje u ruci */
	void ComputeDragAmounts(TArray<int32>& OutAmounts, int32& OutRemaining) const;

	/** Prikaz rasporeda u slotovima i cursor slotu dok tipka jos nije otpustena */
	void ApplyDragPreview();

	/** Tipka otpustena: rasporedi iteme (ili obican klik ako je slot samo jedan) */
	void FinishDrag();

	UInventorySlotWidget* FindSlotWidget(int32 SlotIndex) const;

	// ==================== Dvostruki klik (Minecraft prag 250 ms, mjeri se ovdje) ====================

	/** Najveci razmak dva lijeva klika na isti slot koji se broji kao dvostruki klik */
	static constexpr double DoubleClickSeconds = 0.25;

	double LastLeftClickTime = 0.0;
	int32 LastLeftClickSlot = -1;

	/** Tip itema zadnjeg shift+klika - shift+dvostruki klik premjesta sve takve */
	EItemType LastQuickMovedType = EItemType::None;

	/** Lijevi pritisak: je li ovo drugi klik dvostrukog klika (i zapamti ovaj) */
	bool RegisterLeftClick(int32 SlotIndex);

	/** Shift+dvostruki klik: quick-move svaki slot s LastQuickMovedType u istom dijelu inventara */
	void QuickMoveAllOfType(int32 ClickedSlot);
};
