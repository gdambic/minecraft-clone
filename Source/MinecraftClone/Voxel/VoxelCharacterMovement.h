#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "VoxelCharacterMovement.generated.h"

/**
 * Kretanje igraca s Minecraft letom (Docs/PLAN_Flying.md). Dok je let
 * ukljucen (SetCreativeFlying), MOVE_Flying racuna brzinu po vanilla formuli
 * umjesto UE ubrzanja/kocenja, a dodir tla pri spustanju prebaci lika u hod.
 * Bez ukljucenog leta (npr. swimtest lebdi s MOVE_Flying) vrijedi UE default.
 */
UCLASS()
class MINECRAFTCLONE_API UVoxelCharacterMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	/**
	 * Vanilla brzina leta: 0.05 bloka/tick dodaje se po ticku (input x0.98),
	 * trenje zraka 0.91/tick -> 0.98*0.05/0.09 bloka/tick = 1089 UU/s.
	 */
	UPROPERTY(EditAnywhere, Category = "Character Movement: Flying")
	float CreativeFlySpeed = 1088.9f;

	/** Sprint u letu udvostruci brzinu leta (Player.getFlyingSpeed) */
	UPROPERTY(EditAnywhere, Category = "Character Movement: Flying")
	float CreativeFlySprintMultiplier = 2.0f;

	/** Dizanje/spustanje: +-0.15 bloka/tick, okomito trenje 0.6/tick -> 0.375 bloka/tick = 750 UU/s */
	UPROPERTY(EditAnywhere, Category = "Character Movement: Flying")
	float CreativeFlyVerticalSpeed = 750.0f;

	/** Ukljuci/iskljuci Minecraft let (stanje leta drzi lik; ovo samo bira formulu) */
	void SetCreativeFlying(bool bFlying) { bCreativeFlying = bFlying; }
	bool IsCreativeFlying() const { return bCreativeFlying; }

	/** Input koji vanilla cita u LocalPlayer.aiStep: skok, Shift, sprint */
	void SetCreativeFlyInput(bool bUp, bool bDown, bool bSprint);

	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;

protected:
	virtual void PhysFlying(float DeltaTime, int32 Iterations) override;

private:
	bool bCreativeFlying = false;
	bool bFlyUp = false;
	bool bFlyDown = false;
	bool bFlySprint = false;

	/** Je li ovaj frame lik u letu htio prema dolje - samo tada dodir tla znaci slijetanje */
	bool bFlyMovingDown = false;
};
