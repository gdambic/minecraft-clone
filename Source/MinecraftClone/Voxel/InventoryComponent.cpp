#include "InventoryComponent.h"
#include "Engine/DataTable.h"

UInventoryComponent::UInventoryComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Inicijaliziraj 36 praznih slotova
	Slots.SetNum(TotalSlots);
}

void UInventoryComponent::BeginPlay()
{
	Super::BeginPlay();
}

// ==================== Slot-based API ====================

FInventorySlot UInventoryComponent::GetSlot(int32 SlotIndex) const
{
	if (IsValidSlotIndex(SlotIndex))
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

	FInventorySlot OldSlot = Slots[SlotIndex];

	if (Type == EItemType::None || Quantity <= 0)
	{
		Slots[SlotIndex].Clear();
	}
	else
	{
		Slots[SlotIndex].ItemType = Type;
		Slots[SlotIndex].Quantity = Quantity;
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

	// Swap
	FInventorySlot TempSlot = Slots[SlotA];
	Slots[SlotA] = Slots[SlotB];
	Slots[SlotB] = TempSlot;

	BroadcastSlotChanged(SlotA);
	BroadcastSlotChanged(SlotB);

	return true;
}

bool UInventoryComponent::IsValidSlotIndex(int32 SlotIndex) const
{
	return SlotIndex >= 0 && SlotIndex < TotalSlots;
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
		OnSlotChanged.Broadcast(SlotIndex, Slots[SlotIndex]);
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
	// Ako već držimo item, ne možemo uzeti drugi
	if (!HeldItem.IsEmpty())
	{
		return false;
	}

	// Provjeri valjanost indexa
	if (!IsValidSlotIndex(SlotIndex))
	{
		return false;
	}

	// Ako je slot prazan, nema što uzeti
	if (Slots[SlotIndex].IsEmpty())
	{
		return false;
	}

	// Uzmi item u ruku
	HeldItem = Slots[SlotIndex];
	HeldItemSlotIndex = SlotIndex;

	// Isprazni slot
	Slots[SlotIndex].Clear();
	BroadcastSlotChanged(SlotIndex);

	return true;
}

bool UInventoryComponent::PlaceItem(int32 SlotIndex)
{
	// Ako ne držimo ništa, nema što spustiti
	if (HeldItem.IsEmpty())
	{
		return false;
	}

	// Provjeri valjanost indexa
	if (!IsValidSlotIndex(SlotIndex))
	{
		return false;
	}

	// Ako slot nije prazan, ne možemo spustiti (za sada - swap dolazi kasnije)
	if (!Slots[SlotIndex].IsEmpty())
	{
		return false;
	}

	// Spusti item u slot
	Slots[SlotIndex] = HeldItem;
	BroadcastSlotChanged(SlotIndex);

	// Isprazni ruku
	HeldItem.Clear();
	HeldItemSlotIndex = -1;

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
		// Ako je originalni slot sada zauzet, swap
		if (!Slots[HeldItemSlotIndex].IsEmpty())
		{
			FInventorySlot TempSlot = Slots[HeldItemSlotIndex];
			Slots[HeldItemSlotIndex] = HeldItem;
			BroadcastSlotChanged(HeldItemSlotIndex);

			// Pokušaj staviti swapani item negdje
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
			Slots[HeldItemSlotIndex] = HeldItem;
			BroadcastSlotChanged(HeldItemSlotIndex);
		}
	}
	else
	{
		// Nema originalni slot, pronađi bilo koji prazan
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
