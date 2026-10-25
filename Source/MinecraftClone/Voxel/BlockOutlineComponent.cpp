#include "BlockOutlineComponent.h"

#include "Engine/CollisionProfile.h"
#include "MeshElementCollector.h"
#include "PrimitiveDrawInterface.h"
#include "PrimitiveSceneProxy.h"
#include "PrimitiveViewRelevance.h"
#include "SceneView.h"

namespace BlockOutlineCVars
{
	/**
	 * Debljina u pikselima. Proxy je po viewu pretvara u Unrealovu debljinu
	 * (vidi FBlockOutlineSceneProxy::GetDynamicMeshElements), pa je neovisna o
	 * rezoluciji i FOV-u.
	 */
	static TAutoConsoleVariable<float> CVarPixels(
		TEXT("voxel.outline.px"), 2.5f,
		TEXT("Debljina obruba fokusiranog bloka u pikselima."),
		ECVF_RenderThreadSafe);

	static TAutoConsoleVariable<float> CVarAlpha(
		TEXT("voxel.outline.alpha"), 0.4f,
		TEXT("Alpha crne linije obruba (vanilla je 102/255)."),
		ECVF_RenderThreadSafe);

	/**
	 * Napuhavanje kutije u UE jedinicama. Linija lezi tocno na bridu, a brid
	 * dijeli s plohom susjednog bloka - bez napuhavanja bi se na tom savu
	 * gubila u z-fightingu. Vanilla napuhava 0.002 bloka = 0.2 uu.
	 */
	static TAutoConsoleVariable<float> CVarInflate(
		TEXT("voxel.outline.inflate"), 0.3f,
		TEXT("Napuhavanje kutije obruba u UE jedinicama."),
		ECVF_RenderThreadSafe);

	/** Rezerva ako napuhavanje nije dovoljno - ide u rasterizer state linije */
	static TAutoConsoleVariable<float> CVarDepthBias(
		TEXT("voxel.outline.depthbias"), 0.0f,
		TEXT("Depth bias linija obruba."),
		ECVF_RenderThreadSafe);
}

/**
 * Bounds margina: inflate se mijenja iz konzole na render threadu, pa bounds
 * mora pokriti i najdeblji obrub bez ponovnog izracuna.
 */
static constexpr float GOutlineBoundsMargin = 4.0f;

class FBlockOutlineSceneProxy final : public FPrimitiveSceneProxy
{
public:
	explicit FBlockOutlineSceneProxy(const UBlockOutlineComponent* InComponent)
		: FPrimitiveSceneProxy(InComponent)
		, LocalBox(InComponent->GetOutlineBox())
	{
		bWillEverBeLit = false;
	}

	virtual SIZE_T GetTypeHash() const override
	{
		static size_t UniquePointer;
		return reinterpret_cast<size_t>(&UniquePointer);
	}

	virtual uint32 GetMemoryFootprint() const override
	{
		return sizeof(*this);
	}

	/**
	 * Obrub je namjerno depth-testiran po pikselu. HZB occlusion radi nad
	 * boundsom, pa bi cijeli primitiv odbacio kad je blok u ravnini sa
	 * susjedima - a tada obrub mora biti vidljiv.
	 */
	virtual bool CanBeOccluded() const override
	{
		return false;
	}

	virtual FPrimitiveViewRelevance GetViewRelevance(const FSceneView* View) const override
	{
		FPrimitiveViewRelevance Result;
		Result.bDrawRelevance = IsShown(View);
		Result.bDynamicRelevance = true;
		// Oba flaga konzervativno, kao FLineBatcherSceneProxy::GetViewRelevance
		// (LineBatchComponent.cpp:120-128) - linije ne dolaze iz materijala, pa
		// relevantnost passa nema odakle biti precizna.
		Result.bNormalTranslucency = true;
		Result.bSeparateTranslucency = true;
		Result.bShadowRelevance = false;
		return Result;
	}

	virtual void GetDynamicMeshElements(const TArray<const FSceneView*>& Views,
		const FSceneViewFamily& ViewFamily, uint32 VisibilityMap,
		FMeshElementCollector& Collector) const override
	{
		const float Pixels = BlockOutlineCVars::CVarPixels.GetValueOnRenderThread();
		const float Alpha = BlockOutlineCVars::CVarAlpha.GetValueOnRenderThread();
		const float Inflate = BlockOutlineCVars::CVarInflate.GetValueOnRenderThread();
		const float DepthBias = BlockOutlineCVars::CVarDepthBias.GetValueOnRenderThread();

		if (Pixels <= 0.0f || Alpha <= 0.0f)
		{
			return;
		}

		const FBox Box = LocalBox.ExpandBy(Inflate);
		const FMatrix& ToWorld = GetLocalToWorld();

		// Bit 0 = X, bit 1 = Y, bit 2 = Z: 0 je Min, 1 je Max te osi
		FVector Corner[8];
		for (int32 Index = 0; Index < 8; ++Index)
		{
			Corner[Index] = ToWorld.TransformPosition(FVector(
				(Index & 1) ? Box.Max.X : Box.Min.X,
				(Index & 2) ? Box.Max.Y : Box.Min.Y,
				(Index & 4) ? Box.Max.Z : Box.Min.Z));
		}

		// 12 bridova kutije: cetiri po X, cetiri po Y, cetiri po Z
		static constexpr int32 Edges[12][2] = {
			{ 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 },
			{ 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 },
			{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
		};

		const FLinearColor Color(0.0f, 0.0f, 0.0f, Alpha);

		for (int32 ViewIndex = 0; ViewIndex < Views.Num(); ++ViewIndex)
		{
			if (!(VisibilityMap & (1 << ViewIndex)))
			{
				continue;
			}

			const FSceneView* View = Views[ViewIndex];

			// Sirina na ekranu je Thickness / tan(FOVx/2) (izvod iz
			// BatchedElements.cpp:1061-1102 kroz matricu projekcije), a
			// M[0][0] perspektivne projekcije je 1 / tan(FOVx/2) - pa je
			// pretvorba piksela u Unrealovu debljinu samo dijeljenje s njim.
			float Thickness = Pixels;
			if (View->IsPerspectiveProjection())
			{
				const float ProjScaleX = View->ViewMatrices.GetProjectionMatrix().M[0][0];
				if (ProjScaleX > UE_SMALL_NUMBER)
				{
					Thickness = Pixels / ProjScaleX;
				}
			}

			FPrimitiveDrawInterface* PDI = Collector.GetPDI(ViewIndex);
			for (const int32 (&Edge)[2] : Edges)
			{
				PDI->DrawTranslucentLine(Corner[Edge[0]], Corner[Edge[1]], Color,
					SDPG_World, Thickness, DepthBias, /*bScreenSpace=*/true);
			}
		}
	}

private:
	FBox LocalBox;
};

UBlockOutlineComponent::UBlockOutlineComponent()
{
	PrimaryComponentTick.bCanEverTick = false;

	// Cisti vizual: ne sudjeluje u koliziji, navigaciji, sjenama ni okluziji
	SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SetGenerateOverlapEvents(false);
	SetCanEverAffectNavigation(false);
	SetCastShadow(false);
	bUseAsOccluder = false;
	bReceivesDecals = false;
	bAffectDynamicIndirectLighting = false;
	bAffectDistanceFieldLighting = false;
	Mobility = EComponentMobility::Movable;

	// Ukljucuje ga character kad zrak iz kamere pogodi blok
	SetVisibility(false);
}

void UBlockOutlineComponent::SetOutlineBox(const FBox& InLocalBox)
{
	if (LocalBox.Min.Equals(InLocalBox.Min) && LocalBox.Max.Equals(InLocalBox.Max))
	{
		return;
	}

	LocalBox = InLocalBox;

	// Kutija zivi u proxyju, pa promjena oblika trazi novi proxy
	UpdateBounds();
	MarkRenderStateDirty();
}

FPrimitiveSceneProxy* UBlockOutlineComponent::CreateSceneProxy()
{
	return new FBlockOutlineSceneProxy(this);
}

FBoxSphereBounds UBlockOutlineComponent::CalcBounds(const FTransform& LocalToWorld) const
{
	return FBoxSphereBounds(LocalBox.ExpandBy(GOutlineBoundsMargin)).TransformBy(LocalToWorld);
}
