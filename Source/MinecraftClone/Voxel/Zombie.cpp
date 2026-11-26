#include "Zombie.h"
#include "HeadLookComponent.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BrainComponent.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MinecraftClone.h"

namespace
{
	// Sve iz Minecraft 1.21.11 jara (Docs/PLAN_Zombie.md)

	// LivingEntity.updateWalkAnimation: cilj = min(pomak u blokovima * 4, 1),
	// WalkAnimationState.update: speed += (cilj - speed) * 0.4, position += speed
	constexpr float WalkTargetScale = 4.0f;
	constexpr float WalkSpeedLerp = 0.4f;

	// HumanoidModel.setupAnim: cos(walkPos * 0.6662)
	constexpr float WalkFrequency = 0.6662f;

	// LivingEntity.hurtDuration = 10 tickova crvenog tinta
	constexpr float HurtTintSeconds = 10 * AZombie::TickSeconds;

	// SwingAnimation default: WHACK, 6 tickova
	constexpr float AttackSeconds = 6 * AZombie::TickSeconds;

	// LivingEntity.tickDeath: entitet nestaje na deathTime = 20
	constexpr float DeathSeconds = 20 * AZombie::TickSeconds;

	// Hitbox 0.6 x 1.95 bloka
	constexpr float CapsuleRadius = 30.0f;
	constexpr float CapsuleHalfHeight = 97.5f;

	// Sredina glave (glava je 24..32 px iznad tla, 1 px = 6.25 UU) iznad sredine kapsule
	constexpr float HeadCenterAboveCapsuleCenter = 28 * 6.25f - CapsuleHalfHeight;

	const FName TargetActorKeyName(TEXT("TargetActor"));
	const FName HurtAmountParam(TEXT("HurtAmount"));
}

AZombie::AZombie()
{
	// Set Minecraft zombie default values
	MaxHealth = 20.0f;
	CurrentHealth = 20.0f;
	AttackDamage = 3.0f;
	AttackRange = 150.0f;        // 1.5 blocks
	AttackCooldown = 2.0f;       // 2 seconds between attacks
	DetectionRange = 3500.0f;    // 35 blocks
	LoseTargetRange = 4000.0f;   // 40 blocks
	WalkSpeed = 230.0f;          // Minecraft zombie speed
	WanderRadius = 500.0f;       // 5 blocks

	GetCapsuleComponent()->InitCapsuleSize(CapsuleRadius, CapsuleHalfHeight);
	GetCapsuleComponent()->SetCanEverAffectNavigation(false);
	GetCapsuleComponent()->bDynamicObstacle = false;

	// Skeletal mesh is set in Blueprint (BP_Zombie)
	// GetMesh() returns the inherited USkeletalMeshComponent from ACharacter
	if (GetMesh())
	{
		// SK_Zombie ima ishodiste na tlu i gleda prema +Y (kao mannequin)
		GetMesh()->SetRelativeLocationAndRotation(
			FVector(0.0f, 0.0f, -CapsuleHalfHeight), FRotator(0.0f, -90.0f, 0.0f));
		GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
		GetMesh()->SetCanEverAffectNavigation(false);
	}

	HeadLook = CreateDefaultSubobject<UHeadLookComponent>(TEXT("HeadLook"));
	HeadLook->HeadHeightOffset = HeadCenterAboveCapsuleCenter;
	// Kost glave je vec na vratu, kao pivot u Minecraftu
	HeadLook->HeadPivotOffset = FVector::ZeroVector;
}

void AZombie::BeginPlay()
{
	Super::BeginPlay();

	// Apply walk speed
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = WalkSpeed;
	}

	LastTickLocation = GetActorLocation();

	if (GetMesh())
	{
		BodyMaterial = GetMesh()->CreateDynamicMaterialInstance(0);
	}
}

void AZombie::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Minecraft hod se racuna po ticku (20 Hz); frame izmedu tickova
	// interpoliraju GetWalkAmount/GetWalkCycleTime kao Minecraftov renderer
	TickAccumulator += DeltaTime;
	while (TickAccumulator >= TickSeconds)
	{
		TickAccumulator -= TickSeconds;
		StepWalkAnimation();
	}

	const float Now = GetWorld()->GetTimeSeconds();

	if (!bDead)
	{
		AActor* Target = FindTarget();
		bAggressive = Target != nullptr;
		HeadLook->SetForcedTarget(Target);
	}

	if (bIsAttacking && Now >= AttackEndTime)
	{
		OnAttackAnimationEnd();
	}

	// Minecraft: crveno dok traje ozljeda i cijelo vrijeme umiranja
	SetHurtTint((bDead || Now < HurtTintEndTime) ? 1.0f : 0.0f);
}

void AZombie::StepWalkAnimation()
{
	const FVector Location = GetActorLocation();
	const float MovedBlocks = FVector::Dist2D(Location, LastTickLocation) / 100.0f;
	LastTickLocation = Location;

	const float Target = FMath::Min(MovedBlocks * WalkTargetScale, 1.0f);
	WalkSpeedOld = WalkSpeed01;
	WalkSpeed01 += (Target - WalkSpeed01) * WalkSpeedLerp;
	WalkPos += WalkSpeed01;
}

float AZombie::GetWalkAmount() const
{
	// WalkAnimationState.speed(partialTick)
	const float Partial = TickAccumulator / TickSeconds;
	return FMath::Lerp(WalkSpeedOld, WalkSpeed01, Partial);
}

float AZombie::GetWalkCycleTime() const
{
	// WalkAnimationState.position(partialTick) = position - speed * (1 - partialTick)
	const float Partial = TickAccumulator / TickSeconds;
	const float Pos = WalkPos - WalkSpeed01 * (1.0f - Partial);
	const float Cycles = Pos * WalkFrequency / (2.0f * PI);
	return (Cycles - FMath::FloorToFloat(Cycles)) * WalkLoopSeconds;
}

AActor* AZombie::FindTarget() const
{
	const AAIController* AI = Cast<AAIController>(GetController());
	const UBlackboardComponent* Blackboard = AI ? AI->GetBlackboardComponent() : nullptr;
	return Blackboard ? Cast<AActor>(Blackboard->GetValueAsObject(TargetActorKeyName)) : nullptr;
}

void AZombie::SetHurtTint(float Amount)
{
	if (BodyMaterial && Amount != CurrentHurtTint)
	{
		BodyMaterial->SetScalarParameterValue(HurtAmountParam, Amount);
	}
	CurrentHurtTint = Amount;
}

void AZombie::PerformAttack()
{
	// Call base to update LastAttackTime
	Super::PerformAttack();

	// Trigger attack animation
	bIsAttacking = true;
	AttackEndTime = GetWorld()->GetTimeSeconds() + AttackSeconds;

	// Minecraft zamah krece i zavrsava bez pretapanja
	UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (Anim && AttackAnimation)
	{
		Anim->PlaySlotAnimationAsDynamicMontage(AttackAnimation, UpperBodySlot, 0.0f, 0.0f);
	}

	// Display HIT message on screen
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(
			-1,                    // Key (-1 = don't overwrite)
			1.0f,                  // Duration
			FColor::Red,           // Color
			TEXT("HIT!")           // Message
		);
	}

	// TODO: In future, apply actual damage to player
	// if (AFirstPersonCharacter* Player = ...)
	// {
	//     Player->TakeDamage(AttackDamage, ...);
	// }
}

void AZombie::OnAttackAnimationEnd()
{
	bIsAttacking = false;
}

bool AZombie::TakeDamage(float DamageAmount)
{
	const bool bCouldBeHurt = CanBeHurt();
	const bool bKilled = Super::TakeDamage(DamageAmount);
	if (bCouldBeHurt)
	{
		HurtTintEndTime = GetWorld()->GetTimeSeconds() + HurtTintSeconds;
	}
	return bKilled;
}

void AZombie::OnDeath()
{
	if (bDead)
	{
		return;
	}
	bDead = true;
	bAggressive = false;
	bIsAttacking = false;
	HeadLook->SetForcedTarget(nullptr);
	HeadLook->SetSuppressed(true);

	// Bez AI-a do kraja: mrtvi zombi samo pada i lezi. Knockback koji jos
	// traje zavrsava sam (EndKnockback bez kontrolera ne radi nista).
	if (AController* OldController = GetController())
	{
		if (const AAIController* AI = Cast<AAIController>(OldController))
		{
			if (UBrainComponent* Brain = AI->GetBrainComponent())
			{
				Brain->StopLogic(TEXT("Death"));
			}
		}
		OldController->UnPossess();
		OldController->Destroy();
	}

	// Igrac prolazi kroz lesinu i ne pogada je trace-om
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

	UAnimInstance* Anim = GetMesh() ? GetMesh()->GetAnimInstance() : nullptr;
	if (Anim && DeathAnimation)
	{
		if (UAnimMontage* Montage = Anim->PlaySlotAnimationAsDynamicMontage(
			DeathAnimation, FullBodySlot, 0.0f, 0.0f))
		{
			// Lezi na zadnjem frameu dok ga SetLifeSpan ne ukloni
			Montage->bEnableAutoBlendOut = false;
		}
	}

	UE_LOG(LogMinecraftClone, Log, TEXT("[ZOMBIE] %s umire, nestaje za %.2f s"), *GetName(), DeathSeconds);
	SetLifeSpan(DeathSeconds);
}

void AZombie::LogTestState(const TCHAR* Label) const
{
	const USkeletalMeshComponent* BodyMesh = GetMesh();
	// Kut kosti od rest poze (rest rotacije su jedinicne, build_zombie_blender.py)
	auto BoneAngle = [BodyMesh](const TCHAR* Bone) -> float
	{
		if (!BodyMesh || BodyMesh->GetBoneIndex(Bone) == INDEX_NONE)
		{
			return -1.0f;
		}
		return FMath::RadiansToDegrees(BodyMesh->GetBoneQuaternion(Bone, EBoneSpaces::ComponentSpace).GetAngle());
	};

	const APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	const float DistanceBlocks = Player ? FVector::Dist2D(GetActorLocation(), Player->GetActorLocation()) / 100.0f : -1.0f;

	UE_LOG(LogMinecraftClone, Log,
		TEXT("[ZOMBIE] %s %s: dist=%.1f v=%.0f walk=%.2f cikl=%.3f aggr=%d napad=%d tint=%.0f | noga=%.1f ruka=%.1f trup=%.1f glava=%.1f root=%.1f"),
		Label, *GetName(), DistanceBlocks, GetVelocity().Size2D(), GetWalkAmount(), GetWalkCycleTime(),
		bAggressive ? 1 : 0, bIsAttacking ? 1 : 0, CurrentHurtTint,
		BoneAngle(TEXT("right_leg")), BoneAngle(TEXT("right_arm")), BoneAngle(TEXT("body")),
		BoneAngle(TEXT("head")), BoneAngle(TEXT("root")));
}

// ==================== Dijagnostika: voxel.zombietest ====================
//
// Headless run nema inputa ni renderiranja. Komanda uzme zombija iz svijeta,
// natjera mesh da racuna kosti i bez renderiranja, 14 s prati kako lovi
// igraca (hod, agresivne ruke, napad, glava), pa ga ozlijedi i ubije.
// Ocekivano: ruka ~120 dok ima metu, noga se njise do walk*80, tint=1 nakon
// pogotka, root ide do 90 i zombi nestaje 1 s nakon smrti.

namespace
{
	constexpr float ZombieTestStart = 3.0f;
	constexpr float ZombieTestChaseSeconds = 14.0f;
	constexpr float ZombieTestSample = 0.25f;

	void ZombieTestSampleLoop(TWeakObjectPtr<AZombie> Zombie, const TCHAR* Label, float Remaining, float Step)
	{
		if (!Zombie.IsValid())
		{
			return;
		}
		Zombie->LogTestState(Label);
		if (Remaining <= Step)
		{
			return;
		}
		FTimerHandle Handle;
		Zombie->GetWorld()->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(
			[Zombie, Label, Remaining, Step]()
			{
				ZombieTestSampleLoop(Zombie, Label, Remaining - Step, Step);
			}), Step, false);
	}

	float RunZombieTest(UWorld* World)
	{
		FTimerHandle StartHandle;
		World->GetTimerManager().SetTimer(StartHandle, FTimerDelegate::CreateLambda([World]()
		{
			TActorIterator<AZombie> It(World);
			if (!It)
			{
				UE_LOG(LogMinecraftClone, Warning, TEXT("[ZOMBIE] test: u svijetu nema zombija"));
				return;
			}
			TWeakObjectPtr<AZombie> Zombie = *It;
			USkeletalMeshComponent* Mesh = Zombie->GetMesh();

			// Bez renderiranja ACharacter mesh ne osvjezava kosti
			Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;

			const UAnimInstance* Anim = Mesh->GetAnimInstance();
			UE_LOG(LogMinecraftClone, Log, TEXT("[ZOMBIE] test: %s mesh=%s anim=%s attack=%s death=%s materijal=%s"),
				*Zombie->GetName(),
				Mesh->GetSkeletalMeshAsset() ? *Mesh->GetSkeletalMeshAsset()->GetName() : TEXT("None"),
				Anim ? *Anim->GetClass()->GetName() : TEXT("None"),
				Zombie->AttackAnimation ? *Zombie->AttackAnimation->GetName() : TEXT("None"),
				Zombie->DeathAnimation ? *Zombie->DeathAnimation->GetName() : TEXT("None"),
				Mesh->GetMaterial(0) ? *Mesh->GetMaterial(0)->GetName() : TEXT("None"));

			ZombieTestSampleLoop(Zombie, TEXT("lov"), ZombieTestChaseSeconds, ZombieTestSample);

			FTimerHandle HurtHandle;
			World->GetTimerManager().SetTimer(HurtHandle, FTimerDelegate::CreateLambda([Zombie]()
			{
				if (!Zombie.IsValid())
				{
					return;
				}
				Zombie->TakeDamage(2.0f);
				ZombieTestSampleLoop(Zombie, TEXT("ozljeda"), 1.0f, ZombieTestSample);
			}), ZombieTestChaseSeconds + 0.5f, false);

			FTimerHandle DeathHandle;
			World->GetTimerManager().SetTimer(DeathHandle, FTimerDelegate::CreateLambda([Zombie, World]()
			{
				if (!Zombie.IsValid())
				{
					return;
				}
				Zombie->TakeDamage(1000.0f);
				ZombieTestSampleLoop(Zombie, TEXT("smrt"), 1.2f, 0.1f);

				FTimerHandle GoneHandle;
				World->GetTimerManager().SetTimer(GoneHandle, FTimerDelegate::CreateLambda([Zombie]()
				{
					UE_LOG(LogMinecraftClone, Log, TEXT("[ZOMBIE] test GOTOVO: zombi nakon 1.5 s %s"),
						Zombie.IsValid() ? TEXT("JOS POSTOJI -> FAIL") : TEXT("uklonjen -> OK"));
				}), 1.5f, false);
			}), ZombieTestChaseSeconds + 2.0f, false);
		}), ZombieTestStart, false);

		return ZombieTestStart + ZombieTestChaseSeconds + 2.0f + 1.5f;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GZombieTestCmd(
	TEXT("voxel.zombietest"),
	TEXT("Prati zombija koji lovi igraca, pa ga ozlijedi i ubije; [ZOMBIE] linije u logu"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			const float Duration = RunZombieTest(World);

			// Headless run se sam gasi da se log flusha. U editoru/PIE se ne gasi nista.
			if (!GIsEditor)
			{
				FTimerHandle QuitHandle;
				World->GetTimerManager().SetTimer(QuitHandle, FTimerDelegate::CreateLambda(
					[]() { FPlatformMisc::RequestExit(false); }), Duration + 1.0f, false);
			}
		}));
