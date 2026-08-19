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
	OakPlanks,
	BirchPlanks,   // Daske od breze
	Stick,         // Štap
	CraftingTable, // Crafting stol
	Wool,          // Vuna (od ovce)

	// Weapons - Swords
	WoodenSword,   // Drveni mač
	StoneSword,    // Kameni mač
	IronSword,     // Željezni mač
	DiamondSword,  // Dijamantni mač

	Porkchop       // Svinjetina (od svinje)
};
