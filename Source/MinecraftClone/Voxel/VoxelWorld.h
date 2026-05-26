#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BlockType.h"
#include "TreeGenerator.h"
#include "VoxelWorld.generated.h"

class ABlock;

UCLASS()
class MINECRAFTCLONE_API AVoxelWorld : public AActor
{
	GENERATED_BODY()

public:
	AVoxelWorld();

	/** Blueprint klase za svaki tip bloka - konfigurira se u BP_VoxelWorld */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World")
	TMap<EBlockType, TSubclassOf<ABlock>> BlockClasses;

	/** Z nivo površine (Grass). Ispod je Stone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World")
	int32 SurfaceLevel;

	// Dimenzije svijeta
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World")
	int32 WorldSizeX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World")
	int32 WorldSizeY;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World")
	int32 WorldSizeZ;

	// Veličina jednog bloka u UE units
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World")
	float BlockSize;

	// Broj random blokova na vrhu
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World")
	int32 RandomBlockCount;

	// === TREES ===

	/** Broj stabala za generirati */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "World|Trees")
	int32 TreeCount;

	// === LEAF DECAY ===

	/** Poziva se kad se log uništi - pokreće provjeru decay-a obližnjeg lišća */
	UFUNCTION(BlueprintCallable, Category = "World")
	void OnLogDestroyed(FIntVector LogPosition);

	/** Poziva se kad list propadne - dodaje susjede u listu za provjeru */
	UFUNCTION(BlueprintCallable, Category = "World")
	void OnLeafDecayed(FIntVector LeafPosition);

	UFUNCTION(BlueprintCallable, Category = "World")
	ABlock* GetBlock(int32 X, int32 Y, int32 Z);

	UFUNCTION(BlueprintCallable, Category = "World")
	void SetBlockType(int32 X, int32 Y, int32 Z, EBlockType NewType);

	UFUNCTION(BlueprintCallable, Category = "World")
	FVector GridToWorld(int32 X, int32 Y, int32 Z);

	/** Postavi novi blok na zadanu grid poziciju */
	UFUNCTION(BlueprintCallable, Category = "World")
	ABlock* PlaceBlockAt(FIntVector GridPosition, EBlockType Type);

	/** Vraća listu svih tipova blokova koji se mogu postaviti (bez Air) */
	UFUNCTION(BlueprintCallable, Category = "World")
	TArray<EBlockType> GetPlaceableBlockTypes() const;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY()
	TMap<FIntVector, ABlock*> Blocks;

	void GenerateWorld();
	void SpawnBlock(int32 X, int32 Y, int32 Z, EBlockType Type);

	// === TREES ===
	void GenerateTrees();

	// === LEAF DECAY ===

	/** Lista lišća koje čeka provjeru decay-a */
	TArray<FIntVector> LeavesToCheck;

	/** Timer handle za procesiranje decay-a */
	FTimerHandle LeafDecayTimerHandle;

	/** Procesiraj pending decay provjere */
	void ProcessLeafDecay();

	/** Provjeri ima li lišće na poziciji vezu s logom */
	bool HasLogConnection(FIntVector LeafPosition) const;
};
