#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ItemType.h"
#include "ItemDrop.generated.h"

class USphereComponent;
class UProceduralMeshComponent;
class UBlockRegistry;

UCLASS()
class MINECRAFTCLONE_API AItemDrop : public AActor
{
	GENERATED_BODY()

public:
	AItemDrop();

	virtual void Tick(float DeltaTime) override;

	/** Mesh komponenta - postavlja se u BP */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ItemDrop")
	UStaticMeshComponent* MeshComponent;

	/** Ekstrudirani 3D mesh za sprite iteme (Minecraft stil) - skriven za blok dropove */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "ItemDrop")
	UProceduralMeshComponent* ExtrudedMeshComponent;

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

	// === REGISTRY INITIALIZATION ===

	/** Inicijaliziraj item drop iz registry-a (postavlja mesh, materijal, ItemType) */
	UFUNCTION(BlueprintCallable, Category = "ItemDrop")
	void InitializeFromRegistry(EItemType Type);

	/** Statički helper za spawn item dropa - koristi registry */
	UFUNCTION(BlueprintCallable, Category = "ItemDrop", meta = (WorldContext = "WorldContextObject"))
	static AItemDrop* SpawnItemDrop(const UObject* WorldContextObject, EItemType Type, FVector Location);

protected:
	virtual void BeginPlay() override;

	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

private:
	float CurrentLifetime;

	/** Je li mesh flat sprite quad (display type "sprite") umjesto mini kocke */
	bool bIsSpriteDrop = false;

	/** Je li aktivan ekstrudirani proceduralni mesh (sprite item s uspjesnom ekstruzijom) */
	bool bIsExtrudedDrop = false;

	/** Akumulirani yaw za vrtnju uspravnog sprite mesha oko vertikalne osi */
	float SpriteSpinYaw = 0.0f;
};
