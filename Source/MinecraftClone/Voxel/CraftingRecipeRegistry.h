#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CraftingRecipe.h"
#include "CraftingRecipeRegistry.generated.h"

/**
 * Registar svih crafting recepata.
 * Učitava recepte iz JSON fajlova i omogućuje pretragu recepata po gridu.
 */
UCLASS()
class MINECRAFTCLONE_API UCraftingRecipeRegistry : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Inicijalizacija - učitava sve recepte */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	/**
	 * Pronađi recept koji odgovara trenutnom stanju crafting grida.
	 * @param CraftingGrid - Array itema u gridu (linearno, row-major)
	 * @param GridSize - Veličina grida (2 za 2x2, 3 za 3x3)
	 * @return Pointer na recept ako postoji match, nullptr inače
	 */
	const FCraftingRecipe* FindMatchingRecipe(const TArray<EItemType>& CraftingGrid, int32 GridSize) const;

	/** Dohvati sve učitane recepte */
	UFUNCTION(BlueprintCallable, Category = "Crafting")
	const TArray<FCraftingRecipe>& GetAllRecipes() const { return Recipes; }

	/** Dohvati broj učitanih recepata */
	UFUNCTION(BlueprintPure, Category = "Crafting")
	int32 GetRecipeCount() const { return Recipes.Num(); }

	/** Static helper za dohvat iz bilo kojeg UObject konteksta */
	UFUNCTION(BlueprintCallable, Category = "Crafting", meta = (WorldContext = "WorldContextObject"))
	static UCraftingRecipeRegistry* Get(const UObject* WorldContextObject);

protected:
	/** Učitaj sve recepte iz Content/Data/Recipes/ */
	void LoadAllRecipes();

	/** Parsiraj pojedinačni JSON u recept */
	bool ParseRecipeFromJson(const FString& JsonString, FCraftingRecipe& OutRecipe);

	/** Provjeri odgovara li shapeless recept gridu */
	bool MatchesShapelessRecipe(const FCraftingRecipe& Recipe, const TArray<EItemType>& CraftingGrid) const;

	/** Provjeri odgovara li shaped recept gridu */
	bool MatchesShapedRecipe(const FCraftingRecipe& Recipe, const TArray<EItemType>& CraftingGrid, int32 GridSize) const;

	/**
	 * Pokušaj matchati pattern na određenom offsetu u gridu.
	 * @param Pattern - Normalizirani 2D pattern recepta
	 * @param CraftingGrid - Crafting grid (linearni, row-major)
	 * @param GridSize - Veličina grida
	 * @param OffsetX - X offset u gridu
	 * @param OffsetY - Y offset u gridu
	 * @return true ako pattern odgovara na tom offsetu
	 */
	bool TryMatchPatternAtOffset(
		const TArray<TArray<EItemType>>& Pattern,
		const TArray<EItemType>& CraftingGrid,
		int32 GridSize,
		int32 OffsetX,
		int32 OffsetY) const;

	/** Konvertiraj string ime itema u EItemType */
	EItemType StringToItemType(const FString& ItemName) const;

private:
	/** Svi učitani recepti */
	UPROPERTY()
	TArray<FCraftingRecipe> Recipes;
};
