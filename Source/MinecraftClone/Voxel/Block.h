#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BlockType.h"
#include "ItemType.h"
#include "BlockDefinition.h"
#include "ToolData.h"
#include "Block.generated.h"

class AItemDrop;
class UBlockRegistry;

UCLASS()
class MINECRAFTCLONE_API ABlock : public AActor
{
	GENERATED_BODY()

public:
	/** Veličina bloka u UE jedinicama */
	static constexpr float BlockSize = 100.0f;

	/**
	 * Koliko dulje traje kopanje bloka koji zahtijeva alat kad igrač nema
	 * pravi alat. Minecraft bazno vrijeme je hardness × 1.5 s pravim alatom
	 * i hardness × 5 bez njega - odnos tih faktora.
	 */
	static constexpr float WrongToolTimePenalty = 5.0f / 1.5f;

	ABlock();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block")
	UStaticMeshComponent* MeshComponent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	EBlockType BlockType;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Block")
	FIntVector GridPosition;

	UFUNCTION(BlueprintCallable, Category = "Block")
	void SetBlockType(EBlockType NewType);

	UFUNCTION(BlueprintCallable, Category = "Block")
	void SetGridPosition(FIntVector NewPosition);

	// === DESTRUCTION ===

	/** Vrijeme potrebno za uništenje bloka golom rukom (sekunde) - iz registryja */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|Destruction")
	float TimeToDestroy;

	/** Klasa alata koja ubrzava kopanje (None = nijedan alat ne pomaže) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|Destruction")
	EToolClass ToolClass;

	/** Zahtijeva li blok pravi alat da bi ostavio drop */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|Destruction")
	bool bRequiresTool;

	// === DROP ===

	/** Tip itema koji pada kad se blok uništi */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|Drop")
	EItemType DropItemType;

	/** Šansa za drop (0.0-1.0), default 1.0 = uvijek */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|Drop", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DropChance;

	// === REGISTRY INITIALIZATION ===

	/**
	 * Inicijaliziraj blok iz definicije i unaprijed razrijesenih asseta.
	 * Mesh/materijali dolaze kao gotovi pointeri (razrijeseni jednom u
	 * AVoxelWorld::BuildBlockAssetCache) - bez TryLoad-a po bloku.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block")
	void InitializeFromRegistry(EBlockType Type, const FBlockDefinition& BlockDefinition,
		UStaticMesh* Mesh, UMaterialInterface* Material);

	/**
	 * Dodaj progress uništavanju, vraća true ako je blok uništen.
	 * HeldItem je item u ruci kopača - pravi alat ubrzava kopanje, a na
	 * bloku s bRequiresTool njegov nedostatak usporava i gasi drop.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block")
	bool AddDestroyProgress(float DeltaTime, EItemType HeldItem = EItemType::None);

	/** Resetiraj progress uništavanja na 0 */
	UFUNCTION(BlueprintCallable, Category = "Block")
	void ResetDestroyProgress();

	/** Dohvati postotak integriteta (100, 67, 33, 0) */
	UFUNCTION(BlueprintPure, Category = "Block")
	int32 GetIntegrityPercent() const;

protected:
	virtual void BeginPlay() override;

private:
	void UpdateVisibility();

	// Destruction state
	float DestroyProgress = 0.0f;
	int32 LastReportedStep = 100; // Prati zadnji ispisan integrity (100, 67, 33, 0)
};
