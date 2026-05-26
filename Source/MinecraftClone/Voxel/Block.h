#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BlockType.h"
#include "Block.generated.h"

class AItemDrop;

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

	/** Highlight materijal - postavlja se u BP_Block */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|Highlight")
	UMaterialInterface* HighlightMaterial;

	/** Uključi/isključi highlight na bloku */
	UFUNCTION(BlueprintCallable, Category = "Block")
	void SetHighlighted(bool bHighlight);

	/** Je li blok trenutno uključen */
	UFUNCTION(BlueprintPure, Category = "Block")
	bool IsHighlighted() const { return bIsHighlighted; }

	// === DESTRUCTION ===

	/** Vrijeme potrebno za uništenje bloka (sekunde) - postavlja se u BP_Block */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|Destruction")
	float TimeToDestroy;

	// === DROP ===

	/** Blueprint klasa dropa koji se spawna kad se blok uništi - postavlja se u BP */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|Drop")
	TSubclassOf<AItemDrop> DropClass;

	/** Šansa za drop (0.0-1.0), default 1.0 = uvijek */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block|Drop", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DropChance;

	/** Dodaj progress uništavanju, vraća true ako je blok uništen */
	UFUNCTION(BlueprintCallable, Category = "Block")
	bool AddDestroyProgress(float DeltaTime);

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

	UPROPERTY()
	UMaterialInterface* OriginalMaterial;

	bool bIsHighlighted = false;

	// Destruction state
	float DestroyProgress = 0.0f;
	int32 LastReportedStep = 100; // Prati zadnji ispisan integrity (100, 67, 33, 0)
};
