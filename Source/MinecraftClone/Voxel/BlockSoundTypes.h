#pragma once

#include "CoreMinimal.h"
#include "BlockSoundTypes.generated.h"

/**
 * Sound grupa bloka (Minecraft "SoundType"): skup sampleova koji se svira pri
 * kopanju, lomu i postavljanju. Blok je navodi u Blocks.json ("soundGroup"),
 * a sampleove po grupi daje Content/Data/BlockSounds.json.
 *
 * Zvuk ovisi iskljucivo o materijalu bloka - alat mijenja samo brzinu kopanja,
 * pa se zvuk kopanja ponovi manje puta prije loma (isto kao u Minecraftu).
 */
UENUM(BlueprintType)
enum class EBlockSoundGroup : uint8
{
	None,
	Stone,
	Wood,
	Gravel,
	Grass,
	Basalt
};

/** Dogadaj koji se cuje - svaki ima vlastitu listu sampleova u grupi. */
UENUM(BlueprintType)
enum class EBlockSoundEvent : uint8
{
	/** Ponavlja se dok traje kopanje (vanilla: svakih 4 ticka = 0.2 s) */
	Hit,
	/** Jednokratno kad blok pukne */
	Break,
	/** Jednokratno kad se blok postavi */
	Place
};

/**
 * Sampleovi jedne grupe. Ucitava se iz BlockSounds.json kao putanje; same
 * USoundBase objekte ucitava i kesira UBlockRegistry na prvo trazenje.
 */
USTRUCT()
struct MINECRAFTCLONE_API FBlockSoundSet
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FSoftObjectPath> HitSounds;

	UPROPERTY()
	TArray<FSoftObjectPath> BreakSounds;

	UPROPERTY()
	TArray<FSoftObjectPath> PlaceSounds;

	const TArray<FSoftObjectPath>& GetSounds(EBlockSoundEvent Event) const
	{
		switch (Event)
		{
		case EBlockSoundEvent::Break: return BreakSounds;
		case EBlockSoundEvent::Place: return PlaceSounds;
		default:                      return HitSounds;
		}
	}
};
