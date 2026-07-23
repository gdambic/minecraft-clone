#pragma once

#include "CoreMinimal.h"
#include "ItemType.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "WeaponData.generated.h"

/**
 * Data structure holding weapon combat statistics.
 * Based on Minecraft combat mechanics.
 */
USTRUCT(BlueprintType)
struct FWeaponData
{
	GENERATED_BODY()

	/** Damage dealt per hit */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float Damage = 1.0f;

	/** Attacks per second (determines cooldown) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float AttackSpeed = 4.0f;

	/** Knockback force multiplier */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	float KnockbackMultiplier = 1.0f;

	/** Whether this is a weapon (false = fist) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly)
	bool bIsWeapon = false;

	/** Calculate cooldown from attack speed */
	float GetCooldown() const
	{
		return AttackSpeed > 0.0f ? 1.0f / AttackSpeed : 0.25f;
	}

	/** Default constructor (fist) */
	FWeaponData()
		: Damage(1.0f)
		, AttackSpeed(4.0f)
		, KnockbackMultiplier(1.0f)
		, bIsWeapon(false)
	{}

	/** Parameterized constructor */
	FWeaponData(float InDamage, float InAttackSpeed, float InKnockback, bool InIsWeapon)
		: Damage(InDamage)
		, AttackSpeed(InAttackSpeed)
		, KnockbackMultiplier(InKnockback)
		, bIsWeapon(InIsWeapon)
	{}
};


/**
 * Static library for weapon data lookup.
 * Contains the weapon registry with Minecraft-accurate stats.
 */
UCLASS()
class MINECRAFTCLONE_API UWeaponDataLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Get weapon data for an item type */
	UFUNCTION(BlueprintPure, Category = "Weapons")
	static FWeaponData GetWeaponData(EItemType ItemType);

	/** Check if item type is a weapon */
	UFUNCTION(BlueprintPure, Category = "Weapons")
	static bool IsWeapon(EItemType ItemType);

private:
	/** Get the weapon registry (initialized once) */
	static const TMap<EItemType, FWeaponData>& GetWeaponRegistry();
};
