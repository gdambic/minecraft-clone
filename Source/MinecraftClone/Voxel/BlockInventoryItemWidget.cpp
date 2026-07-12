#include "BlockInventoryItemWidget.h"
#include "Components/TextBlock.h"

void UBlockInventoryItemWidget::SetItemType(EItemType NewItemType)
{
	ItemType = NewItemType;

	if (TextItemName)
	{
		// Konvertiraj enum u string
		FString EnumString = UEnum::GetValueAsString(NewItemType);
		// Ukloni "EItemType::" prefix
		EnumString.RemoveFromStart(TEXT("EItemType::"));

		TextItemName->SetText(FText::FromString(EnumString));
	}
}

void UBlockInventoryItemWidget::SetItemCount(int32 Count)
{
	ItemCount = Count;

	if (TextItemCount)
	{
		TextItemCount->SetText(FText::AsNumber(Count));
	}
}

void UBlockInventoryItemWidget::SetSelected(bool bSelected)
{
	bIsSelected = bSelected;

	if (TextItemName)
	{
		FSlateColor NewColor = bSelected ? FSlateColor(SelectedColor) : FSlateColor(DefaultColor);
		TextItemName->SetColorAndOpacity(NewColor);
	}
}
