#include "BlockSoundLibrary.h"
#include "BlockRegistry.h"
#include "CoreGlobals.h"
#include "HAL/IConsoleManager.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

namespace
{
	// Vanilla racuna glasnocu kao (SoundType.volume + 1)/8 za kopanje i
	// (volume + 1)/2 za lom/postavljanje, uz pitch x0.5 odnosno x0.8. Pitch
	// drzimo na vanilla vrijednostima, glasnocu kopanja smo digli s 0.25 na
	// 0.6 jer je u UE mikseru vanilla vrijednost pretiha.
	TAutoConsoleVariable<float> CVarHitVolume(
		TEXT("voxel.sound.hitvolume"), 0.6f, TEXT("Glasnoca zvuka kopanja bloka (vanilla racuna 0.25)."));
	TAutoConsoleVariable<float> CVarBreakVolume(
		TEXT("voxel.sound.breakvolume"), 1.0f, TEXT("Glasnoca zvuka loma bloka."));
	TAutoConsoleVariable<float> CVarPlaceVolume(
		TEXT("voxel.sound.placevolume"), 0.8f, TEXT("Glasnoca zvuka postavljanja bloka."));

	TAutoConsoleVariable<float> CVarHitPitch(
		TEXT("voxel.sound.hitpitch"), 0.5f, TEXT("Pitch zvuka kopanja bloka."));
	TAutoConsoleVariable<float> CVarBreakPitch(
		TEXT("voxel.sound.breakpitch"), 0.8f, TEXT("Pitch zvuka loma bloka."));
	TAutoConsoleVariable<float> CVarPlacePitch(
		TEXT("voxel.sound.placepitch"), 0.8f, TEXT("Pitch zvuka postavljanja bloka."));

	TAutoConsoleVariable<float> CVarPitchJitter(
		TEXT("voxel.sound.pitchjitter"), 0.05f,
		TEXT("Nasumicno odstupanje pitcha (+-), da ponavljanje ne zvuci kao petlja."));

	TAutoConsoleVariable<float> CVarHitInterval(
		TEXT("voxel.sound.hitinterval"), 0.2f,
		TEXT("Razmak zvukova kopanja u sekundama (vanilla 4 ticka = 0.2 s)."));

	// Sampleovi traju 0.42-0.69 s, a na pitchu 0.5 i do 1.4 s - bez gasenja
	// prethodnog preklopi se 5-7 glasova i cuje se drone umjesto udaraca.
	TAutoConsoleVariable<float> CVarHitFadeOut(
		TEXT("voxel.sound.hitfadeout"), 0.06f,
		TEXT("Gasenje prethodnog zvuka kopanja kad krene novi (s; 0 = bez gasenja)."));

	// Leaf decay unisti desetke blokova odjednom - bez granice jedno sruseno
	// stablo zvuci kao eksplozija.
	TAutoConsoleVariable<int32> CVarMaxPerFrame(
		TEXT("voxel.sound.maxperframe"), 4,
		TEXT("Koliko zvukova blokova najvise krene u jednom frameu (0 = bez granice)."));

	/** Koliko je zvukova vec krenulo u tekucem frameu */
	uint64 GSoundFrame = 0;
	int32 GSoundsThisFrame = 0;

	bool ConsumeFrameBudget()
	{
		const int32 MaxPerFrame = CVarMaxPerFrame.GetValueOnGameThread();
		if (MaxPerFrame <= 0)
		{
			return true;
		}

		if (GFrameCounter != GSoundFrame)
		{
			GSoundFrame = GFrameCounter;
			GSoundsThisFrame = 0;
		}

		if (GSoundsThisFrame >= MaxPerFrame)
		{
			return false;
		}

		++GSoundsThisFrame;
		return true;
	}

	void GetEventMix(EBlockSoundEvent Event, float& OutVolume, float& OutPitch)
	{
		switch (Event)
		{
		case EBlockSoundEvent::Break:
			OutVolume = CVarBreakVolume.GetValueOnGameThread();
			OutPitch = CVarBreakPitch.GetValueOnGameThread();
			break;
		case EBlockSoundEvent::Place:
			OutVolume = CVarPlaceVolume.GetValueOnGameThread();
			OutPitch = CVarPlacePitch.GetValueOnGameThread();
			break;
		default:
			OutVolume = CVarHitVolume.GetValueOnGameThread();
			OutPitch = CVarHitPitch.GetValueOnGameThread();
			break;
		}
	}
}

UAudioComponent* UBlockSoundLibrary::PlayBlockSound(const UObject* WorldContextObject, EBlockType BlockType,
	EBlockSoundEvent Event, const FVector& Location)
{
	if (BlockType == EBlockType::Air)
	{
		return nullptr;
	}

	UBlockRegistry* Registry = UBlockRegistry::Get(WorldContextObject);
	if (!Registry)
	{
		return nullptr;
	}

	USoundBase* Sound = Registry->GetRandomBlockSound(BlockType, Event);
	if (!Sound)
	{
		return nullptr;
	}

	if (!ConsumeFrameBudget())
	{
		UE_LOG(LogTemp, Verbose, TEXT("[SND] preskocen zvuk bloka - granica po frameu"));
		return nullptr;
	}

	float Volume = 1.0f;
	float Pitch = 1.0f;
	GetEventMix(Event, Volume, Pitch);

	const float Jitter = CVarPitchJitter.GetValueOnGameThread();
	Pitch *= FMath::FRandRange(1.0f - Jitter, 1.0f + Jitter);

	// Spawn (a ne PlaySoundAtLocation) jer pozivatelj treba drzac da zvuk moze
	// ugasiti prije sljedeceg - vidi GetHitSoundFadeOut.
	return UGameplayStatics::SpawnSoundAtLocation(WorldContextObject, Sound, Location,
		FRotator::ZeroRotator, Volume, Pitch, 0.0f, Registry->GetBlockSoundAttenuation());
}

float UBlockSoundLibrary::GetHitSoundInterval()
{
	return FMath::Max(CVarHitInterval.GetValueOnGameThread(), 0.01f);
}

float UBlockSoundLibrary::GetHitSoundFadeOut()
{
	return FMath::Max(CVarHitFadeOut.GetValueOnGameThread(), 0.0f);
}

// ==================== Dijagnostika: voxel.sndtest ====================
//
// Ispise koju sound grupu i koje sampleove koristi svaki blok (ili samo
// trazeni) i odsvira hit + break kod igraca. Headless run bez zvuka i dalje
// provjeri da nijedan blok ne pada na fallback i da svi assetovi postoje.
// Primjer: UnrealEditor-Cmd.exe ... -ExecCmds="voxel.sndtest"

static FAutoConsoleCommandWithWorldAndArgs GBlockSoundTestCmd(
	TEXT("voxel.sndtest"),
	TEXT("Provjeri zvukove blokova: [BlockType] ili bez argumenta za sve."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			UBlockRegistry* Registry = UBlockRegistry::Get(World);
			if (!World || !Registry)
			{
				UE_LOG(LogTemp, Error, TEXT("[SND] nema svijeta ili registryja"));
				return;
			}

			const UEnum* BlockEnum = StaticEnum<EBlockType>();
			const UEnum* GroupEnum = StaticEnum<EBlockSoundGroup>();
			const FString Filter = Args.Num() > 0 ? Args[0] : FString();

			FVector PlayLocation = FVector::ZeroVector;
			if (const APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0))
			{
				PlayLocation = Pawn->GetActorLocation();
			}

			int32 Checked = 0;
			int32 Missing = 0;

			// -1: zadnji unos enuma je _MAX koji nije pravi blok
			for (int32 Index = 0; Index < BlockEnum->NumEnums() - 1; ++Index)
			{
				const EBlockType BlockType = (EBlockType)BlockEnum->GetValueByIndex(Index);
				const FString Name = BlockEnum->GetNameStringByIndex(Index);

				if (BlockType == EBlockType::Air || (!Filter.IsEmpty() && !Name.Equals(Filter, ESearchCase::IgnoreCase)))
				{
					continue;
				}

				++Checked;
				const EBlockSoundGroup Group = Registry->GetBlockSoundGroup(BlockType);
				USoundBase* Hit = Registry->GetRandomBlockSound(BlockType, EBlockSoundEvent::Hit);
				USoundBase* Break = Registry->GetRandomBlockSound(BlockType, EBlockSoundEvent::Break);

				UE_LOG(LogTemp, Log, TEXT("[SND] %s grupa=%s hit=%s break=%s"),
					*Name, *GroupEnum->GetNameStringByValue((int64)Group),
					Hit ? *Hit->GetName() : TEXT("NEMA"),
					Break ? *Break->GetName() : TEXT("NEMA"));

				if (!Hit || !Break)
				{
					++Missing;
					continue;
				}

				UBlockSoundLibrary::PlayBlockSound(World, BlockType, EBlockSoundEvent::Hit, PlayLocation);
				UBlockSoundLibrary::PlayBlockSound(World, BlockType, EBlockSoundEvent::Break, PlayLocation);
			}

			UE_LOG(LogTemp, Log, TEXT("[SND] GOTOVO: provjereno=%d, bez zvuka=%d"), Checked, Missing);
		}));
