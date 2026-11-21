#include "Block.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "VoxelWorld.h"
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
}

void ABlock::BeginPlay()
{
	Super::BeginPlay();
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

void ABlock::UpdateVisibility()
{
	// Actor uvijek predstavlja cvrst blok - Air znaci da actor ne postoji
	// (unisteni blokovi se Destroy()-aju kroz AVoxelWorld::NotifyBlockDestroyed)
	if (MeshComponent)
	{
		MeshComponent->SetVisibility(true);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	}
}

void ABlock::InitializeFromRegistry(EBlockType Type, const FBlockDefinition& BlockDefinition,
	UStaticMesh* Mesh, UMaterialInterface* Material)
{
	// Kopanje i drop cita UBlockMiningComponent izravno iz registryja
	BlockType = Type;

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
	}

	UpdateVisibility();
}
