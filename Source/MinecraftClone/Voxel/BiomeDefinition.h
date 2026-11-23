#pragma once

#include "CoreMinimal.h"
#include "BlockType.h"
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
 * Kako biom dobiva mjesto u svijetu (JSON polje "placement" u Biomes.json).
 * Noise = bira ga biome noise jednakim pragovima (default).
 * Peaks = ne sudjeluje u biome noiseu; pripada stupcima koje noise planina
 * podigne barem za AVoxelWorld::PeakBiomeLift (Docs/PLAN_StonyPeaks.md).
 */
UENUM(BlueprintType)
enum class EBiomePlacement : uint8
{
	Noise,
	Peaks
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

	/** Mnozitelj sanse za stablo na stupcima ovog bioma (1 = puna gustoca, 0 = bez stabala) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome")
	float TreeDensity = 1.0f;

	/** REZERVIRANO: vrste drveca u biomu - generacija ih jos ne cita */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome")
	TArray<FString> TreeTypes;

	/** Najgornji blok stupca (JSON "surfaceBlock") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome")
	EBlockType SurfaceBlock = EBlockType::Grass;

	/** Blok ispod najgornjeg, do dubine SubsurfaceDepth (JSON "subsurfaceBlock") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome")
	EBlockType SubsurfaceBlock = EBlockType::Dirt;

	/** Debljina povrsinskog sloja ukljucujuci najgornji blok; ispod je Stone (JSON "subsurfaceDepth") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome")
	int32 SubsurfaceDepth = 3;

	/**
	 * Blok pruga u povrsinskom sloju (JSON "stripeBlock", npr. Calcite za Stony
	 * Peaks). Pruge su linije na karti gdje noise pruga pada blizu nule, kao
	 * Minecraftovo calcite pravilo. Air = biom nema pruge.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome")
	EBlockType StripeBlock = EBlockType::Air;

	/** REZERVIRANO: mnozitelj amplitude visine terena - generacija ga jos ne cita */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome")
	float HeightAmplitudeScale = 1.0f;

	/** Kako biom dobiva mjesto u svijetu - vidi EBiomePlacement */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Biome")
	EBiomePlacement Placement = EBiomePlacement::Noise;

	bool IsValid() const { return !Name.IsNone(); }
};
