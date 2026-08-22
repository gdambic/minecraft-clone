#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "BlockDefinition.h"
#include "BlockRegistry.generated.h"

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

	/** Cache: Item -> Block mapping za brzi lookup */
	UPROPERTY()
	TMap<EItemType, EBlockType> ItemToBlockMap;

	/** Cache: Block -> Item mapping za brzi lookup */
	UPROPERTY()
	TMap<EBlockType, EItemType> BlockToItemMap;
};
