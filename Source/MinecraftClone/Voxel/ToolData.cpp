#include "ToolData.h"

const TMap<EItemType, FToolData>& UToolDataLibrary::GetToolRegistry()
{
	// Static registry - initialized once on first call
	static TMap<EItemType, FToolData> Registry;

	if (Registry.Num() == 0)
	{
		// Minecraft mining speed po materijalu alata:
		// drvo 2, kamen 4, zeljezo 6, dijamant 8
		// Format: ToolClass, MiningSpeed, Tier
		Registry.Add(EItemType::WoodenAxe, FToolData(EToolClass::Axe, 2.0f, EToolTier::Wood));

		// Krampe: brzina iz Minecraftove klase ToolMaterial (drvo 2, kamen 4,
		// bakar 5, zeljezo 6, dijamant 8, zlato 12). Razina odreduje sto krampa
		// moze pozeti (tagovi incorrect_for_*_tool): zlatna kao drvena, bakrena
		// kao kamena.
		Registry.Add(EItemType::WoodenPickaxe, FToolData(EToolClass::Pickaxe, 2.0f, EToolTier::Wood));
		Registry.Add(EItemType::StonePickaxe, FToolData(EToolClass::Pickaxe, 4.0f, EToolTier::Stone));
		Registry.Add(EItemType::CopperPickaxe, FToolData(EToolClass::Pickaxe, 5.0f, EToolTier::Stone));
		Registry.Add(EItemType::IronPickaxe, FToolData(EToolClass::Pickaxe, 6.0f, EToolTier::Iron));
		Registry.Add(EItemType::GoldenPickaxe, FToolData(EToolClass::Pickaxe, 12.0f, EToolTier::Wood));
		Registry.Add(EItemType::DiamondPickaxe, FToolData(EToolClass::Pickaxe, 8.0f, EToolTier::Diamond));
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

bool UToolDataLibrary::CanHarvest(EItemType ItemType, EToolClass BlockToolClass, bool bBlockRequiresTool, EToolTier RequiredTier)
{
	if (!bBlockRequiresTool)
	{
		return true;
	}
	return IsCorrectTool(ItemType, BlockToolClass) && GetToolData(ItemType).Tier >= RequiredTier;
}

float UToolDataLibrary::GetMiningSpeed(EItemType ItemType, EToolClass BlockToolClass)
{
	return IsCorrectTool(ItemType, BlockToolClass)
		? GetToolData(ItemType).MiningSpeed
		: 1.0f;
}
