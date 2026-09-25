#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BlockDefinition.h"
#include "ItemMeshExtruder.h"
#include "BlockRegistry.generated.h"

class UTexture2D;
class UMaterialInterface;

/**
 * Centralni registar svih blokova i itema u igri.
 * GameInstanceSubsystem - automatski se kreira i živi kroz cijelu sesiju.
 *
 * Korištenje:
 *   UBlockRegistry* Registry = GetGameInstance()->GetSubsystem<UBlockRegistry>();
 *   FBlockDefinition* Def = Registry->GetBlockDefinition(EBlockType::Dirt);
 */
UCLASS()
class MINECRAFTCLONE_API UBlockRegistry : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	/** Inicijalizacija - registrira sve blokove */
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	// === Block API ===

	/** Dohvati definiciju bloka po tipu (vraća nullptr ako ne postoji) - samo C++ */
	const FBlockDefinition* GetBlockDefinition(EBlockType BlockType) const;

	/** Dohvati definiciju bloka po tipu (Blueprint verzija - vraća kopiju, IsValid() za provjeru) */
	UFUNCTION(BlueprintCallable, Category = "BlockRegistry")
	FBlockDefinition GetBlockDefinitionCopy(EBlockType BlockType) const;

	/** Dohvati definiciju bloka koji se može postaviti iz danog itema - samo C++ */
	const FBlockDefinition* GetBlockForItem(EItemType ItemType) const;

	/** Dohvati tip bloka za item (za placement) - vraća Air ako se ne može postaviti */
	UFUNCTION(BlueprintPure, Category = "BlockRegistry")
	EBlockType GetBlockTypeForItem(EItemType ItemType) const;

	/** Dohvati sve registrirane blokove */
	UFUNCTION(BlueprintCallable, Category = "BlockRegistry")
	TArray<FBlockDefinition> GetAllBlockDefinitions() const;

	// === Item API ===

	/** Dohvati definiciju itema po tipu (vraća nullptr ako ne postoji) - samo C++ */
	const FItemDefinition* GetItemDefinition(EItemType ItemType) const;

	/** Dohvati definiciju itema po tipu (Blueprint verzija - vraća kopiju, IsValid() za provjeru) */
	UFUNCTION(BlueprintCallable, Category = "BlockRegistry")
	FItemDefinition GetItemDefinitionCopy(EItemType ItemType) const;

	/** Dohvati tip itema koji pada iz bloka - vraća None ako blok ne droppa ništa */
	UFUNCTION(BlueprintPure, Category = "BlockRegistry")
	EItemType GetItemTypeForBlock(EBlockType BlockType) const;

	/** Provjeri može li se item postaviti kao blok */
	UFUNCTION(BlueprintPure, Category = "BlockRegistry")
	bool CanItemBePlaced(EItemType ItemType) const;

	/** Dohvati sve registrirane iteme */
	UFUNCTION(BlueprintCallable, Category = "BlockRegistry")
	TArray<FItemDefinition> GetAllItemDefinitions() const;

	/**
	 * Dohvati generiranu ikonu itema (izometrijski sprite bloka iz
	 * Content/Items/Generated, konvencija T_Item_<Display.Block>).
	 * Jedini izvor istine za "koju teksturu prikazati" - koriste ga i
	 * inventory UI i prikaz itema u ruci.
	 * Vraća nullptr ako item nema block prikaz ili tekstura ne postoji.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlockRegistry")
	UTexture2D* GetItemIconTexture(EItemType ItemType);

	/**
	 * Dohvati materijal bloka koji se postavlja iz danog itema - isti MI koji
	 * koristi teren, pa item u ruci izgleda točno kao blok u svijetu.
	 * Vraća nullptr ako item nije placeable blok ili materijal ne postoji.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlockRegistry")
	UMaterialInterface* GetBlockMaterialForItem(EItemType ItemType);

	/**
	 * Master materijal za flat sprite prikaz itema (M_ItemSprite, texture
	 * parametar "SpriteTexture") - koriste ga prikaz u ruci i item drop.
	 * Vraća nullptr (+ Error u logu) ako asset nije izgrađen.
	 */
	UFUNCTION(BlueprintCallable, Category = "BlockRegistry")
	UMaterialInterface* GetItemSpriteMaterial();

	/**
	 * Dohvati ekstrudirani 3D mesh itema (Minecraft stil: tekstura debljine
	 * 1 piksela s bocnim stranicama po rubovima piksela; FItemMeshExtruder).
	 * Samo za iteme s display type "sprite". Vraca nullptr ako item nema
	 * sprite prikaz ili ekstruzija ne uspije - pozivatelj tada koristi flat
	 * quad fallback. Generira se jednom po itemu (cache). Samo C++.
	 */
	const FItemExtrudedMeshData* GetItemExtrudedMesh(EItemType ItemType);

	// === Biome API ===

	/**
	 * Dohvati definiciju bioma po indeksu (indeks = pozicija u Biomes.json,
	 * vraca ga FBiomeGenerator). Indeks izvan raspona vraca prvi biom -
	 * lista nikad nije prazna (fallback bijeli biom ako JSON fali).
	 */
	const FBiomeDefinition& GetBiome(int32 Index) const;

	/** Blueprint verzija - vraca kopiju */
	UFUNCTION(BlueprintPure, Category = "BlockRegistry")
	FBiomeDefinition GetBiomeCopy(int32 Index) const;

	/** Broj registriranih bioma (uvijek >= 1) */
	UFUNCTION(BlueprintPure, Category = "BlockRegistry")
	int32 GetBiomeCount() const;

	/**
	 * Default tint = tint PRVOG bioma u Biomes.json. Koriste ga svi prikazi
	 * itema izvan svijeta (drop, ruka, inventory ikona) - item je apstraktan
	 * i svugdje isti, tek blok u svijetu ima biom. Bijelo za None.
	 */
	UFUNCTION(BlueprintPure, Category = "BlockRegistry")
	FLinearColor GetDefaultBiomeTint(EBiomeTintType TintType) const;

	// === Static Helper ===

	/** Dohvati registry iz bilo kojeg UObject konteksta */
	UFUNCTION(BlueprintCallable, Category = "BlockRegistry", meta = (WorldContext = "WorldContextObject"))
	static UBlockRegistry* Get(const UObject* WorldContextObject);

protected:
	/** Registriraj blok definiciju */
	void RegisterBlock(const FBlockDefinition& Definition);

	/** Registriraj item definiciju */
	void RegisterItem(const FItemDefinition& Definition);

	/** Učitaj definicije iz Content/Data/Blocks.json odnosno Items.json */
	void LoadBlocksFromJson();
	void LoadItemsFromJson();

	/**
	 * Učitaj biome iz Content/Data/Biomes.json. Ako datoteka fali ili je
	 * prazna, registrira jedan fallback biom s bijelim tintom (= današnji
	 * izgled) + UE_LOG(Error) - lista bioma nikad nije prazna.
	 */
	void LoadBiomesFromJson();

	/**
	 * Za svaku EBlockType/EItemType vrijednost bez JSON definicije registrira
	 * fallback (defaultna kocka bez materijala = siva) + UE_LOG(Error).
	 * Garantira da registry pokriva cijeli enum pa blok nikad ne nestane iz igre.
	 */
	void RegisterFallbackBlocks();
	void RegisterFallbackItems();

private:
	/** Mapa svih blok definicija */
	UPROPERTY()
	TMap<EBlockType, FBlockDefinition> BlockDefinitions;

	/** Mapa svih item definicija */
	UPROPERTY()
	TMap<EItemType, FItemDefinition> ItemDefinitions;

	/** Lista bioma redoslijedom iz Biomes.json - indeks je identitet bioma */
	UPROPERTY()
	TArray<FBiomeDefinition> BiomeDefinitions;

	/** Cache: Item -> Block mapping za brzi lookup */
	UPROPERTY()
	TMap<EItemType, EBlockType> ItemToBlockMap;

	/** Cache: Block -> Item mapping za brzi lookup */
	UPROPERTY()
	TMap<EBlockType, EItemType> BlockToItemMap;

	/** Cache učitanih ikona - TryLoad se radi jednom po tipu (i nullptr se pamti) */
	UPROPERTY()
	TMap<EItemType, TObjectPtr<UTexture2D>> ItemIconCache;

	/** Cache blok materijala po itemu - TryLoad jednom po tipu (i nullptr se pamti) */
	UPROPERTY()
	TMap<EItemType, TObjectPtr<UMaterialInterface>> ItemBlockMaterialCache;

	/** Cache M_ItemSprite master materijala (bool pamti i neuspjeli load) */
	UPROPERTY()
	TObjectPtr<UMaterialInterface> ItemSpriteMaterial;
	bool bItemSpriteMaterialLoaded = false;

	/** Cache ekstrudiranih mesheva - nullptr pamti i neuspjeh/ne-sprite item */
	TMap<EItemType, TSharedPtr<FItemExtrudedMeshData>> ItemExtrudedMeshCache;
};
