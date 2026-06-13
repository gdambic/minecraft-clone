#include "CraftingRecipe.h"

bool FCraftingRecipe::IsValid() const
{
	// Recept mora imati ID i validan rezultat
	if (RecipeId.IsEmpty() || !Result.IsValid())
	{
		return false;
	}

	// Grid size mora biti 2 ili 3
	if (GridSize < 2 || GridSize > 3)
	{
		return false;
	}

	if (Type == ECraftingRecipeType::Shapeless)
	{
		// Shapeless mora imati barem jedan ingredient
		return Ingredients.Num() > 0;
	}
	else // Shaped
	{
		// Shaped mora imati pattern i key
		if (Pattern.Num() == 0 || Key.Num() == 0)
		{
			return false;
		}

		// Provjeri da svi simboli u patternu imaju definiciju u key
		for (const FString& Row : Pattern)
		{
			for (int32 i = 0; i < Row.Len(); ++i)
			{
				TCHAR Char = Row[i];
				if (Char != ' ' && Char != '_') // Prazan prostor
				{
					FString CharStr;
					CharStr.AppendChar(Char);
					if (!Key.Contains(CharStr))
					{
						return false;
					}
				}
			}
		}

		return true;
	}
}

FIntPoint FCraftingRecipe::GetPatternDimensions() const
{
	if (Pattern.Num() == 0)
	{
		return FIntPoint(0, 0);
	}

	int32 Height = Pattern.Num();
	int32 Width = 0;
	for (const FString& Row : Pattern)
	{
		Width = FMath::Max(Width, Row.Len());
	}

	return FIntPoint(Width, Height);
}

TArray<TArray<EItemType>> FCraftingRecipe::GetNormalizedPattern() const
{
	TArray<TArray<EItemType>> NormalizedGrid;

	if (Pattern.Num() == 0)
	{
		return NormalizedGrid;
	}

	// Prvo konvertiraj pattern u 2D grid
	int32 MaxWidth = 0;
	for (const FString& Row : Pattern)
	{
		MaxWidth = FMath::Max(MaxWidth, Row.Len());
	}

	TArray<TArray<EItemType>> Grid;
	for (const FString& Row : Pattern)
	{
		TArray<EItemType> GridRow;
		for (int32 i = 0; i < MaxWidth; ++i)
		{
			if (i < Row.Len())
			{
				TCHAR Char = Row[i];
				if (Char == ' ' || Char == '_')
				{
					GridRow.Add(EItemType::None);
				}
				else
				{
					FString CharStr;
					CharStr.AppendChar(Char);
					const EItemType* ItemType = Key.Find(CharStr);
					GridRow.Add(ItemType ? *ItemType : EItemType::None);
				}
			}
			else
			{
				GridRow.Add(EItemType::None);
			}
		}
		Grid.Add(GridRow);
	}

	// Pronađi bounding box (ukloni prazne rubne redove/stupce)
	int32 MinRow = Grid.Num();
	int32 MaxRow = -1;
	int32 MinCol = MaxWidth;
	int32 MaxCol = -1;

	for (int32 Row = 0; Row < Grid.Num(); ++Row)
	{
		for (int32 Col = 0; Col < Grid[Row].Num(); ++Col)
		{
			if (Grid[Row][Col] != EItemType::None)
			{
				MinRow = FMath::Min(MinRow, Row);
				MaxRow = FMath::Max(MaxRow, Row);
				MinCol = FMath::Min(MinCol, Col);
				MaxCol = FMath::Max(MaxCol, Col);
			}
		}
	}

	// Ako je pattern prazan, vrati prazni grid
	if (MaxRow < 0 || MaxCol < 0)
	{
		return NormalizedGrid;
	}

	// Ekstraktira normalizirani pattern
	for (int32 Row = MinRow; Row <= MaxRow; ++Row)
	{
		TArray<EItemType> NormalizedRow;
		for (int32 Col = MinCol; Col <= MaxCol; ++Col)
		{
			NormalizedRow.Add(Grid[Row][Col]);
		}
		NormalizedGrid.Add(NormalizedRow);
	}

	return NormalizedGrid;
}
