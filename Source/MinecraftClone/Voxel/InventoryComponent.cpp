#include "InventoryComponent.h"
#include "Engine/DataTable.h"

UInventoryComponent::UInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Inicijaliziraj 36 praznih slotova
	Slots.SetNum(TotalSlots);

	// Inicijaliziraj 5 crafting slotova (4 input + 1 output)
	CraftingSlots.SetNum(TotalCraftingSlots);
}

void UInventoryComponent::BeginPlay()
{
	Super::BeginPlay();
}

// ==================== Slot-based API ====================

FInventorySlot UInventoryComponent::GetSlot(int32 SlotIndex) const
{
	if (IsCraftingSlotIndex(SlotIndex))
	{
		return CraftingSlots[CraftingSlotToArrayIndex(SlotIndex)];
	}
	if (SlotIndex >= 0 && SlotIndex < TotalSlots)
	{
		return Slots[SlotIndex];
	}
	return FInventorySlot();
}

void UInventoryComponent::SetSlot(int32 SlotIndex, EItemType Type, int32 Quantity)
{
	if (!IsValidSlotIndex(SlotIndex))
	{
		return;
	}

	// Dohvati referencu na odgovarajući slot
	FInventorySlot* TargetSlot = nullptr;
	if (IsCraftingSlotIndex(SlotIndex))
	{
		TargetSlot = &CraftingSlots[CraftingSlotToArrayIndex(SlotIndex)];
	}
	else
	{
		TargetSlot = &Slots[SlotIndex];
	}

	if (Type == EItemType::None || Quantity <= 0)
	{
		TargetSlot->Clear();
	}
	else
	{
		TargetSlot->ItemType = Type;
		TargetSlot->Quantity = Quantity;
	}

	BroadcastSlotChanged(SlotIndex);
}

bool UInventoryComponent::MoveItem(int32 FromSlot, int32 ToSlot)
{
	return SwapSlots(FromSlot, ToSlot);
}

bool UInventoryComponent::SwapSlots(int32 SlotA, int32 SlotB)
{
	if (!IsValidSlotIndex(SlotA) || !IsValidSlotIndex(SlotB))
	{
		return false;
	}

	if (SlotA == SlotB)
	{
		return true; // Ništa za napraviti
	}

	// Dohvati reference na odgovarajuće slotove
	FInventorySlot* SlotPtrA = nullptr;
	FInventorySlot* SlotPtrB = nullptr;

	if (IsCraftingSlotIndex(SlotA))
	{
		SlotPtrA = &CraftingSlots[CraftingSlotToArrayIndex(SlotA)];
	}
	else
	{
		SlotPtrA = &Slots[SlotA];
	}

	if (IsCraftingSlotIndex(SlotB))
	{
		SlotPtrB = &CraftingSlots[CraftingSlotToArrayIndex(SlotB)];
	}
	else
	{
		SlotPtrB = &Slots[SlotB];
	}

	// Swap
	FInventorySlot TempSlot = *SlotPtrA;
	*SlotPtrA = *SlotPtrB;
	*SlotPtrB = TempSlot;

	BroadcastSlotChanged(SlotA);
	BroadcastSlotChanged(SlotB);

	return true;
}

bool UInventoryComponent::IsValidSlotIndex(int32 SlotIndex) const
{
	// Inventory slotovi (0-35) ili crafting slotovi (36-40)
	return (SlotIndex >= 0 && SlotIndex < TotalSlots) || IsCraftingSlotIndex(SlotIndex);
}

bool UInventoryComponent::IsCraftingSlotIndex(int32 SlotIndex) const
{
	return SlotIndex >= CraftingInputStartIndex && SlotIndex <= CraftingOutputIndex;
}

bool UInventoryComponent::IsCraftingInputSlot(int32 SlotIndex) const
{
	return SlotIndex >= CraftingInputStartIndex && SlotIndex < CraftingInputStartIndex + CraftingInputSize;
}

bool UInventoryComponent::IsCraftingOutputSlot(int32 SlotIndex) const
{
	return SlotIndex == CraftingOutputIndex;
}

int32 UInventoryComponent::CraftingSlotToArrayIndex(int32 SlotIndex) const
{
	return SlotIndex - CraftingInputStartIndex;
}

void UInventoryComponent::UpdateCraftingOutput()
{
	// Pronađi prvi non-empty input slot
	FInventorySlot* OutputSlot = &CraftingSlots[CraftingSlotToArrayIndex(CraftingOutputIndex)];

	for (int32 i = 0; i < CraftingInputSize; ++i)
	{
		if (!CraftingSlots[i].IsEmpty())
		{
			// Kopiraj prvi pronađeni item u output
			OutputSlot->ItemType = CraftingSlots[i].ItemType;
			OutputSlot->Quantity = 1; // Output je uvijek 1 za sada
			BroadcastSlotChanged(CraftingOutputIndex);
			UE_LOG(LogTemp, Warning, TEXT("UpdateCraftingOutput: Set output to %d from input slot %d"),
				(int32)OutputSlot->ItemType, CraftingInputStartIndex + i);
			return;
		}
	}

	// Ako nema inputa, isprazni output
	if (!OutputSlot->IsEmpty())
	{
		OutputSlot->Clear();
		BroadcastSlotChanged(CraftingOutputIndex);
		UE_LOG(LogTemp, Warning, TEXT("UpdateCraftingOutput: Cleared output (no inputs)"));
	}
}

int32 UInventoryComponent::FindFirstEmptySlot() const
{
	// Prvo traži prazan slot u hotbaru (27-35) - Minecraft stil
	for (int32 i = HotbarStartIndex; i < TotalSlots; ++i)
	{
		if (Slots[i].IsEmpty())
		{
			return i;
		}
	}

	// Zatim traži u main inventory (0-26)
	for (int32 i = 0; i < HotbarStartIndex; ++i)
	{
		if (Slots[i].IsEmpty())
		{
			return i;
		}
	}

	return -1;
}

int32 UInventoryComponent::FindSlotWithItem(EItemType Type) const
{
	// Prvo traži u hotbaru (27-35)
	for (int32 i = HotbarStartIndex; i < TotalSlots; ++i)
	{
		if (Slots[i].ItemType == Type && Slots[i].Quantity > 0)
		{
			return i;
		}
	}

	// Zatim traži u main inventory (0-26)
	for (int32 i = 0; i < HotbarStartIndex; ++i)
	{
		if (Slots[i].ItemType == Type && Slots[i].Quantity > 0)
		{
			return i;
		}
	}

	return -1;
}

void UInventoryComponent::BroadcastSlotChanged(int32 SlotIndex)
{
	if (IsValidSlotIndex(SlotIndex))
	{
		OnSlotChanged.Broadcast(SlotIndex, GetSlot(SlotIndex));
	}
}

void UInventoryComponent::AddItem(EItemType Type, int32 Amount)
{
	if (Type == EItemType::None || Amount <= 0)
	{
		return;
	}

	int32 RemainingAmount = Amount;

	// Prvo probaj stackati na postojeće slotove s istim itemom
	// Prioritet: hotbar (27-35) pa main inventory (0-26)
	for (int32 i = HotbarStartIndex; i < TotalSlots && RemainingAmount > 0; ++i)
	{
		if (Slots[i].ItemType == Type)
		{
			Slots[i].Quantity += RemainingAmount;
			RemainingAmount = 0;
			BroadcastSlotChanged(i);
			break;
		}
	}

	// Ako nema u hotbaru, traži u main inventory
	for (int32 i = 0; i < HotbarStartIndex && RemainingAmount > 0; ++i)
	{
		if (Slots[i].ItemType == Type)
		{
			Slots[i].Quantity += RemainingAmount;
			RemainingAmount = 0;
			BroadcastSlotChanged(i);
			break;
		}
	}

	// Ako još ima ostatka, pronađi prazan slot (hotbar first)
	while (RemainingAmount > 0)
	{
		int32 EmptySlot = FindFirstEmptySlot();
		if (EmptySlot == -1)
		{
			// Nema više mjesta u inventoryu
			break;
		}

		Slots[EmptySlot].ItemType = Type;
		Slots[EmptySlot].Quantity = RemainingAmount;
		RemainingAmount = 0;
		BroadcastSlotChanged(EmptySlot);
	}
}

int32 UInventoryComponent::RemoveItem(EItemType Type, int32 Amount)
{
	if (Type == EItemType::None || Amount <= 0)
	{
		return 0;
	}

	int32 TotalCount = GetItemCount(Type);
	if (TotalCount == 0)
	{
		return 0;
	}

	int32 AmountToRemove = FMath::Min(Amount, TotalCount);
	int32 RemainingToRemove = AmountToRemove;

	// Ukloni iz slotova koji sadrže ovaj item
	for (int32 i = 0; i < TotalSlots && RemainingToRemove > 0; ++i)
	{
		if (Slots[i].ItemType == Type)
		{
			int32 RemoveFromSlot = FMath::Min(RemainingToRemove, Slots[i].Quantity);
			Slots[i].Quantity -= RemoveFromSlot;
			RemainingToRemove -= RemoveFromSlot;

			if (Slots[i].Quantity <= 0)
			{
				Slots[i].Clear();
			}

			BroadcastSlotChanged(i);
		}
	}

	return AmountToRemove;
}

int32 UInventoryComponent::GetItemCount(EItemType Type) const
{
	if (Type == EItemType::None)
	{
		return 0;
	}

	int32 TotalCount = 0;
	for (const FInventorySlot& Slot : Slots)
	{
		if (Slot.ItemType == Type)
		{
			TotalCount += Slot.Quantity;
		}
	}
	return TotalCount;
}

bool UInventoryComponent::HasItem(EItemType Type, int32 Amount) const
{
	return GetItemCount(Type) >= Amount;
}

EBlockType UInventoryComponent::ItemTypeToBlockType(EItemType ItemType)
{
	switch (ItemType)
	{
	case EItemType::Dirt:
		return EBlockType::Dirt;
	case EItemType::Stone:
		return EBlockType::Stone;
	case EItemType::Grass:
		return EBlockType::Grass;
	case EItemType::OakLog:
		return EBlockType::OakLog;
	case EItemType::BirchLog:
		return EBlockType::BirchLog;
	default:
		return EBlockType::Air; // Ne može se postaviti
	}
}

EItemType UInventoryComponent::BlockTypeToItemType(EBlockType BlockType)
{
	switch (BlockType)
	{
	case EBlockType::Dirt:
		return EItemType::Dirt;
	case EBlockType::Stone:
		return EItemType::Stone;
	case EBlockType::Grass:
		return EItemType::Grass;
	case EBlockType::OakLog:
		return EItemType::OakLog;
	case EBlockType::BirchLog:
		return EItemType::BirchLog;
	default:
		return EItemType::None; // Nema odgovarajući item
	}
}

bool UInventoryComponent::CanItemBePlaced(EItemType ItemType)
{
	// Saplings se ne mogu postaviti
	if (ItemType == EItemType::OakSapling || ItemType == EItemType::BirchSapling)
	{
		return false;
	}

	// Provjeri ima li odgovarajući BlockType
	return ItemTypeToBlockType(ItemType) != EBlockType::Air;
}

FItemData UInventoryComponent::GetItemData(EItemType ItemType, UDataTable* ItemDataTable)
{
	if (!ItemDataTable)
	{
		return FItemData();
	}

	// Pretvori enum u string za Row Name
	FString RowName = UEnum::GetValueAsString(ItemType);
	// Ukloni "EItemType::" prefix ako postoji
	RowName.RemoveFromStart(TEXT("EItemType::"));

	FItemData* FoundRow = ItemDataTable->FindRow<FItemData>(FName(*RowName), TEXT("GetItemData"));
	if (FoundRow)
	{
		return *FoundRow;
	}

	return FItemData();
}

// ==================== Held Item API ====================

bool UInventoryComponent::PickUpItem(int32 SlotIndex)
{
	UE_LOG(LogTemp, Warning, TEXT("PickUpItem called with SlotIndex: %d"), SlotIndex);

	// Ako već držimo item, ne možemo uzeti drugi
	if (!HeldItem.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("PickUpItem FAILED: Already holding item"));
		return false;
	}

	// Provjeri valjanost indexa
	if (!IsValidSlotIndex(SlotIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("PickUpItem FAILED: Invalid slot index %d"), SlotIndex);
		return false;
	}

	UE_LOG(LogTemp, Warning, TEXT("PickUpItem: IsCraftingSlot=%d"), IsCraftingSlotIndex(SlotIndex) ? 1 : 0);

	// Specijalna logika za output slot
	if (IsCraftingOutputSlot(SlotIndex))
	{
		FInventorySlot* OutputSlot = &CraftingSlots[CraftingSlotToArrayIndex(SlotIndex)];

		if (OutputSlot->IsEmpty())
		{
			UE_LOG(LogTemp, Warning, TEXT("PickUpItem FAILED: Output slot is empty"));
			return false;
		}

		// Uzmi item iz output slota
		HeldItem = *OutputSlot;
		HeldItemSlotIndex = SlotIndex;
		OutputSlot->Clear();
		BroadcastSlotChanged(SlotIndex);

		// Pronađi i isprazni prvi non-empty input slot (potroši ingredient)
		for (int32 i = 0; i < CraftingInputSize; ++i)
		{
			if (!CraftingSlots[i].IsEmpty())
			{
				int32 InputSlotIndex = CraftingInputStartIndex + i;
				CraftingSlots[i].Quantity -= 1;
				if (CraftingSlots[i].Quantity <= 0)
				{
					CraftingSlots[i].Clear();
				}
				BroadcastSlotChanged(InputSlotIndex);
				UE_LOG(LogTemp, Warning, TEXT("PickUpItem: Consumed 1 item from input slot %d"), InputSlotIndex);
				break;
			}
		}

		// Ažuriraj output za sljedeći item
		UpdateCraftingOutput();

		UE_LOG(LogTemp, Warning, TEXT("PickUpItem SUCCESS: Picked up crafted item from output slot"));
		return true;
	}

	// Dohvati referencu na odgovarajući slot
	FInventorySlot* TargetSlot = nullptr;
	if (IsCraftingSlotIndex(SlotIndex))
	{
		TargetSlot = &CraftingSlots[CraftingSlotToArrayIndex(SlotIndex)];
	}
	else
	{
		TargetSlot = &Slots[SlotIndex];
	}

	// Ako je slot prazan, nema što uzeti
	if (TargetSlot->IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("PickUpItem FAILED: Slot %d is empty"), SlotIndex);
		return false;
	}

	// Uzmi item u ruku
	HeldItem = *TargetSlot;
	HeldItemSlotIndex = SlotIndex;

	// Isprazni slot
	TargetSlot->Clear();
	BroadcastSlotChanged(SlotIndex);

	// Ako je crafting input slot, ažuriraj output
	if (IsCraftingInputSlot(SlotIndex))
	{
		UpdateCraftingOutput();
	}

	UE_LOG(LogTemp, Warning, TEXT("PickUpItem SUCCESS: Picked up %d items from slot %d"), HeldItem.Quantity, SlotIndex);
	return true;
}

bool UInventoryComponent::PlaceItem(int32 SlotIndex)
{
	UE_LOG(LogTemp, Warning, TEXT("PlaceItem called with SlotIndex: %d"), SlotIndex);

	// Ako ne držimo ništa, nema što spustiti
	if (HeldItem.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("PlaceItem FAILED: Not holding any item"));
		return false;
	}

	// Provjeri valjanost indexa
	if (!IsValidSlotIndex(SlotIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("PlaceItem FAILED: Invalid slot index %d, IsCrafting=%d"), SlotIndex, IsCraftingSlotIndex(SlotIndex) ? 1 : 0);
		return false;
	}

	UE_LOG(LogTemp, Warning, TEXT("PlaceItem: Valid slot, IsCraftingSlot=%d"), IsCraftingSlotIndex(SlotIndex) ? 1 : 0);

	// Ne dozvoli stavljanje itema u output slot
	if (IsCraftingOutputSlot(SlotIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("PlaceItem FAILED: Cannot place items in output slot"));
		return false;
	}

	// Dohvati referencu na odgovarajući slot
	FInventorySlot* TargetSlot = nullptr;
	if (IsCraftingSlotIndex(SlotIndex))
	{
		TargetSlot = &CraftingSlots[CraftingSlotToArrayIndex(SlotIndex)];
	}
	else
	{
		TargetSlot = &Slots[SlotIndex];
	}

	// Ako slot nije prazan, ne možemo spustiti (za sada - swap dolazi kasnije)
	if (!TargetSlot->IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("PlaceItem FAILED: Slot %d is not empty"), SlotIndex);
		return false;
	}

	// Spusti item u slot
	*TargetSlot = HeldItem;
	BroadcastSlotChanged(SlotIndex);

	UE_LOG(LogTemp, Warning, TEXT("PlaceItem SUCCESS: Placed item in slot %d"), SlotIndex);

	// Isprazni ruku
	HeldItem.Clear();
	HeldItemSlotIndex = -1;

	// Ako je crafting input slot, ažuriraj output
	if (IsCraftingInputSlot(SlotIndex))
	{
		UpdateCraftingOutput();
	}

	return true;
}

void UInventoryComponent::ReturnHeldItem()
{
	// Ako ne držimo ništa, nema što vratiti
	if (HeldItem.IsEmpty())
	{
		return;
	}

	// Ako imamo valjan originalni slot, vrati tamo
	if (IsValidSlotIndex(HeldItemSlotIndex))
	{
		// Dohvati referencu na originalni slot
		FInventorySlot* OriginalSlot = nullptr;
		if (IsCraftingSlotIndex(HeldItemSlotIndex))
		{
			OriginalSlot = &CraftingSlots[CraftingSlotToArrayIndex(HeldItemSlotIndex)];
		}
		else
		{
			OriginalSlot = &Slots[HeldItemSlotIndex];
		}

		// Ako je originalni slot sada zauzet, swap
		if (!OriginalSlot->IsEmpty())
		{
			FInventorySlot TempSlot = *OriginalSlot;
			*OriginalSlot = HeldItem;
			BroadcastSlotChanged(HeldItemSlotIndex);

			// Pokušaj staviti swapani item negdje (samo u inventory, ne u crafting)
			int32 EmptySlot = FindFirstEmptySlot();
			if (EmptySlot != -1)
			{
				Slots[EmptySlot] = TempSlot;
				BroadcastSlotChanged(EmptySlot);
			}
			// Ako nema mjesta, item se gubi (edge case)
		}
		else
		{
			*OriginalSlot = HeldItem;
			BroadcastSlotChanged(HeldItemSlotIndex);
		}
	}
	else
	{
		// Nema originalni slot, pronađi bilo koji prazan (samo u inventory)
		int32 EmptySlot = FindFirstEmptySlot();
		if (EmptySlot != -1)
		{
			Slots[EmptySlot] = HeldItem;
			BroadcastSlotChanged(EmptySlot);
		}
		// Ako nema mjesta, item se gubi (edge case)
	}

	// Isprazni ruku
	HeldItem.Clear();
	HeldItemSlotIndex = -1;
}
