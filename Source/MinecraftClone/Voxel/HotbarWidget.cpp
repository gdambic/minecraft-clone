#include "HotbarWidget.h"
#include "InventorySlotWidget.h"
#include "FirstPersonCharacter.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Engine/DataTable.h"

void UHotbarWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Automatski pronađi player charactera ako nije postavljen
	if (!PlayerCharacter)
	{
		APlayerController* PC = GetOwningPlayer();
		if (PC)
		{
			PlayerCharacter = Cast<AFirstPersonCharacter>(PC->GetPawn());
		}
	}

	// Bind na delegate ako imamo player charactera
	if (PlayerCharacter)
	{
		if (UInventoryComponent* InvComp = PlayerCharacter->GetInventoryComponent())
		{
			InvComp->OnSlotChanged.AddDynamic(this, &UHotbarWidget::OnSlotChangedHandler);
		}
		PlayerCharacter->OnSelectedItemChanged.AddDynamic(this, &UHotbarWidget::OnSelectedItemChangedHandler);
	}
}

void UHotbarWidget::NativeDestruct()
{
	// Unbind delegate
	if (PlayerCharacter)
	{
		if (UInventoryComponent* InvComp = PlayerCharacter->GetInventoryComponent())
		{
			InvComp->OnSlotChanged.RemoveDynamic(this, &UHotbarWidget::OnSlotChangedHandler);
		}
		PlayerCharacter->OnSelectedItemChanged.RemoveDynamic(this, &UHotbarWidget::OnSelectedItemChangedHandler);
	}

	Super::NativeDestruct();
}

void UHotbarWidget::CreateHotbarSlots()
{
	if (!SlotContainer)
	{
		UE_LOG(LogTemp, Warning, TEXT("UHotbarWidget::CreateHotbarSlots - SlotContainer is not bound!"));
		return;
	}

	if (!InventorySlotWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("UHotbarWidget::CreateHotbarSlots - InventorySlotWidgetClass is not set!"));
		return;
	}

	// Očisti postojeće slotove
	SlotContainer->ClearChildren();
	SlotWidgets.Empty();

	// Kreiraj 9 slotova
	for (int32 i = 0; i < HotbarSlotCount; i++)
	{
		UInventorySlotWidget* SlotWidget = CreateWidget<UInventorySlotWidget>(GetOwningPlayer(), InventorySlotWidgetClass);
		if (SlotWidget)
		{
			// Postavi ItemDataTable na slot
			SlotWidget->ItemDataTable = ItemDataTable;

			// Dodaj u kontejner
			UHorizontalBoxSlot* BoxSlot = SlotContainer->AddChildToHorizontalBox(SlotWidget);
			if (BoxSlot)
			{
				BoxSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
				BoxSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
			}

			// Dodaj u array
			SlotWidgets.Add(SlotWidget);
		}
	}

	// Bind delegate ako još nije (može se desiti da NativeConstruct nije našao playera)
	if (PlayerCharacter)
	{
		if (UInventoryComponent* InvComp = PlayerCharacter->GetInventoryComponent())
		{
			// Unbind prvo da ne bude duplikata
			InvComp->OnSlotChanged.RemoveDynamic(this, &UHotbarWidget::OnSlotChangedHandler);
			InvComp->OnSlotChanged.AddDynamic(this, &UHotbarWidget::OnSlotChangedHandler);
		}
		PlayerCharacter->OnSelectedItemChanged.RemoveDynamic(this, &UHotbarWidget::OnSelectedItemChangedHandler);
		PlayerCharacter->OnSelectedItemChanged.AddDynamic(this, &UHotbarWidget::OnSelectedItemChangedHandler);
	}

	// Automatski osvježi sve slotove nakon kreiranja
	RefreshAllSlots();
}

void UHotbarWidget::HandleSlotChanged(int32 SlotIndex, FInventorySlot NewSlot)
{
	// Provjeri je li slot u hotbar rasponu (27-35)
	if (SlotIndex < HotbarStartIndex || SlotIndex > HotbarStartIndex + HotbarSlotCount - 1)
	{
		return;
	}

	// Konvertiraj u hotbar index
	int32 HotbarIndex = InventoryToHotbarIndex(SlotIndex);

	// Ažuriraj widget
	if (IsValidHotbarIndex(HotbarIndex))
	{
		UInventorySlotWidget* SlotWidget = SlotWidgets[HotbarIndex];
		if (SlotWidget)
		{
			SlotWidget->SetSlotData(NewSlot.ItemType, NewSlot.Quantity);
		}
	}
}

void UHotbarWidget::SelectedItemChanged(int32 NewIndex, EItemType NewItemType)
{
	// Provjeri je li stari index validan i deselektiraj
	if (IsValidHotbarIndex(CurrentSelectedIndex))
	{
		if (UInventorySlotWidget* OldSlot = SlotWidgets[CurrentSelectedIndex])
		{
			OldSlot->SetSelected(false);
		}
	}

	// Provjeri je li novi index validan i selektiraj
	if (IsValidHotbarIndex(NewIndex))
	{
		if (UInventorySlotWidget* NewSlot = SlotWidgets[NewIndex])
		{
			NewSlot->SetSelected(true);
		}
	}

	// Ažuriraj trenutni index
	CurrentSelectedIndex = NewIndex;
}

void UHotbarWidget::RefreshAllSlots()
{
	// Pokušaj pronaći player charactera ako nije postavljen
	if (!PlayerCharacter)
	{
		APlayerController* PC = GetOwningPlayer();
		if (PC)
		{
			PlayerCharacter = Cast<AFirstPersonCharacter>(PC->GetPawn());

			// Ako smo sada pronašli playera, bindaj delegate
			if (PlayerCharacter)
			{
				if (UInventoryComponent* InvComp = PlayerCharacter->GetInventoryComponent())
				{
					InvComp->OnSlotChanged.RemoveDynamic(this, &UHotbarWidget::OnSlotChangedHandler);
					InvComp->OnSlotChanged.AddDynamic(this, &UHotbarWidget::OnSlotChangedHandler);
				}
				PlayerCharacter->OnSelectedItemChanged.RemoveDynamic(this, &UHotbarWidget::OnSelectedItemChangedHandler);
				PlayerCharacter->OnSelectedItemChanged.AddDynamic(this, &UHotbarWidget::OnSelectedItemChangedHandler);
			}
		}
	}

	UInventoryComponent* InvComp = GetInventoryComponent();
	if (!InvComp)
	{
		return;
	}

	// Dohvati selected index od playera
	int32 SelectedIndex = 0;
	if (PlayerCharacter)
	{
		SelectedIndex = PlayerCharacter->SelectedItemIndex;
	}

	// Iteriraj kroz sve hotbar slotove
	for (int32 i = 0; i < HotbarSlotCount; i++)
	{
		if (!IsValidHotbarIndex(i))
		{
			continue;
		}

		// Dohvati slot iz inventara (offset +27 za hotbar)
		int32 InventorySlotIndex = HotbarStartIndex + i;
		FInventorySlot SlotData = InvComp->GetSlot(InventorySlotIndex);

		// Ažuriraj widget
		UInventorySlotWidget* SlotWidget = SlotWidgets[i];
		if (SlotWidget)
		{
			SlotWidget->SetSlotData(SlotData.ItemType, SlotData.Quantity);

			// Postavi selekciju
			bool bIsSelected = (i == SelectedIndex);
			SlotWidget->SetSelected(bIsSelected);
		}
	}

	// Ažuriraj trenutni selektirani index
	CurrentSelectedIndex = SelectedIndex;
}

UInventorySlotWidget* UHotbarWidget::GetSlotWidget(int32 Index) const
{
	if (IsValidHotbarIndex(Index))
	{
		return SlotWidgets[Index];
	}
	return nullptr;
}

void UHotbarWidget::SetPlayerCharacter(AFirstPersonCharacter* Character)
{
	// Unbind od starog charactera
	if (PlayerCharacter)
	{
		if (UInventoryComponent* OldInvComp = PlayerCharacter->GetInventoryComponent())
		{
			OldInvComp->OnSlotChanged.RemoveDynamic(this, &UHotbarWidget::OnSlotChangedHandler);
		}
		PlayerCharacter->OnSelectedItemChanged.RemoveDynamic(this, &UHotbarWidget::OnSelectedItemChangedHandler);
	}

	PlayerCharacter = Character;

	// Bind na novog charactera
	if (PlayerCharacter)
	{
		if (UInventoryComponent* NewInvComp = PlayerCharacter->GetInventoryComponent())
		{
			NewInvComp->OnSlotChanged.AddDynamic(this, &UHotbarWidget::OnSlotChangedHandler);
		}
		PlayerCharacter->OnSelectedItemChanged.AddDynamic(this, &UHotbarWidget::OnSelectedItemChangedHandler);
	}
}

UInventoryComponent* UHotbarWidget::GetInventoryComponent() const
{
	if (PlayerCharacter)
	{
		return PlayerCharacter->GetInventoryComponent();
	}
	return nullptr;
}

bool UHotbarWidget::IsValidHotbarIndex(int32 Index) const
{
	return Index >= 0 && Index < SlotWidgets.Num();
}

int32 UHotbarWidget::InventoryToHotbarIndex(int32 InventorySlotIndex) const
{
	return InventorySlotIndex - HotbarStartIndex;
}

void UHotbarWidget::OnSlotChangedHandler(int32 SlotIndex, FInventorySlot NewSlot)
{
	HandleSlotChanged(SlotIndex, NewSlot);
}

void UHotbarWidget::OnSelectedItemChangedHandler(int32 NewIndex, EItemType NewItemType)
{
	SelectedItemChanged(NewIndex, NewItemType);
}
