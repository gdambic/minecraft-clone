#include "Sheep.h"
#include "ItemDrop.h"
#include "ItemType.h"

ASheep::ASheep()
{
	// Minecraft-accurate values
	MaxHealth = 8.0f;         // 4 hearts
	WalkSpeed = 140.0f;       // Slower than most mobs
	WanderRadius = 500.0f;

	// Flee behavior
	FleeDistance = 800.0f;          // 8 blocks
	ThreatDetectionRange = 300.0f;  // 3 blocks
}

void ASheep::OnDeath()
{
	// Drop wool before destroying
	DropWool();

	// Call parent implementation (destroys actor)
	Super::OnDeath();
}

void ASheep::DropWool()
{
	if (!GetWorld()) return;

	// Calculate random drop amount
	int32 DropCount = FMath::RandRange(WoolDropMin, WoolDropMax);

	FVector SpawnLocation = GetActorLocation();

	// Spawn wool drops
	for (int32 i = 0; i < DropCount; ++i)
	{
		// Add small random offset so items don't stack perfectly
		FVector Offset(
			FMath::RandRange(-30.0f, 30.0f),
			FMath::RandRange(-30.0f, 30.0f),
			50.0f  // Spawn slightly above ground
		);

		AItemDrop::SpawnItemDrop(this, EItemType::Wool, SpawnLocation + Offset);
	}

	UE_LOG(LogTemp, Log, TEXT("Sheep: Dropped %d wool"), DropCount);
}
