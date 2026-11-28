#include "BlockLightReceiverComponent.h"
#include "VoxelWorld.h"
#include "Components/MeshComponent.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	const FName GLightPosParam(TEXT("LightFallbackPos"));
	const FName GLightNegParam(TEXT("LightFallbackNeg"));
}

UBlockLightReceiverComponent::UBlockLightReceiverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Svjetlo se mijenja kad vlasnik prijede u drugu celiju ili se promijeni
	// baklja u blizini; 10 puta u sekundi je dovoljno i za jedno i za drugo
	PrimaryComponentTick.TickInterval = 0.1f;
}

void UBlockLightReceiverComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdateNow();
}

FVector UBlockLightReceiverComponent::GetProbeLocation() const
{
	const AActor* Owner = GetOwner();
	if (const APawn* Pawn = Cast<APawn>(Owner))
	{
		return Pawn->GetPawnViewLocation();
	}
	return Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
}

void UBlockLightReceiverComponent::UpdateNow()
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}
	if (!VoxelWorld.IsValid())
	{
		for (TActorIterator<AVoxelWorld> It(GetWorld()); It; ++It)
		{
			VoxelWorld = *It;
			break;
		}
		if (!VoxelWorld.IsValid())
		{
			return;
		}
	}

	Brightness = VoxelWorld->GetBlockLightBrightnessAt(GetProbeLocation());
	const FLinearColor Light(Brightness, Brightness, Brightness);
	LitMaterials = 0;
	UnlitMaterials = 0;

	TInlineComponentArray<UMeshComponent*> Meshes(Owner);
	for (UMeshComponent* Mesh : Meshes)
	{
		for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); Slot++)
		{
			UMaterialInterface* Material = Mesh->GetMaterial(Slot);
			if (!Material)
			{
				continue;
			}

			// Materijal bez parametra svjetla se ne dira (ne stvara se instanca bez svrhe)
			FLinearColor Existing;
			if (!Material->GetVectorParameterValue(FHashedMaterialParameterInfo(GLightPosParam), Existing))
			{
				UnlitMaterials++;
				continue;
			}
			LitMaterials++;

			// Parametar se pise u dinamicku instancu koja pripada ovom vlasniku.
			// Tudu (npr. zajednicku iz registryja) zamijeni vlastita kopija, da
			// svjetlo jednog vlasnika ne oboji drugoga.
			UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Material);
			const UObject* Outer = MID ? MID->GetOuter() : nullptr;
			const UActorComponent* OuterComponent = Cast<UActorComponent>(Outer);
			const bool bOwnInstance = MID && (Outer == Owner || (OuterComponent && OuterComponent->GetOwner() == Owner));
			if (!bOwnInstance)
			{
				UMaterialInstanceDynamic* Own = UMaterialInstanceDynamic::Create(MID ? MID->Parent.Get() : Material, Mesh);
				if (MID)
				{
					Own->CopyParameterOverrides(MID);
				}
				Mesh->SetMaterial(Slot, Own);
				MID = Own;
			}

			MID->SetVectorParameterValue(GLightPosParam, Light);
			MID->SetVectorParameterValue(GLightNegParam, Light);
		}
	}
}
