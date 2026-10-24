#include "ToolData.h"

const TMap<EItemType, FToolData>& UToolDataLibrary::GetToolRegistry()
{
	// Static registry - initialized once on first call
	static TMap<EItemType, FToolData> Registry;

	if (Registry.Num() == 0)
	{
		// Minecraft mining speed po materijalu alata:
		// drvo 2, kamen 4, zeljezo 6, dijamant 8
		// Format: ToolClass, MiningSpeed
		Registry.Add(EItemType::WoodenAxe, FToolData(EToolClass::Axe, 2.0f));

		Registry.Add(EItemType::WoodenPickaxe, FToolData(EToolClass::Pickaxe, 2.0f));
	}

	return Registry;
}

FToolData UToolDataLibrary::GetToolData(EItemType ItemType)
{
	const FToolData* Found = GetToolRegistry().Find(ItemType);
	return Found ? *Found : FToolData();
}

bool UToolDataLibrary::IsCorrectTool(EItemType ItemType, EToolClass BlockToolClass)
{
	// Blok bez preferirane klase (None) ne moze imati "pravi" alat
	return BlockToolClass != EToolClass::None
		&& GetToolData(ItemType).ToolClass == BlockToolClass;
}

float UToolDataLibrary::GetMiningSpeed(EItemType ItemType, EToolClass BlockToolClass)
{
	return IsCorrectTool(ItemType, BlockToolClass)
		? GetToolData(ItemType).MiningSpeed
		: 1.0f;
}
