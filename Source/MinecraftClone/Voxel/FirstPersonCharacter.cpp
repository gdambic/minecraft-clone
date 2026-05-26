#include "FirstPersonCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "InputActionValue.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MinecraftClone.h"
#include "Block.h"
#include "VoxelWorld.h"
#include "InventoryComponent.h"
#include "FirstPersonPlayerController.h"

AFirstPersonCharacter::AFirstPersonCharacter()
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

	// Create inventory component
	InventoryComponent = CreateDefaultSubobject<UInventoryComponent>(TEXT("InventoryComponent"));
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

void AFirstPersonCharacter::DoJumpStart()
{
	Jump();
}

void AFirstPersonCharacter::DoJumpEnd()
{
	StopJumping();
}

void AFirstPersonCharacter::BeginPlay()
{
	// Pronađi VoxelWorld PRIJE Super::BeginPlay() jer Blueprint BeginPlay ovisi o njemu
	VoxelWorld = Cast<AVoxelWorld>(UGameplayStatics::GetActorOfClass(GetWorld(), AVoxelWorld::StaticClass()));
	if (!VoxelWorld)
	{
		UE_LOG(LogMinecraftClone, Warning, TEXT("FirstPersonCharacter: VoxelWorld not found in scene!"));
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
	}

	Super::BeginPlay();
}

void AFirstPersonCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	UpdateBlockLookAt();

	// Destruction logic
	if (bIsAttacking && CurrentlyLookedAtBlock)
	{
		bool bDestroyed = CurrentlyLookedAtBlock->AddDestroyProgress(DeltaTime);
		if (bDestroyed)
		{
			CurrentlyLookedAtBlock = nullptr;
		}
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
	if (bHit)
	{
		HitBlock = Cast<ABlock>(HitResult.GetActor());
		CurrentHitNormal = HitResult.ImpactNormal;
	}
	else
	{
		CurrentHitNormal = FVector::ZeroVector;
	}

	// Ako gledamo u novi blok
	if (HitBlock != CurrentlyLookedAtBlock)
	{
		// Unhighlight i resetiraj progress starog bloka
		if (CurrentlyLookedAtBlock)
		{
			CurrentlyLookedAtBlock->SetHighlighted(false);
			CurrentlyLookedAtBlock->ResetDestroyProgress();
		}

		// Highlight novi blok
		if (HitBlock)
		{
			HitBlock->SetHighlighted(true);
		}

		CurrentlyLookedAtBlock = HitBlock;
	}
}

void AFirstPersonCharacter::StartAttack()
{
	bIsAttacking = true;
}

void AFirstPersonCharacter::StopAttack()
{
	bIsAttacking = false;

	// Resetiraj progress ako pustimo tipku
	if (CurrentlyLookedAtBlock)
	{
		CurrentlyLookedAtBlock->ResetDestroyProgress();
	}
}

void AFirstPersonCharacter::PlaceBlock()
{
	if (!VoxelWorld || !CurrentlyLookedAtBlock || !InventoryComponent)
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
	ABlock* NewBlock = VoxelWorld->PlaceBlockAt(NewBlockPos, BlockToPlace);
	if (NewBlock)
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
	bIsInventoryOpen = !bIsInventoryOpen;

	AFirstPersonPlayerController* PC = Cast<AFirstPersonPlayerController>(GetController());
	if (PC)
	{
		// Swap IMC-ove kroz PlayerController
		PC->SetInventoryInputMode(bIsInventoryOpen);

		if (bIsInventoryOpen)
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

	OnInventoryToggled.Broadcast(bIsInventoryOpen);
}
