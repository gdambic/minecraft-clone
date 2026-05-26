#include "Block.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "ItemDrop.h"
#include "VoxelWorld.h"
#include "Kismet/GameplayStatics.h"

ABlock::ABlock()
{
	PrimaryActorTick.bCanEverTick = false;

	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	RootComponent = MeshComponent;

	// Mesh, materijal i ostala svojstva se postavljaju kroz BP_Block u editoru

	BlockType = EBlockType::Dirt;
	GridPosition = FIntVector(0, 0, 0);

	// Default destruction time
	TimeToDestroy = 1.5f;

	// Default drop chance (100%)
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

	UpdateVisibility();
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
		if (DropClass && FMath::FRand() < DropChance)
		{
			// Centriraj drop u središte bloka (origin je na kutu)
			const float HalfBlock = BlockSize / 2.0f;
			FVector SpawnLocation = GetActorLocation() + FVector(HalfBlock, HalfBlock, HalfBlock);
			FActorSpawnParameters SpawnParams;
			GetWorld()->SpawnActor<AItemDrop>(DropClass, SpawnLocation, FRotator::ZeroRotator, SpawnParams);
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
