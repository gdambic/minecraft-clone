#pragma once

#include "CoreMinimal.h"
#include "ItemType.h"
#include "CraftingRecipe.generated.h"

/**
 * Ingredient za crafting recept
 */
USTRUCT(BlueprintType)
struct FCraftingIngredient
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EItemType ItemType = EItemType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Count = 1;

	bool IsValid() const { return ItemType != EItemType::None && Count > 0; }
};

/**
 * Rezultat craftanja
 */
USTRUCT(BlueprintType)
struct FCraftingResult
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	EItemType ItemType = EItemType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 Count = 1;

	bool IsValid() const { return ItemType != EItemType::None && Count > 0; }
};

/**
 * Tip recepta - shaped (oblik je bitan) ili shapeless (samo ingredijenti)
 */
UENUM(BlueprintType)
enum class ECraftingRecipeType : uint8
{
	Shapeless,  // Samo ingredijenti, pozicija nije bitna
	Shaped      // Oblik patterna je bitan
};

/**
 * Crafting recept
 */
USTRUCT(BlueprintType)
struct FCraftingRecipe
{
	GENERATED_BODY()

	/** Jedinstveni ID recepta */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString RecipeId;

	/** Tip recepta (shaped/shapeless) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	ECraftingRecipeType Type = ECraftingRecipeType::Shapeless;

	/** Minimalna veličina grida potrebna za ovaj recept (2 ili 3) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 GridSize = 2;

	/** Rezultat craftanja */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FCraftingResult Result;

	// === Za Shapeless recepte ===

	/** Lista ingredijenata (za shapeless recepte) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FCraftingIngredient> Ingredients;

	// === Za Shaped recepte ===

	/** Pattern (npr. ["PP", "PP"] za 2x2 planks) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TArray<FString> Pattern;

	/** Mapiranje simbola na iteme (npr. "P" -> OakPlanks) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	TMap<FString, EItemType> Key;

	/** Provjeri je li recept validan */
	bool IsValid() const;

	/** Dohvati dimenzije patterna (širina, visina) */
	FIntPoint GetPatternDimensions() const;

	/**
	 * Dohvati normalizirani pattern (ukloni prazne redove/stupce na rubovima)
	 * Vraća pattern kao 2D grid EItemType vrijednosti
	 */
	TArray<TArray<EItemType>> GetNormalizedPattern() const;
};
