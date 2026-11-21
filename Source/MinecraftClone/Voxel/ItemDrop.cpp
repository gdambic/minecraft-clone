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
#include "Components/CapsuleComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"
#include "MinecraftClone.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace
{
	/** Vanilla entity.item.pickup = random/pop (uvozi Scripts/import_pickup_sound.py) */
	const TCHAR* GPickupSoundPath = TEXT("/Game/Audio/Items/Pickup_Pop.Pickup_Pop");

	// Minecraft wiki (Item entity): glasnoca 0.2, pitch 1.6-3.4
	constexpr float PickupSoundVolume = 0.2f;
	constexpr float PickupSoundPitchMin = 1.6f;
	constexpr float PickupSoundPitchMax = 3.4f;

	/** Vanilla ItemPickupParticle traje 3 ticka */
	constexpr float PickupFlightDuration = 0.15f;

	/** Hitbox item entiteta je 0.25 x 0.25 bloka */
	constexpr float ItemHalfExtent = 12.5f;

	/** Razmak izmedu dna mesha i tla */
	constexpr float GroundGap = 7.5f;
}

AItemDrop::AItemDrop()
{
	PrimaryActorTick.bCanEverTick = true;

	// Default vrijednosti
	ItemType = EItemType::None;
	DespawnTime = 300.0f; // 5 minuta
	RotationSpeed = 90.0f; // stupnjeva po sekundi
	PickupDelay = 0.5f; // 10 tickova
	PickupReachHorizontal = 100.0f; // 1 blok
	PickupReachVertical = 50.0f; // 0.5 bloka
	FallSpeed = 400.0f; // 4 bloka po sekundi
	bIsGrounded = false;
	CurrentLifetime = 0.0f;

	// Pickup collision sphere (root)
	PickupCollision = CreateDefaultSubobject<USphereComponent>(TEXT("PickupCollision"));
	RootComponent = PickupCollision;
	// Pickup ide kroz pickup zonu igraca u Ticku, ne kroz overlap - sfera je
	// samo root bez kolizije
	PickupCollision->InitSphereRadius(ItemHalfExtent);
	PickupCollision->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	PickupCollision->SetGenerateOverlapEvents(false);

	// Mesh komponenta
	MeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MeshComponent"));
	MeshComponent->SetupAttachment(RootComponent);
	MeshComponent->SetRelativeScale3D(FVector(0.25f)); // vanilla ground scale bloka (block.json)
	MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// Ekstrudirani mesh za sprite iteme - aktivira ga InitializeFromRegistry
	ExtrudedMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("ExtrudedMeshComponent"));
	ExtrudedMeshComponent->SetupAttachment(RootComponent);
	ExtrudedMeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	ExtrudedMeshComponent->SetVisibility(false);
}

void AItemDrop::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (PickupTarget.IsValid() || PickupFlightTime > 0.0f)
	{
		if (TickPickupFlight(DeltaTime))
		{
			Destroy();
		}
		return;
	}

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
		const float HoverHeight = HalfVisualHeight + GroundGap;
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

		// Pickup sfere drugih dropova su WorldDynamic pa ih ovaj trace vraca kao
		// pogodak - drop blizi od PickupRadius zaustavio bi pad u zraku. Zato
		// trazimo prvi pogodak koji nije drugi item drop.
		TArray<FHitResult> Hits;
		GetWorld()->LineTraceMultiByObjectType(
			Hits,
			CurrentLocation,
			CurrentLocation - FVector(0.0f, 0.0f, FallDistance + 20.0f), // Trace dalje od pada
			ObjectQueryParams,
			QueryParams
		);

		bool bHit = false;
		for (const FHitResult& Candidate : Hits)
		{
			if (!Candidate.GetActor() || !Candidate.GetActor()->IsA<AItemDrop>())
			{
				HitResult = Candidate;
				bHit = true;
				break;
			}
		}

		if (bHit)
		{
			// Provjeri je li udarili u Block koji nije Air
			ABlock* HitBlock = Cast<ABlock>(HitResult.GetActor());
			if (HitBlock && HitBlock->BlockType != EBlockType::Air)
			{
				// Vrh bloka: kocka je centrirana na pivotu actora
				float BlockTopZ = HitBlock->GetActorLocation().Z + ABlock::BlockSize * 0.5f;

				// Ako smo blizu vrha bloka, stani tako da dno mesha lebdi iznad njega
				if (CurrentLocation.Z <= BlockTopZ + HoverHeight)
				{
					SetActorLocation(FVector(CurrentLocation.X, CurrentLocation.Y, BlockTopZ + HoverHeight));
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
				if (CurrentLocation.Z <= SurfaceZ + HoverHeight)
				{
					SetActorLocation(FVector(CurrentLocation.X, CurrentLocation.Y, SurfaceZ + HoverHeight));
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
		return;
	}

	if (CurrentLifetime >= PickupDelay)
	{
		TryPickup();
	}
}

void AItemDrop::TryPickup()
{
	ACharacter* Player = UGameplayStatics::GetPlayerCharacter(this, 0);
	UCapsuleComponent* Capsule = Player ? Player->GetCapsuleComponent() : nullptr;
	if (!Capsule)
	{
		return;
	}

	// Vanilla: hitbox itema sijece hitbox igraca prosiren za 1 blok vodoravno
	// i 0.5 bloka gore/dolje. Kapsula se gleda kao kutija oko nje.
	const FVector Delta = GetActorLocation() - Player->GetActorLocation();
	const float ReachXY = Capsule->GetScaledCapsuleRadius() + PickupReachHorizontal + ItemHalfExtent;
	const float ReachZ = Capsule->GetScaledCapsuleHalfHeight() + PickupReachVertical + ItemHalfExtent;
	if (FMath::Abs(Delta.X) > ReachXY || FMath::Abs(Delta.Y) > ReachXY || FMath::Abs(Delta.Z) >= ReachZ)
	{
		return;
	}

	UInventoryComponent* Inventory = Player->FindComponentByClass<UInventoryComponent>();
	if (!Inventory || Inventory->AddItem(ItemType, 1) > 0)
	{
		// Pun inventar - drop ostaje lezati
		return;
	}

	if (USoundBase* Sound = LoadObject<USoundBase>(nullptr, GPickupSoundPath))
	{
		UBlockRegistry* Registry = UBlockRegistry::Get(this);
		UGameplayStatics::PlaySoundAtLocation(this, Sound, GetActorLocation(), PickupSoundVolume,
			FMath::FRandRange(PickupSoundPitchMin, PickupSoundPitchMax), 0.0f,
			Registry ? Registry->GetBlockSoundAttenuation() : nullptr);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ItemDrop: nema pickup zvuka %s"), GPickupSoundPath);
	}

	UE_LOG(LogMinecraftClone, Log, TEXT("[PICKUP] %s pokupljen nakon %.2f s, udaljenost %.0f"),
		*StaticEnum<EItemType>()->GetNameStringByValue((int64)ItemType),
		CurrentLifetime, Delta.Size());

	PickupTarget = Player;
	PickupStartLocation = GetActorLocation();
	PickupFlightTime = KINDA_SMALL_NUMBER;
}

bool AItemDrop::TickPickupFlight(float DeltaTime)
{
	ACharacter* Player = PickupTarget.Get();
	if (!Player)
	{
		return true;
	}

	// Vanilla ItemPickupParticle: item ubrzano (kvadratno) klizi prema tocki
	// izmedu stopala i ociju igraca, koja se pomice s igracem
	PickupFlightTime += DeltaTime;
	const float Alpha = FMath::Square(FMath::Clamp(PickupFlightTime / PickupFlightDuration, 0.0f, 1.0f));

	const float FeetZ = Player->GetActorLocation().Z - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FVector Target = Player->GetActorLocation();
	Target.Z = (FeetZ + Player->GetPawnViewLocation().Z) * 0.5f;

	SetActorLocation(FMath::Lerp(PickupStartLocation, Target, Alpha));
	if (Alpha >= 1.0f)
	{
		UE_LOG(LogMinecraftClone, Log, TEXT("[PICKUP] %s let gotov nakon %.3f s, do igraca %.0f"),
			*StaticEnum<EItemType>()->GetNameStringByValue((int64)ItemType),
			PickupFlightTime, FVector::Dist(GetActorLocation(), Target));
	}
	return Alpha >= 1.0f;
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
				ExtrudedMeshComponent->SetRelativeScale3D(FVector(0.5f)); // vanilla ground scale (generated.json)
				HalfVisualHeight = FItemMeshExtruder::PlateSize * 0.25f;
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
			MeshComponent->SetRelativeScale3D(FVector(0.5f)); // vanilla ground scale (generated.json)
			HalfVisualHeight = 25.0f;
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

// ==================== Dijagnostika: voxel.pickuptest ====================
//
// Spawna Dirt drop 0.8 bloka ispred igraca (u pickup zoni) i jedan 2 bloka
// ispred (izvan nje). Ocekivano: blizi "[PICKUP] ... pokupljen nakon 0.50 s"
// i "let gotov nakon 0.150 s", dalji nikad.
// Primjer: UnrealEditor-Cmd.exe ... -ExecCmds="voxel.pickuptest"

static FAutoConsoleCommandWithWorldAndArgs GPickupTestCmd(
	TEXT("voxel.pickuptest"),
	TEXT("Spawnaj drop u pickup zoni igraca i jedan izvan nje te ispisi kada je pokupljen"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(
		[](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}

			// Svijet treba vremena za generaciju i doskok igraca
			constexpr float Delay = 5.0f;
			FTimerHandle Handle;
			TWeakObjectPtr<UWorld> WeakWorld(World);
			World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(
				[WeakWorld]()
				{
					ACharacter* Player = WeakWorld.IsValid() ? UGameplayStatics::GetPlayerCharacter(WeakWorld.Get(), 0) : nullptr;
					if (!Player)
					{
						UE_LOG(LogMinecraftClone, Warning, TEXT("[PICKUP] test: nema igraca"));
						return;
					}

					const FVector Forward = Player->GetActorForwardVector().GetSafeNormal2D();
					AItemDrop::SpawnItemDrop(Player, EItemType::Dirt, Player->GetActorLocation() + Forward * 80.0f);
					AItemDrop::SpawnItemDrop(Player, EItemType::Stone, Player->GetActorLocation() + Forward * 200.0f);
					UE_LOG(LogMinecraftClone, Log, TEXT("[PICKUP] test: Dirt na 80, Stone na 200 ispred igraca"));
				}), Delay, false);

			// Headless run se sam gasi da se log flusha. U editoru/PIE ne.
			if (!GIsEditor)
			{
				FTimerHandle QuitHandle;
				World->GetTimerManager().SetTimer(QuitHandle, FTimerDelegate::CreateLambda(
					[]() { FPlatformMisc::RequestExit(false); }), Delay + 3.0f, false);
			}
		}));
