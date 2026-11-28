#pragma once

#include "CoreMinimal.h"

class FJsonObject;
class UStaticMesh;
class UMaterialInterface;

/**
 * Model bloka koji nije puna kocka, u Minecraftovom formatu (models/block/*.json):
 * popis kvadara s koordinatama 0..16 unutar celije, opcionalnom rotacijom oko
 * jedne osi i UV pravokutnikom po stranici. JSON polje "model" u Blocks.json
 * se prepisuje iz jara bez preracunavanja.
 *
 * Minecraftove osi su x = istok, y = gore, z = jug; nase su X = istok, Y = jug,
 * Z = gore. Pretvorbu radi BlockModel::McToLocal i nigdje drugdje.
 */
struct FBlockModelFace
{
	bool bPresent = false;

	/** u1, v1, u2, v2 u pikselima teksture (v raste prema dolje) */
	FVector4f UV = FVector4f(0.f, 0.f, 16.f, 16.f);
};

struct FBlockModelElement
{
	FVector3f From = FVector3f::ZeroVector;
	FVector3f To = FVector3f(16.f);

	bool bHasRotation = false;
	FVector3f RotationOrigin = FVector3f(8.f);

	/** 0 = x, 1 = y, 2 = z (Minecraftove osi) */
	int32 RotationAxis = 1;

	/** Stupnjevi; pozitivno = suprotno od kazaljke gledano s pozitivnog kraja osi */
	float RotationAngle = 0.f;

	/** Redom: down, up, north, south, west, east */
	FBlockModelFace Faces[6];
};

struct FBlockModel
{
	TArray<FBlockModelElement> Elements;

	/** Stranica teksture u pikselima - UV se dijeli s njom */
	float TextureSize = 16.f;
};

namespace BlockModel
{
	/** Procitaj "model" objekt iz Blocks.json. Vraca false i opis greske ako unos nije ispravan. */
	bool Parse(const TSharedPtr<FJsonObject>& Json, FBlockModel& OutModel, FString& OutError);

	/** Tocka u Minecraftovim koordinatama celije (0..16) -> lokalni prostor bloka (ishodiste = srediste celije). */
	FVector McToLocal(const FVector3f& McPoint, float BlockSize);

	/**
	 * Izgradi static mesh iz modela (u memoriji, bez asseta). Kolizija mesha je
	 * jedna kutija CollisionBox (obris za ciljanje), u lokalnom prostoru bloka.
	 */
	UStaticMesh* BuildStaticMesh(UObject* Outer, FName Name, const FBlockModel& Model,
		UMaterialInterface* Material, const FBox& CollisionBox, float BlockSize);
}
