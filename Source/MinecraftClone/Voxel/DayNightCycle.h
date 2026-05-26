#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DayNightCycle.generated.h"

class ADirectionalLight;

UENUM(BlueprintType)
enum class EDayPhase : uint8
{
	Dawn,   // Jutro
	Day,    // Dan
	Dusk,   // Sumrak
	Night   // Noć
};

UCLASS()
class MINECRAFTCLONE_API ADayNightCycle : public AActor
{
	GENERATED_BODY()

public:
	ADayNightCycle();

	virtual void Tick(float DeltaTime) override;

	// === TRAJANJA FAZA (sekunde) ===

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Timing")
	float DawnDuration;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Timing")
	float DayDuration;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Timing")
	float DuskDuration;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Timing")
	float NightDuration;

	// === KONTROLE ===

	/** Množitelj brzine ciklusa (1.0 = normalno, 2.0 = duplo brže) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Control", meta = (ClampMin = "0.1", ClampMax = "100.0"))
	float TimeScale;

	/** Trenutno vrijeme u ciklusu (sekunde, 0 - ukupno trajanje) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Control", meta = (ClampMin = "0.0"))
	float CurrentTime;

	// === REFERENCE ===

	/** Directional light koji predstavlja sunce */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|References")
	ADirectionalLight* SunLight;

	/** Directional light koji predstavlja mjesec */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|References")
	ADirectionalLight* MoonLight;

	// === SVJETLO ===

	/** Maksimalni intenzitet sunca (lux) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Light", meta = (ClampMin = "0.0"))
	float SunIntensity;

	/** Maksimalni intenzitet mjeseca (lux) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Light", meta = (ClampMin = "0.0"))
	float MoonIntensity;

	/** Boja mjesečeve svjetlosti */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayNight|Light")
	FLinearColor MoonColor;

	// === HELPER FUNKCIJE ===

	/** Vraća trenutnu fazu dana */
	UFUNCTION(BlueprintPure, Category = "DayNight")
	EDayPhase GetCurrentPhase() const;

	/** Vraća napredak unutar trenutne faze (0.0 - 1.0) */
	UFUNCTION(BlueprintPure, Category = "DayNight")
	float GetPhaseProgress() const;

	/** Vraća ukupno trajanje ciklusa u sekundama */
	UFUNCTION(BlueprintPure, Category = "DayNight")
	float GetFullCycleDuration() const;

protected:
	virtual void BeginPlay() override;

private:
	void UpdateSunRotation();
	void UpdateMoonRotation();
	void UpdateLightIntensities();

	// Keširane granice faza u sekundama (izračunate u BeginPlay)
	float DawnEnd;    // Kraj jutra = DawnDuration
	float DayEnd;     // Kraj dana = DawnDuration + DayDuration
	float DuskEnd;    // Kraj sumraka = DawnDuration + DayDuration + DuskDuration
};
