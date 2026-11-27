#include "BlockMiningComponent.h"
#include "BlockRegistry.h"
#include "BlockSoundLibrary.h"
#include "MinecraftClone.h"
#include "ToolData.h"
#include "VoxelWorld.h"
#include "Components/AudioComponent.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

namespace
{
	/** Minecraft tick: trajanje kopanja je uvijek cijeli broj tickova */
	constexpr float SecondsPerTick = 1.0f / 20.0f;

	/**
	 * Koliko dulje traje kopanje bloka koji zahtijeva alat kad igrac nema
	 * pravi alat. Minecraft bazno vrijeme je hardness × 1.5 s pravim alatom
	 * i hardness × 5 bez njega - odnos tih faktora.
	 */
	constexpr float WrongToolTimePenalty = 5.0f / 1.5f;

	TAutoConsoleVariable<int32> CVarCrackStage(
		TEXT("voxel.crack.stage"), -1,
		TEXT("Prisilno prikazi fazu pukotine 0..9 na bloku u koji igrac gleda (-1 = iskljuceno)."));
}

UBlockMiningComponent::UBlockMiningComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

int32 UBlockMiningComponent::ProgressToStage(float Progress)
{
	if (Progress <= 0.0f)
	{
		return -1;
	}
	return FMath::Clamp(FMath::FloorToInt(Progress * 10.0f), 0, 9);
}

uint32 UBlockMiningComponent::GetBreakerId() const
{
	// Kad dode multiplayer, ovdje ide PlayerId
	return GetOwner() ? GetOwner()->GetUniqueID() : 0;
}

void UBlockMiningComponent::SetTarget(FIntVector GridPos)
{
	if (bHasTarget && GridPos == TargetPos)
	{
		return;
	}

	ResetProgress();
	bHasTarget = true;
	TargetPos = GridPos;
	UpdateCrack();
}

void UBlockMiningComponent::ClearTarget()
{
	if (!bHasTarget)
	{
		return;
	}

	ResetProgress();
	bHasTarget = false;
	UpdateCrack();
}

void UBlockMiningComponent::StartMining()
{
	bMining = true;
}

void UBlockMiningComponent::StopMining()
{
	bMining = false;
	ResetProgress();
	UpdateCrack();
}

void UBlockMiningComponent::ResetProgress()
{
	Progress = 0.0f;
	ElapsedTime = 0.0f;

	// Sljedeci pocetak kopanja (ili promjena alata) opet zvuci odmah
	HitSoundTimer = 0.0f;
}

float UBlockMiningComponent::GetBreakTime(FIntVector GridPos, EItemType HeldItem, bool* bOutCanHarvest) const
{
	const EBlockType Type = VoxelWorld ? VoxelWorld->GetBlockTypeAt(GridPos) : EBlockType::Air;
	const UBlockRegistry* Registry = UBlockRegistry::Get(this);
	const FBlockDefinition* Def = Registry && Type != EBlockType::Air
		? Registry->GetBlockDefinition(Type) : nullptr;
	if (!Def || Def->TimeToDestroy <= 0.0f)
	{
		return -1.0f;
	}

	// Minecraft model: pravi alat kopa brze za svoj mining speed, a blok koji
	// alat zahtijeva bez njega traje dulje i ne ostavlja drop (canHarvest).
	const bool bCorrectTool = UToolDataLibrary::IsCorrectTool(HeldItem, Def->ToolClass);
	const bool bCanHarvest = UToolDataLibrary::CanHarvest(HeldItem, Def->ToolClass, Def->bRequiresTool, Def->RequiredToolTier);
	const float MiningSpeed = bCorrectTool ? UToolDataLibrary::GetToolData(HeldItem).MiningSpeed : 1.0f;
	if (bOutCanHarvest)
	{
		*bOutCanHarvest = bCanHarvest;
	}

	const float Seconds = Def->TimeToDestroy * (bCanHarvest ? 1.0f : WrongToolTimePenalty)
		/ FMath::Max(MiningSpeed, KINDA_SMALL_NUMBER);

	// Zaokruzi na gore na cijeli tick (vanilla). Epsilon da 1.5 s * 20 ne
	// postane 31 ticka zbog float greske.
	const float Ticks = FMath::CeilToFloat(Seconds / SecondsPerTick - 1e-3f);
	return FMath::Max(Ticks, 1.0f) * SecondsPerTick;
}

bool UBlockMiningComponent::TickMining(float DeltaTime, EItemType HeldItem)
{
	if (!bMining || !bHasTarget || !VoxelWorld)
	{
		UpdateCrack();
		return false;
	}

	if (HeldItem != MiningItem)
	{
		MiningItem = HeldItem;
		ResetProgress();
	}

	bool bCanHarvest = true;
	const float BreakTime = GetBreakTime(TargetPos, HeldItem, &bCanHarvest);
	if (BreakTime < 0.0f)
	{
		UpdateCrack();
		return false;
	}

	ElapsedTime += DeltaTime;

	// Blok od jednog ticka puca odmah, na prvi frame (vanilla instant mining)
	Progress = BreakTime <= SecondsPerTick ? 1.0f : Progress + DeltaTime / BreakTime;

	if (Progress >= 1.0f)
	{
		if (bLogStages)
		{
			UE_LOG(LogMinecraftClone, Log, TEXT("[CRACK] lom t=%.3f (ocekivano %.3f)"), ElapsedTime, BreakTime);
		}

		const FIntVector BrokenPos = TargetPos;
		ResetProgress();
		bHasTarget = false;
		UpdateCrack();

		// Drop samo ako je blok kopan alatom koji ga moze pozeti
		VoxelWorld->DestroyBlockAt(BrokenPos, bCanHarvest);
		return true;
	}

	UpdateCrack();
	PlayHitSound(DeltaTime);
	return false;
}

void UBlockMiningComponent::UpdateCrack()
{
	int32 Stage = bHasTarget ? ProgressToStage(Progress) : -1;

	const int32 ForcedStage = CVarCrackStage.GetValueOnGameThread();
	if (bHasTarget && ForcedStage >= 0)
	{
		Stage = FMath::Min(ForcedStage, 9);
	}

	if (Stage == SentStage && (Stage < 0 || TargetPos == SentPos))
	{
		return;
	}

	if (bLogStages && Stage >= 0)
	{
		UE_LOG(LogMinecraftClone, Log, TEXT("[CRACK] blok=%s faza=%d progres=%.3f t=%.3f"),
			*UEnum::GetValueAsString(VoxelWorld ? VoxelWorld->GetBlockTypeAt(TargetPos) : EBlockType::Air),
			Stage, Progress, ElapsedTime);
	}

	SentStage = Stage;
	SentPos = TargetPos;
	if (VoxelWorld)
	{
		VoxelWorld->SetBlockCrack(GetBreakerId(), TargetPos, Stage);
	}
}

void UBlockMiningComponent::PlayHitSound(float DeltaTime)
{
	// Zvuk kopanja: prvi tick odmah, dalje svakih voxel.sound.hitinterval
	// (vanilla 4 ticka). Blok koji je upravo puknuo ne zvuci - njega vec
	// pokriva zvuk loma iz AVoxelWorld::NotifyBlockDestroyed.
	HitSoundTimer -= DeltaTime;
	if (HitSoundTimer > 0.0f)
	{
		return;
	}
	HitSoundTimer = UBlockSoundLibrary::GetHitSoundInterval();

	// Prethodni udarac se gasi - sample na pola pitcha traje do 1.4 s, pa
	// bi se bez toga slozilo 5-7 glasova u jedan drone.
	if (UAudioComponent* Previous = HitSoundComponent.Get())
	{
		Previous->FadeOut(UBlockSoundLibrary::GetHitSoundFadeOut(), 0.0f);
	}

	HitSoundComponent = UBlockSoundLibrary::PlayBlockSound(this, VoxelWorld->GetBlockTypeAt(TargetPos),
		EBlockSoundEvent::Hit,
		VoxelWorld->GridToWorld(TargetPos.X, TargetPos.Y, TargetPos.Z));
}

// ==================== Dijagnostika: voxel.cracktest ====================
//
// Headless run nema inputa, pa se kopanje simulira: za svaki zadani item
// iskopa se blok ispod igraca koracima od 1/60 s i za svaku fazu ispise
// "[CRACK] blok=... faza=N", a na kraju "[CRACK] lom t=... (ocekivano ...)".
// Primjer: UnrealEditor-Cmd.exe ... -ExecCmds="voxel.cracktest None WoodenPickaxe"

namespace
{
	void RunCrackTest(UWorld* World, EItemType Item)
	{
		APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0);
		UBlockMiningComponent* Mining = Player ? Player->FindComponentByClass<UBlockMiningComponent>() : nullptr;
		AVoxelWorld* VoxelWorld = nullptr;
		for (TActorIterator<AVoxelWorld> It(World); It; ++It)
		{
			VoxelWorld = *It;
			break;
		}
		if (!Mining || !VoxelWorld)
		{
			UE_LOG(LogMinecraftClone, Warning, TEXT("[CRACK] test: nema igraca, mining komponente ili svijeta"));
			return;
		}

		// Prvi cvrsti blok ispod igraca (blok je centriran na GridToWorld).
		// Ne GetTerrainHeightAt: prethodni test je vrh stupca vec iskopao.
		const FVector Rel = (Player->GetActorLocation() - VoxelWorld->GetActorLocation()) / VoxelWorld->BlockSize;
		FIntVector Pos(FMath::RoundToInt(Rel.X), FMath::RoundToInt(Rel.Y), FMath::RoundToInt(Rel.Z));
		EBlockType Type = EBlockType::Air;
		for (int32 Step = 0; Step < 8 && Type == EBlockType::Air; ++Step)
		{
			--Pos.Z;
			Type = VoxelWorld->GetBlockTypeAt(Pos);
		}
		if (Type == EBlockType::Air)
		{
			UE_LOG(LogMinecraftClone, Warning, TEXT("[CRACK] test: ispod igraca nema bloka (%s)"), *Pos.ToString());
			return;
		}

		UE_LOG(LogMinecraftClone, Log, TEXT("[CRACK] test: %s na %s, item %s"),
			*UEnum::GetValueAsString(Type), *Pos.ToString(), *UEnum::GetValueAsString(Item));

		// Kopanje ide sinkrono unutar jednog poziva, pa ga UpdateBlockLookAt
		// characterovog Ticka ne moze prekinuti promjenom cilja
		Mining->bLogStages = true;
		Mining->StopMining();
		Mining->SetTarget(Pos);
		Mining->StartMining();

		constexpr float Step = 1.0f / 60.0f;
		bool bBroken = false;
		for (int32 Frame = 0; Frame < 60 * 60 && !bBroken; ++Frame)
		{
			bBroken = Mining->TickMining(Step, Item);
		}

		Mining->StopMining();
		Mining->bLogStages = false;

		if (!bBroken)
		{
			UE_LOG(LogMinecraftClone, Warning, TEXT("[CRACK] test: blok nije puknuo u 60 s"));
		}
	}
}

static FAutoConsoleCommandWithWorldAndArgs GCrackTestCmd(
	TEXT("voxel.cracktest"),
	TEXT("Simuliraj kopanje bloka ispod igraca zadanim itemima i ispisi faze pukotina. Default: None WoodenPickaxe"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}

			TArray<EItemType> Items;
			const UEnum* Enum = StaticEnum<EItemType>();
			for (const FString& Arg : Args)
			{
				const int64 Value = Enum->GetValueByNameString(Arg);
				if (Value == INDEX_NONE)
				{
					UE_LOG(LogMinecraftClone, Warning, TEXT("[CRACK] test: nepoznat item '%s'"), *Arg);
					continue;
				}
				Items.Add(static_cast<EItemType>(Value));
			}
			if (Items.Num() == 0)
			{
				Items = { EItemType::None, EItemType::WoodenPickaxe };
			}

			// Svijet treba vremena za generaciju, a igrac nakon loma pada na
			// sljedeci blok - testovi idu 3 s razmaka
			float Delay = 5.0f;
			for (EItemType Item : Items)
			{
				FTimerHandle Handle;
				TWeakObjectPtr<UWorld> WeakWorld(World);
				World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(
					[WeakWorld, Item]()
					{
						if (WeakWorld.IsValid())
						{
							RunCrackTest(WeakWorld.Get(), Item);
						}
					}), Delay, false);
				Delay += 3.0f;
			}

			// Headless run se sam gasi da se log flusha. U editoru/PIE ne.
			if (!GIsEditor)
			{
				FTimerHandle QuitHandle;
				World->GetTimerManager().SetTimer(QuitHandle, FTimerDelegate::CreateLambda(
					[]() { FPlatformMisc::RequestExit(false); }), Delay + 2.0f, false);
			}
		}));
