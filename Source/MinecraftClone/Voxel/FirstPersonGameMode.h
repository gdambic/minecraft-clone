#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "FirstPersonGameMode.generated.h"

/**
 * Simple GameMode for a first person voxel game
 */
UCLASS(abstract)
class MINECRAFTCLONE_API AFirstPersonGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AFirstPersonGameMode();
};
