#pragma once

#include "CoreMinimal.h"

UENUM(BlueprintType)
enum class EBlockType : uint8
{
	Air,         // Prazan prostor - nema renderiranja
	Dirt,        // Smeđi blok zemlje
	Andesite,
	Basalt,
	Limestone,
	Stone,       // Sivi kameni blok
	Grass,       // Zeleni travnati blok
	OakLog,      // Deblo hrasta
	BirchLog,    // Deblo breze
	OakLeaves,   // Lišće hrasta
	BirchLeaves,   // Lišće breze
	OakPlanks,
	BirchPlanks,   // Daske od breze
	CraftingTable  // Crafting stol
};
