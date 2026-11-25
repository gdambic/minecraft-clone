#include "VoxelWaterVolume.h"
#include "Components/BoxComponent.h"
#include "Components/BrushComponent.h"

AVoxelWaterVolume::AVoxelWaterVolume(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	bWaterVolume = true;
	// Minecraft: u vodi se brzina svaki tick mnozi s 0.8, tj. gubi ln(1/0.8)*20
	// = 4.46 /s. PhysSwimming koci s 0.5 * FluidFriction (ImmersionDepth je
	// ovdje uvijek 1 jer brush nema geometriju), pa je FluidFriction 2 * 4.46.
	FluidFriction = 8.9f;
	// Kutija se pomice za igracem, pa root mora biti Movable (brush je Static)
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("WaterBox"));
	Box->SetMobility(EComponentMobility::Movable);
	Box->SetCollisionObjectType(ECC_WorldDynamic);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	// Samo likovi. Kapsula lika volumen trazi u svom popisu preklapanja
	// (UPrimitiveComponent::UpdatePhysicsVolume), a tamo dolaze samo komponente
	// s overlap eventima - bez njih lik kutiju nikad ne nade.
	Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
	Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Box->SetGenerateOverlapEvents(true);
	Box->SetCanEverAffectNavigation(false);
	Box->SetBoxExtent(FVector(50.f), false);
	RootComponent = Box;

	GetBrushComponent()->SetMobility(EComponentMobility::Movable);
	GetBrushComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	GetBrushComponent()->SetupAttachment(Box);
}

void AVoxelWaterVolume::SetWaterBox(const FVector& TopCenter, float HalfWidth, float Height, bool bActive)
{
	Box->SetBoxExtent(FVector(HalfWidth, HalfWidth, Height * 0.5f), false);
	SetActorLocation(TopCenter - FVector(0.f, 0.f, Height * 0.5f), false, nullptr, ETeleportType::TeleportPhysics);
	if (bActive != bWaterActive)
	{
		bWaterActive = bActive;
		Box->SetCollisionEnabled(bActive ? ECollisionEnabled::QueryOnly : ECollisionEnabled::NoCollision);
	}
}
