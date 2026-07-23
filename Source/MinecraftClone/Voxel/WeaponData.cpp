#include "WeaponData.h"

const TMap<EItemType, FWeaponData>& UWeaponDataLibrary::GetWeaponRegistry()
{
	// Static registry - initialized once on first call
	static TMap<EItemType, FWeaponData> Registry;

	if (Registry.Num() == 0)
	{
		// Minecraft-accurate weapon stats
		// Format: Damage, AttackSpeed (attacks/sec), KnockbackMultiplier, bIsWeapon

		// Fist (unarmed) - 1 damage, 4 attacks/sec, 0.25s cooldown
		Registry.Add(EItemType::None, FWeaponData(1.0f, 4.0f, 1.0f, false));

		// Swords - all have 1.6 attacks/sec = 0.625s cooldown
		Registry.Add(EItemType::WoodenSword,  FWeaponData(4.0f, 1.6f, 1.2f, true));
		Registry.Add(EItemType::StoneSword,   FWeaponData(5.0f, 1.6f, 1.2f, true));
		Registry.Add(EItemType::IronSword,    FWeaponData(6.0f, 1.6f, 1.2f, true));
		Registry.Add(EItemType::DiamondSword, FWeaponData(7.0f, 1.6f, 1.2f, true));
	}

	return Registry;
}

FWeaponData UWeaponDataLibrary::GetWeaponData(EItemType ItemType)
{
	const TMap<EItemType, FWeaponData>& Registry = GetWeaponRegistry();

	if (const FWeaponData* Data = Registry.Find(ItemType))
	{
		return *Data;
	}

	// Default to fist for any non-weapon item
	return FWeaponData();
}

bool UWeaponDataLibrary::IsWeapon(EItemType ItemType)
{
	return GetWeaponData(ItemType).bIsWeapon;
}
