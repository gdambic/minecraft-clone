#pragma once

#include "CoreMinimal.h"

class UTexture2D;

/**
 * Mesh podaci "ekstrudiranog" 2D itema - Minecraft stil: prednja i straznja
 * ploca s teksturom + bocni quadovi po rubovima neprozirnih piksela, pa item
 * izgleda kao da je svaki piksel mala kockica.
 * Jedna mesh sekcija; materijal je M_ItemSprite (Masked, Two Sided, Nearest).
 */
struct FItemExtrudedMeshData
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;

	bool IsValid() const { return Vertices.Num() > 0 && Triangles.Num() > 0; }
};

/**
 * Generator ekstrudirane geometrije iz teksture itema, ekvivalent
 * Minecraftovog ItemModelGeneratora (JSON parent builtin/generated).
 *
 * Lokalni prostor mesha: ploca u X-Z ravnini (X = tekstura U, Z gore = red 0
 * teksture), debljina 1 piksel duz Y osi, origin u centru ploce.
 * Cijela ploca je PlateSize x PlateSize UU (kao blok od 100 UU).
 */
class MINECRAFTCLONE_API FItemMeshExtruder
{
public:
	/** Velicina cijele ploce teksture u Unreal jedinicama (= velicina bloka) */
	static constexpr float PlateSize = 100.0f;

	/** Prag ispod kojeg se piksel smatra prozirnim (Minecraft cutout) */
	static constexpr uint8 AlphaThreshold = 128;

	/**
	 * Generiraj ekstrudirani mesh iz teksture. Tekstura mora biti
	 * nekomprimirana BGRA8 (TC_EDITOR_ICON - postavlja Build Block Materials
	 * skripta). Neuspjeh (kriv format, nedostupni pikseli) -> Error u log i
	 * prazan mesh; pozivatelj tada koristi BuildFlatQuad() fallback.
	 */
	static FItemExtrudedMeshData Extrude(UTexture2D* Texture);

	/** Flat quad ploca (bez bocnih stranica) - fallback kad ekstruzija ne uspije */
	static FItemExtrudedMeshData BuildFlatQuad();
};
