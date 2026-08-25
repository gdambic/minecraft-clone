#pragma once

#include "CoreMinimal.h"
#include "BlockType.h"
#include "ItemType.h"
#include "BlockDefinition.generated.h"

/**
 * Definicija bloka - sadrži sve podatke potrebne za spawn i ponašanje bloka.
 * Registrira se u UBlockRegistry.
 */
USTRUCT(BlueprintType)
struct MINECRAFTCLONE_API FBlockDefinition
{
	GENERATED_BODY()

	/** Tip bloka */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	EBlockType BlockType = EBlockType::Air;

	/** Item koji pada kad se blok uništi */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	EItemType DropItemType = EItemType::None;

	/** Item iz kojeg se može postaviti ovaj blok (None = ne može se postaviti) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	EItemType PlaceableFromItem = EItemType::None;

	/** Šansa za drop (0.0-1.0), default 1.0 = uvijek */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float DropChance = 1.0f;

	/** Vrijeme potrebno za uništenje bloka (sekunde) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	float TimeToDestroy = 1.5f;

	/** Ime bloka za prikaz */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	FText DisplayName;

	/** Putanja do static mesh asseta */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	FSoftObjectPath Mesh;

	/** Putanja do materijala */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	FSoftObjectPath Material;

	/** Putanja do highlight materijala (opcionalno) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	FSoftObjectPath HighlightMaterial;

	/** Je li definicija validna */
	bool IsValid() const { return BlockType != EBlockType::Air; }
};

class UStaticMesh;
class UMaterialInterface;

/**
 * Razrijeseni asseti bloka - gotovi pointeri dobiveni jednim TryLoad-om po assetu.
 * Cache-ira se u AVoxelWorld (BuildBlockAssetCache) da se izbjegne TryLoad po svakom bloku.
 */
USTRUCT()
struct MINECRAFTCLONE_API FBlockAssets
{
	GENERATED_BODY()

	UPROPERTY()
	UStaticMesh* Mesh = nullptr;

	UPROPERTY()
	UMaterialInterface* Material = nullptr;

	UPROPERTY()
	UMaterialInterface* HighlightMaterial = nullptr;
};

/**
 * Nacin prikaza itema u inventory/hotbar UI-ju (JSON polje "display").
 * type "block": ikona je generirana izometrijska kocka bloka Block -
 * ucitava se po konvenciji /Game/Items/Generated/T_Item_<Block>
 * (generira je Tools > MinecraftClone > Generate Items Sprites).
 * type "none": nema ikone (transparent placeholder).
 */
USTRUCT(BlueprintType)
struct MINECRAFTCLONE_API FItemDisplayDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FString Type = TEXT("none");

	/** Ime bloka (EBlockType vrijednost) ciji se izgled prikazuje, za type "block" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FString Block;
};

/**
 * Definicija itema - sadrži podatke za item drop vizuale i UI.
 * Registrira se u UBlockRegistry.
 */
USTRUCT(BlueprintType)
struct MINECRAFTCLONE_API FItemDefinition
{
	GENERATED_BODY()

	/** Tip itema */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	EItemType ItemType = EItemType::None;

	/** Ime itema za prikaz */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FText DisplayName;

	/** Putanja do static mesh asseta (mini verzija za drop) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FSoftObjectPath Mesh;

	/** Putanja do materijala */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FSoftObjectPath Material;

	/** Maksimalan stack size (64 = default Minecraft stil) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	int32 MaxStackSize = 64;

	/** Nacin prikaza u UI (JSON polje "display") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FItemDisplayDefinition Display;

	/** Je li definicija validna */
	bool IsValid() const { return ItemType != EItemType::None; }
};
