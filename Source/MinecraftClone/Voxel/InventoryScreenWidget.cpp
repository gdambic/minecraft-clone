#include "InventoryScreenWidget.h"
#include "InventorySlotWidget.h"
#include "InventoryComponent.h"
#include "CraftingPanel.h"
#include "PlayerInventoryPanel.h"
#include "FirstPersonCharacter.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetLayoutLibrary.h"

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
}

void UInventoryScreenWidget::HandleSlotInteracted(int32 SlotIndex, EInventoryClickType ClickType)
{
	if (!InventoryComp)
	{
		return;
	}

	switch (ClickType)
	{
	case EInventoryClickType::ShiftLeft:
		InventoryComp->QuickMove(SlotIndex);
		break;

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
		CursorSlot->SetVisibility(ESlateVisibility::Hidden);

		if (UCanvasPanelSlot* CanvasSlot = CanvasPanel->AddChildToCanvas(CursorSlot))
		{
			CanvasSlot->SetSize(CursorSlotSize);
			CanvasSlot->SetPosition(FVector2D::ZeroVector);
		}
	}
}
