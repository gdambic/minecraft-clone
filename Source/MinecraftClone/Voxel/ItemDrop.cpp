#include "ItemDrop.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "FirstPersonCharacter.h"
#include "InventoryComponent.h"
#include "Engine/Engine.h"
#include "Block.h"
#include "BlockType.h"

AItemDrop::AItemDrop()
{
	PrimaryActorTick.bCanEverTick = true;

	// Default vrijednosti
	ItemType = EItemType::None;
	DespawnTime = 300.0f; // 5 minuta
	RotationSpeed = 90.0f; // stupnjeva po sekundi
	PickupRadius = 150.0f; // 1.5 bloka
	FallSpeed = 400.0f; // 4 bloka po sekundi
	bIsGrounded = false;
	CurrentLifetime = 0.0f;

	// Pickup collision sphere (root)
	PickupCollision = CreateDefaultSubobject<USphereComponent>(TEXT("PickupCollision"));
	RootComponent = PickupCollision;
	PickupCollision->InitSphereRadius(PickupRadius);
	PickupCollision->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	PickupCollision->SetGenerateOverlapEvents(true);

	// Mesh komponenta
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(RootComponent);
	MeshComponent->SetRelativeScale3D(FVector(0.1f)); // Mini verzija
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
}

void AItemDrop::BeginPlay()
{
	Super::BeginPlay();

	// Bind overlap event
	PickupCollision->OnComponentBeginOverlap.AddDynamic(this, &AItemDrop::OnOverlapBegin);

	// Ažuriraj radius ako je promijenjen u BP
	PickupCollision->SetSphereRadius(PickupRadius);
}

void AItemDrop::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// Rotacija
	if (MeshComponent)
	{
		FRotator DeltaRotation(0.0f, RotationSpeed * DeltaTime, 0.0f);
		MeshComponent->AddRelativeRotation(DeltaRotation);
	}

	// Gravitacija - padaj dok ne udariš u tlo
	if (!bIsGrounded)
	{
		float FallDistance = FallSpeed * DeltaTime;
		FVector CurrentLocation = GetActorLocation();
		FVector NewLocation = CurrentLocation - FVector(0.0f, 0.0f, FallDistance);

		// Line trace prema dolje da provjerimo ima li tla
		FHitResult HitResult;
		FCollisionQueryParams QueryParams;
		QueryParams.AddIgnoredActor(this);

		// Koristi ObjectType query umjesto channel - traži WorldStatic i WorldDynamic objekte
		FCollisionObjectQueryParams ObjectQueryParams;
		ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldStatic);
		ObjectQueryParams.AddObjectTypesToQuery(ECC_WorldDynamic);

		bool bHit = GetWorld()->LineTraceSingleByObjectType(
			HitResult,
			CurrentLocation,
			CurrentLocation - FVector(0.0f, 0.0f, FallDistance + 20.0f), // Trace dalje od pada
			ObjectQueryParams,
			QueryParams
		);

		if (bHit)
		{
			// Provjeri je li udarili u Block koji nije Air
			ABlock* HitBlock = Cast<ABlock>(HitResult.GetActor());
			if (HitBlock && HitBlock->BlockType != EBlockType::Air)
			{
				// Izračunaj vrh bloka (block origin + BlockSize)
				float BlockTopZ = HitBlock->GetActorLocation().Z + ABlock::BlockSize;

				// Ako smo blizu vrha bloka, stani 20 cm iznad
				if (CurrentLocation.Z <= BlockTopZ + 20.0f)
				{
					SetActorLocation(FVector(CurrentLocation.X, CurrentLocation.Y, BlockTopZ + 20.0f));
					bIsGrounded = true;
				}
				else
				{
					// Još nismo stigli do bloka, nastavi padati
					SetActorLocation(NewLocation);
				}
			}
			else
			{
				// Nije ABlock ili je Air - fallback za pod svijeta i druge površine
				float SurfaceZ = HitResult.ImpactPoint.Z;
				if (CurrentLocation.Z <= SurfaceZ + 20.0f)
				{
					SetActorLocation(FVector(CurrentLocation.X, CurrentLocation.Y, SurfaceZ + 20.0f));
					bIsGrounded = true;
				}
				else
				{
					SetActorLocation(NewLocation);
				}
			}
		}
		else
		{
			// Nema ničega ispod, nastavi padati
			SetActorLocation(NewLocation);

			// Ako padne predaleko ispod svijeta, uništi
			if (NewLocation.Z < -1000.0f)
			{
				Destroy();
				return;
			}
		}
	}

	// Despawn timer
	CurrentLifetime += DeltaTime;
	if (CurrentLifetime >= DespawnTime)
	{
		Destroy();
	}
}

void AItemDrop::OnOverlapBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	AFirstPersonCharacter* Player = Cast<AFirstPersonCharacter>(OtherActor);
	if (Player)
	{
		UInventoryComponent* Inventory = Player->FindComponentByClass<UInventoryComponent>();
		if (Inventory)
		{
			Inventory->AddItem(ItemType, 1);

			Destroy();
		}
	}
}
