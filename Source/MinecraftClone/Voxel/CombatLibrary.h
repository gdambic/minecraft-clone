#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "CombatLibrary.generated.h"

class ACharacter;

/**
 * Dijeljena melee matematika (knockback) za igraca i sve mobove.
 *
 * Knockback je u Minecraftu promjena BRZINE, ne sila, i izrazen je u
 * blokovima/tick. Sve konstante nize su vanilla vrijednosti; pretvorba u
 * Unreal jedinice ide kroz BlocksPerTickToUU. Detalji i izvori:
 * Docs/PLAN_Knockback.md.
 */
UCLASS()
class MINECRAFTCLONE_API UCombatLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Jedan blok u Unreal jedinicama */
	static constexpr float BlockSizeUU = 100.0f;

	/** Trajanje jednog Minecraft ticka */
	static constexpr float SecondsPerTick = 0.05f;

	/** 1 blok/tick izrazen u uu/s (100 uu / 0.05 s = 2000) */
	static constexpr float BlocksPerTickToUU = BlockSizeUU / SecondsPerTick;

	/** Osnovni knockback svakog pogotka (blokova/tick) */
	static constexpr float BaseKnockbackStrength = 0.4f;

	/** Dodatak sprint-hita (blokova/tick) - glavni izvor "velikog" odbacivanja */
	static constexpr float SprintKnockbackBonus = 0.5f;

	/** Gornja granica vertikalne komponente (blokova/tick) */
	static constexpr float MaxVerticalKnockback = 0.4f;

	/** Minimalni udio napunjenog attack cooldowna za sprint udarac */
	static constexpr float SprintAttackChargeThreshold = 0.848f;

	/** Horizontalni zracni otpor po ticku dok zrtva leti */
	static constexpr float AirDragPerTick = 0.91f;

	/** Napadacu koji je izveo sprint-hit se XY brzina skalira ovime */
	static constexpr float SprintAttackSelfSlowdown = 0.6f;

	/** Minecraft gravitacija (0.08 bloka/tick^2) u uu/s^2 */
	static constexpr float MinecraftGravityUU = 0.08f * BlockSizeUU / (SecondsPerTick * SecondsPerTick);

	/**
	 * Primijeni knockback po vanilla formuli:
	 * jacina se smanji otporom, horizontalna brzina se prepolovi i doda joj se
	 * knockback, a vertikala se dira samo ako je zrtva na tlu (pola stare +
	 * jacina, klesano na MaxVerticalKnockback).
	 *
	 * @param HorizontalDir smjer odbacivanja; Z se ignorira i vektor se normalizira
	 * @param StrengthBlocksPerTick jacina u blokovima/tick (0.4 = obican pogodak)
	 * @param KnockbackResistance 0..1 (1 = imun, vanilla atribut knockback_resistance)
	 * @return je li knockback primijenjen (false ako je zrtva imuna ili nema smjera)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	static bool ApplyKnockback(ACharacter* Victim, FVector HorizontalDir,
		float StrengthBlocksPerTick, float KnockbackResistance = 0.0f);

	/** Normaliziran horizontalni smjer od napadaca prema zrtvi (fallback smjer) */
	UFUNCTION(BlueprintPure, Category = "Combat")
	static FVector HorizontalDirection(const AActor* From, const AActor* To);

	/** Zracni otpor za zadani DeltaTime (0.91 po ticku) */
	static float AirDragForDelta(float DeltaTime);
};
