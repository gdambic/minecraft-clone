#pragma once

#include "CoreMinimal.h"

UENUM(BlueprintType)
enum class EItemType : uint8
{
	None,
	Dirt,
	Stone,
	Grass,
	OakLog,       // Deblo hrasta
	BirchLog,     // Deblo breze
	OakSapling,   // Sadnica hrasta
	BirchSapling,  // Sadnica breze
	OakPlanks
};
