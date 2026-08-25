#include "InventorySlotWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "BlockRegistry.h"
#include "Engine/Texture2D.h"
#include "Styling/SlateColor.h"

namespace
{
	/**
	 * Ucitaj generiranu ikonu bloka po konvenciji:
	 * Content/Items/Generated/T_Item_<Block> (generira je editor alat
	 * Tools > MinecraftClone > Generate Items Sprites iz Items.json "display").
	 */
	UTexture2D* LoadGeneratedItemIcon(const FItemDefinition& Def)
	{
		if (Def.Display.Type != TEXT("block") || Def.Display.Block.IsEmpty())
		{
			return nullptr;
		}

		const FString Path = FString::Printf(
			TEXT("/Game/Items/Generated/T_Item_%s.T_Item_%s"),
			*Def.Display.Block, *Def.Display.Block);
		return Cast<UTexture2D>(FSoftObjectPath(Path).TryLoad());
	}
}

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
		// Dohvati podatke o itemu (naziv za tooltip)
		FItemDefinition Data = GetItemData(ItemType);

		UpdateIconVisual(ItemType);
		UpdateQuantityVisual(Quantity);
		UpdateTooltip(Data);
	}
}

UTexture2D* UInventorySlotWidget::GetItemIcon(EItemType ItemType) const
{
	return LoadGeneratedItemIcon(GetItemData(ItemType));
}

FItemDefinition UInventorySlotWidget::GetItemData(EItemType ItemType) const
{
	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	if (!Registry)
	{
		UE_LOG(LogTemp, Warning, TEXT("UInventorySlotWidget::GetItemData - BlockRegistry not available!"));
		return FItemDefinition();
	}

	return Registry->GetItemDefinitionCopy(ItemType);
}

void UInventorySlotWidget::UpdateIconVisual(EItemType ItemType)
{
	if (!ItemIcon)
	{
		return;
	}

	ItemIcon->SetVisibility(ESlateVisibility::Visible);

	// Generirani izometrijski sprite bloka (Items.json "display" -> T_Item_<Block>)
	UTexture2D* IconTexture = LoadGeneratedItemIcon(GetItemData(ItemType));
	if (IconTexture)
	{
		ItemIcon->SetBrushTintColor(FSlateColor(FLinearColor::White));
		ItemIcon->SetBrushFromTexture(IconTexture, false);
	}
	else
	{
		// Item bez block prikaza (alat, hrana, sadnica...) - placeholder (transparentno)
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

void UInventorySlotWidget::UpdateTooltip(const FItemDefinition& Data)
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
