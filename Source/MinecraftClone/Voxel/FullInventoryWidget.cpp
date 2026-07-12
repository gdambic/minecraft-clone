#include "FullInventoryWidget.h"
#include "InventorySlotWidget.h"
#include "FirstPersonCharacter.h"
#include "Components/UniformGridPanel.h"
#include "Components/UniformGridSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Engine/DataTable.h"

void UFullInventoryWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// Sakrij inventory na početku
	SetVisibility(ESlateVisibility::Collapsed);

	// Pronađi player charactera
	APlayerController* PC = GetOwningPlayer();
	if (PC)
	{
		PlayerCharacter = Cast<AFirstPersonCharacter>(PC->GetPawn());
		if (PlayerCharacter)
		{
			InventoryComp = PlayerCharacter->GetInventoryComponent();
		}
	}
}

void UFullInventoryWidget::NativeDestruct()
{
	Super::NativeDestruct();
}

void UFullInventoryWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// Ažuriraj poziciju cursor slota da prati miš
	if (CursorSlot && CursorSlot->IsVisible())
	{
		APlayerController* PC = GetOwningPlayer();
		if (PC)
		{
			float MouseX, MouseY;
			if (PC->GetMousePosition(MouseX, MouseY))
			{
				// Dohvati DPI scale
				float Scale = UWidgetLayoutLibrary::GetViewportScale(this);

				// Skaliraj koordinate i centriraj slot
				FVector2D NewPosition;
				NewPosition.X = MouseX / Scale - CursorOffset.X;
				NewPosition.Y = MouseY / Scale - CursorOffset.Y;

				UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(CursorSlot->Slot);
				if (CanvasSlot)
				{
					CanvasSlot->SetPosition(NewPosition);
				}
			}
		}
	}
}

void UFullInventoryWidget::InitializeInventory()
{
	// Pronađi reference ako još nisu postavljene
	if (!PlayerCharacter)
	{
		APlayerController* PC = GetOwningPlayer();
		if (PC)
		{
			PlayerCharacter = Cast<AFirstPersonCharacter>(PC->GetPawn());
			if (PlayerCharacter)
			{
				InventoryComp = PlayerCharacter->GetInventoryComponent();
			}
		}
	}

	// Kreiraj sve slotove
	CreateMainSlots();
	CreateHotbarSlots();
	CreateCraftingSlots();
	CreateCursorSlot();

	// Osvježi prikaz
	RefreshAllSlots();
}

void UFullInventoryWidget::CreateMainSlots()
{
	if (!MainInventoryGrid || !InventorySlotWidgetClass)
	{
		return;
	}

	MainInventoryGrid->ClearChildren();
	MainSlotWidgets.Empty();

	for (int32 i = 0; i < MainSlotCount; i++)
	{
		UInventorySlotWidget* SlotWidget = CreateSlotWidget(i);
		if (SlotWidget)
		{
			// Dodaj u grid (3 reda x 9 stupaca)
			int32 Row = i / 9;
			int32 Column = i % 9;

			UUniformGridSlot* GridSlot = MainInventoryGrid->AddChildToUniformGrid(SlotWidget, Row, Column);
			if (GridSlot)
			{
				GridSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
				GridSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
			}

			// Bind na click event
			SlotWidget->OnSlotClicked.AddDynamic(this, &UFullInventoryWidget::OnSlotClickedHandler);

			MainSlotWidgets.Add(SlotWidget);
		}
	}
}

void UFullInventoryWidget::CreateHotbarSlots()
{
	if (!HotbarRow || !InventorySlotWidgetClass)
	{
		return;
	}

	HotbarRow->ClearChildren();
	HotbarSlotWidgets.Empty();

	for (int32 i = 0; i < HotbarSlotCount; i++)
	{
		int32 SlotIndex = HotbarStartIndex + i;
		UInventorySlotWidget* SlotWidget = CreateSlotWidget(SlotIndex);
		if (SlotWidget)
		{
			UHorizontalBoxSlot* BoxSlot = HotbarRow->AddChildToHorizontalBox(SlotWidget);
			if (BoxSlot)
			{
				BoxSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
				BoxSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
			}

			SlotWidget->OnSlotClicked.AddDynamic(this, &UFullInventoryWidget::OnSlotClickedHandler);

			HotbarSlotWidgets.Add(SlotWidget);
		}
	}
}

void UFullInventoryWidget::CreateCraftingSlots()
{
	if (!CraftingGrid || !HorizontalBox_CraftingOutput || !InventorySlotWidgetClass)
	{
		return;
	}

	// Crafting input slotovi (2x2 grid)
	CraftingGrid->ClearChildren();
	CraftingSlotWidgets.Empty();

	for (int32 i = 0; i < CraftingInputCount; i++)
	{
		int32 SlotIndex = CraftingStartIndex + i;
		UInventorySlotWidget* SlotWidget = CreateSlotWidget(SlotIndex);
		if (SlotWidget)
		{
			int32 Row = i / 2;
			int32 Column = i % 2;

			UUniformGridSlot* GridSlot = CraftingGrid->AddChildToUniformGrid(SlotWidget, Row, Column);
			if (GridSlot)
			{
				GridSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
				GridSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
			}

			SlotWidget->OnSlotClicked.AddDynamic(this, &UFullInventoryWidget::OnSlotClickedHandler);

			CraftingSlotWidgets.Add(SlotWidget);
		}
	}

	// Crafting output slot
	HorizontalBox_CraftingOutput->ClearChildren();

	OutputSlotWidget = CreateSlotWidget(CraftingOutputIndex);
	if (OutputSlotWidget)
	{
		UHorizontalBoxSlot* BoxSlot = HorizontalBox_CraftingOutput->AddChildToHorizontalBox(OutputSlotWidget);
		if (BoxSlot)
		{
			BoxSlot->SetHorizontalAlignment(EHorizontalAlignment::HAlign_Fill);
			BoxSlot->SetVerticalAlignment(EVerticalAlignment::VAlign_Fill);
		}

		OutputSlotWidget->OnSlotClicked.AddDynamic(this, &UFullInventoryWidget::OnSlotClickedHandler);
	}
}

void UFullInventoryWidget::CreateCursorSlot()
{
	if (!CanvasPanel || !InventorySlotWidgetClass)
	{
		return;
	}

	CursorSlot = CreateSlotWidget(-1); // -1 = cursor slot
	if (CursorSlot)
	{
		// Sakrij na početku
		CursorSlot->SetVisibility(ESlateVisibility::Hidden);

		// Dodaj u canvas
		UCanvasPanelSlot* CanvasSlot = CanvasPanel->AddChildToCanvas(CursorSlot);
		if (CanvasSlot)
		{
			CanvasSlot->SetSize(CursorSlotSize);
			CanvasSlot->SetPosition(FVector2D::ZeroVector);
		}
	}
}

void UFullInventoryWidget::RefreshAllSlots()
{
	if (!InventoryComp)
	{
		return;
	}

	// Main slotovi (0-26)
	for (int32 i = 0; i < MainSlotWidgets.Num(); i++)
	{
		if (MainSlotWidgets[i])
		{
			FInventorySlot SlotData = InventoryComp->GetSlot(i);
			MainSlotWidgets[i]->SetSlotData(SlotData.ItemType, SlotData.Quantity);
		}
	}

	// Hotbar slotovi (27-35)
	for (int32 i = 0; i < HotbarSlotWidgets.Num(); i++)
	{
		if (HotbarSlotWidgets[i])
		{
			FInventorySlot SlotData = InventoryComp->GetSlot(HotbarStartIndex + i);
			HotbarSlotWidgets[i]->SetSlotData(SlotData.ItemType, SlotData.Quantity);
		}
	}

	// Crafting input slotovi (36-39)
	for (int32 i = 0; i < CraftingSlotWidgets.Num(); i++)
	{
		if (CraftingSlotWidgets[i])
		{
			FInventorySlot SlotData = InventoryComp->GetSlot(CraftingStartIndex + i);
			CraftingSlotWidgets[i]->SetSlotData(SlotData.ItemType, SlotData.Quantity);
		}
	}

	// Output slot (40)
	if (OutputSlotWidget)
	{
		FInventorySlot SlotData = InventoryComp->GetSlot(CraftingOutputIndex);
		OutputSlotWidget->SetSlotData(SlotData.ItemType, SlotData.Quantity);
	}
}

void UFullInventoryWidget::HandleSlotClicked(int32 SlotIndex)
{
	if (!InventoryComp)
	{
		return;
	}

	// Ako držimo item, pokušaj ga staviti u slot
	if (InventoryComp->IsHoldingItem())
	{
		InventoryComp->PlaceItem(SlotIndex);
	}
	else
	{
		// Inače, pokušaj uzeti item iz slota
		InventoryComp->PickUpItem(SlotIndex);
	}

	// Osvježi prikaz
	RefreshAllSlots();
	UpdateCursorSlot();
}

void UFullInventoryWidget::UpdateCursorSlot()
{
	if (!CursorSlot || !InventoryComp)
	{
		return;
	}

	if (InventoryComp->IsHoldingItem())
	{
		// Prikaži held item na cursoru
		FInventorySlot HeldItem = InventoryComp->GetHeldItem();
		CursorSlot->SetSlotData(HeldItem.ItemType, HeldItem.Quantity);
		CursorSlot->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
	else
	{
		// Sakrij cursor slot
		CursorSlot->SetVisibility(ESlateVisibility::Hidden);
	}
}

void UFullInventoryWidget::SetPlayerCharacter(AFirstPersonCharacter* Character)
{
	PlayerCharacter = Character;
	if (PlayerCharacter)
	{
		InventoryComp = PlayerCharacter->GetInventoryComponent();
	}
	else
	{
		InventoryComp = nullptr;
	}
}

UInventorySlotWidget* UFullInventoryWidget::CreateSlotWidget(int32 SlotIndex)
{
	if (!InventorySlotWidgetClass)
	{
		return nullptr;
	}

	UInventorySlotWidget* SlotWidget = CreateWidget<UInventorySlotWidget>(GetOwningPlayer(), InventorySlotWidgetClass);
	if (SlotWidget)
	{
		SlotWidget->SlotIndex = SlotIndex;
		SlotWidget->ItemDataTable = ItemDataTable;
	}

	return SlotWidget;
}

void UFullInventoryWidget::OnSlotClickedHandler(int32 SlotIndex)
{
	HandleSlotClicked(SlotIndex);
}
