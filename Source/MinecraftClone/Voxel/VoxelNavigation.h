#pragma once

#include "CoreMinimal.h"
#include "NavAreas/NavArea.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "VoxelNavigation.generated.h"

class APawn;

/**
 * Navmesh nad morem (Docs/PLAN_Water.md, faza 5). AVoxelWorld u svakom chunku
 * s morem prekrije stupce mora kutijama koje ovo podrucje upisu u navmesh.
 * Cijena je visoka da mob koji je ipak u vodi izade najkracim putem.
 */
UCLASS()
class MINECRAFTCLONE_API UNavArea_Water : public UNavArea
{
	GENERATED_BODY()

public:
	UNavArea_Water();
};

/** Filter bez vode: put i nasumicna tocka nikad ne idu u more. */
UCLASS()
class MINECRAFTCLONE_API UNavFilter_NoWater : public UNavigationQueryFilter
{
	GENERATED_BODY()

public:
	UNavFilter_NoWater();
};

namespace VoxelNav
{
	/**
	 * Filter za stvorenje: na kopnu UNavFilter_NoWater, a nad morem default
	 * (voda dopustena, ali skupa), inace mob u moru ne bi mogao izaci.
	 */
	MINECRAFTCLONE_API TSubclassOf<UNavigationQueryFilter> FilterFor(const APawn* Pawn);

	/** Je li tocka nad stupcem mora (AVoxelWorld::GetSeaSurfaceAt). */
	MINECRAFTCLONE_API bool IsOverSea(const UObject* WorldContext, const FVector& Location);
}
