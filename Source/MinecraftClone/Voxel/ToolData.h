#pragma once

#include "CoreMinimal.h"
#include "ItemType.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "ToolData.generated.h"

/**
 * Klasa alata. Blok u Blocks.json navodi koju klasu preferira ("toolClass"),
 * a item u ruci kojoj klasi pripada (registry u ToolData.cpp) - poklapanje
 * ubrzava kopanje, neslaganje ga ostavlja na brzini gole ruke.
 */
UENUM(BlueprintType)
enum class EToolClass : uint8
{
	None,
	Axe,
	Pickaxe,
	Shovel
};

/**
 * Podaci alata za kopanje blokova.
 * Mining speed prati Minecraft tablicu po materijalu alata:
 * gola ruka 1, drvo 2, kamen 4, zeljezo 6, dijamant 8.
 */
USTRUCT(BlueprintType)
struct FToolData
{
	GENERATED_BODY()

	/** Klasa alata (None = item nije alat) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	EToolClass ToolClass = EToolClass::None;

	/** Mining speed multiplier (1 = gola ruka) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float MiningSpeed = 1.0f;

	/** Default konstruktor (nije alat) */
	FToolData()
		: ToolClass(EToolClass::None)
		, MiningSpeed(1.0f)
	{}

	FToolData(EToolClass InToolClass, float InMiningSpeed)
		: ToolClass(InToolClass)
		, MiningSpeed(InMiningSpeed)
	{}
};

/**
 * Staticna biblioteka za dohvat podataka alata.
 * Sadrzi registry alata (isti uzorak kao UWeaponDataLibrary).
 */
UCLASS()
class MINECRAFTCLONE_API UToolDataLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Podaci alata za tip itema (item koji nije alat vraca default) */
	UFUNCTION(BlueprintPure, Category = "Tools")
	static FToolData GetToolData(EItemType ItemType);

	/** Je li item u ruci pravi alat za blok koji preferira BlockToolClass */
	UFUNCTION(BlueprintPure, Category = "Tools")
	static bool IsCorrectTool(EItemType ItemType, EToolClass BlockToolClass);

	/** Mining speed itema na bloku: pravi alat svoj multiplier, inace 1 */
	UFUNCTION(BlueprintPure, Category = "Tools")
	static float GetMiningSpeed(EItemType ItemType, EToolClass BlockToolClass);

private:
	/** Registry alata (inicijalizira se jednom) */
	static const TMap<EItemType, FToolData>& GetToolRegistry();
};
