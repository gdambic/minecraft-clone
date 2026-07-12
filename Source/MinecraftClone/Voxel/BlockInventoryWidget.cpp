#include "BlockInventoryWidget.h"
#include "BlockInventoryItemWidget.h"
#include "FirstPersonCharacter.h"

void UBlockInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();
}

void UBlockInventoryWidget::NativeDestruct()
{
	UnbindDelegates();
	Super::NativeDestruct();
}

void UBlockInventoryWidget::InitializeBlockInventory()
{
	// Pronađi player charactera
	APlayerController* PC = GetOwningPlayer();
	if (PC)
	{
		PlayerCharacter = Cast<AFirstPersonCharacter>(PC->GetPawn());
	}

	// Bindaj delegate
	BindDelegates();

	// Početni refresh
	RefreshUI();
}

void UBlockInventoryWidget::UpdateBlockHighlight(int32 NewIndex)
{
	for (int32 i = 0; i < BlockTextWidgets.Num(); i++)
	{
		if (BlockTextWidgets[i])
		{
			bool bIsSelected = (i == NewIndex);
			BlockTextWidgets[i]->SetSelected(bIsSelected);
		}
	}

	CurrentSelectedIndex = NewIndex;
}

void UBlockInventoryWidget::HandleInventoryChanged(EItemType ItemType, int32 OldCount, int32 NewCount)
{
	// Refresh UI kada se inventar promijeni
	RefreshUI();
}

void UBlockInventoryWidget::HandleSelectedItemChanged(int32 NewIndex, EItemType NewItemType)
{
	// Ažuriraj highlight
	UpdateBlockHighlight(NewIndex);

	// Refresh UI
	RefreshUI();
}

void UBlockInventoryWidget::RefreshUI()
{
	// Bazna implementacija - može se override-ati u Blueprintu
	// ili proširiti za specifične potrebe
}

void UBlockInventoryWidget::BindDelegates()
{
	if (PlayerCharacter)
	{
		PlayerCharacter->OnSelectedItemChanged.AddDynamic(this, &UBlockInventoryWidget::OnSelectedItemChangedHandler);
	}
}

void UBlockInventoryWidget::UnbindDelegates()
{
	if (PlayerCharacter)
	{
		PlayerCharacter->OnSelectedItemChanged.RemoveDynamic(this, &UBlockInventoryWidget::OnSelectedItemChangedHandler);
	}
}

void UBlockInventoryWidget::OnSelectedItemChangedHandler(int32 NewIndex, EItemType NewItemType)
{
	HandleSelectedItemChanged(NewIndex, NewItemType);
}
