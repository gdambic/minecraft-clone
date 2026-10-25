#pragma once

#include "CoreMinimal.h"
#include "Components/PrimitiveComponent.h"
#include "BlockOutlineComponent.generated.h"

/**
 * Obrub bloka u koji igrac gleda - Minecraftov zicani okvir, ne presvlacenje
 * materijala.
 *
 * Komponenta renderu preda scene proxy koji svaki kadar nacrta 12 bridova
 * kutije kao translucentne linije kroz PDI->DrawTranslucentLine s uključenim
 * bScreenSpace. Time debljina ostaje konstantna na ekranu (BatchedElements.cpp:
 * 1060-1102), alpha se cuva (AddTranslucentLine, isti file 165-196), a linije
 * se crtaju u glavnom translucency passu s depth testom bez depth writea
 * (TranslucentRendering.cpp:1393-1396) - pa blokovi ispred obrub zaklone, a
 * sama kocka skrije svoja tri zadnja brida. Vidi Docs/PLAN_BlockOutline.md.
 *
 * Boja, debljina i napuhavanje su konzolne varijable (voxel.outline.*) jer se
 * citaju na render threadu, pa se stimaju u igri bez rebuilda.
 */
UCLASS(ClassGroup = (Voxel), meta = (BlueprintSpawnableComponent))
class MINECRAFTCLONE_API UBlockOutlineComponent : public UPrimitiveComponent
{
	GENERATED_BODY()

public:
	UBlockOutlineComponent();

	/**
	 * Kutija koja se obrubljuje, u lokalnom prostoru komponente. No-op ako je
	 * zadana kutija ista kao trenutna (inace bi svaki kadar rusio proxy).
	 */
	void SetOutlineBox(const FBox& InLocalBox);

	const FBox& GetOutlineBox() const { return LocalBox; }

	virtual FPrimitiveSceneProxy* CreateSceneProxy() override;
	virtual FBoxSphereBounds CalcBounds(const FTransform& LocalToWorld) const override;

private:
	/** Default je puna kocka od 100 uu centrirana na ishodiste komponente */
	FBox LocalBox = FBox(FVector(-50.0f), FVector(50.0f));
};
