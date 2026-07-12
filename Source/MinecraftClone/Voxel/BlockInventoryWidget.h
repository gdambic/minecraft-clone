#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ItemType.h"
#include "BlockInventoryWidget.generated.h"

class UBlockInventoryItemWidget;
class AFirstPersonCharacter;

/**
 * C++ bazna klasa za Block Inventory widget.
 * Prikazuje listu block itema u tekstualnom obliku.
 */
UCLASS()
class MINECRAFTCLONE_API UBlockInventoryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==================== Glavne funkcije ====================

	/**
	 * Ažurira highlight na block itemima.
	 * @param NewIndex - index novog selektiranog itema
	 */
	UFUNCTION(BlueprintCallable, Category = "Block Inventory")
	void UpdateBlockHighlight(int32 NewIndex);

	/**
	 * Handler za promjenu inventara.
	 * @param ItemType - tip itema koji se promijenio
	 * @param OldCount - stara količina
	 * @param NewCount - nova količina
	 */
	UFUNCTION(BlueprintCallable, Category = "Block Inventory")
	void HandleInventoryChanged(EItemType ItemType, int32 OldCount, int32 NewCount);

	/**
	 * Handler za promjenu selektiranog itema.
	 * @param NewIndex - novi selektirani index
	 * @param NewItemType - novi tip itema
	 */
	UFUNCTION(BlueprintCallable, Category = "Block Inventory")
	void HandleSelectedItemChanged(int32 NewIndex, EItemType NewItemType);

	/**
	 * Osvježava UI prikaz.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block Inventory")
	void RefreshUI();

	/**
	 * Inicijalizira widget - pronalazi reference i bindira delegate.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block Inventory")
	void InitializeBlockInventory();

	// ==================== Reference ====================

	UFUNCTION(BlueprintPure, Category = "Block Inventory")
	AFirstPersonCharacter* GetPlayerCharacter() const { return PlayerCharacter; }

protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	/** Array block item widgeta */
	UPROPERTY(BlueprintReadOnly, Category = "Block Inventory|State")
	TArray<TObjectPtr<UBlockInventoryItemWidget>> BlockTextWidgets;

	/** Referenca na player charactera */
	UPROPERTY(BlueprintReadOnly, Category = "Block Inventory|References")
	TObjectPtr<AFirstPersonCharacter> PlayerCharacter;

	/** Trenutno selektirani index */
	UPROPERTY(BlueprintReadOnly, Category = "Block Inventory|State")
	int32 CurrentSelectedIndex = 0;

private:
	/** Bindira delegate na inventory i character evente */
	void BindDelegates();

	/** Unbindira delegate */
	void UnbindDelegates();

	/** Handler za OnSelectedItemChanged */
	UFUNCTION()
	void OnSelectedItemChangedHandler(int32 NewIndex, EItemType NewItemType);
};
