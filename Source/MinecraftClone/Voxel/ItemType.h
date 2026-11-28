#pragma once

#include "CoreMinimal.h"
#include "UObject/ObjectMacros.h"

UENUM(BlueprintType)
enum class EItemType : uint8
{
	None,
	Dirt,
	Andesite,
	Basalt,
	Limestone,
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

	WoodenAxe,

	WoodenPickaxe,

	Porkchop,      // Svinjetina (od svinje)

	Calcite,       // Kalcit
	Gravel,        // Šljunak

	Mace,          // Buzdovan: kugla na lancu (Terraria flail, Docs/PLAN_Mace.md)

	// Podzemlje (Docs/PLAN_Underground.md) - blokovi kamena i dropovi ruda
	Cobblestone,
	Deepslate,
	CobbledDeepslate,
	Granite,
	Diorite,
	Tuff,
	Flint,
	Coal,
	RawIron,
	RawCopper,
	RawGold,
	Redstone,
	LapisLazuli,
	Diamond,
	Emerald,

	Torch           // Baklja (postavlja blok Torch ili WallTorch)
};
