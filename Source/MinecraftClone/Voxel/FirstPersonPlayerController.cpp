#include "FirstPersonPlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "FirstPersonCameraManager.h"

AFirstPersonPlayerController::AFirstPersonPlayerController()
{
	// Set the player camera manager class
	PlayerCameraManagerClass = AFirstPersonCameraManager::StaticClass();
}

void AFirstPersonPlayerController::BeginPlay()
{
	Super::BeginPlay();
}

void AFirstPersonPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// Only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}
		}
	}
}

void AFirstPersonPlayerController::SetInventoryInputMode(bool bInventoryMode)
{
	if (!IsLocalPlayerController()) return;

	UEnhancedInputLocalPlayerSubsystem* Subsystem =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer());
	if (!Subsystem) return;

	if (bInventoryMode)
	{
		// Ukloni gameplay IMC-ove
		for (UInputMappingContext* IMC : DefaultMappingContexts)
		{
			Subsystem->RemoveMappingContext(IMC);
		}
		// Dodaj inventory IMC-ove
		for (UInputMappingContext* IMC : InventoryMappingContexts)
		{
			Subsystem->AddMappingContext(IMC, 0);
		}
	}
	else
	{
		// Ukloni inventory IMC-ove
		for (UInputMappingContext* IMC : InventoryMappingContexts)
		{
			Subsystem->RemoveMappingContext(IMC);
		}
		// Vrati gameplay IMC-ove
		for (UInputMappingContext* IMC : DefaultMappingContexts)
		{
			Subsystem->AddMappingContext(IMC, 0);
		}
	}
}
