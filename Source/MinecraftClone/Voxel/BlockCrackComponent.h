#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BlockCrackComponent.generated.h"

class UStaticMesh;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UTexture;

/**
 * Crta Minecraft pukotine (destroy_stage_0..9) preko blokova koji se kopaju.
 * Docs/PLAN_BlockDamage.md.
 *
 * Zivi na AVoxelWorld. Svaki kopac (BreakerId) ima najvise jednu pukotinu:
 * kocku malo vecu od bloka s M_BlockCrack materijalom, postavljenu na grid
 * poziciju. Ne ovisi o tome je li blok ISM instanca ili promovirani actor, pa
 * ce isto raditi i za blokove koje kopaju drugi igraci.
 *
 * Materijal i teksture uvozi Scripts/import_block_cracks.py.
 */
UCLASS()
class MINECRAFTCLONE_API UBlockCrackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBlockCrackComponent();

	static constexpr int32 NumStages = 10;

	/**
	 * Postavi pukotinu kopaca na blok. Stage 0..9 je faza, -1 brise pukotinu.
	 * WorldLocation je sredina bloka (GridToWorld).
	 */
	void SetCrack(uint32 BreakerId, FIntVector GridPos, FVector WorldLocation, int32 Stage);

	/** Obrisi sve pukotine na poziciji (blok je unisten ili chunk unloadan) */
	void ClearCracksAt(FIntVector GridPos);

	/** Obrisi sve pukotine unutar chunka */
	void ClearCracksInChunk(FIntPoint ChunkCoord);

	/** Faza koja se trenutno prikazuje za kopaca (-1 = nema pukotine) */
	int32 GetDisplayedStage(uint32 BreakerId) const;

private:
	struct FCrack
	{
		FIntVector GridPos;
		int32 Stage = -1;
		TObjectPtr<UStaticMeshComponent> Mesh;
		TObjectPtr<UMaterialInstanceDynamic> Material;
	};

	/** Ucitaj mesh, materijal i teksture jednom; false ako nesto nedostaje */
	bool EnsureAssets();

	void RemoveCrack(uint32 BreakerId);

	/** Primijeni voxel.crack.inflate / opacity na postojecu pukotinu */
	void ApplyAppearance(FCrack& Crack, FVector WorldLocation) const;

	TMap<uint32, FCrack> Cracks;

	/** Skrivene komponente spremne za ponovnu upotrebu */
	TArray<TPair<TObjectPtr<UStaticMeshComponent>, TObjectPtr<UMaterialInstanceDynamic>>> Pool;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInterface> CrackMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UTexture>> StageTextures;

	/** Komponente pukotina - drzi ih zivima za GC (FCrack/Pool nisu UPROPERTY) */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UObject>> OwnedObjects;

	bool bAssetsTried = false;
	bool bAssetsValid = false;
};
