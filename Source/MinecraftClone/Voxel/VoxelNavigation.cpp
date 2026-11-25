#include "VoxelNavigation.h"
#include "VoxelWorld.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"

UNavArea_Water::UNavArea_Water()
{
	DefaultCost = 20.f;
	DrawColor = FColor(40, 90, 220);
}

UNavFilter_NoWater::UNavFilter_NoWater()
{
	FNavigationFilterArea Water;
	Water.AreaClass = UNavArea_Water::StaticClass();
	Water.bIsExcluded = true;
	Areas.Add(Water);
}

namespace
{
	/** A/B provjera popravka: 0 = mobovi se ponasaju kao prije faze 5 (voda dopustena) */
	TAutoConsoleVariable<int32> CVarMobsAvoidWater(
		TEXT("voxel.mobs.avoidwater"), 1,
		TEXT("1 = mobovi ne ulaze u more (navmesh filter i cilj lutanja), 0 = iskljuceno za usporedbu"));

	/** Jedan AVoxelWorld po svijetu; trazi se samo kad cache nije valjan */
	AVoxelWorld* FindVoxelWorld(const UWorld* World)
	{
		static TWeakObjectPtr<AVoxelWorld> Cached;
		if (!Cached.IsValid() || Cached->GetWorld() != World)
		{
			Cached = nullptr;
			for (TActorIterator<AVoxelWorld> It(World); It; ++It)
			{
				Cached = *It;
				break;
			}
		}
		return Cached.Get();
	}
}

bool VoxelNav::IsOverSea(const UObject* WorldContext, const FVector& Location)
{
	if (CVarMobsAvoidWater.GetValueOnGameThread() == 0)
	{
		return false;
	}
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const AVoxelWorld* VoxelWorld = World ? FindVoxelWorld(World) : nullptr;
	float SurfaceZ = 0.f;
	return VoxelWorld && VoxelWorld->GetSeaSurfaceAt(Location, SurfaceZ);
}

TSubclassOf<UNavigationQueryFilter> VoxelNav::FilterFor(const APawn* Pawn)
{
	if (CVarMobsAvoidWater.GetValueOnGameThread() == 0 || (Pawn && IsOverSea(Pawn, Pawn->GetActorLocation())))
	{
		return nullptr;
	}
	return UNavFilter_NoWater::StaticClass();
}
