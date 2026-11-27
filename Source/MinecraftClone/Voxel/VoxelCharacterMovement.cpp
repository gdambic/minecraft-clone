#include "VoxelCharacterMovement.h"
#include "GameFramework/Character.h"

namespace
{
	// Vanilla po ticku (20/s): vodoravna brzina x0.91 (LivingEntity.travelInAir),
	// okomita x0.6 (Player.travel u letu). Kao potencija s 20*dt brzina prati
	// istu krivulju neovisno o broju frameova.
	constexpr float TicksPerSecond = 20.0f;
	constexpr float HorizontalDragPerTick = 0.91f;
	constexpr float VerticalDragPerTick = 0.6f;
}

void UVoxelCharacterMovement::SetCreativeFlyInput(bool bUp, bool bDown, bool bSprint)
{
	bFlyUp = bUp;
	bFlyDown = bDown;
	bFlySprint = bSprint;
}

void UVoxelCharacterMovement::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (MovementMode != MOVE_Flying || !bCreativeFlying || HasAnimRootMotion() || CurrentRootMotion.HasOverrideVelocity())
	{
		Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
		return;
	}

	// Acceleration je input skaliran na MaxAcceleration; vodoravni dio je WASD
	const float MaxAccel = FMath::Max(GetMaxAcceleration(), KINDA_SMALL_NUMBER);
	const FVector Input = (FVector(Acceleration.X, Acceleration.Y, 0.f) / MaxAccel).GetClampedToMaxSize(1.f);
	const float Speed = CreativeFlySpeed * (bFlySprint ? CreativeFlySprintMultiplier : 1.f);
	const FVector TargetXY = Input * Speed;

	// Skok i Shift zajedno se ponistavaju (LocalPlayer.aiStep)
	const float VerticalInput = (bFlyUp ? 1.f : 0.f) - (bFlyDown ? 1.f : 0.f);
	const float TargetZ = VerticalInput * CreativeFlyVerticalSpeed;

	const float Ticks = DeltaTime * TicksPerSecond;
	const float HorizontalKeep = FMath::Pow(HorizontalDragPerTick, Ticks);
	const float VerticalKeep = FMath::Pow(VerticalDragPerTick, Ticks);

	Velocity.X = TargetXY.X + (Velocity.X - TargetXY.X) * HorizontalKeep;
	Velocity.Y = TargetXY.Y + (Velocity.Y - TargetXY.Y) * HorizontalKeep;
	Velocity.Z = TargetZ + (Velocity.Z - TargetZ) * VerticalKeep;

	bFlyMovingDown = Velocity.Z < -1.f;
}

void UVoxelCharacterMovement::PhysFlying(float DeltaTime, int32 Iterations)
{
	bFlyMovingDown = false;
	Super::PhysFlying(DeltaTime, Iterations);

	// PhysFlying ne trazi tlo. Vanilla gasi let kad je lik onGround, a to
	// postane samo pomakom prema dolje koji udari u pod - let tik iznad tla
	// ili udarac glavom u strop ne prekida let.
	if (!bCreativeFlying || !bFlyMovingDown || MovementMode != MOVE_Flying)
	{
		return;
	}

	FFindFloorResult Floor;
	FindFloor(UpdatedComponent->GetComponentLocation(), Floor, false);
	if (Floor.IsWalkableFloor() && Floor.FloorDist <= MAX_FLOOR_DIST)
	{
		SetMovementMode(MOVE_Walking);
	}
}
