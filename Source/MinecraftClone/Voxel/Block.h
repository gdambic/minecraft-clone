#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BlockType.h"
#include "ItemType.h"
#include "BlockDefinition.h"
#include "Block.generated.h"

class UBlockRegistry;

UCLASS()
class MINECRAFTCLONE_API ABlock : public AActor
{
	GENERATED_BODY()

public:
	/** Veličina bloka u UE jedinicama */
	static constexpr float BlockSize = 100.0f;

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

	// === REGISTRY INITIALIZATION ===

	/**
	 * Inicijaliziraj blok iz definicije i unaprijed razrijesenih asseta.
	 * Mesh/materijali dolaze kao gotovi pointeri (razrijeseni jednom u
	 * AVoxelWorld::BuildBlockAssetCache) - bez TryLoad-a po bloku.
	 */
	UFUNCTION(BlueprintCallable, Category = "Block")
	void InitializeFromRegistry(EBlockType Type, const FBlockDefinition& BlockDefinition,
		UStaticMesh* Mesh, UMaterialInterface* Material);

protected:
	virtual void BeginPlay() override;

private:
	void UpdateVisibility();
};
