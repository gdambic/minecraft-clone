#include "ItemDrop.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "ProceduralMeshComponent.h"
#include "ItemMeshExtruder.h"
#include "FirstPersonCharacter.h"
#include "InventoryComponent.h"
#include "Engine/Engine.h"
#include "Block.h"
#include "BlockType.h"
#include "BlockRegistry.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

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

	// Ekstrudirani mesh za sprite iteme - aktivira ga InitializeFromRegistry
	ExtrudedMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ExtrudedMeshComponent"));
	ExtrudedMeshComponent->SetupAttachment(RootComponent);
	ExtrudedMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ExtrudedMeshComponent->SetVisibility(false);
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
	if (bIsExtrudedDrop && ExtrudedMeshComponent)
	{
		// Uspravni ekstrudirani mesh (ploca u X-Z ravnini) vrti se oko
		// vertikalne osi kao Minecraft dropped item
		SpriteSpinYaw = FMath::Fmod(SpriteSpinYaw + RotationSpeed * DeltaTime, 360.0f);
		ExtrudedMeshComponent->SetRelativeRotation(FRotator(0.0f, SpriteSpinYaw, 0.0f));
	}
	else if (MeshComponent)
	{
		if (bIsSpriteDrop)
		{
			// Uspravan quad koji se vrti oko vertikalne osi - AddRelativeRotation
			// bi na pitchanom quadu vrtio oko njegove lokalne (horizontalne) osi
			SpriteSpinYaw = FMath::Fmod(SpriteSpinYaw + RotationSpeed * DeltaTime, 360.0f);
			MeshComponent->SetRelativeRotation(FRotator(90.0f, SpriteSpinYaw, 0.0f));
		}
		else
		{
			FRotator DeltaRotation(0.0f, RotationSpeed * DeltaTime, 0.0f);
			MeshComponent->AddRelativeRotation(DeltaRotation);
		}
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
					UE_LOG(LogTemp, Log, TEXT("ItemDrop: %s sletio na %s (blok)"),
						*StaticEnum<EItemType>()->GetNameStringByValue((int64)ItemType),
						*GetActorLocation().ToCompactString());
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
					UE_LOG(LogTemp, Log, TEXT("ItemDrop: %s sletio na %s (povrsina: %s)"),
						*StaticEnum<EItemType>()->GetNameStringByValue((int64)ItemType),
						*GetActorLocation().ToCompactString(),
						*GetNameSafe(HitResult.GetComponent()));
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
				UE_LOG(LogTemp, Warning, TEXT("ItemDrop: %s propao kroz svijet i unisten na %s"),
					*StaticEnum<EItemType>()->GetNameStringByValue((int64)ItemType),
					*NewLocation.ToCompactString());
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

void AItemDrop::InitializeFromRegistry(EItemType Type)
{
	ItemType = Type;

	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	if (!Registry)
	{
		UE_LOG(LogTemp, Warning, TEXT("ItemDrop::InitializeFromRegistry: Registry not found!"));
		return;
	}

	const FItemDefinition* ItemDef = Registry->GetItemDefinition(Type);
	if (!ItemDef)
	{
		UE_LOG(LogTemp, Warning, TEXT("ItemDrop::InitializeFromRegistry: No definition for item type %d"), (int32)Type);
		return;
	}

	// Item sa "sprite" prikazom (mac, alat...): ekstrudirani rotirajuci 3D
	// mesh (Minecraft stil), fallback uspravni quad ako ekstruzija ne uspije
	if (ItemDef->Display.Type == TEXT("sprite") && MeshComponent)
	{
		UTexture2D* SpriteTexture = Registry->GetItemIconTexture(Type);
		UMaterialInterface* SpriteMaterial = Registry->GetItemSpriteMaterial();
		if (SpriteTexture && SpriteMaterial)
		{
			UMaterialInstanceDynamic* SpriteMID = UMaterialInstanceDynamic::Create(SpriteMaterial, this);
			SpriteMID->SetTextureParameterValue(TEXT("SpriteTexture"), SpriteTexture);

			const FItemExtrudedMeshData* MeshData = Registry->GetItemExtrudedMesh(Type);
			if (MeshData && ExtrudedMeshComponent)
			{
				ExtrudedMeshComponent->CreateMeshSection(0, MeshData->Vertices,
					MeshData->Triangles, MeshData->Normals, MeshData->UVs,
					TArray<FColor>(), TArray<FProcMeshTangent>(), false);
				ExtrudedMeshComponent->SetMaterial(0, SpriteMID);
				ExtrudedMeshComponent->SetRelativeScale3D(FVector(0.25f)); // 25 UU
				ExtrudedMeshComponent->SetVisibility(true);
				MeshComponent->SetVisibility(false);
				bIsExtrudedDrop = true;
				UE_LOG(LogTemp, Log, TEXT("ItemDrop: ekstrudirani drop za %s na %s"),
					*StaticEnum<EItemType>()->GetNameStringByValue((int64)Type),
					*GetActorLocation().ToCompactString());
				return;
			}

			UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
			if (PlaneMesh)
			{
				MeshComponent->SetStaticMesh(PlaneMesh);
			}

			MeshComponent->SetMaterial(0, SpriteMID);
			MeshComponent->SetRelativeScale3D(FVector(0.25f)); // 25 UU sprite
			MeshComponent->SetRelativeRotation(FRotator(90.0f, 0.0f, 0.0f)); // uspravno
			bIsSpriteDrop = true;
			UE_LOG(LogTemp, Log, TEXT("ItemDrop: sprite drop za %s na %s"),
				*StaticEnum<EItemType>()->GetNameStringByValue((int64)Type),
				*GetActorLocation().ToCompactString());
			return;
		}
	}

	// Postavi mesh
	if (!ItemDef->Mesh.IsNull() && MeshComponent)
	{
		UStaticMesh* LoadedMesh = Cast<UStaticMesh>(ItemDef->Mesh.TryLoad());
		if (LoadedMesh)
		{
			MeshComponent->SetStaticMesh(LoadedMesh);
		}
	}

	// Postavi materijal
	if (!ItemDef->Material.IsNull() && MeshComponent)
	{
		UMaterialInterface* LoadedMaterial = Cast<UMaterialInterface>(ItemDef->Material.TryLoad());
		if (LoadedMaterial)
		{
			// Blok s biome tintom: drop nosi default tint (prvi biom), ne prati
			// biom u kojem lezi - isto pravilo kao ikona i item u ruci
			const FBlockDefinition* BlockDef = Registry->GetBlockForItem(Type);
			if (BlockDef && BlockDef->BiomeTint != EBiomeTintType::None)
			{
				UMaterialInstanceDynamic* TintedMID = UMaterialInstanceDynamic::Create(LoadedMaterial, this);
				TintedMID->SetVectorParameterValue(TEXT("TintFallback"),
					Registry->GetDefaultBiomeTint(BlockDef->BiomeTint));
				LoadedMaterial = TintedMID;
			}
			MeshComponent->SetMaterial(0, LoadedMaterial);
		}
	}
}

AItemDrop* AItemDrop::SpawnItemDrop(const UObject* WorldContextObject, EItemType Type, FVector Location)
{
	if (!WorldContextObject || Type == EItemType::None)
	{
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		return nullptr;
	}

	FActorSpawnParameters SpawnParams;
	AItemDrop* NewDrop = World->SpawnActor<AItemDrop>(AItemDrop::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);

	if (NewDrop)
	{
		NewDrop->InitializeFromRegistry(Type);
	}

	return NewDrop;
}
