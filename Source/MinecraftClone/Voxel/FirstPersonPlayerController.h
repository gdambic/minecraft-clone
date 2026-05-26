#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "FirstPersonPlayerController.generated.h"

class UInputMappingContext;

/**
 * Simple first person Player Controller
 * Manages the input mapping context.
 * Overrides the Player Camera Manager class.
 */
UCLASS(abstract)
class MINECRAFTCLONE_API AFirstPersonPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	AFirstPersonPlayerController();

public:
	/** Swap između gameplay i inventory input modea */
	UFUNCTION(BlueprintCallable, Category = "Input")
	void SetInventoryInputMode(bool bInventoryMode);

protected:
	/** Input Mapping Contexts (gameplay) */
	UPROPERTY(EditAnywhere, Category = "Input|Input Mappings")
	TArray<UInputMappingContext*> DefaultMappingContexts;

	/** Inventory Input Mapping Contexts (aktivni dok je inventory otvoren) */
	UPROPERTY(EditAnywhere, Category = "Input|Input Mappings")
	TArray<UInputMappingContext*> InventoryMappingContexts;

	/** Gameplay initialization */
	virtual void BeginPlay() override;

	/** Input mapping context setup */
	virtual void SetupInputComponent() override;
};
