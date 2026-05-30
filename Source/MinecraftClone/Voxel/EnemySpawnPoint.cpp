#include "EnemySpawnPoint.h"
#include "EnemyBase.h"
#include "Components/BillboardComponent.h"

AEnemySpawnPoint::AEnemySpawnPoint()
{
	PrimaryActorTick.bCanEverTick = false;

	// Create billboard for editor visualization
	BillboardSprite = CreateDefaultSubobject<UBillboardComponent>(TEXT("BillboardSprite"));
	RootComponent = BillboardSprite;

	// Load a default sprite for the billboard
	static ConstructorHelpers::FObjectFinder<UTexture2D> SpawnIconFinder(
		TEXT("/Engine/EditorResources/S_Actor.S_Actor"));
	if (SpawnIconFinder.Succeeded())
	{
		BillboardSprite->SetSprite(SpawnIconFinder.Object);
	}

	// Only visible in editor
	BillboardSprite->bHiddenInGame = true;

#if WITH_EDITORONLY_DATA
	// Make it visible in editor
	bIsSpatiallyLoaded = false;
#endif
}

void AEnemySpawnPoint::BeginPlay()
{
	Super::BeginPlay();

	if (bSpawnOnBeginPlay && EnemyClass)
	{
		SpawnEnemy();
	}
}

AEnemyBase* AEnemySpawnPoint::SpawnEnemy()
{
	if (!EnemyClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("EnemySpawnPoint: No EnemyClass set!"));
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

	AEnemyBase* SpawnedEnemy = GetWorld()->SpawnActor<AEnemyBase>(
		EnemyClass,
		GetActorLocation(),
		GetActorRotation(),
		SpawnParams
	);

	if (SpawnedEnemy)
	{
		UE_LOG(LogTemp, Log, TEXT("EnemySpawnPoint: Spawned %s at (%0.f, %0.f, %0.f)"),
			*SpawnedEnemy->GetName(),
			GetActorLocation().X, GetActorLocation().Y, GetActorLocation().Z);
	}

	return SpawnedEnemy;
}
