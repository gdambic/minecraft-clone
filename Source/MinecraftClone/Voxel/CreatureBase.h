#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "CreatureBase.generated.h"

class UBehaviorTree;
class UNavigationInvokerComponent;

/**
 * Abstract base class for all creatures (enemies and passive mobs).
 * Provides shared stats, movement, and AI properties.
 */
UCLASS(abstract)
class MINECRAFTCLONE_API ACreatureBase : public ACharacter
{
	GENERATED_BODY()

public:
	ACreatureBase();

	// ==================== Stats ====================

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature|Stats")
	float MaxHealth = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature|Stats")
	float CurrentHealth = 20.0f;

	// ==================== Knockback ====================

	/**
	 * Vanilla atribut knockback_resistance (0..1): koliko horizontalnog
	 * odbacivanja stvorenje odbija. 0 za vecinu mobova, 0.6 hoglin/zoglin,
	 * 0.75 ravager, 1.0 iron golem/warden.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature|Knockback")
	float KnockbackResistance = 0.0f;

	/**
	 * Vanilla atribut attack_knockback: dodatna jacina (blokova/tick) kojom OVO
	 * stvorenje odbacuje svoju zrtvu. 0 za vecinu, 1.0 hoglin/zoglin,
	 * 1.5 ravager/warden.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature|Knockback")
	float AttackKnockback = 0.0f;

	/** Nepovredivost nakon pogotka (vanilla 10 tickova = 0.5 s) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature|Knockback")
	float HurtImmunityTime = 0.5f;

	// ==================== Movement ====================

	/** Walk speed in Unreal units per second */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature|Movement")
	float WalkSpeed = 200.0f;

	/** Wander radius when idle */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Creature|Movement")
	float WanderRadius = 500.0f;

	// ==================== AI ====================

	/** Behavior tree asset for this creature type */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Creature|AI")
	UBehaviorTree* BehaviorTree;

	/**
	 * Navmesh se generira samo oko invokera (config:
	 * bGenerateNavigationOnlyAroundNavigationInvokers) - svaki mob nosi svoj
	 * "balon" navigacije, pa AI radi bilo gdje u svijetu bez globalnog navmesha.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Creature|AI")
	UNavigationInvokerComponent* NavInvoker;

	// ==================== Functions ====================

	/** Returns the behavior tree for this creature */
	UFUNCTION(BlueprintCallable, Category = "Creature|AI")
	UBehaviorTree* GetBehaviorTree() const { return BehaviorTree; }

	/** Returns wander radius */
	UFUNCTION(BlueprintCallable, Category = "Creature|Movement")
	float GetWanderRadius() const { return WanderRadius; }

	/** Called when creature dies - override in child classes */
	UFUNCTION(BlueprintCallable, Category = "Creature|Stats")
	virtual void OnDeath();

	/** Apply damage to creature, returns true if creature died */
	UFUNCTION(BlueprintCallable, Category = "Creature|Stats")
	virtual bool TakeDamage(float DamageAmount);

	/** Je li stvorenje van prozora nepovredivosti (vanilla: pogodak unutar njega ne radi nista) */
	UFUNCTION(BlueprintCallable, Category = "Creature|Stats")
	bool CanBeHurt() const;

	/**
	 * Odbaci stvorenje (jacina u blokovima/tick) i pauziraj mu AI dok ne
	 * doskoci - inace path following u istom ticku prepise brzinu natrag
	 * prema napadacu.
	 */
	UFUNCTION(BlueprintCallable, Category = "Creature|Knockback")
	virtual void ReceiveKnockback(FVector HorizontalDir, float StrengthBlocksPerTick);

	UFUNCTION(BlueprintCallable, Category = "Creature|Knockback")
	bool IsInKnockback() const { return bInKnockback; }

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	/** Vrati AI u normalu i zatvori knockback prozor */
	void EndKnockback();

	/** Vrijeme (world seconds) zadnjeg primljenog pogotka */
	float LastHurtTime = -1000.0f;

	/** Je li knockback prozor otvoren (AI pauziran, zracni otpor aktivan) */
	bool bInKnockback = false;

	/** Pocetak i tvrdi rok knockback prozora (world seconds) */
	float KnockbackStartTime = 0.0f;
	float KnockbackEndTime = 0.0f;

	/** Pozicija u trenutku odbacivanja - za mjerenje dometa u logu */
	FVector KnockbackStartLocation = FVector::ZeroVector;

	/**
	 * Tvrdi rok trajanja prozora. Doskok ga normalno zatvori ranije; rok
	 * postoji da mob koji zavrsi u zraku (ili umre u letu) ne ostane s
	 * pauziranim AI-em zauvijek.
	 */
	static constexpr float MaxKnockbackDuration = 1.5f;

	/**
	 * Prvih 0.1 s se doskok ne provjerava: LaunchCharacter prebacuje u
	 * MOVE_Falling tek u sljedecem movement updateu, pa bi IsMovingOnGround()
	 * odmah zatvorio prozor.
	 */
	static constexpr float KnockbackAirborneGrace = 0.1f;
};
