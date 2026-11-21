#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ItemType.h"
#include "ItemDrop.generated.h"

class USphereComponent;
class UProceduralMeshComponent;
class UBlockRegistry;
class ACharacter;

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

	/** Vrijeme od nastanka do mogucnosti pickupa (vanilla: 10 tickova za drop iz bloka) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ItemDrop")
	float PickupDelay;

	/** Pickup zona = hitbox igraca prosiren ovoliko vodoravno (vanilla: 1 blok) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ItemDrop")
	float PickupReachHorizontal;

	/** Pickup zona = hitbox igraca prosiren ovoliko gore i dolje (vanilla: 0.5 bloka) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "ItemDrop")
	float PickupReachVertical;

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

private:
	/** Je li igrac u pickup zoni i ima li mjesta u inventaru - ako da, krene let */
	void TryPickup();

	/** Pomakni drop prema igracu; vraca true kad je let gotov */
	bool TickPickupFlight(float DeltaTime);

	float CurrentLifetime;

	/** Pola visine vidljivog mesha - drop lezi toliko (+ razmak) iznad tla */
	float HalfVisualHeight = 12.5f;

	/** Igrac koji je pokupio drop; drop leti prema njemu i nestaje */
	TWeakObjectPtr<ACharacter> PickupTarget;
	FVector PickupStartLocation = FVector::ZeroVector;
	float PickupFlightTime = 0.0f;

	/** Je li mesh flat sprite quad (display type "sprite") umjesto mini kocke */
	bool bIsSpriteDrop = false;

	/** Je li aktivan ekstrudirani proceduralni mesh (sprite item s uspjesnom ekstruzijom) */
	bool bIsExtrudedDrop = false;

	/** Akumulirani yaw za vrtnju uspravnog sprite mesha oko vertikalne osi */
	float SpriteSpinYaw = 0.0f;
};
