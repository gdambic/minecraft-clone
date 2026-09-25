#pragma once

#include "CoreMinimal.h"
#include "BiomeDefinition.generated.h"

/**
 * Koji biome tint blok prima (JSON polje "biomeTint" u Blocks.json).
 * None = blok se ne boja po biomu (default).
 */
UENUM(BlueprintType)
enum class EBiomeTintType : uint8
{
	None,
	Grass,
	Foliage
};

/**
 * Definicija bioma - ucitava se iz Content/Data/Biomes.json (UBlockRegistry).
 * Biom nema C++ enum: identitet mu je ime + indeks u listi, na biom se nista
 * ne switcha - potrosaci citaju svojstva. Vidi Docs/PLAN_Biomes.md.
 */
USTRUCT(BlueprintType)
struct MINECRAFTCLONE_API FBiomeDefinition
{
	GENERATED_BODY()

	/** Jedinstveno ime bioma (npr. "Plains") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome")
	FName Name;

	/** Ime za prikaz */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome")
	FText DisplayName;

	/** Tint blokova s biomeTint "grass" (JSON: "#RRGGBB") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome")
	FLinearColor GrassTint = FLinearColor::White;

	/** Tint blokova s biomeTint "foliage" - REZERVIRANO, lisce jos nije u opsegu */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome")
	FLinearColor FoliageTint = FLinearColor::White;

	/** REZERVIRANO: mnozitelj gustoce stabala - generacija ga jos ne cita */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome")
	float TreeDensity = 1.0f;

	/** REZERVIRANO: vrste drveca u biomu - generacija ih jos ne cita */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome")
	TArray<FString> TreeTypes;

	/** REZERVIRANO: povrsinski blok (npr. Sand za pustinju) - generacija ga jos ne cita */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome")
	FString SurfaceBlock;

	/** REZERVIRANO: mnozitelj amplitude visine terena - generacija ga jos ne cita */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome")
	float HeightAmplitudeScale = 1.0f;

	bool IsValid() const { return !Name.IsNone(); }
};
