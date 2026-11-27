#include "FirstPersonCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MinecraftClone.h"
#include "Block.h"
#include "BlockOutlineComponent.h"
#include "BlockMiningComponent.h"
#include "BlockRegistry.h"
#include "VoxelWorld.h"
#include "InventoryComponent.h"
#include "FirstPersonPlayerController.h"
#include "InventoryScreenWidget.h"
#include "Blueprint/UserWidget.h"
#include "CreatureBase.h"
#include "MobBase.h"
#include "WeaponData.h"
#include "CombatLibrary.h"
#include "InputCoreTypes.h"
#include "FirstPersonArmComponent.h"
#include "ItemDrop.h"
#include "MaceComponent.h"
#include "VoxelCharacterMovement.h"

/** voxel.fly.allow - smije li igrac letjeti (kasnije ga postavlja creative/survival) */
static TAutoConsoleVariable<int32> CVarFlyAllow(
	TEXT("voxel.fly.allow"),
	1,
	TEXT("1 = dvostruki skok ukljucuje let (Docs/PLAN_Flying.md), 0 = let zabranjen i ugasen."));

AFirstPersonCharacter::AFirstPersonCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UVoxelCharacterMovement>(ACharacter::CharacterMovementComponentName))
{
	// Enable Tick
	PrimaryActorTick.bCanEverTick = true;

	// Default block interaction range (5 blocks = 500 units)
	BlockInteractionRange = 500.0f;
	CurrentlyLookedAtBlock = nullptr;
	VoxelWorld = nullptr;
	CurrentHitNormal = FVector::ZeroVector;
	SelectedItemIndex = 0;

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(34.0f, 96.0f);

	// Create the Camera Component attached directly to capsule
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
	FirstPersonCameraComponent->SetupAttachment(GetCapsuleComponent());
	FirstPersonCameraComponent->SetRelativeLocation(FVector(0.0f, 0.0f, 64.0f)); // Eye height
	FirstPersonCameraComponent->bUsePawnControlRotation = true;

	// Don't show the mesh (no visible body)
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->SetVisibility(false);

	// Configure character movement
	GetCharacterMovement()->BrakingDecelerationFalling = 1500.0f;
	GetCharacterMovement()->AirControl = 0.5f;
	// Plivanje (Docs/PLAN_Water.md, faza 4): Minecraft tone ~1.6 bloka/s. Uz
	// kocenje vode 4.46 /s (AVoxelWaterVolume) tonjenje je g*(1-Buoyancy)/4.46,
	// pa 160 UU/s pri g=980 daje Buoyancy 0.27. Vodoravno ~2 bloka/s.
	GetCharacterMovement()->Buoyancy = 0.27f;
	GetCharacterMovement()->MaxSwimSpeed = 200.0f;
	// Izlaz na obalu: na povrsini je sredina na razini vode, a stopala ~1 blok
	// ispod nje. Kad lik izade iz vode s razmakom i W prema zidu,
	// UCharacterMovementComponent::CheckWaterJump ga odbaci s OutofWaterZ.
	// Default trazi pogled gore >11.25° (Minecraft ne trazi) i 420 UU/s (vrh
	// 90 UU, premalo za obalu Z=0); 520 UU/s digne stopala ~30 UU iznad bloka.
	GetCharacterMovement()->JumpOutOfWaterPitch = -90.0f;
	GetCharacterMovement()->OutofWaterZ = 520.0f;

	// Create inventory component
	InventoryComponent = CreateDefaultSubobject<UInventoryComponent>(TEXT("InventoryComponent"));

	// Create first person arm component
	FirstPersonArmComponent = CreateDefaultSubobject<UFirstPersonArmComponent>(TEXT("FirstPersonArmComponent"));

	// Meshevi ruke kao native komponente (prezive PIE izmjene svojstava);
	// mesh assete, materijale i transformacije postavlja FirstPersonArmComponent
	ArmMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FPArmMesh"));
	ArmMeshComponent->SetupAttachment(FirstPersonCameraComponent);

	HeldItemMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FPHeldItemMesh"));
	HeldItemMeshComponent->SetupAttachment(ArmMeshComponent);

	HeldSpriteMeshComponent = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("FPHeldSpriteMesh"));
	HeldSpriteMeshComponent->SetupAttachment(ArmMeshComponent);

	// Obrub stoji na bloku u svijetu, pa mu transform ne smije nasljedivati
	// igracev - attachment je tu samo zbog zivotnog ciklusa komponente.
	BlockOutlineComponent = CreateDefaultSubobject<UBlockOutlineComponent>(TEXT("BlockOutline"));
	BlockOutlineComponent->SetupAttachment(GetCapsuleComponent());
	BlockOutlineComponent->SetUsingAbsoluteLocation(true);
	BlockOutlineComponent->SetUsingAbsoluteRotation(true);
	BlockOutlineComponent->SetUsingAbsoluteScale(true);

	MiningComponent = CreateDefaultSubobject<UBlockMiningComponent>(TEXT("BlockMining"));

	MaceComponent = CreateDefaultSubobject<UMaceComponent>(TEXT("Mace"));
}

void AFirstPersonCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &AFirstPersonCharacter::DoJumpStart);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &AFirstPersonCharacter::DoJumpEnd);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AFirstPersonCharacter::MoveInput);

		// Looking/Aiming
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AFirstPersonCharacter::LookInput);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &AFirstPersonCharacter::LookInput);

		// Attack/Destroy
		EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Started, this, &AFirstPersonCharacter::StartAttack);
		EnhancedInputComponent->BindAction(AttackAction, ETriggerEvent::Completed, this, &AFirstPersonCharacter::StopAttack);

		// Interact/Place Block
		EnhancedInputComponent->BindAction(InteractAction, ETriggerEvent::Started, this, &AFirstPersonCharacter::PlaceBlock);

		// Scroll Inventory
		if (ScrollAction)
		{
			EnhancedInputComponent->BindAction(ScrollAction, ETriggerEvent::Triggered, this, &AFirstPersonCharacter::ScrollInventory);
		}

		// Toggle Inventory (E tipka)
		if (ToggleInventoryAction)
		{
			EnhancedInputComponent->BindAction(ToggleInventoryAction, ETriggerEvent::Started, this, &AFirstPersonCharacter::ToggleInventory);
		}

		// Sprint (uvjet za sprint-hit knockback); Ctrl kao u Minecraftu
		if (SprintAction)
		{
			EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Started, this, &AFirstPersonCharacter::StartSprint);
			EnhancedInputComponent->BindAction(SprintAction, ETriggerEvent::Completed, this, &AFirstPersonCharacter::StopSprint);
		}
		else
		{
			// Bez IA_Sprint asseta: Ctrl se vezuje legacy putem, pa sprint radi
			// odmah. Kad asset postoji i postavi se u BP, ova grana se ne izvodi.
			PlayerInputComponent->BindKey(EKeys::LeftControl, IE_Pressed, this, &AFirstPersonCharacter::StartSprint);
			PlayerInputComponent->BindKey(EKeys::LeftControl, IE_Released, this, &AFirstPersonCharacter::StopSprint);
		}

		// Shift: u letu spustanje (kasnije sneak na tlu); nema IA asseta
		PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Pressed, this, &AFirstPersonCharacter::StartDescend);
		PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Released, this, &AFirstPersonCharacter::StopDescend);
	}
	else
	{
		UE_LOG(LogMinecraftClone, Error, TEXT("'%s' Failed to find an Enhanced Input Component!"), *GetNameSafe(this));
	}
}

void AFirstPersonCharacter::MoveInput(const FInputActionValue& Value)
{
	FVector2D MovementVector = Value.Get<FVector2D>();
	DoMove(MovementVector.X, MovementVector.Y);
}

void AFirstPersonCharacter::LookInput(const FInputActionValue& Value)
{
	FVector2D LookAxisVector = Value.Get<FVector2D>();
	DoAim(LookAxisVector.X, LookAxisVector.Y);
}

void AFirstPersonCharacter::DoAim(float Yaw, float Pitch)
{
	if (GetController())
	{
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void AFirstPersonCharacter::DoMove(float Right, float Forward)
{
	if (GetController())
	{
		AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void AFirstPersonCharacter::StartSprint()
{
	if (bIsSprinting)
	{
		return;
	}

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp)
	{
		return;
	}

	BaseWalkSpeed = MoveComp->MaxWalkSpeed;
	MoveComp->MaxWalkSpeed = BaseWalkSpeed * SprintSpeedMultiplier;
	bIsSprinting = true;
	SyncFlyInput();
}

void AFirstPersonCharacter::StopSprint()
{
	if (!bIsSprinting)
	{
		return;
	}

	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->MaxWalkSpeed = BaseWalkSpeed;
	}
	bIsSprinting = false;
	SyncFlyInput();
}

void AFirstPersonCharacter::DoJumpStart()
{
	bJumpHeld = true;
	SyncFlyInput();

	// Vanilla: prvi pritisak pokrene prozor od 7 tickova, drugi unutar njega
	// prebaci let. U vodi se pritisci ne broje (plivanje gore).
	const bool bCanToggle = CVarFlyAllow.GetValueOnGameThread() != 0 && !GetCharacterMovement()->IsSwimming();
	const double Now = GetWorld()->GetTimeSeconds();
	if (bCanToggle && LastJumpPressTime >= 0.0 && Now - LastJumpPressTime <= FlyToggleWindow)
	{
		LastJumpPressTime = -1.0;
		SetCreativeFlying(!bIsFlying);
		return;
	}
	LastJumpPressTime = bCanToggle ? Now : -1.0;
	Jump();
}

void AFirstPersonCharacter::DoJumpEnd()
{
	bJumpHeld = false;
	SyncFlyInput();
	StopJumping();
}

void AFirstPersonCharacter::StartDescend()
{
	bDescendHeld = true;
	SyncFlyInput();
}

void AFirstPersonCharacter::StopDescend()
{
	bDescendHeld = false;
	SyncFlyInput();
}

void AFirstPersonCharacter::SyncFlyInput()
{
	if (UVoxelCharacterMovement* Move = Cast<UVoxelCharacterMovement>(GetCharacterMovement()))
	{
		Move->SetCreativeFlyInput(bJumpHeld, bDescendHeld, bIsSprinting);
	}
}

void AFirstPersonCharacter::SetCreativeFlying(bool bFly)
{
	UCharacterMovementComponent* Move = GetCharacterMovement();
	UVoxelCharacterMovement* VoxelMove = Cast<UVoxelCharacterMovement>(Move);
	if (!VoxelMove || bFly == bIsFlying)
	{
		return;
	}
	if (bFly && (CVarFlyAllow.GetValueOnGameThread() == 0 || Move->IsSwimming()))
	{
		return;
	}

	if (bFly)
	{
		// Vanilla na tlu odgurne lika skokom da se odvoji od poda
		const bool bOnGround = Move->IsMovingOnGround();
		bIsFlying = true;
		VoxelMove->SetCreativeFlying(true);
		SyncFlyInput();
		Move->SetMovementMode(MOVE_Flying);
		if (bOnGround)
		{
			Move->Velocity.Z = Move->JumpZVelocity;
		}
	}
	else
	{
		// Gasi i OnMovementModeChanged kad lik sleti ili upadne u vodu
		bIsFlying = false;
		VoxelMove->SetCreativeFlying(false);
		if (Move->MovementMode == MOVE_Flying)
		{
			Move->SetMovementMode(MOVE_Falling);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[FLY] let %s (mod=%s, Z=%.0f)"), bIsFlying ? TEXT("UKLJUCEN") : TEXT("iskljucen"),
		*StaticEnum<EMovementMode>()->GetNameStringByValue(Move->MovementMode).RightChop(5), GetActorLocation().Z);
}

void AFirstPersonCharacter::UpdateFlyFieldOfView(float DeltaTime)
{
	if (!FirstPersonCameraComponent || BaseFieldOfView <= 0.0f)
	{
		return;
	}
	// AbstractClientPlayer.getFieldOfViewModifier: x1.1 u letu; GameRenderer
	// mu se primice za pola razlike po ticku
	const float Target = bIsFlying ? 1.1f : 1.0f;
	FieldOfViewModifier = Target + (FieldOfViewModifier - Target) * FMath::Pow(0.5f, DeltaTime * 20.0f);
	FirstPersonCameraComponent->SetFieldOfView(BaseFieldOfView * FieldOfViewModifier);
}

void AFirstPersonCharacter::BeginPlay()
{
	// Pronađi VoxelWorld PRIJE Super::BeginPlay() jer Blueprint BeginPlay ovisi o njemu
	VoxelWorld = Cast<AVoxelWorld>(UGameplayStatics::GetActorOfClass(GetWorld(), AVoxelWorld::StaticClass()));
	if (!VoxelWorld)
	{
		UE_LOG(LogMinecraftClone, Warning, TEXT("FirstPersonCharacter: VoxelWorld not found in scene!"));
	}
	if (MiningComponent)
	{
		MiningComponent->SetVoxelWorld(VoxelWorld);
	}
	if (MaceComponent)
	{
		MaceComponent->SetVoxelWorld(VoxelWorld);
	}

	// Prati promjene sadržaja slotova - ruka se mora osvježiti i kad se
	// odabrani slot isprazni/napuni bez promjene indeksa
	if (InventoryComponent)
	{
		InventoryComponent->OnSlotChanged.AddDynamic(this, &AFirstPersonCharacter::OnInventorySlotChanged);
	}

	// Početni inventory - stavi iteme u hotbar (slotovi 27-35)
	if (InventoryComponent)
	{
		// Slot 27 = hotbar pozicija 0 (Dirt)
		InventoryComponent->SetSlot(UInventoryComponent::HotbarStartIndex + 0, EItemType::Dirt, 10);
		// Slot 28 = hotbar pozicija 1 (Stone)
		InventoryComponent->SetSlot(UInventoryComponent::HotbarStartIndex + 1, EItemType::Stone, 10);
		// Slot 29 = hotbar pozicija 2 (OakLog)
		InventoryComponent->SetSlot(UInventoryComponent::HotbarStartIndex + 2, EItemType::OakLog, 10);
		// Slot 31 = hotbar pozicija 4 (DiamondSword) - za testiranje
		InventoryComponent->SetSlot(UInventoryComponent::HotbarStartIndex + 4, EItemType::DiamondSword, 1);
		// Slot 32 = hotbar pozicija 5 (Mace) - za testiranje
		InventoryComponent->SetSlot(UInventoryComponent::HotbarStartIndex + 5, EItemType::Mace, 1);
	}

	// WoodenSword se ne dobiva u startnom inventoryju nego kao drop u svijetu.
	// Odgodeno timerom: teren se generira u VoxelWorld::BeginPlay koji moze
	// doci NAKON characterovog BeginPlay - trace u ovom trenutku nema sto pogoditi.
	FTimerHandle SwordDropTimer;
	GetWorldTimerManager().SetTimer(SwordDropTimer, this,
		&AFirstPersonCharacter::SpawnStartingItemDrops, 0.2f, false);

	// Initialize first person arm with current held item
	if (FirstPersonArmComponent && InventoryComponent)
	{
		EItemType InitialItem = GetSelectedItemType();
		FirstPersonArmComponent->SetHeldItem(InitialItem);
	}

	// Ekrani su u vlasnistvu C++-a: kreiraju se jednom, skriveni, a
	// OpenScreen/CloseCurrentScreen im pale/gase vidljivost
	InventoryScreen = CreateScreen(InventoryScreenClass);
	CraftingTableScreen = CreateScreen(CraftingTableScreenClass);

	if (FirstPersonCameraComponent)
	{
		BaseFieldOfView = FirstPersonCameraComponent->FieldOfView;
	}

	Super::BeginPlay();
}

UInventoryScreenWidget* AFirstPersonCharacter::CreateScreen(TSubclassOf<UInventoryScreenWidget> ScreenClass)
{
	if (!ScreenClass)
	{
		return nullptr;
	}

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		return nullptr;
	}

	UInventoryScreenWidget* Screen = CreateWidget<UInventoryScreenWidget>(PC, ScreenClass);
	if (Screen)
	{
		Screen->AddToViewport();
		Screen->InitializeScreen();
	}
	return Screen;
}

UInventoryScreenWidget* AFirstPersonCharacter::GetScreenWidget(EOpenScreen Screen) const
{
	switch (Screen)
	{
	case EOpenScreen::Inventory:     return InventoryScreen;
	case EOpenScreen::CraftingTable: return CraftingTableScreen;
	default:                         return nullptr;
	}
}

void AFirstPersonCharacter::SpawnStartingItemDrops()
{
	// 2 bloka desno od igraca - 1 blok (100 UU) bi bio unutar auto-pickup
	// radiusa (150 UU) pa bi se mac pokupio odmah pri spawnu. Drop sam
	// padne na tlo vlastitom gravitacijom.
	const FVector SpawnLocation = GetActorLocation() + GetActorRightVector() * 200.0f;

	UE_LOG(LogTemp, Log, TEXT("SpawnStartingSwordDrop: igrac na %s, drop na %s"),
		*GetActorLocation().ToCompactString(), *SpawnLocation.ToCompactString());

	AItemDrop::SpawnItemDrop(this, EItemType::WoodenSword, SpawnLocation);
	AItemDrop::SpawnItemDrop(this, EItemType::WoodenAxe, GetActorLocation() + GetActorRightVector() * 300.0f);
	AItemDrop::SpawnItemDrop(this, EItemType::WoodenPickaxe, GetActorLocation() + GetActorRightVector() * 400.0f);
}

void AFirstPersonCharacter::OnMovementModeChanged(EMovementMode PrevMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PrevMovementMode, PreviousCustomMode);

	// Bez inputa CalcVelocity u vodi koci dvaput: kocenjem (BrakingFrictionFactor
	// 2 * 0.5 * FluidFriction) i trenjem fluida (0.5 * FluidFriction). Izmjereno
	// tonjenje bilo je 54 UU/s umjesto 160. Minecraft koci isto s inputom i bez,
	// pa u vodi ostaje samo trenje fluida; na kopnu vrijedi default 2.
	UCharacterMovementComponent* Move = GetCharacterMovement();
	Move->BrakingFrictionFactor = Move->IsSwimming() ? 0.0f : 2.0f;

	// Let zavrsava kad komponenta sleti na tlo ili UE prebaci lika u plivanje
	// (PhysicsVolumeChanged pri ulasku u more)
	if (bIsFlying && Move->MovementMode != MOVE_Flying)
	{
		SetCreativeFlying(false);
	}
}

void AFirstPersonCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateBlockLookAt();

	if (bIsFlying && CVarFlyAllow.GetValueOnGameThread() == 0)
	{
		SetCreativeFlying(false);
	}
	UpdateFlyFieldOfView(DeltaTime);
	if (!TestMoveInput.IsZero())
	{
		DoMove(TestMoveInput.X, TestMoveInput.Y);
	}

	// U vodi skok ne radi (CanJump trazi tlo), pa drzanje skoka pliva prema gore
	if (bJumpHeld && GetCharacterMovement()->IsSwimming())
	{
		AddMovementInput(FVector::UpVector, 1.f);
	}
	TickOpenScreenSafety();

	// Kopanje: komponenta zna cilj (UpdateBlockLookAt) i drzi li se tipka
	// (StartAttack/StopAttack); promjena itema u ruci resetira progres.
	if (MiningComponent && MiningComponent->TickMining(DeltaTime, GetSelectedItemType()))
	{
		// Promovirani actor je unisten zajedno s blokom
		CurrentlyLookedAtBlock = nullptr;

		// Blok je nestao pod obrubom - ugasi ga do sljedeceg raycasta
		HideBlockOutline();
	}

	if (MaceComponent)
	{
		MaceComponent->TickMace(DeltaTime, GetSelectedItemType());
	}
}

void AFirstPersonCharacter::UpdateBlockLookAt()
{
	if (!FirstPersonCameraComponent)
	{
		return;
	}

	FVector Start = FirstPersonCameraComponent->GetComponentLocation();
	FVector End = Start + (FirstPersonCameraComponent->GetForwardVector() * BlockInteractionRange);

	FHitResult HitResult;
	FCollisionQueryParams QueryParams;
	QueryParams.AddIgnoredActor(this);

	bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, QueryParams);

	ABlock* HitBlock = nullptr;
	bool bHasTarget = false;
	if (bHit)
	{
		CurrentHitNormal = HitResult.ImpactNormal;

		// Svijet su ISM instance + najvise jedan promovirani ABlock (fokus).
		// Hit se prevede u grid poziciju, a fokusirani blok se promovira u
		// actor koji preuzima kopanje.
		FIntVector HitGridPos;
		EBlockType HitType;
		if (VoxelWorld && VoxelWorld->ResolveHitToGrid(HitResult, HitGridPos, HitType))
		{
			bHasTarget = true;
			ShowBlockOutline(HitGridPos, HitType);
			if (MiningComponent)
			{
				MiningComponent->SetTarget(HitGridPos);
			}
			HitBlock = VoxelWorld->PromoteToActor(HitGridPos);
		}
	}
	else
	{
		CurrentHitNormal = FVector::ZeroVector;
	}

	if (!bHasTarget)
	{
		HideBlockOutline();
		if (MiningComponent)
		{
			MiningComponent->ClearTarget();
		}
	}

	// Ako gledamo u novi blok
	if (HitBlock != CurrentlyLookedAtBlock)
	{
		// Stari fokus natrag u instancu (progres kopanja je vec resetirao
		// SetTarget). IsValid: actor je mogao nestati izvana (npr. leaf decay).
		if (IsValid(CurrentlyLookedAtBlock) && VoxelWorld)
		{
			VoxelWorld->DemoteToInstance(CurrentlyLookedAtBlock->GridPosition);
		}

		CurrentlyLookedAtBlock = HitBlock;
	}
}

const FIntVector AFirstPersonCharacter::OutlineHiddenPos(MIN_int32, MIN_int32, MIN_int32);

void AFirstPersonCharacter::ShowBlockOutline(FIntVector GridPos, EBlockType Type)
{
	if (!BlockOutlineComponent || !VoxelWorld || GridPos == OutlineGridPos)
	{
		return;
	}

	OutlineGridPos = GridPos;
	BlockOutlineComponent->SetOutlineBox(VoxelWorld->GetBlockOutlineBox(Type));
	BlockOutlineComponent->SetWorldLocation(
		VoxelWorld->GridToWorld(GridPos.X, GridPos.Y, GridPos.Z));
	BlockOutlineComponent->SetVisibility(true);
}

void AFirstPersonCharacter::HideBlockOutline()
{
	if (!BlockOutlineComponent || OutlineGridPos == OutlineHiddenPos)
	{
		return;
	}

	OutlineGridPos = OutlineHiddenPos;
	BlockOutlineComponent->SetVisibility(false);
}

void AFirstPersonCharacter::StartAttack()
{
	// Mace preuzima lijevu tipku: ne kopa i ne udara melee (Terraria noMelee)
	if (MaceComponent && UMaceComponent::IsMace(GetSelectedItemType()))
	{
		MaceComponent->PressAttack();
		return;
	}

	// Always play swing animation on attack input
	if (FirstPersonArmComponent)
	{
		FWeaponData Weapon = GetEquippedWeaponData();
		FirstPersonArmComponent->PlaySwingAnimation(Weapon.GetCooldown() * 0.6f);
	}

	// PRIORITY 1: Attack creature if in range
	if (MeleeTrace() != nullptr)
	{
		PerformMeleeAttack();
		return;
	}

	// PRIORITY 2: Block destruction
	if (MiningComponent)
	{
		MiningComponent->StartMining();
	}
}

void AFirstPersonCharacter::StopAttack()
{
	if (MaceComponent)
	{
		MaceComponent->ReleaseAttack();
	}

	// Pustena tipka resetira progres i brise pukotinu
	if (MiningComponent)
	{
		MiningComponent->StopMining();
	}
}

void AFirstPersonCharacter::PlaceBlock()
{
	if (!VoxelWorld || !IsValid(CurrentlyLookedAtBlock) || !InventoryComponent)
	{
		return;
	}

	// Interakcija s blokom ima prednost pred postavljanjem (Minecraft semantika);
	// radi i praznom rukom jer dolazi prije provjera hotbar slota
	if (TryInteractWithFocusedBlock())
	{
		return;
	}

	// Dohvati selektirani hotbar slot
	int32 HotbarSlotIndex = UInventoryComponent::HotbarStartIndex + SelectedItemIndex;
	FInventorySlot Slot = InventoryComponent->GetSlot(HotbarSlotIndex);

	if (Slot.IsEmpty())
	{
		return; // Prazan slot
	}

	// Provjeri može li se item postaviti kao blok
	if (!UInventoryComponent::CanItemBePlaced(Slot.ItemType))
	{
		return;
	}

	// Konvertiraj u BlockType
	EBlockType BlockToPlace = UInventoryComponent::ItemTypeToBlockType(Slot.ItemType);
	if (BlockToPlace == EBlockType::Air)
	{
		return; // Ovaj item se ne može postaviti kao blok
	}

	// Izračunaj poziciju novog bloka na temelju normale
	FIntVector CurrentGridPos = CurrentlyLookedAtBlock->GridPosition;
	FIntVector NormalOffset(
		FMath::RoundToInt(CurrentHitNormal.X),
		FMath::RoundToInt(CurrentHitNormal.Y),
		FMath::RoundToInt(CurrentHitNormal.Z)
	);
	FIntVector NewBlockPos = CurrentGridPos + NormalOffset;

	// Postavi novi blok
	const bool bPlaced = VoxelWorld->PlaceBlockAt(NewBlockPos, BlockToPlace);
	if (bPlaced)
	{
		// Oduzmi item iz hotbar slota
		int32 NewQuantity = Slot.Quantity - 1;
		if (NewQuantity > 0)
		{
			InventoryComponent->SetSlot(HotbarSlotIndex, Slot.ItemType, NewQuantity);
		}
		else
		{
			InventoryComponent->SetSlot(HotbarSlotIndex, EItemType::None, 0);
		}

		UE_LOG(LogMinecraftClone, Log, TEXT("Placed block at (%d, %d, %d), remaining: %d"),
			NewBlockPos.X, NewBlockPos.Y, NewBlockPos.Z, NewQuantity);
	}
}

void AFirstPersonCharacter::ScrollInventory(const FInputActionValue& Value)
{
	if (!InventoryComponent) return;

	float ScrollValue = Value.Get<float>();
	int32 OldIndex = SelectedItemIndex;

	if (ScrollValue > 0)
	{
		// Scroll gore = prema početku hotbara
		SelectedItemIndex--;
		if (SelectedItemIndex < 0)
		{
			SelectedItemIndex = UInventoryComponent::HotbarSize - 1; // Wrap na kraj (8)
		}
	}
	else if (ScrollValue < 0)
	{
		// Scroll dolje = prema kraju hotbara
		SelectedItemIndex++;
		if (SelectedItemIndex >= UInventoryComponent::HotbarSize)
		{
			SelectedItemIndex = 0; // Wrap na početak
		}
	}

	// Broadcast delegate ako se index promijenio
	if (OldIndex != SelectedItemIndex)
	{
		EItemType NewItemType = GetSelectedItemType();
		OnSelectedItemChanged.Broadcast(SelectedItemIndex, NewItemType);

		// Update first person arm held item display
		if (FirstPersonArmComponent)
		{
			FirstPersonArmComponent->SetHeldItem(NewItemType);
		}
	}
}

void AFirstPersonCharacter::OnInventorySlotChanged(int32 SlotIndex, FInventorySlot NewSlot)
{
	if (FirstPersonArmComponent &&
		SlotIndex == UInventoryComponent::HotbarStartIndex + SelectedItemIndex)
	{
		FirstPersonArmComponent->SetHeldItem(NewSlot.ItemType);
	}
}

EItemType AFirstPersonCharacter::GetSelectedItemType() const
{
	if (!InventoryComponent) return EItemType::None;

	// SelectedItemIndex je 0-8, mapira na hotbar slotove 27-35
	int32 HotbarSlotIndex = UInventoryComponent::HotbarStartIndex + SelectedItemIndex;
	FInventorySlot Slot = InventoryComponent->GetSlot(HotbarSlotIndex);

	return Slot.ItemType;
}

EBlockType AFirstPersonCharacter::GetSelectedBlockType() const
{
	EItemType SelectedItem = GetSelectedItemType();
	if (SelectedItem == EItemType::None) return EBlockType::Air;

	return UInventoryComponent::ItemTypeToBlockType(SelectedItem);
}

void AFirstPersonCharacter::ToggleInventory()
{
	// E otvara inventar, ili zatvara bilo koji trenutno otvoreni ekran
	if (CurrentOpenScreen == EOpenScreen::None)
	{
		OpenScreen(EOpenScreen::Inventory);
	}
	else
	{
		CloseCurrentScreen();
	}
}

void AFirstPersonCharacter::OpenScreen(EOpenScreen Screen)
{
	if (Screen == EOpenScreen::None)
	{
		CloseCurrentScreen();
		return;
	}

	if (CurrentOpenScreen != EOpenScreen::None)
	{
		CloseCurrentScreen();
	}

	CurrentOpenScreen = Screen;
	bIsInventoryOpen = true;

	// Aktivna velicina grida odreduje koje crafting slotove matcher i
	// drag&drop smatraju vazecima (ekran stola ima 3x3 panel)
	if (InventoryComponent)
	{
		InventoryComponent->SetActiveCraftingGridSize(Screen == EOpenScreen::CraftingTable ? 3 : 2);
	}

	if (UInventoryScreenWidget* Widget = GetScreenWidget(Screen))
	{
		Widget->RefreshAll();
		Widget->SetVisibility(ESlateVisibility::Visible);
	}

	ApplyScreenInputMode(true);
	OnInventoryToggled.Broadcast(true);
}

void AFirstPersonCharacter::CloseCurrentScreen()
{
	if (CurrentOpenScreen == EOpenScreen::None)
	{
		return;
	}

	const EOpenScreen ClosedScreen = CurrentOpenScreen;

	if (InventoryComponent)
	{
		// Redoslijed je bitan: held item prvo (dok je njegov originalni slot
		// jos aktivan), pa tek onda povrat/smanjenje crafting grida
		InventoryComponent->ReturnHeldItem();

		if (ClosedScreen == EOpenScreen::CraftingTable)
		{
			// Interno vraca iteme iz 3x3 grida u inventar prije smanjenja
			InventoryComponent->SetActiveCraftingGridSize(2);
		}
		else
		{
			// Minecraft semantika i za osobni 2x2: itemi iz grida natrag u inventar
			InventoryComponent->ReturnCraftingInputToInventory();
		}
	}

	if (UInventoryScreenWidget* Widget = GetScreenWidget(ClosedScreen))
	{
		Widget->SetVisibility(ESlateVisibility::Collapsed);
	}

	ApplyScreenInputMode(false);

	CurrentOpenScreen = EOpenScreen::None;
	bIsInventoryOpen = false;

	OnInventoryToggled.Broadcast(false);
}

void AFirstPersonCharacter::ApplyScreenInputMode(bool bUIMode)
{
	AFirstPersonPlayerController* PC = Cast<AFirstPersonPlayerController>(GetController());
	if (!PC)
	{
		return;
	}

	// Swap IMC-ove kroz PlayerController
	PC->SetInventoryInputMode(bUIMode);

	if (bUIMode)
	{
		// Pokaži kursor i omogući UI interakciju
		PC->SetShowMouseCursor(true);
		FInputModeGameAndUI InputMode;
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		InputMode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(InputMode);
	}
	else
	{
		// Sakrij kursor i vrati na game-only input
		PC->SetShowMouseCursor(false);
		PC->SetInputMode(FInputModeGameOnly());
	}
}

bool AFirstPersonCharacter::TryInteractWithFocusedBlock()
{
	if (!IsValid(CurrentlyLookedAtBlock))
	{
		return false;
	}

	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	if (!Registry)
	{
		return false;
	}

	const FBlockDefinition* Def = Registry->GetBlockDefinition(CurrentlyLookedAtBlock->BlockType);
	if (!Def)
	{
		return false;
	}

	switch (Def->Interaction)
	{
	case EBlockInteraction::CraftingTable:
		OpenCraftingTablePos = CurrentlyLookedAtBlock->GridPosition;
		OpenScreen(EOpenScreen::CraftingTable);
		UE_LOG(LogMinecraftClone, Log, TEXT("Interact: CraftingTable na (%d,%d,%d)"),
			OpenCraftingTablePos.X, OpenCraftingTablePos.Y, OpenCraftingTablePos.Z);
		return true;

	default:
		return false;
	}
}

void AFirstPersonCharacter::TickOpenScreenSafety()
{
	if (CurrentOpenScreen != EOpenScreen::CraftingTable || !VoxelWorld)
	{
		return;
	}

	// Stol unisten (npr. izvana) -> zatvori ekran
	if (VoxelWorld->GetBlockTypeAt(OpenCraftingTablePos) != EBlockType::CraftingTable)
	{
		CloseCurrentScreen();
		return;
	}

	// Igrac predaleko (knockback, pad) -> zatvori ekran
	const FVector TableCenter = VoxelWorld->GridToWorld(
		OpenCraftingTablePos.X, OpenCraftingTablePos.Y, OpenCraftingTablePos.Z)
		+ FVector(VoxelWorld->BlockSize * 0.5f);
	if (FVector::Dist(GetActorLocation(), TableCenter) > ScreenAutoCloseDistance)
	{
		CloseCurrentScreen();
	}
}

// === MELEE COMBAT ===

ACreatureBase* AFirstPersonCharacter::MeleeTrace() const
{
	if (!FirstPersonCameraComponent)
	{
		return nullptr;
	}

	FVector Start = FirstPersonCameraComponent->GetComponentLocation();
	FVector End = Start + FirstPersonCameraComponent->GetForwardVector() * MeleeRange;

	FHitResult HitResult;
	FCollisionQueryParams Params;
	Params.AddIgnoredActor(this);

	// Trace on Pawn channel to find creatures
	if (GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, ECC_Pawn, Params))
	{
		return Cast<ACreatureBase>(HitResult.GetActor());
	}

	return nullptr;
}

FWeaponData AFirstPersonCharacter::GetEquippedWeaponData() const
{
	if (!InventoryComponent)
	{
		return FWeaponData(); // Fist default
	}

	// Get item from current hotbar slot
	int32 HotbarSlotIndex = UInventoryComponent::HotbarStartIndex + SelectedItemIndex;
	FInventorySlot CurrentSlot = InventoryComponent->GetSlot(HotbarSlotIndex);

	return UWeaponDataLibrary::GetWeaponData(CurrentSlot.ItemType);
}

float AFirstPersonCharacter::CalculateDamageMultiplier(float CooldownProgress) const
{
	// Minecraft formula: 0.2 + (progress^2) * 0.8
	// At 0% progress: 0.2 (20% damage)
	// At 50% progress: 0.4 (40% damage)
	// At 100% progress: 1.0 (100% damage)
	float ClampedProgress = FMath::Clamp(CooldownProgress, 0.0f, 1.0f);
	return 0.2f + FMath::Square(ClampedProgress) * 0.8f;
}

void AFirstPersonCharacter::PerformMeleeAttack()
{
	const float CurrentTime = GetWorld()->GetTimeSeconds();

	// Get equipped weapon data
	FWeaponData Weapon = GetEquippedWeaponData();
	const float Cooldown = Weapon.GetCooldown();

	// Calculate time since last attack
	const float TimeSinceAttack = CurrentTime - LastAttackTime;

	// Cooldown check - prevent attack if not ready
	if (TimeSinceAttack < Cooldown)
	{
		return;
	}

	// Find target
	ACreatureBase* Target = MeleeTrace();
	if (!Target)
	{
		// Update time even on miss (shorter cooldown on miss like Minecraft)
		LastAttackTime = CurrentTime;
		return;
	}

	// Update attack time (i promasaj i pogodak resetiraju cooldown)
	LastAttackTime = CurrentTime;

	// Vanilla: pogodak unutar prozora nepovredivosti (0.5 s) ne radi nista -
	// ni stetu ni knockback
	if (!Target->CanBeHurt())
	{
		return;
	}

	// Calculate damage multiplier based on cooldown progress
	const float CooldownProgress = FMath::Clamp(TimeSinceAttack / Cooldown, 0.0f, 1.0f);
	const float DamageMultiplier = CalculateDamageMultiplier(CooldownProgress);

	// Calculate final damage
	const float FinalDamage = Weapon.Damage * DamageMultiplier;

	// Apply damage
	const bool bKilled = Target->TakeDamage(FinalDamage);

	// Play hit sound on successful hit
	if (FirstPersonArmComponent)
	{
		FirstPersonArmComponent->PlayHitSound();
	}

	// Knockback: osnovnih 0.4 bloka/tick + 0.5 za sprint-hit (vanilla zahtijeva
	// napunjen cooldown >= 84.8%). Mnozitelj oruzja je 1.0 po vanilli.
	const bool bSprintHit = bIsSprinting
		&& CooldownProgress >= UCombatLibrary::SprintAttackChargeThreshold;

	float KnockbackStrength = UCombatLibrary::BaseKnockbackStrength;
	if (bSprintHit)
	{
		KnockbackStrength += UCombatLibrary::SprintKnockbackBonus;
	}
	KnockbackStrength *= Weapon.KnockbackMultiplier;

	// Smjer je yaw pogleda (vanilla sprint-knockback smjer), a ne razlika
	// pozicija - inace odbacivanje skrene kad je mob postrance u kapsuli
	FVector KnockbackDir = GetControlRotation().Vector();
	KnockbackDir.Z = 0.0f;
	if (!KnockbackDir.Normalize())
	{
		KnockbackDir = UCombatLibrary::HorizontalDirection(this, Target);
	}

	if (!bKilled)
	{
		Target->ReceiveKnockback(KnockbackDir, KnockbackStrength);
	}

	// Vanilla: sprint-hit napadacu pojede zalet i prekine sprint
	if (bSprintHit)
	{
		if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
		{
			MoveComp->Velocity.X *= UCombatLibrary::SprintAttackSelfSlowdown;
			MoveComp->Velocity.Y *= UCombatLibrary::SprintAttackSelfSlowdown;
		}
		StopSprint();
	}

	// Notify passive mobs to trigger flee behavior
	if (AMobBase* Mob = Cast<AMobBase>(Target))
	{
		Mob->OnDamageTaken(this);
	}

	UE_LOG(LogMinecraftClone, Log,
		TEXT("Melee hit %s for %.1f damage (weapon: %s, multiplier: %.2f, knockback: %.2f b/t%s)"),
		*Target->GetName(),
		FinalDamage,
		Weapon.bIsWeapon ? TEXT("Sword") : TEXT("Fist"),
		DamageMultiplier,
		KnockbackStrength,
		bSprintHit ? TEXT(", sprint") : TEXT(""));
}
