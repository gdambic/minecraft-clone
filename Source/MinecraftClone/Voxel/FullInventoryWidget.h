#pragma once

#include "CoreMinimal.h"
#include "InventoryScreenWidget.h"
#include "FullInventoryWidget.generated.h"

/**
 * Ekran osobnog inventara (WBP_FullInventory): CraftingPanel (GridSize=2) +
 * InventoryPanel. Sva zajednicka logika je u UInventoryScreenWidget; ova
 * klasa je mjesto za buduce specificnosti osobnog inventara (prikaz lika,
 * armor slotovi...).
 */
UCLASS()
class MINECRAFTCLONE_API UFullInventoryWidget : public UInventoryScreenWidget
{
	GENERATED_BODY()

public:
	/** Aliasi zadrzani za postojece BP pozive (stari nazivi) */
	UFUNCTION(BlueprintCallable, Category = "Full Inventory")
	void InitializeInventory() { InitializeScreen(); }

	UFUNCTION(BlueprintCallable, Category = "Full Inventory")
	void RefreshAllSlots() { RefreshAll(); }
};
