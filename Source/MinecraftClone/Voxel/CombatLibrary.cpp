#include "CombatLibrary.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "EngineUtils.h"
#include "CreatureBase.h"
#include "MinecraftClone.h"

bool UCombatLibrary::ApplyKnockback(ACharacter* Victim, FVector HorizontalDir,
	float StrengthBlocksPerTick, float KnockbackResistance)
{
	if (!Victim)
	{
		return false;
	}

	// Vanilla: jacina *= (1 - knockback_resistance); 1.0 znaci potpuni imunitet
	StrengthBlocksPerTick *= 1.0f - FMath::Clamp(KnockbackResistance, 0.0f, 1.0f);
	if (StrengthBlocksPerTick <= 0.0f)
	{
		return false;
	}

	// Smjer je strogo horizontalan - inace razlika u visini kapsula pojede dio jacine
	HorizontalDir.Z = 0.0f;
	if (!HorizontalDir.Normalize())
	{
		return false;
	}

	UCharacterMovementComponent* Move = Victim->GetCharacterMovement();
	if (!Move)
	{
		return false;
	}

	const FVector OldVelocity = Move->Velocity;
	const float StrengthUU = StrengthBlocksPerTick * BlocksPerTickToUU;

	// XY: pola postojece brzine + knockback (zato udarac u moba koji bjezi prema
	// tebi ne "zbroji" njegov zalet, nego ga prepolovi kao u vanilli)
	FVector NewVelocity = OldVelocity * 0.5f + HorizontalDir * StrengthUU;

	if (Move->IsMovingOnGround())
	{
		NewVelocity.Z = FMath::Min(MaxVerticalKnockback * BlocksPerTickToUU,
			OldVelocity.Z * 0.5f + StrengthUU);
	}
	else
	{
		// Zrtva koja je vec u zraku ne dobiva dodatni hop
		NewVelocity.Z = OldVelocity.Z;
	}

	// LaunchCharacter prebacuje u MOVE_Falling, pa brzinu ne pojede trenje tla
	Victim->LaunchCharacter(NewVelocity, true, true);

	UE_LOG(LogMinecraftClone, Log,
		TEXT("[KB] %s jacina=%.2f b/t smjer=%s brzina %s -> %s"),
		*Victim->GetName(), StrengthBlocksPerTick, *HorizontalDir.ToCompactString(),
		*OldVelocity.ToCompactString(), *NewVelocity.ToCompactString());

	return true;
}

FVector UCombatLibrary::HorizontalDirection(const AActor* From, const AActor* To)
{
	if (!From || !To)
	{
		return FVector::ZeroVector;
	}

	FVector Dir = To->GetActorLocation() - From->GetActorLocation();
	Dir.Z = 0.0f;
	return Dir.GetSafeNormal();
}

float UCombatLibrary::AirDragForDelta(float DeltaTime)
{
	return FMath::Pow(AirDragPerTick, DeltaTime / SecondsPerTick);
}

// ==================== Dijagnostika: voxel.kbtest ====================
//
// Headless run nema inputa, pa se knockback ne moze izmjeriti kroz udarac.
// Komanda nadje stvorenje najblize igracu, pocaka da stoji na tlu i odbaci ga
// zadanom jacinom; domet ispise ACreatureBase::Tick kao "[KB] ... doskok".
// Primjer: UnrealEditor-Cmd.exe ... -ExecCmds="voxel.kbtest 0.4 0.9"

namespace
{
	/** Stvorenje najblize igracu (zombi koji ga lovi) */
	ACreatureBase* FindTestTarget(UWorld* World, AActor* Player)
	{
		ACreatureBase* Best = nullptr;
		float BestDistSq = TNumericLimits<float>::Max();

		for (TActorIterator<ACreatureBase> It(World); It; ++It)
		{
			const float DistSq = FVector::DistSquared(It->GetActorLocation(), Player->GetActorLocation());
			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				Best = *It;
			}
		}
		return Best;
	}

	/** Odbaci metu kad stane na tlo (poziva se iz timera svakih 0.1 s) */
	void KnockbackTestStep(TWeakObjectPtr<ACreatureBase> Target, TWeakObjectPtr<AActor> Player,
		float Strength, int32 Attempt)
	{
		if (!Target.IsValid() || !Player.IsValid())
		{
			return;
		}

		UWorld* World = Target->GetWorld();
		UCharacterMovementComponent* Move = Target->GetCharacterMovement();

		if (!Move || !Move->IsMovingOnGround())
		{
			if (Attempt >= 50)   // 5 s cekanja na tlo
			{
				UE_LOG(LogMinecraftClone, Warning,
					TEXT("[KB] test: %s nije na tlu (mode=%d, pos=%s), odustajem"),
					*Target->GetName(), Move ? (int32)Move->MovementMode.GetValue() : -1,
					*Target->GetActorLocation().ToCompactString());
				return;
			}

			FTimerHandle NextHandle;
			World->GetTimerManager().SetTimer(NextHandle, FTimerDelegate::CreateLambda(
				[Target, Player, Strength, Attempt]()
				{
					KnockbackTestStep(Target, Player, Strength, Attempt + 1);
				}), 0.1f, false);
			return;
		}

		UE_LOG(LogMinecraftClone, Log, TEXT("[KB] test: %s, jacina %.2f b/t, otpor %.2f"),
			*Target->GetName(), Strength, Target->KnockbackResistance);

		Target->ReceiveKnockback(Target->GetActorLocation() - Player->GetActorLocation(), Strength);
	}

	void RunKnockbackTest(UWorld* World, float Strength, float StartDelay)
	{
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(
			[World, Strength]()
			{
				AActor* Player = UGameplayStatics::GetPlayerPawn(World, 0);
				if (!Player)
				{
					UE_LOG(LogMinecraftClone, Warning, TEXT("[KB] test: nema player pawna"));
					return;
				}

				ACreatureBase* Target = FindTestTarget(World, Player);
				if (!Target)
				{
					UE_LOG(LogMinecraftClone, Warning, TEXT("[KB] test: u svijetu nema stvorenja"));
					return;
				}

				KnockbackTestStep(Target, Player, Strength, 0);
			}), StartDelay, false);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GKnockbackTestCmd(
	TEXT("voxel.kbtest"),
	TEXT("Izmjeri knockback: odbaci stvorenje najblize igracu zadanim jacinama (blokova/tick). Default: 0.4 0.9"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}

			TArray<float> Strengths;
			for (const FString& Arg : Args)
			{
				Strengths.Add(FCString::Atof(*Arg));
			}
			if (Strengths.Num() == 0)
			{
				Strengths = { UCombatLibrary::BaseKnockbackStrength,
					UCombatLibrary::BaseKnockbackStrength + UCombatLibrary::SprintKnockbackBonus };
			}

			// Testovi idu 8 s razmaka da se doskok jednog ne mjesa s drugim
			float Delay = 3.0f;
			for (float Strength : Strengths)
			{
				RunKnockbackTest(World, Strength, Delay);
				Delay += 8.0f;
			}

			// Headless run se sam gasi da se log flusha (kill ostavi zadnjih
			// par sekundi u bufferu). U editoru/PIE se ne gasi nista.
			if (!GIsEditor)
			{
				FTimerHandle QuitHandle;
				World->GetTimerManager().SetTimer(QuitHandle, FTimerDelegate::CreateLambda(
					[]() { FPlatformMisc::RequestExit(false); }), Delay + 3.0f, false);
			}
		}));
