#include "WeaponData.h"

const TMap<EItemType, FWeaponData>& UWeaponDataLibrary::GetWeaponRegistry()
{
	// Static registry - initialized once on first call
	static TMap<EItemType, FWeaponData> Registry;

	if (Registry.Num() == 0)
	{
		// Minecraft-accurate weapon stats
		// Format: Damage, AttackSpeed (attacks/sec), KnockbackMultiplier, bIsWeapon
		//
		// KnockbackMultiplier je 1.0 svugdje jer vanilla NE mijenja knockback po
		// oruzju - mac, sjekira i gola ruka odbacuju jednako (0.4 bloka/tick).
		// Jaci udarac dolazi od sprint-hita (+0.5 bloka/tick). Polje ostaje za
		// buduce svjesno odstupanje od vanille. Vidi Docs/PLAN_Knockback.md.

		// Fist (unarmed) - 1 damage, 4 attacks/sec, 0.25s cooldown
		Registry.Add(EItemType::None, FWeaponData(1.0f, 4.0f, 1.0f, false));

		// Swords - all have 1.6 attacks/sec = 0.625s cooldown
		Registry.Add(EItemType::WoodenSword,  FWeaponData(4.0f, 1.6f, 1.0f, true));
		Registry.Add(EItemType::StoneSword,   FWeaponData(5.0f, 1.6f, 1.0f, true));
		Registry.Add(EItemType::IronSword,    FWeaponData(6.0f, 1.6f, 1.0f, true));
		Registry.Add(EItemType::DiamondSword, FWeaponData(7.0f, 1.6f, 1.0f, true));

		// Axes - 7 damage (drvo/kamen), 0.8 attacks/sec
		Registry.Add(EItemType::WoodenAxe, FWeaponData(7.0f, 0.8f, 1.0f, true));

		// Pickaxes - 2 damage (drvo), 1.2 attacks/sec; alat, ne oruzje
		// (bIsWeapon pali placeholder mac u ruci, pa ostaje false)
		Registry.Add(EItemType::WoodenPickaxe, FWeaponData(2.0f, 1.2f, 1.0f, false));

		// Ostale krampe: 2 + dodatak materijala iz ToolMaterial (kamen 1, bakar 1,
		// zeljezo 2, dijamant 3, zlato 0)
		Registry.Add(EItemType::StonePickaxe, FWeaponData(3.0f, 1.2f, 1.0f, false));
		Registry.Add(EItemType::CopperPickaxe, FWeaponData(3.0f, 1.2f, 1.0f, false));
		Registry.Add(EItemType::IronPickaxe, FWeaponData(4.0f, 1.2f, 1.0f, false));
		Registry.Add(EItemType::GoldenPickaxe, FWeaponData(2.0f, 1.2f, 1.0f, false));
		Registry.Add(EItemType::DiamondPickaxe, FWeaponData(5.0f, 1.2f, 1.0f, false));

		// Mace - steta s Terraria tooltipa (stvarna osnovna je pola, let x2, vrtnja x1.2);
		// useTime 45 tickova = 1.33 upotrebe/s. Napad vodi UMaceComponent, ne melee.
		Registry.Add(EItemType::Mace, FWeaponData(6.0f, 60.0f / 45.0f, 1.0f, true));

		// Visi tieri sjekira/krampa i lopate se dodaju kad dobiju unos u
		// EItemType i Items.json (bez toga bi item pao na sivi fallback).
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
