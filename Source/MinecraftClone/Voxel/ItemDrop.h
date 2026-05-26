#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ItemType.h"
#include "ItemDrop.generated.h"

class USphereComponent;

UCLASS(Abstract)
class MINECRAFTCLONE_API AItemDrop : public AActor
{
	GENERATED_BODY()

public:
	AItemDrop();

	virtual void Tick(float DeltaTime) override;

	/** Mesh komponenta - postavlja se u BP */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ItemDrop")
	UStaticMeshComponent* MeshComponent;

	/** Collision sphere za pickup */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ItemDrop")
	USphereComponent* PickupCollision;

	/** Tip itema - postavlja se u BP */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ItemDrop")
	EItemType ItemType;

	/** Vrijeme do nestanka (sekunde) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ItemDrop")
	float DespawnTime;

	/** Brzina rotacije (stupnjevi/sekunda) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ItemDrop")
	float RotationSpeed;

	/** Radius za automatski pickup */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ItemDrop")
	float PickupRadius;

	/** Brzina padanja (UE jedinica/sekunda) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ItemDrop")
	float FallSpeed;

	/** Je li item na tlu */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ItemDrop")
	bool bIsGrounded;

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

private:
	float CurrentLifetime;
};
