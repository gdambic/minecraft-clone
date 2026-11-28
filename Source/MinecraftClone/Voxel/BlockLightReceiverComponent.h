#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BlockLightReceiverComponent.generated.h"

class AVoxelWorld;
class UMeshComponent;

/**
 * Svjetlo blokova (baklje) na onome sto nije blok: stvorenja, dropovi, igraceva
 * ruka i predmet u njoj (Docs/PLAN_Torches.md, faza 5).
 *
 * Kao Minecraftov EntityRenderer.getPackedLightCoords: vlasnik dobiva JEDNU
 * razinu svjetla, onu celije u kojoj su mu oci (za actor koji nije pawn,
 * njegova pozicija), i cijeli se oboji njome. Komponenta tu svjetlinu upisuje
 * u parametre LightFallbackPos i LightFallbackNeg svih materijala vlasnikovih
 * mesheva - iste parametre kojima svjetlo prima i promovirani blok.
 * Materijal bez tih parametara ostaje neosvijetljen.
 */
UCLASS(ClassGroup = (Voxel), meta = (BlueprintSpawnableComponent))
class MINECRAFTCLONE_API UBlockLightReceiverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UBlockLightReceiverComponent();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** Procitaj svjetlo na mjestu vlasnika i upisi ga u materijale odmah (inace 10 puta u sekundi). */
	void UpdateNow();

	/** Zadnja upisana svjetlina (Minecraftova krivulja, 0 = mrak, 1.17 = razina 14) */
	float GetBrightness() const { return Brightness; }

	/** Broj materijala vlasnika koji imaju parametar svjetla, i onih koji ga nemaju (nakon zadnjeg UpdateNow) */
	int32 GetLitMaterialCount() const { return LitMaterials; }
	int32 GetUnlitMaterialCount() const { return UnlitMaterials; }

private:
	/** Mjesto s kojeg se cita svjetlo: oci pawna, inace pozicija actora */
	FVector GetProbeLocation() const;

	TWeakObjectPtr<AVoxelWorld> VoxelWorld;
	float Brightness = 0.f;
	int32 LitMaterials = 0;
	int32 UnlitMaterials = 0;
};
