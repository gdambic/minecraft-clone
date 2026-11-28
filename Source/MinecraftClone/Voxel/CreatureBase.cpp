#include "CreatureBase.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "NavigationInvokerComponent.h"
#include "BlockLightReceiverComponent.h"
#include "AIController.h"
#include "BrainComponent.h"
#include "CombatLibrary.h"
#include "MinecraftClone.h"

ACreatureBase::ACreatureBase()
{
	PrimaryActorTick.bCanEverTick = true;

	// Generira navmesh u radijusu oko moba (3000 gen / 3500 uklanjanje).
	// Pokriva WanderRadius (500) s velikom rezervom; balon prati moba u chase-u.
	NavInvoker = CreateDefaultSubobject<UNavigationInvokerComponent>(TEXT("NavInvoker"));

	// Svjetlo baklje na stvorenju: svjetlina celije u kojoj su mu oci
	CreateDefaultSubobject<UBlockLightReceiverComponent>(TEXT("BlockLightReceiver"));
	NavInvoker->SetGenerationRadii(3000.0f, 3500.0f);

	// Configure character movement
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = WalkSpeed;
		MoveComp->bOrientRotationToMovement = true;
		MoveComp->RotationRate = FRotator(0.0f, 500.0f, 0.0f);

		// Default MaxStepHeight (~45uu) je manji od BlockSize (100uu) - kapsula bi
		// se fizicki sudarala s rubom bloka i ne mogla se popeti, iako je NavMesh
		// (AVoxelWorld::EnsureNavMeshStepHeight) vec dopustio taj korak u pathu.
		// Drzati u sinku s BlockSize + margina koristenom tamo.
		MoveComp->MaxStepHeight = 110.0f;

		// Minecraft gravitacija (3200 uu/s^2) umjesto UE defaulta (980): s vanilla
		// knockbackom od 0.4 bloka/tick vertikale mob pri 980 leti 1.6 s i digne
		// se 3.3 bloka, sto izgleda lebdece. Pri 3200 je hop 1 blok / 0.5 s kao
		// u vanilli. Vidi Docs/PLAN_Knockback.md.
		MoveComp->GravityScale = UCombatLibrary::MinecraftGravityUU / 980.0f;
	}

	// Don't rotate character to controller rotation (AI will handle this)
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;
}

void ACreatureBase::BeginPlay()
{
	Super::BeginPlay();

	// Apply walk speed to movement component
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = WalkSpeed;
	}

	// Initialize health
	CurrentHealth = MaxHealth;
}

void ACreatureBase::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bInKnockback)
	{
		return;
	}

	const float Now = GetWorld()->GetTimeSeconds();
	UCharacterMovementComponent* MoveComp = GetCharacterMovement();

	// Vanilla horizontalni zracni otpor (0.91 po ticku) - bez njega bi mob pri
	// UE-ovoj fizici bez trenja odletio dvostruko dalje nego u Minecraftu
	if (MoveComp && !MoveComp->IsMovingOnGround())
	{
		const float Drag = UCombatLibrary::AirDragForDelta(DeltaTime);
		MoveComp->Velocity.X *= Drag;
		MoveComp->Velocity.Y *= Drag;
	}

	const bool bPastGrace = Now - KnockbackStartTime >= KnockbackAirborneGrace;
	const bool bLanded = MoveComp && MoveComp->IsMovingOnGround() && bPastGrace;

	if (bLanded || Now >= KnockbackEndTime)
	{
		const float DistanceBlocks = FVector::Dist2D(GetActorLocation(), KnockbackStartLocation)
			/ UCombatLibrary::BlockSizeUU;
		UE_LOG(LogMinecraftClone, Log, TEXT("[KB] %s doskok: %.2f bloka u %.2f s (%s)"),
			*GetName(), DistanceBlocks, Now - KnockbackStartTime,
			bLanded ? TEXT("tlo") : TEXT("timeout"));

		EndKnockback();
	}
}

bool ACreatureBase::CanBeHurt() const
{
	// Mrtvo stvorenje (zombi dok se prevrce) se vise ne moze pogoditi
	return CurrentHealth > 0.0f
		&& GetWorld()->GetTimeSeconds() - LastHurtTime >= HurtImmunityTime;
}

bool ACreatureBase::TakeDamage(float DamageAmount)
{
	// Vanilla: pogodak unutar prozora nepovredivosti ne radi nista
	if (!CanBeHurt())
	{
		return false;
	}
	LastHurtTime = GetWorld()->GetTimeSeconds();

	CurrentHealth -= DamageAmount;

	if (CurrentHealth <= 0.0f)
	{
		CurrentHealth = 0.0f;
		OnDeath();
		return true;
	}

	return false;
}

void ACreatureBase::ReceiveKnockback(FVector HorizontalDir, float StrengthBlocksPerTick)
{
	if (!UCombatLibrary::ApplyKnockback(this, HorizontalDir, StrengthBlocksPerTick, KnockbackResistance))
	{
		// Imun ili bez smjera - AI ne treba pauzirati
		return;
	}

	// Path following bi u istom ticku postavio akceleraciju natrag prema
	// napadacu i pojeo odbacivanje, pa BT logika stoji dok mob leti.
	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		AI->StopMovement();
		if (UBrainComponent* Brain = AI->GetBrainComponent())
		{
			Brain->PauseLogic(TEXT("Knockback"));
		}
	}

	const float Now = GetWorld()->GetTimeSeconds();
	bInKnockback = true;
	KnockbackStartTime = Now;
	KnockbackEndTime = Now + MaxKnockbackDuration;
	KnockbackStartLocation = GetActorLocation();
}

void ACreatureBase::EndKnockback()
{
	bInKnockback = false;

	if (AAIController* AI = Cast<AAIController>(GetController()))
	{
		if (UBrainComponent* Brain = AI->GetBrainComponent())
		{
			Brain->ResumeLogic(TEXT("Knockback"));
		}
	}
}

void ACreatureBase::OnDeath()
{
	// Mob koji umre u letu ne smije ostati s pauziranim AI-em prije unistenja
	EndKnockback();

	// Base implementation - override in child classes
	Destroy();
}
