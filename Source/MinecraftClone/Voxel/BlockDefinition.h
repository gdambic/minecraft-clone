#pragma once

#include "CoreMinimal.h"
#include "BlockType.h"
#include "ItemType.h"
#include "BiomeDefinition.h"
#include "ToolData.h"
#include "BlockSoundTypes.h"
#include "BlockModel.h"
#include "BlockDefinition.generated.h"

/**
 * Vrsta interakcije bloka na desni klik (JSON polje "interaction").
 * None = blok nije interaktivan, desni klik postavlja blok iz ruke.
 */
UENUM(BlueprintType)
enum class EBlockInteraction : uint8
{
	None,
	CraftingTable
	// Buduci interaktivni blokovi: Furnace, Chest...
};

/**
 * Ponasanje bloka (JSON "behavior"): pravila postavljanja i opstanka koja u
 * Minecraftu nosi klasa bloka. None = obican blok bez uvjeta.
 */
UENUM(BlueprintType)
enum class EBlockBehavior : uint8
{
	None,
	Torch,      // TorchBlock: treba cvrstu gornju stranicu bloka ispod sebe
	WallTorch   // WallTorchBlock: treba cvrstu stranicu bloka iza sebe (svojstvo facing)
};

/**
 * Svojstvo stanja bloka (Minecraft block state property), npr. facing s
 * vrijednostima north/south/west/east. JSON "properties".
 */
struct FBlockStateProperty
{
	FString Name;
	TArray<FString> Values;
};

/** Strane svijeta za svojstvo "facing": north = -Y, south = +Y, west = -X, east = +X (kao Minecraft uz z -> Y). */
namespace BlockFacing
{
	inline FIntVector DirectionFromName(const FString& Name)
	{
		if (Name == TEXT("north")) { return FIntVector(0, -1, 0); }
		if (Name == TEXT("south")) { return FIntVector(0, 1, 0); }
		if (Name == TEXT("west")) { return FIntVector(-1, 0, 0); }
		if (Name == TEXT("east")) { return FIntVector(1, 0, 0); }
		if (Name == TEXT("up")) { return FIntVector(0, 0, 1); }
		if (Name == TEXT("down")) { return FIntVector(0, 0, -1); }
		return FIntVector::ZeroValue;
	}

	inline const TCHAR* NameFromDirection(const FIntVector& Dir)
	{
		if (Dir == FIntVector(0, -1, 0)) { return TEXT("north"); }
		if (Dir == FIntVector(0, 1, 0)) { return TEXT("south"); }
		if (Dir == FIntVector(-1, 0, 0)) { return TEXT("west"); }
		if (Dir == FIntVector(1, 0, 0)) { return TEXT("east"); }
		if (Dir == FIntVector(0, 0, 1)) { return TEXT("up"); }
		if (Dir == FIntVector(0, 0, -1)) { return TEXT("down"); }
		return TEXT("");
	}
}

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

	/**
	 * Vrijeme potrebno za uništenje bloka golom rukom (sekunde). Kao u Minecraftu:
	 * 0 = puca trenutno (baklja), negativno = ne može se iskopati (Bedrock).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	float TimeToDestroy = 1.5f;

	/**
	 * Klasa alata koja ubrzava kopanje ovog bloka (JSON "toolClass":
	 * "axe"/"pickaxe"/"shovel"). None = nijedan alat ne pomaže.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	EToolClass ToolClass = EToolClass::None;

	/**
	 * Zahtijeva li blok pravi alat za drop (Minecraft "canHarvest", JSON
	 * "requiresTool"). Bez njega kopanje traje 5/1.5 puta dulje i blok se
	 * razbije bez dropa.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	bool bRequiresTool = false;

	/**
	 * Najmanja razina alata koja ostavlja drop (JSON "requiredToolTier":
	 * "Stone"/"Iron"/"Diamond"); vrijedi samo uz "requiresTool". None = bilo
	 * koji alat prave klase.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	EToolTier RequiredToolTier = EToolTier::None;

	/** Koliko komada DropItemType ispadne: nasumicno izmedu Min i Max, ukljucivo (JSON "dropCountMin"/"dropCountMax"). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block", meta = (ClampMin = "1"))
	int32 DropCountMin = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block", meta = (ClampMin = "1"))
	int32 DropCountMax = 1;

	/**
	 * Rijedak drop koji ZAMJENJUJE obicni (JSON "rareDropItemType" i
	 * "rareDropChance") - Gravel u 10 % slucajeva ispusti Flint umjesto sebe.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	EItemType RareDropItemType = EItemType::None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RareDropChance = 0.0f;

	/**
	 * Sound grupa bloka (JSON "soundGroup": "stone"/"wood"/"gravel"/"grass"/
	 * "basalt"). None = blok nema vlastite zvukove i dobiva Stone fallback.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	EBlockSoundGroup SoundGroup = EBlockSoundGroup::None;

	/** Ime bloka za prikaz */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	FText DisplayName;

	/** Putanja do static mesh asseta */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	FSoftObjectPath Mesh;

	/** Putanja do materijala */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	FSoftObjectPath Material;

	/** Boja li se blok po biomu i kojim tintom (JSON "biomeTint": "grass"/"foliage") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	EBiomeTintType BiomeTint = EBiomeTintType::None;

	/** Interakcija na desni klik (None = blok nije interaktivan) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	EBlockInteraction Interaction = EBlockInteraction::None;

	// === Svojstva bloka po Minecraftu (Docs/PLAN_Torches.md) ===

	/** Razina svjetla koju blok daje svojoj celiji, 0..15 (JSON "lightLevel"; baklja 14) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block", meta = (ClampMin = "0", ClampMax = "15"))
	int32 LightLevel = 0;

	/** Pravila postavljanja i opstanka (JSON "behavior") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	EBlockBehavior Behavior = EBlockBehavior::None;

	/**
	 * Ima li blok koliziju (JSON "collision", default true). Blok bez kolizije
	 * nije puna kocka: ne sakriva stranice susjeda, propusta igraca i mobove i
	 * pogada ga samo zraka nisana.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	bool bHasCollision = true;

	/** Zaustavlja li blok svjetlo blokova (JSON "blocksLight", default true; lisce i baklja false) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	bool bBlocksLight = true;

	/** Jesu li stranice bloka cvrst oslonac za baklju i slicno (JSON "sturdy", default true; lisce false) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Block")
	bool bSturdy = true;

	/** Svojstva stanja (JSON "properties"); prazno = blok ima jedno stanje */
	TArray<FBlockStateProperty> StateProperties;

	/** Zakret modela oko okomite osi po stanju, u stupnjevima (JSON "variants" -> "yaw"); prazno = bez zakreta */
	TArray<float> StateYaw;

	/**
	 * Obris za ciljanje za stanje bez zakreta (JSON "shape": x1, y1, z1, x2, y2, z2
	 * u Minecraftovim pikselima). Spremljen u nasim osima i u jedinici "blok":
	 * celija je kocka od -0.5 do 0.5.
	 */
	bool bHasShape = false;
	FBox Shape = FBox(ForceInit);

	/** Model bloka koji nije kocka (JSON "model"); nullptr = Mesh */
	TSharedPtr<const FBlockModel> Model;

	/** Broj stanja = umnozak broja vrijednosti svih svojstava (najmanje 1) */
	int32 GetNumStates() const
	{
		int32 Count = 1;
		for (const FBlockStateProperty& Property : StateProperties)
		{
			Count *= FMath::Max(Property.Values.Num(), 1);
		}
		return Count;
	}

	/** Vrijednost svojstva u stanju; prazno ako svojstvo ne postoji */
	FString GetStatePropertyValue(uint8 State, const FString& Name) const
	{
		int32 Remaining = State;
		for (const FBlockStateProperty& Property : StateProperties)
		{
			const int32 Count = FMath::Max(Property.Values.Num(), 1);
			if (Property.Name == Name)
			{
				return Property.Values.IsValidIndex(Remaining % Count) ? Property.Values[Remaining % Count] : FString();
			}
			Remaining /= Count;
		}
		return FString();
	}

	/** Stanje jednako State, osim sto svojstvo Name ima vrijednost Value. Nepoznato svojstvo ili vrijednost vraca State. */
	uint8 WithStatePropertyValue(uint8 State, const FString& Name, const FString& Value) const
	{
		int32 Multiplier = 1;
		int32 Remaining = State;
		for (const FBlockStateProperty& Property : StateProperties)
		{
			const int32 Count = FMath::Max(Property.Values.Num(), 1);
			if (Property.Name == Name)
			{
				const int32 NewIndex = Property.Values.IndexOfByKey(Value);
				return NewIndex == INDEX_NONE ? State : (uint8)(State + (NewIndex - Remaining % Count) * Multiplier);
			}
			Remaining /= Count;
			Multiplier *= Count;
		}
		return State;
	}

	/** Zakret modela za stanje (0 ako blok nema varijante) */
	float GetStateYaw(uint8 State) const
	{
		return StateYaw.IsValidIndex(State) ? StateYaw[State] : 0.f;
	}

	/** Smjer u koji blok gleda (svojstvo "facing"); nula ako ga nema */
	FIntVector GetFacing(uint8 State) const
	{
		return BlockFacing::DirectionFromName(GetStatePropertyValue(State, TEXT("facing")));
	}

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
};

/**
 * Nacin prikaza itema u inventory/hotbar UI-ju i u ruci (JSON polje "display").
 * type "block": ikona je generirana izometrijska kocka bloka Block -
 * ucitava se po konvenciji /Game/Items/Generated/T_Item_<Block>
 * (generira je Tools > MinecraftClone > Generate Items Sprites).
 * type "sprite": rucno nacrtana flat tekstura itema (mac, alat, hrana) -
 * ucitava se po konvenciji /Game/Items/Textures/T_Item_<Texture>.
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

	/** Ime teksture (bez T_Item_ prefiksa), za type "sprite" */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	FString Texture;
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

	/**
	 * Zidna inacica bloka koji predmet postavlja (JSON "wallBlock"; Minecraft
	 * StandingAndWallBlockItem). Air = predmet postavlja samo jedan blok.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item")
	EBlockType WallBlock = EBlockType::Air;

	/** Je li definicija validna */
	bool IsValid() const { return ItemType != EItemType::None; }
};
