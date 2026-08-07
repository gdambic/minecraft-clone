#include "Block.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "ItemDrop.h"
#include "VoxelWorld.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"

ABlock::ABlock()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	RootComponent = MeshComponent;

	/*static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		MeshComponent->SetStaticMesh(CubeMesh.Object);
	}*/

	// VAŽNO: Omogući da blokovi utječu na NavMesh
	MeshComponent->SetCanEverAffectNavigation(true);

	// Default vrijednosti - mogu se override-ati kroz InitializeFromRegistry()
	BlockType = EBlockType::Air;
	GridPosition = FIntVector(0, 0, 0);

	// Default destruction time
	TimeToDestroy = 1.5f;

	// Default drop
	DropItemType = EItemType::None;
	DropChance = 1.0f;
}

void ABlock::BeginPlay()
{
	Super::BeginPlay();

	// Spremi originalni materijal za highlight swap
	if (MeshComponent)
	{
		OriginalMaterial = MeshComponent->GetMaterial(0);
	}

	//UpdateVisibility();
}

void ABlock::SetBlockType(EBlockType NewType)
{
	BlockType = NewType;
	UpdateVisibility();
}

void ABlock::SetGridPosition(FIntVector NewPosition)
{
	GridPosition = NewPosition;
}

void ABlock::SetHighlighted(bool bHighlight)
{
	if (!MeshComponent || bIsHighlighted == bHighlight)
	{
		return;
	}

	bIsHighlighted = bHighlight;

	if (bHighlight && HighlightMaterial)
	{
		MeshComponent->SetMaterial(0, HighlightMaterial);
	}
	else if (!bHighlight && OriginalMaterial)
	{
		MeshComponent->SetMaterial(0, OriginalMaterial);
	}
}

void ABlock::UpdateVisibility()
{
	if (MeshComponent)
	{
		bool bShouldBeVisible = (BlockType != EBlockType::Air);
		MeshComponent->SetVisibility(bShouldBeVisible);
		MeshComponent->SetCollisionEnabled(bShouldBeVisible ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	}
}

bool ABlock::AddDestroyProgress(float DeltaTime)
{
	if (TimeToDestroy <= 0.0f)
	{
		return false;
	}

	DestroyProgress += DeltaTime / TimeToDestroy;

	// Dohvati trenutni integrity
	int32 CurrentIntegrity = GetIntegrityPercent();

	// Ažuriraj LastReportedStep kad se integrity promijeni
	if (CurrentIntegrity != LastReportedStep)
	{
		LastReportedStep = CurrentIntegrity;
	}

	// Provjeri je li uništen
	if (DestroyProgress >= 1.0f)
	{
		// Dohvati VoxelWorld za decay notifikacije
		AVoxelWorld* VoxelWorld = nullptr;
		TArray<AActor*> VoxelWorlds;
		UGameplayStatics::GetAllActorsOfClass(GetWorld(), AVoxelWorld::StaticClass(), VoxelWorlds);
		if (VoxelWorlds.Num() > 0)
		{
			VoxelWorld = Cast<AVoxelWorld>(VoxelWorlds[0]);
		}

		// Ako je log, obavijesti VoxelWorld za leaf decay
		if (BlockType == EBlockType::OakLog || BlockType == EBlockType::BirchLog)
		{
			if (VoxelWorld)
			{
				VoxelWorld->OnLogDestroyed(GridPosition);
			}
		}
		// Ako je list, obavijesti susjede za kaskadni decay
		else if (BlockType == EBlockType::OakLeaves || BlockType == EBlockType::BirchLeaves)
		{
			if (VoxelWorld)
			{
				VoxelWorld->OnLeafDecayed(GridPosition);
			}
		}

		// Spawnaj drop prije nego što postane Air (ako prođe šansu)
		if (DropItemType != EItemType::None && FMath::FRand() < DropChance)
		{
			// Centriraj drop u središte bloka (origin je na kutu)
			const float HalfBlock = BlockSize / 2.0f;
			FVector SpawnLocation = GetActorLocation() + FVector(HalfBlock, HalfBlock, HalfBlock);
			AItemDrop::SpawnItemDrop(this, DropItemType, SpawnLocation);
		}

		SetBlockType(EBlockType::Air);
		return true;
	}

	return false;
}

void ABlock::ResetDestroyProgress()
{
	if (DestroyProgress > 0.0f)
	{
		DestroyProgress = 0.0f;
		LastReportedStep = 100;
	}
}

int32 ABlock::GetIntegrityPercent() const
{
	// 3 koraka: 100% -> 67% -> 33% -> 0%
	if (DestroyProgress < 0.33f)
	{
		return 100;
	}
	else if (DestroyProgress < 0.67f)
	{
		return 67;
	}
	else if (DestroyProgress < 1.0f)
	{
		return 33;
	}
	return 0;
}

void ABlock::InitializeFromRegistry(EBlockType Type, const FBlockDefinition& BlockDefinition,
	UStaticMesh* Mesh, UMaterialInterface* Material, UMaterialInterface* InHighlightMaterial)
{
	BlockType = Type;

	// Postavi gameplay vrijednosti
	TimeToDestroy = BlockDefinition.TimeToDestroy;
	DropItemType = BlockDefinition.DropItemType;
	DropChance = BlockDefinition.DropChance;

	// Postavi mesh
	if (Mesh && MeshComponent)
	{
		// Za kocke je Mesh jednak postojećem tako da SetStaticMesh odmah exita.
		// Kad budu neki drugi oblici u igri, onda će ovo postaviti konkretan mesh.
		// Funkcija vraća bool koji veli je li mesh sad promijenjen.
		MeshComponent->SetStaticMesh(Mesh);
	}

	// Postavi materijal
	if (Material && MeshComponent)
	{
		MeshComponent->SetMaterial(0, Material);
		// Spremi kao OriginalMaterial za highlight swap
		OriginalMaterial = Material;
	}

	// Postavi highlight materijal
	if (InHighlightMaterial)
	{
		HighlightMaterial = InHighlightMaterial;
	}

	UpdateVisibility();
}
