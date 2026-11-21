#include "BlockCrackComponent.h"
#include "MinecraftClone.h"
#include "VoxelChunk.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const TCHAR* GCrackMeshPath = TEXT("/Engine/BasicShapes/Cube.Cube");
	const TCHAR* GCrackMaterialPath = TEXT("/Game/Blocks/Crack/M_BlockCrack.M_BlockCrack");

	const FName CrackTextureParam(TEXT("CrackTexture"));
	const FName CrackOpacityParam(TEXT("CrackOpacity"));

	// Minecraft pukotine crta s depth biasom preko geometrije bloka; mi kocku
	// malo napuhnemo da ne z-fighta s plohama bloka.
	TAutoConsoleVariable<float> CVarCrackInflate(
		TEXT("voxel.crack.inflate"), 0.1f,
		TEXT("Koliko je kocka pukotine veca od bloka, po strani (UU)."));

	TAutoConsoleVariable<float> CVarCrackOpacity(
		TEXT("voxel.crack.opacity"), 1.0f,
		TEXT("Jacina pukotina (0 = nevidljive, 1 = kao Minecraft)."));

	/** Engine kocka je 100 UU, centrirana na pivotu - isto kao blok */
	constexpr float CubeMeshSize = 100.0f;
}

UBlockCrackComponent::UBlockCrackComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

bool UBlockCrackComponent::EnsureAssets()
{
	if (bAssetsTried)
	{
		return bAssetsValid;
	}
	bAssetsTried = true;

	CubeMesh = LoadObject<UStaticMesh>(nullptr, GCrackMeshPath);
	CrackMaterial = LoadObject<UMaterialInterface>(nullptr, GCrackMaterialPath);
	for (int32 Stage = 0; Stage < NumStages; ++Stage)
	{
		StageTextures.Add(LoadObject<UTexture>(nullptr, *FString::Printf(TEXT("/Game/Blocks/Crack/T_DestroyStage_%d.T_DestroyStage_%d"), Stage, Stage)));
	}

	bAssetsValid = CubeMesh && CrackMaterial && !StageTextures.Contains(nullptr);
	if (!bAssetsValid)
	{
		UE_LOG(LogMinecraftClone, Warning,
			TEXT("[CRACK] nedostaju asseti pukotina (mesh=%d, materijal=%d, teksture=%d/%d) - "
				"kopanje radi bez pukotina. Pokreni Tools > MinecraftClone > Import Block Cracks."),
			CubeMesh != nullptr, CrackMaterial != nullptr,
			NumStages - StageTextures.FilterByPredicate([](const TObjectPtr<UTexture>& T) { return !T; }).Num(),
			NumStages);
	}
	return bAssetsValid;
}

void UBlockCrackComponent::SetCrack(uint32 BreakerId, FIntVector GridPos, FVector WorldLocation, int32 Stage)
{
	if (Stage < 0 || Stage >= NumStages)
	{
		RemoveCrack(BreakerId);
		return;
	}

	if (!EnsureAssets())
	{
		return;
	}

	FCrack* Crack = Cracks.Find(BreakerId);
	if (!Crack)
	{
		Crack = &Cracks.Add(BreakerId);

		if (Pool.Num() > 0)
		{
			const auto Pooled = Pool.Pop();
			Crack->Mesh = Pooled.Key;
			Crack->Material = Pooled.Value;
		}
		else
		{
			UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(GetOwner());
			Mesh->SetStaticMesh(CubeMesh);
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->SetCanEverAffectNavigation(false);
			Mesh->SetCastShadow(false);
			Mesh->SetUsingAbsoluteLocation(true);
			Mesh->SetUsingAbsoluteRotation(true);
			Mesh->SetUsingAbsoluteScale(true);
			Mesh->SetMobility(EComponentMobility::Movable);
			Mesh->SetupAttachment(GetOwner()->GetRootComponent());
			Mesh->RegisterComponent();

			UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(CrackMaterial, this);
			Mesh->SetMaterial(0, Material);

			Crack->Mesh = Mesh;
			Crack->Material = Material;
			OwnedObjects.Add(Mesh);
			OwnedObjects.Add(Material);
		}
	}

	if (Crack->Stage != Stage || Crack->GridPos != GridPos)
	{
		Crack->Material->SetTextureParameterValue(CrackTextureParam, StageTextures[Stage]);
	}
	Crack->GridPos = GridPos;
	Crack->Stage = Stage;

	ApplyAppearance(*Crack, WorldLocation);
	Crack->Mesh->SetVisibility(true);
}

void UBlockCrackComponent::ApplyAppearance(FCrack& Crack, FVector WorldLocation) const
{
	const float Size = CubeMeshSize + 2.0f * FMath::Max(0.0f, CVarCrackInflate.GetValueOnGameThread());
	Crack.Mesh->SetWorldLocationAndRotation(WorldLocation, FRotator::ZeroRotator);
	Crack.Mesh->SetWorldScale3D(FVector(Size / CubeMeshSize));
	Crack.Material->SetScalarParameterValue(CrackOpacityParam,
		FMath::Clamp(CVarCrackOpacity.GetValueOnGameThread(), 0.0f, 1.0f));
}

void UBlockCrackComponent::RemoveCrack(uint32 BreakerId)
{
	FCrack Crack;
	if (!Cracks.RemoveAndCopyValue(BreakerId, Crack))
	{
		return;
	}

	if (Crack.Mesh)
	{
		Crack.Mesh->SetVisibility(false);
		Pool.Emplace(Crack.Mesh, Crack.Material);
	}
}

void UBlockCrackComponent::ClearCracksAt(FIntVector GridPos)
{
	TArray<uint32> ToRemove;
	for (const TPair<uint32, FCrack>& Pair : Cracks)
	{
		if (Pair.Value.GridPos == GridPos)
		{
			ToRemove.Add(Pair.Key);
		}
	}
	for (uint32 BreakerId : ToRemove)
	{
		RemoveCrack(BreakerId);
	}
}

void UBlockCrackComponent::ClearCracksInChunk(FIntPoint ChunkCoord)
{
	TArray<uint32> ToRemove;
	for (const TPair<uint32, FCrack>& Pair : Cracks)
	{
		if (VoxelGrid::GridToChunk(Pair.Value.GridPos) == ChunkCoord)
		{
			ToRemove.Add(Pair.Key);
		}
	}
	for (uint32 BreakerId : ToRemove)
	{
		RemoveCrack(BreakerId);
	}
}

int32 UBlockCrackComponent::GetDisplayedStage(uint32 BreakerId) const
{
	const FCrack* Crack = Cracks.Find(BreakerId);
	return Crack ? Crack->Stage : -1;
}
