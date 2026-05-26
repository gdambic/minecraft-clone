#include "DayNightCycle.h"
#include "Engine/DirectionalLight.h"
#include "Components/LightComponent.h"
#include "Math/UnrealMathUtility.h"

ADayNightCycle::ADayNightCycle()
{
	PrimaryActorTick.bCanEverTick = true;

	// Default trajanja (ukupno 20 minuta)
	DawnDuration = 90.0f;    // 1.5 min
	DayDuration = 600.0f;    // 10 min
	DuskDuration = 90.0f;    // 1.5 min
	NightDuration = 420.0f;  // 7 min

	TimeScale = 1.0f;
	CurrentTime = 0.0f;  // Počni od jutra

	SunLight = nullptr;
	MoonLight = nullptr;

	// Default intenziteti
	SunIntensity = 5.0f;
	MoonIntensity = 0.3f;
	MoonColor = FLinearColor(0.7f, 0.8f, 1.0f);  // Blijedo plava

	// Inicijalne vrijednosti u sekundama (preračunaju se u BeginPlay)
	DawnEnd = 90.0f;
	DayEnd = 690.0f;
	DuskEnd = 780.0f;
}

void ADayNightCycle::BeginPlay()
{
	Super::BeginPlay();

	// Izračunaj granice faza u sekundama
	DawnEnd = DawnDuration;
	DayEnd = DawnEnd + DayDuration;
	DuskEnd = DayEnd + DuskDuration;
	// NightEnd = GetFullCycleDuration() (implicitno)

	// Postavi početne vrijednosti
	UpdateSunRotation();
	UpdateMoonRotation();
	UpdateLightIntensities();
}

void ADayNightCycle::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Napreduj vrijeme (u sekundama, s TimeScale)
	CurrentTime += DeltaTime * TimeScale;

	float TotalDuration = GetFullCycleDuration();
	if (CurrentTime >= TotalDuration)
	{
		CurrentTime -= TotalDuration;
	}

	UpdateSunRotation();
	UpdateMoonRotation();
	UpdateLightIntensities();
}

void ADayNightCycle::UpdateSunRotation()
{
	if (!SunLight)
	{
		return;
	}

	float SunPitch = 0.0f;
	EDayPhase Phase = GetCurrentPhase();
	float Progress = GetPhaseProgress();

	switch (Phase)
	{
	case EDayPhase::Dawn:
		// Sunce izlazi: 0° (horizont) → -50° (visoko na nebu)
		SunPitch = FMath::Lerp(0.0f, -50.0f, Progress);
		break;

	case EDayPhase::Day:
		// Sunce prelazi nebo: -50° → -50° (luk preko neba, koristimo sin za prirodniji pokret)
		// Na sredini dana (Progress=0.5) sunce je najviše (-70°), na rubovima -50°
		SunPitch = -50.0f - 20.0f * FMath::Sin(Progress * UE_PI);
		break;

	case EDayPhase::Dusk:
		// Sunce zalazi: -50° → 0° (horizont)
		SunPitch = FMath::Lerp(-50.0f, 0.0f, Progress);
		break;

	case EDayPhase::Night:
		// Sunce ispod horizonta: 0° → 30° → 0° (luk ispod)
		SunPitch = 30.0f * FMath::Sin(Progress * UE_PI);
		break;
	}

	// Primijeni rotaciju (Pitch rotira oko Y osi)
	FRotator NewRotation = SunLight->GetActorRotation();
	NewRotation.Pitch = SunPitch;
	SunLight->SetActorRotation(NewRotation);
}

void ADayNightCycle::UpdateMoonRotation()
{
	if (!MoonLight)
	{
		return;
	}

	float MoonPitch = 0.0f;
	EDayPhase Phase = GetCurrentPhase();
	float Progress = GetPhaseProgress();

	// Mjesec ide suprotno od sunca
	switch (Phase)
	{
	case EDayPhase::Dawn:
		// Mjesec zalazi: -50° → 0° (horizont)
		MoonPitch = FMath::Lerp(-50.0f, 0.0f, Progress);
		break;

	case EDayPhase::Day:
		// Mjesec ispod horizonta
		MoonPitch = 30.0f * FMath::Sin(Progress * UE_PI);
		break;

	case EDayPhase::Dusk:
		// Mjesec izlazi: 0° (horizont) → -50° (visoko)
		MoonPitch = FMath::Lerp(0.0f, -50.0f, Progress);
		break;

	case EDayPhase::Night:
		// Mjesec na nebu: luk preko neba
		MoonPitch = -50.0f - 20.0f * FMath::Sin(Progress * UE_PI);
		break;
	}

	FRotator NewRotation = MoonLight->GetActorRotation();
	NewRotation.Pitch = MoonPitch;
	MoonLight->SetActorRotation(NewRotation);
}

void ADayNightCycle::UpdateLightIntensities()
{
	EDayPhase Phase = GetCurrentPhase();
	float Progress = GetPhaseProgress();

	float SunFactor = 0.0f;
	float MoonFactor = 0.0f;

	switch (Phase)
	{
	case EDayPhase::Dawn:
		// Sunce: 0% → 100%, Mjesec: 100% → 0%
		SunFactor = Progress;
		MoonFactor = 1.0f - Progress;
		break;

	case EDayPhase::Day:
		// Sunce: 100%, Mjesec: 0%
		SunFactor = 1.0f;
		MoonFactor = 0.0f;
		break;

	case EDayPhase::Dusk:
		// Sunce: 100% → 0%, Mjesec: 0% → 100%
		SunFactor = 1.0f - Progress;
		MoonFactor = Progress;
		break;

	case EDayPhase::Night:
		// Sunce: 0%, Mjesec: 100%
		SunFactor = 0.0f;
		MoonFactor = 1.0f;
		break;
	}

	// Oba lighta mogu biti aktivna - UE5 podržava ako je Atmosphere Sun Light Index različit
	// Sunce: Index 0 (default), Mjesec: Index 1 (postaviti u editoru)
	if (SunLight)
	{
		SunLight->GetLightComponent()->SetIntensity(SunIntensity * SunFactor);
	}

	if (MoonLight)
	{
		MoonLight->GetLightComponent()->SetIntensity(MoonIntensity * MoonFactor);
		MoonLight->GetLightComponent()->SetLightColor(MoonColor);
	}
}

EDayPhase ADayNightCycle::GetCurrentPhase() const
{
	// Granice su u sekundama, CurrentTime je u sekundama
	if (CurrentTime < DawnEnd)
	{
		return EDayPhase::Dawn;
	}
	if (CurrentTime < DayEnd)
	{
		return EDayPhase::Day;
	}
	if (CurrentTime < DuskEnd)
	{
		return EDayPhase::Dusk;
	}
	return EDayPhase::Night;
}

float ADayNightCycle::GetPhaseProgress() const
{
	// Vraća napredak unutar trenutne faze (0.0 - 1.0)
	EDayPhase Phase = GetCurrentPhase();

	switch (Phase)
	{
	case EDayPhase::Dawn:
		return (DawnDuration > 0.0f) ? CurrentTime / DawnDuration : 0.0f;

	case EDayPhase::Day:
		return (DayDuration > 0.0f) ? (CurrentTime - DawnEnd) / DayDuration : 0.0f;

	case EDayPhase::Dusk:
		return (DuskDuration > 0.0f) ? (CurrentTime - DayEnd) / DuskDuration : 0.0f;

	case EDayPhase::Night:
		return (NightDuration > 0.0f) ? (CurrentTime - DuskEnd) / NightDuration : 0.0f;

	default:
		return 0.0f;
	}
}

float ADayNightCycle::GetFullCycleDuration() const
{
	return DawnDuration + DayDuration + DuskDuration + NightDuration;
}
