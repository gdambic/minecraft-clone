#include "InventorySlotWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "Styling/SlateColor.h"

void UInventorySlotWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Bind na button ako postoji
	if (ClickButton)
	{
		ClickButton->OnClicked.AddDynamic(this, &UInventorySlotWidget::OnClickButtonClicked);
	}

	// Postavi početno stanje - prazan, neselektiran slot
	SetSelected(false);
	ClearSlotVisual();
}

FReply UInventorySlotWidget::NativeOnMouseButtonDown(const FGeometry& InGeometry, const FPointerEvent& InMouseEvent)
{
	// Ako nema buttona, koristi direktni mouse input
	if (!ClickButton && InMouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		OnSlotClicked.Broadcast(SlotIndex);
		return FReply::Handled();
	}

	return Super::NativeOnMouseButtonDown(InGeometry, InMouseEvent);
}

void UInventorySlotWidget::OnClickButtonClicked()
{
	OnSlotClicked.Broadcast(SlotIndex);
}

void UInventorySlotWidget::SetSelected(bool bSelected)
{
	bIsSelected = bSelected;

	if (SlotBorder)
	{
		SlotBorder->SetBrushColor(bSelected ? SelectedBorderColor : DefaultBorderColor);
	}
}

void UInventorySlotWidget::SetSlotData(EItemType ItemType, int32 Quantity)
{
	// Spremi trenutno stanje
	CurrentItemType = ItemType;
	CurrentQuantity = Quantity;
	bIsEmpty = (ItemType == EItemType::None || Quantity <= 0);

	if (bIsEmpty)
	{
		ClearSlotVisual();
	}
	else
	{
		// Dohvati podatke o itemu
		FItemData Data = GetItemData(ItemType);

		UpdateIconVisual(Data);
		UpdateQuantityVisual(Quantity);
		UpdateTooltip(Data);
	}
}

UTexture2D* UInventorySlotWidget::GetItemIcon(EItemType ItemType) const
{
	FItemData Data = GetItemData(ItemType);
	return Data.Icon.LoadSynchronous();
}

FItemData UInventorySlotWidget::GetItemData(EItemType ItemType) const
{
	if (!ItemDataTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("UInventorySlotWidget::GetItemData - ItemDataTable is not set!"));
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

	UE_LOG(LogTemp, Warning, TEXT("UInventorySlotWidget::GetItemData - Row '%s' not found in DataTable"), *RowName);
	return FItemData();
}

void UInventorySlotWidget::UpdateIconVisual(const FItemData& Data)
{
	if (!ItemIcon)
	{
		return;
	}

	ItemIcon->SetVisibility(ESlateVisibility::Visible);

	// Učitaj i postavi teksturu
	UTexture2D* IconTexture = Data.Icon.LoadSynchronous();
	if (IconTexture)
	{
		ItemIcon->SetBrushFromTexture(IconTexture, true);
	}
	else
	{
		// Ako nema ikone, prikaži placeholder (transparentno)
		ItemIcon->SetBrushTintColor(FSlateColor(FLinearColor::Transparent));
	}
}

void UInventorySlotWidget::UpdateQuantityVisual(int32 Quantity)
{
	if (!QuantityText)
	{
		return;
	}

	// Prikaži količinu samo ako je > 1 (Minecraft stil)
	if (Quantity > 1)
	{
		QuantityText->SetVisibility(ESlateVisibility::Visible);
		QuantityText->SetText(FText::AsNumber(Quantity));
	}
	else
	{
		QuantityText->SetVisibility(ESlateVisibility::Hidden);
	}
}

void UInventorySlotWidget::UpdateTooltip(const FItemData& Data)
{
	// Postavi tooltip na DisplayName itema
	SetToolTipText(Data.DisplayName);
}

void UInventorySlotWidget::ClearSlotVisual()
{
	// Sakrij ikonu
	if (ItemIcon)
	{
		ItemIcon->SetVisibility(ESlateVisibility::Hidden);
	}

	// Sakrij količinu
	if (QuantityText)
	{
		QuantityText->SetVisibility(ESlateVisibility::Hidden);
	}

	// Isprazni tooltip
	SetToolTipText(FText::GetEmpty());
}
