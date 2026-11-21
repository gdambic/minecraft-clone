#pragma once

#include "CoreMinimal.h"
#include "BlockType.h"
#include "BlockSoundTypes.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BlockSoundLibrary.generated.h"

class UAudioComponent;

/**
 * Jedino mjesto koje zna kako zvuce blokovi: bira nasumicni sample iz grupe
 * bloka i svira ga na poziciji bloka s glasnocom i pitchem po dogadaju.
 *
 * Stimanje u igri (CVar-ovi):
 *   voxel.sound.hitvolume / breakvolume / placevolume
 *   voxel.sound.hitpitch  / breakpitch  / placepitch
 *   voxel.sound.pitchjitter   - nasumicno odstupanje pitcha (±)
 *   voxel.sound.hitinterval   - razmak zvukova kopanja (vanilla 0.2 s)
 *   voxel.sound.maxperframe   - koliko zvukova blokova najvise po frameu
 *
 * Dijagnostika: konzolna komanda `voxel.sndtest [BlockType]` ispise [SND]
 * linije s grupom i assetima po bloku (i odsvira ih ako ima igraca).
 */
UCLASS()
class MINECRAFTCLONE_API UBlockSoundLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Odsviraj zvuk bloka na zadanoj poziciji (sredina bloka). Vraca komponentu
	 * koja svira (nullptr ako blok nema sample ili je potrosen budzet frame-a) -
	 * pozivatelj je moze ugasiti prije sljedeceg zvuka.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block|Sound", meta = (WorldContext = "WorldContextObject"))
	static UAudioComponent* PlayBlockSound(const UObject* WorldContextObject, EBlockType BlockType,
		EBlockSoundEvent Event, const FVector& Location);

	/** Razmak izmedu dva zvuka kopanja u sekundama (vanilla: 4 ticka = 0.2 s) */
	UFUNCTION(BlueprintPure, Category = "Block|Sound")
	static float GetHitSoundInterval();

	/**
	 * Koliko traje gasenje prethodnog zvuka kopanja kad krene novi (sekunde).
	 * Sample na pola pitcha traje ~1 s, a novi krece svakih 0.2 s - bez gasenja
	 * se slozi 5-7 glasova u jedan drone umjesto niza udaraca.
	 */
	UFUNCTION(BlueprintPure, Category = "Block|Sound")
	static float GetHitSoundFadeOut();
};
