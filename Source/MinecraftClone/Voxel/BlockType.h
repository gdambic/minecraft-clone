#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

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
	CraftingTable, // Crafting stol
	Calcite,       // Kalcit (trake u Stony Peaksu)
	Gravel,        // Šljunak (za sada ne pada)

	// Podzemlje (Docs/PLAN_Underground.md) - Minecraftovi blokovi. Nove
	// vrijednosti se dodaju NA KRAJ da postojece zadrze svoj broj.
	Cobblestone,
	Deepslate,
	CobbledDeepslate,
	Bedrock,
	Granite,
	Diorite,
	Tuff,
	CoalOre,
	DeepslateCoalOre,
	IronOre,
	DeepslateIronOre,
	CopperOre,
	DeepslateCopperOre,
	GoldOre,
	DeepslateGoldOre,
	RedstoneOre,
	DeepslateRedstoneOre,
	LapisOre,
	DeepslateLapisOre,
	DiamondOre,
	DeepslateDiamondOre,
	EmeraldOre,
	DeepslateEmeraldOre,

	// Baklje (Docs/PLAN_Torches.md): stojeca i zidna su dvije vrste bloka kao u
	// Minecraftu; smjer zidne je u stanju bloka (svojstvo facing)
	Torch,
	WallTorch
};
