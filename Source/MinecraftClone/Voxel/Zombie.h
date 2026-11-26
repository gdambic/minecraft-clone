#pragma once

#include "CoreMinimal.h"
#include "EnemyBase.h"
#include "Zombie.generated.h"

class UAnimSequenceBase;
class UHeadLookComponent;
class UMaterialInstanceDynamic;

/**
 * Zombie enemy - basic melee attacker.
 * Follows Minecraft zombie behavior:
 * - Detection range: 35 blocks (3500 units)
 * - Attack cooldown: 2 seconds
 * - Random wandering when idle
 *
 * Model, animacije i ABP su asseti (Docs/PLAN_Zombie.md); ovdje se racuna
 * sve sto ih pokrece: Minecraft walk animation state (walkPos/walkSpeed po
 * ticku), agresivnost (ima metu), napad, crveni tint ozljede i smrt.
 * UZombieAnimInstance to cita svaki frame i predaje AnimGraphu.
 */
UCLASS()
class MINECRAFTCLONE_API AZombie : public AEnemyBase
{
	GENERATED_BODY()

public:
	AZombie();

	virtual void Tick(float DeltaTime) override;

	// ==================== Components ====================

	/** Glances at the player while wandering, stares at the target while chasing */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Zombie|HeadLook")
	UHeadLookComponent* HeadLook;

	// ==================== Animation ====================

	/** A_Zombie_Attack - svira se u slotu UpperBodySlot */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Zombie|Animation")
	UAnimSequenceBase* AttackAnimation;

	/** A_Zombie_Death - svira se u slotu FullBodySlot i ostaje na zadnjem frameu */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Zombie|Animation")
	UAnimSequenceBase* DeathAnimation;

	/** Slot u ABP-u koji pokriva trup i ruke (noge nastavljaju hodati) */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Zombie|Animation")
	FName UpperBodySlot = TEXT("UpperBody");

	/** Slot u ABP-u preko cijelog tijela */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Zombie|Animation")
	FName FullBodySlot = TEXT("FullBody");

	/** Is zombie currently playing attack animation */
	UPROPERTY(BlueprintReadWrite, Category = "Animation")
	bool bIsAttacking = false;

	/** Perform melee attack - displays HIT message */
	virtual void PerformAttack() override;

	/** Called by Animation Notify to reset attack state */
	UFUNCTION(BlueprintCallable, Category = "Animation")
	void OnAttackAnimationEnd();

	virtual bool TakeDamage(float DamageAmount) override;
	virtual void OnDeath() override;

	// ==================== Stanje za AnimInstance ====================

	/** Minecraft walkSpeed (0..1) interpoliran unutar ticka - tezina hoda */
	float GetWalkAmount() const;

	/** Vrijeme u A_Zombie_Walk (0..WalkLoopSeconds) za trenutni walkPos */
	float GetWalkCycleTime() const;

	/** Ima metu - ruke podignute za 120 umjesto 80 stupnjeva */
	bool IsAggressive() const { return bAggressive; }

	bool IsDead() const { return bDead; }

	/** voxel.zombietest: jedna [ZOMBIE] linija sa stanjem i kutovima kostiju */
	void LogTestState(const TCHAR* Label) const;

	/** Minecraft tick (20 Hz) */
	static constexpr float TickSeconds = 1.0f / 20.0f;

	/** Trajanje A_Zombie_Walk: jedan period hoda (build_zombie_blender.py) */
	static constexpr float WalkLoopSeconds = 0.5f;

protected:
	virtual void BeginPlay() override;

private:
	/** LivingEntity.calculateEntityAnimation + WalkAnimationState.update za jedan tick */
	void StepWalkAnimation();

	/** Meta iz blackboarda (TargetActor), nullptr ako je nema */
	AActor* FindTarget() const;

	void SetHurtTint(float Amount);

	// Minecraft WalkAnimationState: speedOld, speed, position
	float WalkSpeedOld = 0.0f;
	float WalkSpeed01 = 0.0f;
	float WalkPos = 0.0f;
	float TickAccumulator = 0.0f;
	FVector LastTickLocation = FVector::ZeroVector;

	bool bAggressive = false;
	bool bDead = false;

	/** World time kad prestaje crveni tint ozljede */
	float HurtTintEndTime = -1.0f;
	float CurrentHurtTint = 0.0f;

	/** World time kad zamah napada zavrsava */
	float AttackEndTime = -1.0f;

	UPROPERTY()
	UMaterialInstanceDynamic* BodyMaterial = nullptr;
};
