#include "CraftingPanel.h"
#include "InventorySlotWidget.h"
#include "InventoryComponent.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"

void UCraftingPanel::CreateSlots()
{
	if (!CraftingGrid || !OutputBox || !SlotWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("CraftingPanel: nedostaje BindWidget ili SlotWidgetClass"));
		return;
	}

	// Input slotovi (NxN, indeksi od CraftingInputStartIndex, kompaktno row-major)
	CraftingGrid->ClearChildren();
	InputSlotWidgets.Empty();

	const int32 InputCount = GridSize * GridSize;
	for (int32 i = 0; i < InputCount; i++)
	{
		UInventorySlotWidget* SlotWidget = CreateSlotWidget(UInventoryComponent::CraftingInputStartIndex + i);
		if (SlotWidget)
		{
			UUniformGridSlot* GridSlot = CraftingGrid->AddChildToUniformGrid(SlotWidget, i / GridSize, i % GridSize);
			if (GridSlot)
			{
				GridSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
				GridSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
			}
			InputSlotWidgets.Add(SlotWidget);
		}
	}

	// Output slot
	OutputBox->ClearChildren();

	OutputSlotWidget = CreateSlotWidget(UInventoryComponent::CraftingOutputIndex);
	if (OutputSlotWidget)
	{
		UHorizontalBoxSlot* BoxSlot = OutputBox->AddChildToHorizontalBox(OutputSlotWidget);
		if (BoxSlot)
		{
			BoxSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			BoxSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		}
	}
}

void UCraftingPanel::Refresh(const UInventoryComponent* InventoryComp)
{
	if (!InventoryComp)
	{
		return;
	}

	for (int32 i = 0; i < InputSlotWidgets.Num(); i++)
	{
		if (InputSlotWidgets[i])
		{
			const FInventorySlot SlotData = InventoryComp->GetSlot(UInventoryComponent::CraftingInputStartIndex + i);
			InputSlotWidgets[i]->SetSlotData(SlotData.ItemType, SlotData.Quantity);
		}
	}

	if (OutputSlotWidget)
	{
		const FInventorySlot SlotData = InventoryComp->GetSlot(UInventoryComponent::CraftingOutputIndex);
		OutputSlotWidget->SetSlotData(SlotData.ItemType, SlotData.Quantity);
	}
}

UInventorySlotWidget* UCraftingPanel::CreateSlotWidget(int32 SlotIndex)
{
	UInventorySlotWidget* SlotWidget = CreateWidget<UInventorySlotWidget>(GetOwningPlayer(), SlotWidgetClass);
	if (SlotWidget)
	{
		SlotWidget->SlotIndex = SlotIndex;
		SlotWidget->OnSlotClicked.AddDynamic(this, &UCraftingPanel::HandleSlotClicked);
	}
	return SlotWidget;
}

void UCraftingPanel::HandleSlotClicked(int32 SlotIndex)
{
	OnSlotClicked.Broadcast(SlotIndex);
}
