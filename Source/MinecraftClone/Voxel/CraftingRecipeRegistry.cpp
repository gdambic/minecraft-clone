#include "CraftingRecipeRegistry.h"
#include "Engine/GameInstance.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void UCraftingRecipeRegistry::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LoadAllRecipes();
	UE_LOG(LogTemp, Log, TEXT("CraftingRecipeRegistry: Initialized with %d recipes"), Recipes.Num());
}

void UCraftingRecipeRegistry::LoadAllRecipes()
{
	// Putanja do recepata
	FString RecipesPath = FPaths::ProjectContentDir() / TEXT("Data/Recipes");

	// Pronađi sve JSON fajlove
	TArray<FString> FoundFiles;
	IFileManager::Get().FindFiles(FoundFiles, *RecipesPath, TEXT("*.json"));

	for (const FString& FileName : FoundFiles)
	{
		FString FullPath = RecipesPath / FileName;
		FString JsonString;

		if (FFileHelper::LoadFileToString(JsonString, *FullPath))
		{
			FCraftingRecipe Recipe;
			if (ParseRecipeFromJson(JsonString, Recipe))
			{
				if (Recipe.IsValid())
				{
					Recipes.Add(Recipe);
					UE_LOG(LogTemp, Log, TEXT("CraftingRecipeRegistry: Loaded recipe '%s' from %s"),
						*Recipe.RecipeId, *FileName);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("CraftingRecipeRegistry: Invalid recipe in %s"), *FileName);
				}
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("CraftingRecipeRegistry: Failed to parse %s"), *FileName);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("CraftingRecipeRegistry: Failed to load %s"), *FullPath);
		}
	}
}

bool UCraftingRecipeRegistry::ParseRecipeFromJson(const FString& JsonString, FCraftingRecipe& OutRecipe)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);

	if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
	{
		return false;
	}

	// ID
	OutRecipe.RecipeId = JsonObject->GetStringField(TEXT("id"));

	// Tip
	FString TypeStr = JsonObject->GetStringField(TEXT("type"));
	OutRecipe.Type = TypeStr.Equals(TEXT("shaped"), ESearchCase::IgnoreCase)
		? ECraftingRecipeType::Shaped
		: ECraftingRecipeType::Shapeless;

	// Grid size
	OutRecipe.GridSize = JsonObject->GetIntegerField(TEXT("gridSize"));

	// Result
	const TSharedPtr<FJsonObject>* ResultObject;
	if (JsonObject->TryGetObjectField(TEXT("result"), ResultObject))
	{
		OutRecipe.Result.ItemType = StringToItemType((*ResultObject)->GetStringField(TEXT("item")));
		OutRecipe.Result.Count = (*ResultObject)->GetIntegerField(TEXT("count"));
	}

	if (OutRecipe.Type == ECraftingRecipeType::Shapeless)
	{
		// Shapeless - parsiraj ingredients
		const TArray<TSharedPtr<FJsonValue>>* IngredientsArray;
		if (JsonObject->TryGetArrayField(TEXT("ingredients"), IngredientsArray))
		{
			for (const TSharedPtr<FJsonValue>& IngValue : *IngredientsArray)
			{
				const TSharedPtr<FJsonObject>& IngObject = IngValue->AsObject();
				if (IngObject.IsValid())
				{
					FCraftingIngredient Ingredient;
					Ingredient.ItemType = StringToItemType(IngObject->GetStringField(TEXT("item")));
					Ingredient.Count = IngObject->HasField(TEXT("count"))
						? IngObject->GetIntegerField(TEXT("count"))
						: 1;
					OutRecipe.Ingredients.Add(Ingredient);
				}
			}
		}
	}
	else
	{
		// Shaped - parsiraj pattern i key
		const TArray<TSharedPtr<FJsonValue>>* PatternArray;
		if (JsonObject->TryGetArrayField(TEXT("pattern"), PatternArray))
		{
			for (const TSharedPtr<FJsonValue>& PatternValue : *PatternArray)
			{
				OutRecipe.Pattern.Add(PatternValue->AsString());
			}
		}

		const TSharedPtr<FJsonObject>* KeyObject;
		if (JsonObject->TryGetObjectField(TEXT("key"), KeyObject))
		{
			for (const auto& Pair : (*KeyObject)->Values)
			{
				const TSharedPtr<FJsonObject>& ItemObject = Pair.Value->AsObject();
				if (ItemObject.IsValid())
				{
					FString ItemName = ItemObject->GetStringField(TEXT("item"));
					OutRecipe.Key.Add(Pair.Key, StringToItemType(ItemName));
				}
			}
		}
	}

	return true;
}

const FCraftingRecipe* UCraftingRecipeRegistry::FindMatchingRecipe(const TArray<EItemType>& CraftingGrid, int32 GridSize) const
{
	for (const FCraftingRecipe& Recipe : Recipes)
	{
		// Provjeri da recept odgovara veličini grida
		if (Recipe.GridSize > GridSize)
		{
			continue;
		}

		bool bMatches = false;
		if (Recipe.Type == ECraftingRecipeType::Shapeless)
		{
			bMatches = MatchesShapelessRecipe(Recipe, CraftingGrid);
		}
		else
		{
			bMatches = MatchesShapedRecipe(Recipe, CraftingGrid, GridSize);
		}

		if (bMatches)
		{
			return &Recipe;
		}
	}

	return nullptr;
}

bool UCraftingRecipeRegistry::MatchesShapelessRecipe(const FCraftingRecipe& Recipe, const TArray<EItemType>& CraftingGrid) const
{
	// Prebroji iteme u gridu
	TMap<EItemType, int32> GridItemCounts;
	for (EItemType Item : CraftingGrid)
	{
		if (Item != EItemType::None)
		{
			GridItemCounts.FindOrAdd(Item)++;
		}
	}

	// Prebroji potrebne ingredijente
	TMap<EItemType, int32> RequiredCounts;
	for (const FCraftingIngredient& Ingredient : Recipe.Ingredients)
	{
		RequiredCounts.FindOrAdd(Ingredient.ItemType) += Ingredient.Count;
	}

	// Provjeri da grid ima točno potrebne iteme
	if (GridItemCounts.Num() != RequiredCounts.Num())
	{
		return false;
	}

	for (const auto& Pair : RequiredCounts)
	{
		const int32* GridCount = GridItemCounts.Find(Pair.Key);
		if (!GridCount || *GridCount != Pair.Value)
		{
			return false;
		}
	}

	return true;
}

bool UCraftingRecipeRegistry::MatchesShapedRecipe(const FCraftingRecipe& Recipe, const TArray<EItemType>& CraftingGrid, int32 GridSize) const
{
	// Dohvati normalizirani pattern
	TArray<TArray<EItemType>> Pattern = Recipe.GetNormalizedPattern();
	if (Pattern.Num() == 0)
	{
		return false;
	}

	int32 PatternHeight = Pattern.Num();
	int32 PatternWidth = Pattern[0].Num();

	// Pronađi bounding box itema u gridu
	int32 MinRow = GridSize, MaxRow = -1, MinCol = GridSize, MaxCol = -1;
	for (int32 i = 0; i < CraftingGrid.Num(); ++i)
	{
		if (CraftingGrid[i] != EItemType::None)
		{
			int32 Row = i / GridSize;
			int32 Col = i % GridSize;
			MinRow = FMath::Min(MinRow, Row);
			MaxRow = FMath::Max(MaxRow, Row);
			MinCol = FMath::Min(MinCol, Col);
			MaxCol = FMath::Max(MaxCol, Col);
		}
	}

	// Ako grid je prazan, nema matcha
	if (MaxRow < 0)
	{
		return false;
	}

	int32 GridItemWidth = MaxCol - MinCol + 1;
	int32 GridItemHeight = MaxRow - MinRow + 1;

	// Ako dimenzije ne odgovaraju, nema matcha
	if (GridItemWidth != PatternWidth || GridItemHeight != PatternHeight)
	{
		return false;
	}

	// Pokušaj matchati pattern na offsetu (MinCol, MinRow)
	return TryMatchPatternAtOffset(Pattern, CraftingGrid, GridSize, MinCol, MinRow);
}

bool UCraftingRecipeRegistry::TryMatchPatternAtOffset(
	const TArray<TArray<EItemType>>& Pattern,
	const TArray<EItemType>& CraftingGrid,
	int32 GridSize,
	int32 OffsetX,
	int32 OffsetY) const
{
	int32 PatternHeight = Pattern.Num();
	int32 PatternWidth = Pattern[0].Num();

	// Provjeri da pattern stane u grid
	if (OffsetX + PatternWidth > GridSize || OffsetY + PatternHeight > GridSize)
	{
		return false;
	}

	// Provjeri svaku ćeliju grida
	for (int32 Row = 0; Row < GridSize; ++Row)
	{
		for (int32 Col = 0; Col < GridSize; ++Col)
		{
			int32 GridIndex = Row * GridSize + Col;
			EItemType GridItem = CraftingGrid[GridIndex];

			// Je li ova ćelija unutar patterna?
			bool bInPattern = (Col >= OffsetX && Col < OffsetX + PatternWidth &&
							   Row >= OffsetY && Row < OffsetY + PatternHeight);

			if (bInPattern)
			{
				// Dohvati očekivani item iz patterna
				int32 PatternRow = Row - OffsetY;
				int32 PatternCol = Col - OffsetX;
				EItemType PatternItem = Pattern[PatternRow][PatternCol];

				if (GridItem != PatternItem)
				{
					return false;
				}
			}
			else
			{
				// Izvan patterna - mora biti prazno
				if (GridItem != EItemType::None)
				{
					return false;
				}
			}
		}
	}

	return true;
}

EItemType UCraftingRecipeRegistry::StringToItemType(const FString& ItemName) const
{
	// Mapiranje stringova na EItemType
	static TMap<FString, EItemType> ItemTypeMap = {
		{TEXT("None"), EItemType::None},
		{TEXT("Dirt"), EItemType::Dirt},
		{TEXT("Stone"), EItemType::Stone},
		{TEXT("Grass"), EItemType::Grass},
		{TEXT("OakLog"), EItemType::OakLog},
		{TEXT("BirchLog"), EItemType::BirchLog},
		{TEXT("OakSapling"), EItemType::OakSapling},
		{TEXT("BirchSapling"), EItemType::BirchSapling},
		{TEXT("OakPlanks"), EItemType::OakPlanks},
		{TEXT("BirchPlanks"), EItemType::BirchPlanks},
		{TEXT("Stick"), EItemType::Stick},
		{TEXT("CraftingTable"), EItemType::CraftingTable}
	};

	const EItemType* Found = ItemTypeMap.Find(ItemName);
	return Found ? *Found : EItemType::None;
}

UCraftingRecipeRegistry* UCraftingRecipeRegistry::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	UGameInstance* GameInstance = World->GetGameInstance();
	if (!GameInstance)
	{
		return nullptr;
	}

	return GameInstance->GetSubsystem<UCraftingRecipeRegistry>();
}
