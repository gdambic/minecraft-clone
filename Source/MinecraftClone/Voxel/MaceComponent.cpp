#include "MaceComponent.h"
#include "FirstPersonCharacter.h"
#include "FirstPersonArmComponent.h"
#include "CreatureBase.h"
#include "MobBase.h"
#include "VoxelWorld.h"
#include "WeaponData.h"
#include "CombatLibrary.h"
#include "BlockSoundLibrary.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "MinecraftClone.h"

// Terraria vrijednosti su iz tModLoader ExampleAdvancedFlailProjectile (preslika
// vanilla AI_015_Flails; Docs/PLAN_Mace.md). Pretvorba: 1 tile = 16 px = 1 blok
// = 100 UU, tick = 1/60 s. Brzine su u UU/tick, akceleracije u UU/tick^2.
namespace
{
	constexpr float PxToUU = 100.0f / 16.0f;
	constexpr float TickSeconds = 1.0f / 60.0f;

	constexpr int32 LaunchTimeLimit = 15;
	constexpr float LaunchSpeed = 14.0f * PxToUU;
	constexpr float MaxLaunchLength = 800.0f * PxToUU;
	constexpr float RetractAcceleration = 3.0f * PxToUU;
	constexpr float MaxRetractSpeed = 10.0f * PxToUU;
	constexpr float ForcedRetractAcceleration = 6.0f * PxToUU;
	constexpr float MaxForcedRetractSpeed = 15.0f * PxToUU;
	constexpr int32 DefaultHitCooldown = 10;
	constexpr int32 SpinHitCooldown = 20;
	constexpr int32 MovingHitCooldown = 10;
	constexpr int32 RicochetTimeLimit = LaunchTimeLimit + 5;
	constexpr float LaunchRange = LaunchSpeed * LaunchTimeLimit;
	constexpr float MaxDroppedRange = LaunchRange + 160.0f * PxToUU;

	/** Kugla dalje od ovoga nestane (vanilla 900 px) */
	constexpr float KillDistance = 900.0f * PxToUU;

	/** Vrtnja: jedinicni (1,1) vektor * 30 px, 10*pi rad/s, prvih 12 tickova bez stete */
	constexpr float SpinOffsetScale = 30.0f * PxToUU;
	constexpr float SpinNoDamageTicks = 12.0f;
	constexpr float SpinHitRadius = 55.0f * PxToUU;

	constexpr float DropGravity = 0.8f * PxToUU;
	constexpr float RicochetGravity = 0.6f * PxToUU;

	/** Udarac u blok brzi od 4 px/tick svira zvuk */
	constexpr float ImpactSpeed = 4.0f * PxToUU;

	/** Mace: useTime 45 (Terraria wiki) */
	constexpr int32 UseTimeTicks = 45;

	/** Mnozitelji stete (vanilla ModifyHitNPC) i knockbacka (wiki 1.4.4: vrtnja 35 %) */
	constexpr float SpinDamageMult = 1.2f;
	constexpr float LaunchDamageMult = 2.0f;
	constexpr float SpinKnockbackMult = 0.35f;
	constexpr float DropKnockbackMult = 0.5f;

	/** Vidljiva kugla (proceduralni mesh) */
	constexpr float BallVisualRadius = 20.0f;
	constexpr float SpikeLength = 12.0f;
	constexpr float SpikeBaseRadius = 5.0f;

	/** Karika lanca: kocka 100 UU skalirana na 10 x 6 x 2 UU, svakih 8 UU */
	constexpr float ChainLinkSpacing = 8.0f;
	const FVector ChainLinkScale(0.10f, 0.06f, 0.02f);

	const FName GColorParam(TEXT("Color"));

	TAutoConsoleVariable<float> CVarMaceRadius(
		TEXT("voxel.mace.radius"), 25.0f,
		TEXT("Pola stranice kolizijske kutije kugle (UU) za blokove i stvorenja"));

	TAutoConsoleVariable<float> CVarMaceIdleScale(
		TEXT("voxel.mace.idlescale"), 0.4f,
		TEXT("Velicina kugle dok visi na dršci (1 = prava velicina)"));

	TAutoConsoleVariable<float> CVarMaceIdleDrop(
		TEXT("voxel.mace.idledrop"), 14.0f,
		TEXT("Koliko UU ispod vrha drške visi kugla u mirovanju"));

	TAutoConsoleVariable<int32> CVarMaceLog(
		TEXT("voxel.mace.log"), 0,
		TEXT("1 = [MACE] linija pri svakoj promjeni stanja i pogotku"));

	FVector MoveTowards(const FVector& Current, const FVector& Target, float MaxDelta)
	{
		const FVector Delta = Target - Current;
		const float Dist = Delta.Size();
		return Dist <= MaxDelta || Dist < KINDA_SMALL_NUMBER ? Target : Current + Delta / Dist * MaxDelta;
	}

	const TCHAR* StateName(EMaceState State)
	{
		switch (State)
		{
		case EMaceState::Idle:             return TEXT("Idle");
		case EMaceState::Spinning:         return TEXT("Spinning");
		case EMaceState::LaunchingForward: return TEXT("LaunchingForward");
		case EMaceState::Retracting:       return TEXT("Retracting");
		case EMaceState::ForcedRetracting: return TEXT("ForcedRetracting");
		case EMaceState::Ricochet:         return TEXT("Ricochet");
		case EMaceState::Dropping:         return TEXT("Dropping");
		default:                           return TEXT("?");
		}
	}

	/** Trokut u obje strane: materijal je jednostran, a winding tako nije bitan */
	void AddTriangle(TArray<FVector>& V, TArray<int32>& T, TArray<FVector>& N,
		const FVector& A, const FVector& B, const FVector& C, const FVector& Outward)
	{
		const int32 Base = V.Num();
		V.Append({ A, B, C });
		N.Append({ Outward, Outward, Outward });
		T.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 1 });
	}
}

UMaceComponent::UMaceComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UMaceComponent::BeginPlay()
{
	Super::BeginPlay();

	OwnerCharacter = Cast<AFirstPersonCharacter>(GetOwner());
	if (!OwnerCharacter)
	{
		UE_LOG(LogMinecraftClone, Error, TEXT("MaceComponent mora biti na AFirstPersonCharacter"));
		return;
	}

	CreateVisuals();
	UpdateVisuals(false);
}

void UMaceComponent::CreateVisuals()
{
	USceneComponent* Root = OwnerCharacter->GetRootComponent();
	// M_EntityColor prima svjetlo baklje (parametar Color kao Unrealov materijal kocke, koji ostaje rezerva)
	UMaterialInterface* BaseMaterial = LoadObject<UMaterialInterface>(nullptr,
		TEXT("/Game/Blocks/Materials/M_EntityColor.M_EntityColor"));
	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr,
			TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	}

	// Kugla i lanac stoje u svijetu: attachment je samo zbog zivotnog ciklusa
	BallMesh = NewObject<UProceduralMeshComponent>(OwnerCharacter, TEXT("MaceBall"));
	BallMesh->SetupAttachment(Root);
	BallMesh->SetUsingAbsoluteLocation(true);
	BallMesh->SetUsingAbsoluteRotation(true);
	BallMesh->SetUsingAbsoluteScale(true);
	BallMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BallMesh->RegisterComponent();
	BuildBallMesh();

	ChainMesh = NewObject<UInstancedStaticMeshComponent>(OwnerCharacter, TEXT("MaceChain"));
	ChainMesh->SetupAttachment(Root);
	ChainMesh->SetUsingAbsoluteLocation(true);
	ChainMesh->SetUsingAbsoluteRotation(true);
	ChainMesh->SetUsingAbsoluteScale(true);
	ChainMesh->SetWorldTransform(FTransform::Identity);
	ChainMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ChainMesh->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
	ChainMesh->RegisterComponent();

	if (BaseMaterial)
	{
		BallMID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		BallMID->SetVectorParameterValue(GColorParam, FLinearColor(0.22f, 0.22f, 0.24f));
		BallMesh->SetMaterial(0, BallMID);

		ChainMID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
		ChainMID->SetVectorParameterValue(GColorParam, FLinearColor(0.12f, 0.12f, 0.13f));
		ChainMesh->SetMaterial(0, ChainMID);
	}
}

void UMaceComponent::BuildBallMesh()
{
	// Placeholder dok ne stigne model: kugla (12 x 8 segmenata) i 14 stozaca
	// (6 po osima, 8 po dijagonalama kocke), plosno sjencano
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;

	const int32 Segments = 12;
	const int32 Rings = 8;
	auto SpherePoint = [](int32 Seg, int32 Ring) -> FVector
	{
		const float Theta = PI * Ring / Rings;
		const float Phi = 2.0f * PI * Seg / Segments;
		return FVector(FMath::Sin(Theta) * FMath::Cos(Phi), FMath::Sin(Theta) * FMath::Sin(Phi),
			FMath::Cos(Theta)) * BallVisualRadius;
	};
	for (int32 Ring = 0; Ring < Rings; ++Ring)
	{
		for (int32 Seg = 0; Seg < Segments; ++Seg)
		{
			const FVector A = SpherePoint(Seg, Ring);
			const FVector B = SpherePoint(Seg + 1, Ring);
			const FVector C = SpherePoint(Seg + 1, Ring + 1);
			const FVector D = SpherePoint(Seg, Ring + 1);
			const FVector Outward = ((A + B + C + D) * 0.25f).GetSafeNormal();
			if (Ring > 0)
			{
				AddTriangle(Vertices, Triangles, Normals, A, B, C, Outward);
			}
			if (Ring < Rings - 1)
			{
				AddTriangle(Vertices, Triangles, Normals, A, C, D, Outward);
			}
		}
	}

	TArray<FVector> SpikeDirs = {
		FVector::ForwardVector, -FVector::ForwardVector, FVector::RightVector,
		-FVector::RightVector, FVector::UpVector, -FVector::UpVector };
	for (int32 i = 0; i < 8; ++i)
	{
		SpikeDirs.Add(FVector(i & 1 ? 1 : -1, i & 2 ? 1 : -1, i & 4 ? 1 : -1).GetSafeNormal());
	}

	const int32 Sides = 8;
	for (const FVector& Dir : SpikeDirs)
	{
		FVector Side1, Side2;
		Dir.FindBestAxisVectors(Side1, Side2);
		// Baza malo unutar kugle da ne ostane procjep na zakrivljenosti
		const FVector BaseCenter = Dir * (BallVisualRadius - 2.0f);
		const FVector Tip = Dir * (BallVisualRadius + SpikeLength);
		for (int32 s = 0; s < Sides; ++s)
		{
			const float A0 = 2.0f * PI * s / Sides;
			const float A1 = 2.0f * PI * (s + 1) / Sides;
			const FVector P0 = BaseCenter + (Side1 * FMath::Cos(A0) + Side2 * FMath::Sin(A0)) * SpikeBaseRadius;
			const FVector P1 = BaseCenter + (Side1 * FMath::Cos(A1) + Side2 * FMath::Sin(A1)) * SpikeBaseRadius;
			const FVector Outward = (((P0 + P1) * 0.5f - BaseCenter).GetSafeNormal() + Dir * 0.4f).GetSafeNormal();
			AddTriangle(Vertices, Triangles, Normals, P0, P1, Tip, Outward);
		}
	}

	BallMesh->CreateMeshSection(0, Vertices, Triangles, Normals, TArray<FVector2D>(),
		TArray<FColor>(), TArray<FProcMeshTangent>(), false);
}

// ==================== Input ====================

void UMaceComponent::PressAttack()
{
	bAttackHeld = true;
	bPressPending = true;
}

void UMaceComponent::ReleaseAttack()
{
	bAttackHeld = false;
}

// ==================== Tick ====================

void UMaceComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (bSelfTest)
	{
		TickSelfTest();
		SelfTestTime += DeltaTime;
		TickInternal(DeltaTime, EItemType::Mace);
	}
}

void UMaceComponent::TickMace(float DeltaTime, EItemType HeldItem)
{
	// Tijekom testa kuglu vodi TickComponent s Maceom, bez obzira na odabrani slot
	if (!bSelfTest)
	{
		TickInternal(DeltaTime, HeldItem);
	}
}

void UMaceComponent::TickInternal(float DeltaTime, EItemType HeldItem)
{
	if (!OwnerCharacter || !BallMesh || !ChainMesh)
	{
		return;
	}

	bHeld = IsMace(HeldItem);
	if (!bHeld)
	{
		// Promjena itema ponisti kuglu (u Terrariji se item ne moze promijeniti dok je flail vani)
		if (State != EMaceState::Idle)
		{
			Kill();
		}
		bAttackHeld = false;
		bPressPending = false;
		UpdateVisuals(false);
		return;
	}

	// Fiksni 60 Hz tick; najvise 10 tickova po frameu da hitch ne odvuce kuglu
	TickAccumulator = FMath::Min(TickAccumulator + DeltaTime, 10.0f * TickSeconds);
	while (TickAccumulator >= TickSeconds)
	{
		TickAccumulator -= TickSeconds;
		PrevBallPos = BallPos;
		SimulateTick();
	}

	UpdateVisuals(true);
}

FVector UMaceComponent::GetMountedCenter() const
{
	return OwnerCharacter->GetActorLocation();
}

FVector UMaceComponent::GetAimPoint() const
{
	const UCameraComponent* Camera = OwnerCharacter->GetFirstPersonCameraComponent();
	const FVector Start = Camera->GetComponentLocation();
	const FVector End = Start + Camera->GetForwardVector() * LaunchRange;

	FHitResult Hit;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(OwnerCharacter);
	return GetWorld()->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params)
		? Hit.ImpactPoint : End;
}

FVector UMaceComponent::GetChainAnchorWorld() const
{
	const UProceduralMeshComponent* Sprite = OwnerCharacter->GetHeldSpriteMeshComponent();
	return Sprite ? Sprite->GetComponentTransform().TransformPosition(ChainAnchorLocal)
		: OwnerCharacter->GetFirstPersonCameraComponent()->GetComponentLocation();
}

void UMaceComponent::SetState(EMaceState NewState)
{
	if (NewState == State)
	{
		return;
	}

	if (bSelfTest || CVarMaceLog.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogMinecraftClone, Log, TEXT("[MACE] t=%.3f %s -> %s udaljenost=%.0f"),
			TickCount * TickSeconds, StateName(State), StateName(NewState),
			FVector::Dist(BallPos, GetMountedCenter()));
	}
	State = NewState;
}

void UMaceComponent::Kill()
{
	SetState(EMaceState::Idle);
	Velocity = FVector::ZeroVector;
	KilledTick = TickCount;
}

void UMaceComponent::SimulateTick()
{
	++TickCount;
	const FVector Center = GetMountedCenter();

	// Item se upotrebljava novim pritiskom kad je proslo useTime od zadnje
	// upotrebe i 2 ticka od nestanka kugle (SetDummyItemTime(2))
	if (State == EMaceState::Idle)
	{
		if (bPressPending && bAttackHeld
			&& TickCount - LastUseTick >= UseTimeTicks && TickCount - KilledTick > 2)
		{
			LastUseTick = TickCount;
			StateTimer = 0.0f;
			SpinningStateTimer = 0.0f;
			CollisionCounter = 0.0f;
			LastHitTick.Reset();
			BallPos = PrevBallPos = Center;
			SetState(EMaceState::Spinning);
		}
		bPressPending = false;
		if (State == EMaceState::Idle)
		{
			return;
		}
	}
	bPressPending = false;

	if (FVector::Dist(BallPos, Center) > KillDistance)
	{
		Kill();
		return;
	}

	HitCooldownTicks = DefaultHitCooldown;

	switch (State)
	{
	case EMaceState::Spinning:
	{
		FVector ToMouse = (GetAimPoint() - Center).GetSafeNormal();
		if (ToMouse.IsNearlyZero())
		{
			ToMouse = OwnerCharacter->GetControlRotation().Vector();
		}

		if (!bAttackHeld)
		{
			// Pusteno: baci prema nisanu iz sredine igraca, uz igracevu brzinu
			StateTimer = 0.0f;
			Velocity = ToMouse * LaunchSpeed + OwnerCharacter->GetVelocity() * TickSeconds;
			BallPos = PrevBallPos = Center;
			LastHitTick.Reset();
			HitCooldownTicks = MovingHitCooldown;
			SetState(EMaceState::LaunchingForward);
			break;
		}

		SpinningStateTimer += 1.0f;

		// 2D vrtnja iz Terrarije (X naprijed, Y dolje) u vertikalnoj ravnini
		// smjera gledanja: kugla prolazi iznad glave, pa dolje ispred igraca
		const float Angle = PI * 10.0f * (SpinningStateTimer / 60.0f);
		const float Cos = FMath::Cos(Angle);
		const float Sin = FMath::Sin(Angle);
		const float OffsetX = Cos - Sin;
		float OffsetY = Sin + Cos;
		OffsetY *= 0.8f;
		if (OffsetY > 0.0f)
		{
			OffsetY *= 0.5f;
		}

		FVector Forward = OwnerCharacter->GetControlRotation().Vector();
		Forward.Z = 0.0f;
		Forward = Forward.GetSafeNormal(KINDA_SMALL_NUMBER, FVector::ForwardVector);

		BallPos = Center + (Forward * OffsetX - FVector::UpVector * OffsetY) * SpinOffsetScale;
		Velocity = FVector::ZeroVector;
		HitCooldownTicks = SpinHitCooldown;
		break;
	}

	case EMaceState::LaunchingForward:
	{
		bool bRetract = StateTimer >= LaunchTimeLimit;
		StateTimer += 1.0f;
		bRetract |= FVector::Dist(BallPos, Center) >= MaxLaunchLength;

		if (bAttackHeld)
		{
			StateTimer = 0.0f;
			Velocity *= 0.2f;
			SetState(EMaceState::Dropping);
			break;
		}
		if (bRetract)
		{
			StateTimer = 0.0f;
			Velocity *= 0.3f;
			SetState(EMaceState::Retracting);
		}
		HitCooldownTicks = MovingHitCooldown;
		break;
	}

	case EMaceState::Retracting:
	{
		if (FVector::Dist(BallPos, Center) <= MaxRetractSpeed)
		{
			Kill();
			return;
		}
		if (bAttackHeld)
		{
			StateTimer = 0.0f;
			Velocity *= 0.2f;
			SetState(EMaceState::Dropping);
		}
		else
		{
			const FVector ToPlayer = (Center - BallPos).GetSafeNormal();
			Velocity *= 0.98f;
			Velocity = MoveTowards(Velocity, ToPlayer * MaxRetractSpeed, RetractAcceleration);
		}
		break;
	}

	case EMaceState::ForcedRetracting:
	{
		const FVector ToPlayer = (Center - BallPos).GetSafeNormal();
		if (FVector::Dist(BallPos, Center) <= MaxForcedRetractSpeed)
		{
			Kill();
			return;
		}
		Velocity *= 0.98f;
		Velocity = MoveTowards(Velocity, ToPlayer * MaxForcedRetractSpeed, ForcedRetractAcceleration);

		// Kugla bi u ovom ticku presla igraca
		const FVector FromTarget = (Center - (BallPos + Velocity)).GetSafeNormal();
		if (FVector::DotProduct(ToPlayer, FromTarget) < 0.0f)
		{
			Kill();
			return;
		}
		break;
	}

	case EMaceState::Ricochet:
	{
		const bool bDone = StateTimer >= RicochetTimeLimit;
		StateTimer += 1.0f;
		if (bDone)
		{
			StateTimer = 0.0f;
			SetState(EMaceState::Dropping);
		}
		else
		{
			HitCooldownTicks = MovingHitCooldown;
			Velocity.Z -= RicochetGravity;
			Velocity.X *= 0.95f;
			Velocity.Y *= 0.95f;
		}
		break;
	}

	case EMaceState::Dropping:
	{
		if (!bAttackHeld || FVector::Dist(BallPos, Center) > MaxDroppedRange)
		{
			StateTimer = 0.0f;
			SetState(EMaceState::ForcedRetracting);
		}
		else
		{
			Velocity.Z -= DropGravity;
			Velocity.X *= 0.95f;
			Velocity.Y *= 0.95f;
		}
		break;
	}

	default:
		break;
	}

	if (State == EMaceState::Idle)
	{
		return;
	}

	// Pomak poslije AI-ja, kao Projectile.Update; vrtnja postavlja poziciju izravno
	if (State == EMaceState::ForcedRetracting)
	{
		BallPos += Velocity;
	}
	else if (State != EMaceState::Spinning)
	{
		MoveWithTileCollision();
	}

	CheckHits();
}

// ==================== Kolizija s blokovima ====================

bool UMaceComponent::OverlapsSolid(const FVector& Center) const
{
	if (!VoxelWorld)
	{
		return false;
	}

	// Blok je centriran na GridToWorld: stupac X pokriva [100X - 50, 100X + 50)
	const float R = CVarMaceRadius.GetValueOnGameThread();
	const float Size = VoxelWorld->BlockSize;
	const FVector Rel = (Center - VoxelWorld->GetActorLocation()) / Size;
	const float RelR = R / Size;
	const FIntVector Min(
		FMath::FloorToInt(Rel.X - RelR + 0.5f), FMath::FloorToInt(Rel.Y - RelR + 0.5f),
		FMath::FloorToInt(Rel.Z - RelR + 0.5f));
	const FIntVector Max(
		FMath::FloorToInt(Rel.X + RelR + 0.5f - KINDA_SMALL_NUMBER),
		FMath::FloorToInt(Rel.Y + RelR + 0.5f - KINDA_SMALL_NUMBER),
		FMath::FloorToInt(Rel.Z + RelR + 0.5f - KINDA_SMALL_NUMBER));

	for (int32 Z = Min.Z; Z <= Max.Z; ++Z)
	{
		for (int32 Y = Min.Y; Y <= Max.Y; ++Y)
		{
			for (int32 X = Min.X; X <= Max.X; ++X)
			{
				if (VoxelWorld->GetBlockTypeAt(FIntVector(X, Y, Z)) != EBlockType::Air)
				{
					return true;
				}
			}
		}
	}
	return false;
}

void UMaceComponent::MoveWithTileCollision()
{
	const FVector OldVelocity = Velocity;

	// Kugla vec u bloku (npr. bacena iz igraca uz zid) ne moze se izvuci po
	// osima - pusti je da se slobodno pomakne
	if (OverlapsSolid(BallPos))
	{
		BallPos += Velocity;
		return;
	}

	// Collision.TileCollision: os po os, na sudaru se kugla zaustavi uz blok
	bool bCollided[3] = { false, false, false };
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const float Move = OldVelocity[Axis];
		if (FMath::IsNearlyZero(Move))
		{
			continue;
		}

		FVector Target = BallPos;
		Target[Axis] += Move;
		if (!OverlapsSolid(Target))
		{
			BallPos = Target;
			continue;
		}

		// Najveci slobodan dio pomaka (binarno, ~0.4 % pomaka)
		float Lo = 0.0f;
		float Hi = 1.0f;
		for (int32 Iter = 0; Iter < 8; ++Iter)
		{
			const float Mid = 0.5f * (Lo + Hi);
			FVector Probe = BallPos;
			Probe[Axis] += Move * Mid;
			(OverlapsSolid(Probe) ? Hi : Lo) = Mid;
		}
		BallPos[Axis] += Move * Lo;
		bCollided[Axis] = true;
	}

	if (!bCollided[0] && !bCollided[1] && !bCollided[2])
	{
		return;
	}

	// OnTileCollide
	float Bounce = 0.2f;
	if (State == EMaceState::LaunchingForward || State == EMaceState::Ricochet)
	{
		Bounce = 0.4f;
	}
	if (State == EMaceState::Dropping)
	{
		Bounce = 0.0f;
	}

	int32 ImpactIntensity = 0;
	int32 ImpactAxis = 0;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		if (!bCollided[Axis])
		{
			continue;
		}
		if (FMath::Abs(OldVelocity[Axis]) > ImpactSpeed)
		{
			ImpactIntensity = 1;
		}
		Velocity[Axis] = -OldVelocity[Axis] * Bounce;
		CollisionCounter += 1.0f;
		ImpactAxis = Axis;
	}

	if (State == EMaceState::LaunchingForward)
	{
		HitCooldownTicks = DefaultHitCooldown;
		ImpactIntensity = 2;
		SetState(EMaceState::Ricochet);
	}

	if (ImpactIntensity > 0 && VoxelWorld)
	{
		// Zvuk udarca bloka u koji je kugla udarila (Terraria svira SoundID.Dig)
		FVector Probe = BallPos;
		Probe[ImpactAxis] += FMath::Sign(OldVelocity[ImpactAxis])
			* (CVarMaceRadius.GetValueOnGameThread() + 1.0f);
		const FVector Rel = (Probe - VoxelWorld->GetActorLocation()) / VoxelWorld->BlockSize;
		const FIntVector Grid(FMath::RoundToInt(Rel.X), FMath::RoundToInt(Rel.Y), FMath::RoundToInt(Rel.Z));
		const EBlockType HitType = VoxelWorld->GetBlockTypeAt(Grid);
		if (HitType != EBlockType::Air)
		{
			UBlockSoundLibrary::PlayBlockSound(this, HitType, EBlockSoundEvent::Hit, Probe);
		}
	}

	// Zapela kugla se prisilno vraca
	if (State != EMaceState::Spinning && State != EMaceState::Ricochet
		&& State != EMaceState::Dropping && CollisionCounter >= 10.0f)
	{
		SetState(EMaceState::ForcedRetracting);
	}
}

// ==================== Pogoci ====================

void UMaceComponent::CheckHits()
{
	// Vrtnja ne radi stetu prvih 12 tickova (jedan puni krug)
	if (State == EMaceState::Spinning && SpinningStateTimer <= SpinNoDamageTicks)
	{
		return;
	}

	const FVector Center = GetMountedCenter();
	const float R = CVarMaceRadius.GetValueOnGameThread();
	const FBox BallBox(BallPos - FVector(R), BallPos + FVector(R));

	TArray<ACreatureBase*> Targets;
	for (TActorIterator<ACreatureBase> It(GetWorld()); It; ++It)
	{
		ACreatureBase* Creature = *It;
		const UCapsuleComponent* Capsule = Creature ? Creature->GetCapsuleComponent() : nullptr;
		if (!Capsule)
		{
			continue;
		}
		const FBox TargetBox = Capsule->Bounds.GetBox();

		bool bHit = false;
		if (State == EMaceState::Spinning)
		{
			// Vrtnja pogada sve unutar elipse oko igraca (okomito 0.8 manje),
			// samo uz liniju vidljivosti od igraca (ownerHitCheck)
			FVector ToTarget = TargetBox.GetClosestPointTo(Center) - Center;
			ToTarget.Z /= 0.8f;
			if (ToTarget.Size() <= SpinHitRadius)
			{
				FHitResult Hit;
				FCollisionQueryParams Params;
				Params.AddIgnoredActor(OwnerCharacter);
				Params.AddIgnoredActor(Creature);
				bHit = !GetWorld()->LineTraceSingleByChannel(Hit, Center,
					Creature->GetActorLocation(), ECC_Visibility, Params);
			}
		}
		else
		{
			bHit = BallBox.Intersect(TargetBox);
		}

		if (!bHit)
		{
			continue;
		}

		// Lokalni hit cooldown po meti
		if (const int64* Last = LastHitTick.Find(Creature))
		{
			if (TickCount - *Last < HitCooldownTicks)
			{
				continue;
			}
		}
		Targets.Add(Creature);
	}

	// Steta moze unistiti stvorenje - zato izvan iteratora
	for (ACreatureBase* Target : Targets)
	{
		HitCreature(Target);
	}
}

void UMaceComponent::HitCreature(ACreatureBase* Target)
{
	// Prozor nepovredivosti stvorenja (0.5 s) vrijedi za svaku stetu u igri
	if (!Target->CanBeHurt())
	{
		return;
	}

	// Stvarna osnovna steta flaila je pola iznosa s tooltipa (Terraria wiki);
	// let i vracanje je udvostruce, vrtnja mnozi s 1.2
	const FWeaponData Weapon = UWeaponDataLibrary::GetWeaponData(EItemType::Mace);
	float DamageMult = 1.0f;
	float KnockbackMult = 1.0f;
	switch (State)
	{
	case EMaceState::Spinning:
		DamageMult = SpinDamageMult;
		KnockbackMult = SpinKnockbackMult;
		break;
	case EMaceState::LaunchingForward:
	case EMaceState::Retracting:
		DamageMult = LaunchDamageMult;
		break;
	case EMaceState::Dropping:
		KnockbackMult = DropKnockbackMult;
		break;
	default:
		break;
	}

	const float Damage = Weapon.Damage * 0.5f * DamageMult;
	const float Knockback = UCombatLibrary::BaseKnockbackStrength * Weapon.KnockbackMultiplier * KnockbackMult;
	const bool bKilled = Target->TakeDamage(Damage);
	LastHitTick.Add(Target, TickCount);

	if (UFirstPersonArmComponent* Arm = OwnerCharacter->FindComponentByClass<UFirstPersonArmComponent>())
	{
		Arm->PlayHitSound();
	}

	// Flail uvijek odbacuje od igraca, ne u smjeru leta kugle
	if (!bKilled)
	{
		Target->ReceiveKnockback(UCombatLibrary::HorizontalDirection(OwnerCharacter, Target), Knockback);
	}

	if (AMobBase* Mob = Cast<AMobBase>(Target))
	{
		Mob->OnDamageTaken(OwnerCharacter);
	}

	if (bSelfTest)
	{
		++SelfTestHits;
	}
	if (bSelfTest || CVarMaceLog.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogMinecraftClone, Log, TEXT("[MACE] t=%.3f pogodak %s stanje=%s steta=%.1f knockback=%.2f b/t%s"),
			TickCount * TickSeconds, *Target->GetName(), StateName(State), Damage, Knockback,
			bKilled ? TEXT(" (ubijen)") : TEXT(""));
	}
}

// ==================== Prikaz ====================

void UMaceComponent::UpdateVisuals(bool bVisible)
{
	BallMesh->SetVisibility(bVisible);
	ChainMesh->SetVisibility(bVisible);
	if (!bVisible)
	{
		return;
	}

	const FVector Anchor = GetChainAnchorWorld();
	FVector RenderPos;
	float BallScale = 1.0f;
	if (State == EMaceState::Idle)
	{
		// Kugla visi na kratkom lancu ispod vrha drške
		BallScale = CVarMaceIdleScale.GetValueOnGameThread();
		RenderPos = Anchor - FVector::UpVector * CVarMaceIdleDrop.GetValueOnGameThread();
	}
	else
	{
		RenderPos = FMath::Lerp(PrevBallPos, BallPos, TickAccumulator / TickSeconds);
	}

	// Kotrljanje: kugla se okrece oko osi okomite na pomak
	const FVector Moved = RenderPos - BallMesh->GetComponentLocation();
	FVector RollAxis = FVector::CrossProduct(FVector::UpVector, Moved);
	if (State != EMaceState::Idle && RollAxis.Normalize() && Moved.Size() < 1000.0f)
	{
		const FQuat Roll(RollAxis, Moved.Size() / (BallVisualRadius * BallScale));
		BallMesh->SetWorldRotation(Roll * BallMesh->GetComponentQuat());
	}
	BallMesh->SetWorldLocation(RenderPos);
	BallMesh->SetWorldScale3D(FVector(BallScale));

	// Lanac: ravna linija karika od vrha drške do sredine kugle
	const FVector ChainVec = RenderPos - Anchor;
	const float Length = ChainVec.Size();
	const int32 LinkCount = FMath::Max(1, FMath::CeilToInt(Length / ChainLinkSpacing));
	const FQuat Base = ChainVec.ToOrientationQuat();
	const FQuat Twist(FVector::ForwardVector, HALF_PI);

	TArray<FTransform> Links;
	Links.Reserve(LinkCount);
	for (int32 i = 0; i < LinkCount; ++i)
	{
		const float T = (i + 0.5f) / LinkCount;
		const FQuat Rot = (i & 1) ? Base * Twist : Base;
		Links.Add(FTransform(Rot, Anchor + ChainVec * T, ChainLinkScale * FMath::Min(1.0f, BallScale + 0.4f)));
	}

	if (ChainMesh->GetInstanceCount() != LinkCount)
	{
		ChainMesh->ClearInstances();
		ChainMesh->AddInstances(Links, false, true);
	}
	else
	{
		ChainMesh->BatchUpdateInstancesTransforms(0, Links, true, true);
	}
}

// ==================== Dijagnostika: voxel.macetest ====================
//
// Headless run nema inputa, pa test sam pritisce i pusta tipku:
//   1) kratka vrtnja (0.15 s) pa bacanje prema stvorenju (pogodak u letu), cekanje povratka
//   2) bacanje pa ponovni pritisak nakon 0.1 s (Dropping), drzanje 1 s, pustanje
// Prolaz: oba povratka u Idle, udaljenost nikad preko MaxLaunchLength, >= 1 pogodak.

void UMaceComponent::StartSelfTest()
{
	if (!OwnerCharacter || !VoxelWorld)
	{
		UE_LOG(LogMinecraftClone, Warning, TEXT("[MACE] test: nema igraca ili svijeta"));
		return;
	}

	// Stvorenje najblize igracu stavi 5 blokova ispred njega, na tlo
	ACreatureBase* Target = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	for (TActorIterator<ACreatureBase> It(GetWorld()); It; ++It)
	{
		const float DistSq = FVector::DistSquared(It->GetActorLocation(), OwnerCharacter->GetActorLocation());
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Target = *It;
		}
	}

	FVector Forward = OwnerCharacter->GetControlRotation().Vector();
	Forward.Z = 0.0f;
	Forward = Forward.GetSafeNormal(KINDA_SMALL_NUMBER, FVector::ForwardVector);

	if (Target)
	{
		const FVector Spot = OwnerCharacter->GetActorLocation() + Forward * 500.0f;
		const FVector Rel = (Spot - VoxelWorld->GetActorLocation()) / VoxelWorld->BlockSize;
		FIntVector Grid(FMath::RoundToInt(Rel.X), FMath::RoundToInt(Rel.Y), FMath::RoundToInt(Rel.Z) + 6);
		for (int32 Step = 0; Step < 20 && VoxelWorld->GetBlockTypeAt(Grid - FIntVector(0, 0, 1)) == EBlockType::Air; ++Step)
		{
			--Grid.Z;
		}
		const float HalfHeight = Target->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
		const FVector Ground = VoxelWorld->GridToWorld(Grid.X, Grid.Y, Grid.Z)
			- FVector(0.0f, 0.0f, VoxelWorld->BlockSize * 0.5f);
		Target->SetActorLocation(Ground + FVector(0.0f, 0.0f, HalfHeight + 2.0f), false, nullptr,
			ETeleportType::TeleportPhysics);

		// Pogled na metu (headless kamera prati control rotation)
		if (AController* Controller = OwnerCharacter->GetController())
		{
			const FVector Eye = OwnerCharacter->GetFirstPersonCameraComponent()->GetComponentLocation();
			Controller->SetControlRotation((Target->GetActorLocation() - Eye).Rotation());
		}
		UE_LOG(LogMinecraftClone, Log, TEXT("[MACE] test: meta %s na %s, igrac na %s"),
			*Target->GetName(), *Target->GetActorLocation().ToCompactString(),
			*OwnerCharacter->GetActorLocation().ToCompactString());
	}
	else
	{
		UE_LOG(LogMinecraftClone, Warning, TEXT("[MACE] test: u svijetu nema stvorenja, pogodak se ne mjeri"));
	}

	UE_LOG(LogMinecraftClone, Log,
		TEXT("[MACE] test: domet leta %.0f UU (ocekivano), najvise %.0f UU, drop do %.0f UU"),
		LaunchRange, MaxLaunchLength, MaxDroppedRange);

	bSelfTest = true;
	SelfTestTime = 0.0f;
	SelfTestStep = 0;
	SelfTestMaxDist = 0.0f;
	SelfTestHits = 0;
	LastUseTick = -1000;
	KilledTick = -1000;
	Kill();
}

void UMaceComponent::TickSelfTest()
{
	const float Dist = FVector::Dist(BallPos, GetMountedCenter());
	if (State != EMaceState::Idle && State != EMaceState::Spinning)
	{
		SelfTestMaxDist = FMath::Max(SelfTestMaxDist, Dist);
	}

	// Visina kugle nad tlom dok lezi (Dropping)
	if (State == EMaceState::Dropping && TickCount % 15 == 0 && TickCount != SelfTestLoggedTick)
	{
		SelfTestLoggedTick = TickCount;
		UE_LOG(LogMinecraftClone, Log, TEXT("[MACE] t=%.3f lezi: pozicija=%s brzina=%s"),
			TickCount * TickSeconds, *BallPos.ToCompactString(), *Velocity.ToCompactString());
	}

	switch (SelfTestStep)
	{
	case 0:   // pritisak: vrtnja
		if (SelfTestTime >= 0.5f)
		{
			PressAttack();
			++SelfTestStep;
		}
		break;
	case 1:   // pusti prije prve stete vrtnje (12 tickova): bacanje u metu
		if (SelfTestTime >= 0.65f)
		{
			ReleaseAttack();
			++SelfTestStep;
		}
		break;
	case 2:   // cekaj povratak
		if (State == EMaceState::Idle && SelfTestTime > 0.75f)
		{
			UE_LOG(LogMinecraftClone, Log, TEXT("[MACE] test 1: vracena nakon %.2f s leta, najveca udaljenost %.0f UU"),
				SelfTestTime - 0.65f, SelfTestMaxDist);
			SelfTestTime = 9.5f;
			++SelfTestStep;
		}
		else if (SelfTestTime > 8.0f && SelfTestTime < 9.0f)
		{
			UE_LOG(LogMinecraftClone, Warning, TEXT("[MACE] test 1: kugla se nije vratila (stanje %s)"), StateName(State));
			SelfTestStep = 100;
		}
		break;
	case 3:   // novi pritisak 0.5 s nakon povratka (2 ticka nakon nestanka kugla se ne moze baciti)
		if (SelfTestTime >= 10.0f)
		{
			PressAttack();
			++SelfTestStep;
		}
		break;
	case 4:   // kratka vrtnja pa bacanje
		if (SelfTestTime >= 10.3f)
		{
			ReleaseAttack();
			++SelfTestStep;
		}
		break;
	case 5:   // ponovni pritisak tijekom leta: Dropping
		if (SelfTestTime >= 10.4f)
		{
			PressAttack();
			++SelfTestStep;
		}
		break;
	case 6:   // drzi 1 s pa pusti
		if (SelfTestTime >= 11.4f)
		{
			ReleaseAttack();
			++SelfTestStep;
		}
		break;
	case 7:
		if (State == EMaceState::Idle)
		{
			const bool bOk = SelfTestMaxDist <= MaxLaunchLength && SelfTestHits > 0;
			UE_LOG(LogMinecraftClone, Log,
				TEXT("[MACE] test GOTOVO: oba povratka OK, najveca udaljenost %.0f UU, pogodaka %d -> %s"),
				SelfTestMaxDist, SelfTestHits, bOk ? TEXT("OK") : TEXT("FAIL"));
			SelfTestStep = 100;
		}
		else if (SelfTestTime > 16.0f)
		{
			UE_LOG(LogMinecraftClone, Warning, TEXT("[MACE] test 2: kugla se nije vratila (stanje %s)"), StateName(State));
			SelfTestStep = 100;
		}
		break;
	default:
		bSelfTest = false;
		break;
	}
}

static FAutoConsoleCommandWithWorldAndArgs GMaceTestCmd(
	TEXT("voxel.macetest"),
	TEXT("Simuliraj Mace: vrtnja, bacanje prema stvorenju, drop. [MACE] linije u logu."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}

			// Svijet i mobovi trebaju par sekundi za generaciju i spawn
			TWeakObjectPtr<UWorld> WeakWorld(World);
			FTimerHandle Handle;
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(
				[WeakWorld]()
				{
					APawn* Player = WeakWorld.IsValid() ? UGameplayStatics::GetPlayerPawn(WeakWorld.Get(), 0) : nullptr;
					UMaceComponent* Mace = Player ? Player->FindComponentByClass<UMaceComponent>() : nullptr;
					if (!Mace)
					{
						UE_LOG(LogMinecraftClone, Warning, TEXT("[MACE] test: igrac nema MaceComponent"));
						return;
					}
					Mace->StartSelfTest();
				}), 6.0f, false);

			// Headless run se sam gasi da se log flusha. U editoru/PIE ne.
			if (!GIsEditor)
			{
				FTimerHandle QuitHandle;
				World->GetTimerManager().SetTimer(QuitHandle, FTimerDelegate::CreateLambda(
					[]() { FPlatformMisc::RequestExit(false); }), 6.0f + 18.0f, false);
			}
		}));
