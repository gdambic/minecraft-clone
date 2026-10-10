#include "PlayerInventoryPanel.h"
#include "InventorySlotWidget.h"
#include "InventoryComponent.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"

void UPlayerInventoryPanel::CreateSlots()
{
	if (!MainInventoryGrid || !HotbarRow || !SlotWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("PlayerInventoryPanel: nedostaje BindWidget ili SlotWidgetClass"));
		return;
	}

	// Main slotovi (0-26, 3 reda x 9 stupaca)
	MainInventoryGrid->ClearChildren();
	MainSlotWidgets.Empty();

	for (int32 i = 0; i < UInventoryComponent::MainInventorySize; i++)
	{
		UInventorySlotWidget* SlotWidget = CreateSlotWidget(i);
		if (SlotWidget)
		{
			UUniformGridSlot* GridSlot = MainInventoryGrid->AddChildToUniformGrid(SlotWidget, i / 9, i % 9);
			if (GridSlot)
			{
				GridSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
				GridSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
			}
			MainSlotWidgets.Add(SlotWidget);
		}
	}

	// Hotbar slotovi (27-35)
	HotbarRow->ClearChildren();
	HotbarSlotWidgets.Empty();

	for (int32 i = 0; i < UInventoryComponent::HotbarSize; i++)
	{
		UInventorySlotWidget* SlotWidget = CreateSlotWidget(UInventoryComponent::HotbarStartIndex + i);
		if (SlotWidget)
		{
			UHorizontalBoxSlot* BoxSlot = HotbarRow->AddChildToHorizontalBox(SlotWidget);
			if (BoxSlot)
			{
				BoxSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
				BoxSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
			}
			HotbarSlotWidgets.Add(SlotWidget);
		}
	}
}

void UPlayerInventoryPanel::Refresh(const UInventoryComponent* InventoryComp)
{
	if (!InventoryComp)
	{
		return;
	}

	for (int32 i = 0; i < MainSlotWidgets.Num(); i++)
	{
		if (MainSlotWidgets[i])
		{
			const FInventorySlot SlotData = InventoryComp->GetSlot(i);
			MainSlotWidgets[i]->SetSlotData(SlotData.ItemType, SlotData.Quantity);
		}
	}

	for (int32 i = 0; i < HotbarSlotWidgets.Num(); i++)
	{
		if (HotbarSlotWidgets[i])
		{
			const FInventorySlot SlotData = InventoryComp->GetSlot(UInventoryComponent::HotbarStartIndex + i);
			HotbarSlotWidgets[i]->SetSlotData(SlotData.ItemType, SlotData.Quantity);
		}
	}
}

UInventorySlotWidget* UPlayerInventoryPanel::CreateSlotWidget(int32 SlotIndex)
{
	UInventorySlotWidget* SlotWidget = CreateWidget<UInventorySlotWidget>(GetOwningPlayer(), SlotWidgetClass);
	if (SlotWidget)
	{
		SlotWidget->SlotIndex = SlotIndex;
		SlotWidget->OnSlotClicked.AddDynamic(this, &UPlayerInventoryPanel::HandleSlotClicked);
	}
	return SlotWidget;
}

void UPlayerInventoryPanel::HandleSlotClicked(int32 SlotIndex)
{
	OnSlotClicked.Broadcast(SlotIndex);
}
