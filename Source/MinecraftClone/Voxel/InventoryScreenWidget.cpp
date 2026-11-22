#include "InventoryScreenWidget.h"
#include "InventorySlotWidget.h"
#include "InventoryComponent.h"
#include "CraftingPanel.h"
#include "PlayerInventoryPanel.h"
#include "FirstPersonCharacter.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Framework/Application/SlateApplication.h"

void UInventoryScreenWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Ekrani su skriveni dok ih character ne otvori (OpenScreen)
	SetVisibility(ESlateVisibility::Collapsed);

	ResolveInventoryComponent();
}

void UInventoryScreenWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Otpustanje se ne veze na slot (mis moze biti izvan svih slotova) - provjerava se stanje tipke
	if (bDragActive)
	{
		const FKey DragKey = bDragRight ? EKeys::RightMouseButton : EKeys::LeftMouseButton;
		if (!FSlateApplication::Get().GetPressedMouseButtons().Contains(DragKey))
		{
			FinishDrag();
		}
	}

	// Cursor slot prati mis (DPI-skalirano)
	if (CursorSlot && CursorSlot->IsVisible())
	{
		APlayerController* PC = GetOwningPlayer();
		float MouseX, MouseY;
		if (PC && PC->GetMousePosition(MouseX, MouseY))
		{
			const float Scale = UWidgetLayoutLibrary::GetViewportScale(this);
			const FVector2D NewPosition(MouseX / Scale - CursorOffset.X, MouseY / Scale - CursorOffset.Y);

			if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(CursorSlot->Slot))
			{
				CanvasSlot->SetPosition(NewPosition);
			}
		}
	}
}

void UInventoryScreenWidget::InitializeScreen()
{
	ResolveInventoryComponent();

	if (bInitialized)
	{
		return;
	}
	bInitialized = true;

	// Dijagnostika: paneli su BindWidgetOptional pa krivo ime instance u WBP-u
	// tiho zavrsi kao nullptr - ovaj log to cini vidljivim
	UE_LOG(LogTemp, Log, TEXT("InventoryScreen '%s': CraftingPanel=%s, InventoryPanel=%s"),
		*GetName(),
		CraftingPanel ? TEXT("OK") : TEXT("NIJE PRONADEN (ime instance mora biti 'CraftingPanel')"),
		InventoryPanel ? TEXT("OK") : TEXT("NIJE PRONADEN (ime instance mora biti 'InventoryPanel')"));

	if (CraftingPanel)
	{
		CraftingPanel->CreateSlots();
		CraftingPanel->OnSlotInteracted.AddUniqueDynamic(this, &UInventoryScreenWidget::HandleSlotInteracted);
	}

	if (InventoryPanel)
	{
		InventoryPanel->CreateSlots();
		InventoryPanel->OnSlotInteracted.AddUniqueDynamic(this, &UInventoryScreenWidget::HandleSlotInteracted);
	}

	CreateCursorSlot();
	RefreshAll();
}

void UInventoryScreenWidget::RefreshAll()
{
	if (CraftingPanel)
	{
		CraftingPanel->Refresh(InventoryComp);
	}
	if (InventoryPanel)
	{
		InventoryPanel->Refresh(InventoryComp);
	}
	UpdateCursorSlot();

	if (bDragActive)
	{
		ApplyDragPreview();
	}
}

bool UInventoryScreenWidget::CanDragInto(int32 SlotIndex) const
{
	if (!InventoryComp || !InventoryComp->IsHoldingItem() || InventoryComp->IsCraftingOutputSlot(SlotIndex))
	{
		return false;
	}

	// Crafting slot izvan aktivnog grida (3x3 slotovi dok je aktivan 2x2)
	const bool bCraftingIndex = SlotIndex >= UInventoryComponent::CraftingInputStartIndex;
	if (bCraftingIndex && !InventoryComp->IsCraftingInputSlot(SlotIndex))
	{
		return false;
	}

	const FInventorySlot Held = InventoryComp->GetHeldItem();
	const FInventorySlot Target = InventoryComp->GetSlot(SlotIndex);
	if (Target.IsEmpty())
	{
		return true;
	}
	return Target.ItemType == Held.ItemType && Target.Quantity < InventoryComp->GetMaxStackSize(Held.ItemType);
}

void UInventoryScreenWidget::ComputeDragAmounts(TArray<int32>& OutAmounts, int32& OutRemaining) const
{
	const FInventorySlot Held = InventoryComp->GetHeldItem();
	const int32 MaxStack = InventoryComp->GetMaxStackSize(Held.ItemType);

	// Desna tipka: po jedan; lijeva: jednaki dio, ostatak dijeljenja ostaje u ruci
	const int32 PerSlot = bDragRight ? 1 : (DragSlots.Num() > 0 ? Held.Quantity / DragSlots.Num() : 0);

	OutRemaining = Held.Quantity;
	OutAmounts.Reset(DragSlots.Num());
	for (const int32 SlotIndex : DragSlots)
	{
		const FInventorySlot Target = InventoryComp->GetSlot(SlotIndex);
		const int32 Space = MaxStack - (Target.IsEmpty() ? 0 : Target.Quantity);
		const int32 Amount = FMath::Max(0, FMath::Min3(PerSlot, Space, OutRemaining));
		OutAmounts.Add(Amount);
		OutRemaining -= Amount;
	}
}

void UInventoryScreenWidget::ApplyDragPreview()
{
	if (!InventoryComp || !InventoryComp->IsHoldingItem())
	{
		return;
	}

	TArray<int32> Amounts;
	int32 Remaining = 0;
	ComputeDragAmounts(Amounts, Remaining);

	const EItemType HeldType = InventoryComp->GetHeldItem().ItemType;
	for (int32 i = 0; i < DragSlots.Num(); ++i)
	{
		if (UInventorySlotWidget* SlotWidget = FindSlotWidget(DragSlots[i]))
		{
			const FInventorySlot Target = InventoryComp->GetSlot(DragSlots[i]);
			SlotWidget->SetSlotData(HeldType, (Target.IsEmpty() ? 0 : Target.Quantity) + Amounts[i]);
		}
	}

	if (CursorSlot)
	{
		CursorSlot->SetSlotData(HeldType, Remaining);
		CursorSlot->SetVisibility(Remaining > 0 ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Hidden);
	}
}

void UInventoryScreenWidget::FinishDrag()
{
	bDragActive = false;

	if (InventoryComp && InventoryComp->IsHoldingItem())
	{
		if (DragSlots.Num() >= 2)
		{
			TArray<int32> Amounts;
			int32 Remaining = 0;
			ComputeDragAmounts(Amounts, Remaining);

			for (int32 i = 0; i < DragSlots.Num(); ++i)
			{
				InventoryComp->PlaceAmount(DragSlots[i], Amounts[i]);
			}
		}
		else
		{
			// Jedan slot (ili nijedan valjan) = obican klik
			const int32 ClickSlot = DragSlots.Num() == 1 ? DragSlots[0] : DragPressSlot;
			if (bDragRight)
			{
				InventoryComp->PlaceOne(ClickSlot);
			}
			else
			{
				InventoryComp->PlaceItem(ClickSlot);
			}
		}
	}

	DragSlots.Reset();
	DragPressSlot = -1;
	RefreshAll();
}

bool UInventoryScreenWidget::RegisterLeftClick(int32 SlotIndex)
{
	const double Now = FPlatformTime::Seconds();
	const bool bDouble = SlotIndex == LastLeftClickSlot && (Now - LastLeftClickTime) < DoubleClickSeconds;

	// Nakon dvostrukog klika niz kreće ispočetka (treći klik nije opet dvostruki)
	LastLeftClickSlot = bDouble ? -1 : SlotIndex;
	LastLeftClickTime = Now;
	return bDouble;
}

void UInventoryScreenWidget::QuickMoveAllOfType(int32 ClickedSlot)
{
	// Dio inventara: 0 = main, 1 = hotbar, 2 = crafting grid
	auto SectionOf = [](int32 Index)
	{
		if (Index >= UInventoryComponent::CraftingInputStartIndex) return 2;
		return Index >= UInventoryComponent::HotbarStartIndex ? 1 : 0;
	};

	const int32 Section = SectionOf(ClickedSlot);
	for (int32 Index = 0; Index < UInventoryComponent::CraftingOutputIndex; ++Index)
	{
		if (SectionOf(Index) == Section && InventoryComp->GetSlot(Index).ItemType == LastQuickMovedType)
		{
			InventoryComp->QuickMove(Index);
		}
	}
}

UInventorySlotWidget* UInventoryScreenWidget::FindSlotWidget(int32 SlotIndex) const
{
	UInventorySlotWidget* SlotWidget = CraftingPanel ? CraftingPanel->FindSlotWidget(SlotIndex) : nullptr;
	if (!SlotWidget && InventoryPanel)
	{
		SlotWidget = InventoryPanel->FindSlotWidget(SlotIndex);
	}
	return SlotWidget;
}

void UInventoryScreenWidget::HandleSlotInteracted(int32 SlotIndex, EInventoryClickType ClickType)
{
	if (!InventoryComp)
	{
		return;
	}

	if (ClickType == EInventoryClickType::DragEnter)
	{
		// Svaki slot najvise jednom po potezu; ne vise slotova nego itema u ruci
		if (bDragActive && !DragSlots.Contains(SlotIndex) && CanDragInto(SlotIndex)
			&& DragSlots.Num() < InventoryComp->GetHeldItem().Quantity)
		{
			DragSlots.Add(SlotIndex);
			RefreshAll();
		}
		return;
	}

	// Drugi pritisak tijekom poteza se ignorira
	if (bDragActive)
	{
		return;
	}

	const bool bLeftPress = ClickType == EInventoryClickType::Left || ClickType == EInventoryClickType::ShiftLeft;
	const bool bDoubleClick = bLeftPress && RegisterLeftClick(SlotIndex);

	// Dvostruki klik s itemom u ruci: skupi sve istog tipa (umjesto poteza)
	if (bDoubleClick && ClickType == EInventoryClickType::Left && InventoryComp->IsHoldingItem())
	{
		InventoryComp->CollectAll();
		RefreshAll();
		return;
	}

	// Shift+dvostruki klik: premjesti sve iteme zadnje premjestenog tipa
	if (bDoubleClick && ClickType == EInventoryClickType::ShiftLeft && LastQuickMovedType != EItemType::None)
	{
		QuickMoveAllOfType(SlotIndex);
		RefreshAll();
		return;
	}

	// Pritisak s itemom u ruci zapocinje potez (output slot crafta odmah)
	if (InventoryComp->IsHoldingItem() && !InventoryComp->IsCraftingOutputSlot(SlotIndex)
		&& (ClickType == EInventoryClickType::Left || ClickType == EInventoryClickType::Right))
	{
		bDragActive = true;
		bDragRight = (ClickType == EInventoryClickType::Right);
		DragPressSlot = SlotIndex;
		DragSlots.Reset();
		if (CanDragInto(SlotIndex))
		{
			DragSlots.Add(SlotIndex);
		}
		RefreshAll();
		return;
	}

	switch (ClickType)
	{
	case EInventoryClickType::ShiftLeft:
	{
		const EItemType MovedType = InventoryComp->GetSlot(SlotIndex).ItemType;
		if (InventoryComp->QuickMove(SlotIndex))
		{
			LastQuickMovedType = MovedType;
		}
		break;
	}

	case EInventoryClickType::Right:
		if (InventoryComp->IsHoldingItem())
		{
			InventoryComp->PlaceOne(SlotIndex);
		}
		else
		{
			InventoryComp->PickUpHalf(SlotIndex);
		}
		break;

	case EInventoryClickType::Left:
	default:
		if (InventoryComp->IsHoldingItem())
		{
			InventoryComp->PlaceItem(SlotIndex);
		}
		else
		{
			InventoryComp->PickUpItem(SlotIndex);
		}
		break;
	}

	RefreshAll();
}

void UInventoryScreenWidget::UpdateCursorSlot()
{
	if (!CursorSlot || !InventoryComp)
	{
		return;
	}

	if (InventoryComp->IsHoldingItem())
	{
		const FInventorySlot HeldItem = InventoryComp->GetHeldItem();
		CursorSlot->SetSlotData(HeldItem.ItemType, HeldItem.Quantity);
		CursorSlot->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		CursorSlot->SetVisibility(ESlateVisibility::Hidden);
	}
}

void UInventoryScreenWidget::ResolveInventoryComponent()
{
	if (InventoryComp)
	{
		return;
	}

	APlayerController* PC = GetOwningPlayer();
	if (AFirstPersonCharacter* Character = PC ? Cast<AFirstPersonCharacter>(PC->GetPawn()) : nullptr)
	{
		InventoryComp = Character->GetInventoryComponent();
	}
}

void UInventoryScreenWidget::CreateCursorSlot()
{
	if (!CanvasPanel || !CursorSlotWidgetClass || CursorSlot)
	{
		return;
	}

	CursorSlot = CreateWidget<UInventorySlotWidget>(GetOwningPlayer(), CursorSlotWidgetClass);
	if (CursorSlot)
	{
		CursorSlot->SlotIndex = -1;   // -1 = cursor slot
		CursorSlot->bTransparentBackground = true;   // prije AddChildToCanvas: NativeConstruct poziva SetSelected
		CursorSlot->SetSelected(false);
		CursorSlot->SetVisibility(ESlateVisibility::Hidden);

		if (UCanvasPanelSlot* CanvasSlot = CanvasPanel->AddChildToCanvas(CursorSlot))
		{
			CanvasSlot->SetSize(CursorSlotSize);
			CanvasSlot->SetPosition(FVector2D::ZeroVector);
		}
	}
}
