#include "Pig.h"
#include "HeadLookComponent.h"
#include "ItemDrop.h"
#include "ItemType.h"

APig::APig()
{
	// Minecraft-accurate values
	MaxHealth = 10.0f;        // 5 hearts
	WalkSpeed = 150.0f;       // Slightly faster than sheep
	WanderRadius = 500.0f;

	// Flee behavior
	FleeDistance = 800.0f;          // 8 blocks
	ThreatDetectionRange = 300.0f;  // 3 blocks

	HeadLook = CreateDefaultSubobject<UHeadLookComponent>(TEXT("HeadLook"));
}

void APig::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// A fleeing pig has no time for curiosity
	HeadLook->SetSuppressed(GetCurrentThreat() != nullptr);
}

void APig::OnDeath()
{
	// Drop porkchops before destroying
	DropPorkchops();

	// Call parent implementation (destroys actor)
	Super::OnDeath();
}

void APig::DropPorkchops()
{
	if (!GetWorld()) return;

	// Calculate random drop amount
	int32 DropCount = FMath::RandRange(PorkchopDropMin, PorkchopDropMax);

	FVector SpawnLocation = GetActorLocation();

	// Spawn porkchop drops
	for (int32 i = 0; i < DropCount; ++i)
	{
		// Add small random offset so items don't stack perfectly
		FVector Offset(
			FMath::RandRange(-30.0f, 30.0f),
			FMath::RandRange(-30.0f, 30.0f),
			50.0f  // Spawn slightly above ground
		);

		AItemDrop::SpawnItemDrop(this, EItemType::Porkchop, SpawnLocation + Offset);
	}

	UE_LOG(LogTemp, Log, TEXT("Pig: Dropped %d porkchops"), DropCount);
}
