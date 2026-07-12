#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ItemType.h"
#include "BlockInventoryItemWidget.generated.h"

class UTextBlock;

/**
 * C++ bazna klasa za Block Inventory Item widget.
 * Prikazuje naziv itema i količinu u tekstualnom obliku.
 *
 * Potrebni BindWidget elementi u Blueprintu:
 * - TextItemName (UTextBlock) - naziv itema
 * - TextItemCount (UTextBlock) - količina itema
 */
UCLASS()
class MINECRAFTCLONE_API UBlockInventoryItemWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==================== UI elementi (BindWidget) ====================

	/** Tekst za naziv itema */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TextItemName;

	/** Tekst za količinu itema */
	UPROPERTY(meta = (BindWidget))
	TObjectPtr<UTextBlock> TextItemCount;

	// ==================== Konfiguracija stila ====================

	/** Boja teksta kada je item selektiran */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Block Inventory Item|Style")
	FLinearColor SelectedColor = FLinearColor(1.0f, 1.0f, 0.0f, 1.0f); // Yellow

	/** Boja teksta kada item nije selektiran */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Block Inventory Item|Style")
	FLinearColor DefaultColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f); // White

	// ==================== Glavne funkcije ====================

	/**
	 * Postavlja tip itema i ažurira prikaz naziva.
	 * @param NewItemType - novi tip itema
	 */
	UFUNCTION(BlueprintCallable, Category = "Block Inventory Item")
	void SetItemType(EItemType NewItemType);

	/**
	 * Postavlja količinu itema i ažurira prikaz.
	 * @param Count - nova količina
	 */
	UFUNCTION(BlueprintCallable, Category = "Block Inventory Item")
	void SetItemCount(int32 Count);

	/**
	 * Postavlja stanje selekcije (mijenja boju teksta).
	 * @param bSelected - true za selektirani izgled
	 */
	UFUNCTION(BlueprintCallable, Category = "Block Inventory Item")
	void SetSelected(bool bSelected);

	// ==================== Stanje ====================

	/** Vraća trenutni tip itema */
	UFUNCTION(BlueprintPure, Category = "Block Inventory Item")
	EItemType GetItemType() const { return ItemType; }

	/** Vraća trenutnu količinu */
	UFUNCTION(BlueprintPure, Category = "Block Inventory Item")
	int32 GetItemCount() const { return ItemCount; }

	/** Vraća je li selektiran */
	UFUNCTION(BlueprintPure, Category = "Block Inventory Item")
	bool IsSelected() const { return bIsSelected; }

protected:
	/** Trenutni tip itema */
	UPROPERTY(BlueprintReadOnly, Category = "Block Inventory Item|State")
	EItemType ItemType = EItemType::None;

	/** Trenutna količina */
	UPROPERTY(BlueprintReadOnly, Category = "Block Inventory Item|State")
	int32 ItemCount = 0;

	/** Je li selektiran */
	UPROPERTY(BlueprintReadOnly, Category = "Block Inventory Item|State")
	bool bIsSelected = false;
};
