#include "BlockModel.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "PhysicsEngine/BodySetup.h"

namespace
{
	const TCHAR* GFaceNames[6] = { TEXT("down"), TEXT("up"), TEXT("north"), TEXT("south"), TEXT("west"), TEXT("east") };

	bool ReadVector(const TSharedPtr<FJsonObject>& Json, const TCHAR* Field, FVector3f& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (!Json->TryGetArrayField(Field, Values) || Values->Num() != 3)
		{
			return false;
		}
		Out = FVector3f((float)(*Values)[0]->AsNumber(), (float)(*Values)[1]->AsNumber(), (float)(*Values)[2]->AsNumber());
		return true;
	}

	/** Rotacija tocke oko osi kroz Origin, desno pravilo u Minecraftovim osima. */
	FVector3f RotateMc(const FVector3f& Point, const FVector3f& Origin, int32 Axis, float AngleDeg)
	{
		const float Rad = FMath::DegreesToRadians(AngleDeg);
		const float C = FMath::Cos(Rad);
		const float S = FMath::Sin(Rad);
		const FVector3f P = Point - Origin;
		FVector3f R = P;
		switch (Axis)
		{
		case 0: R = FVector3f(P.X, P.Y * C - P.Z * S, P.Y * S + P.Z * C); break;
		case 1: R = FVector3f(P.X * C + P.Z * S, P.Y, -P.X * S + P.Z * C); break;
		default: R = FVector3f(P.X * C - P.Y * S, P.X * S + P.Y * C, P.Z); break;
		}
		return R + Origin;
	}

	/** Minecraftov vektor smjera -> nas (bez pomaka i mjerila). */
	FVector3f McDirToLocal(const FVector3f& Dir)
	{
		return FVector3f(Dir.X, Dir.Z, Dir.Y);
	}
}

bool BlockModel::Parse(const TSharedPtr<FJsonObject>& Json, FBlockModel& OutModel, FString& OutError)
{
	const TArray<TSharedPtr<FJsonValue>>* Elements = nullptr;
	if (!Json.IsValid() || !Json->TryGetArrayField(TEXT("elements"), Elements) || Elements->Num() == 0)
	{
		OutError = TEXT("model nema 'elements'");
		return false;
	}

	double TextureSize = 16.0;
	if (Json->TryGetNumberField(TEXT("textureSize"), TextureSize) && TextureSize > 0.0)
	{
		OutModel.TextureSize = (float)TextureSize;
	}

	for (const TSharedPtr<FJsonValue>& Value : *Elements)
	{
		const TSharedPtr<FJsonObject>* ElementJson = nullptr;
		FBlockModelElement Element;
		if (!Value.IsValid() || !Value->TryGetObject(ElementJson)
			|| !ReadVector(*ElementJson, TEXT("from"), Element.From) || !ReadVector(*ElementJson, TEXT("to"), Element.To))
		{
			OutError = TEXT("element modela nema 'from'/'to' s tri broja");
			return false;
		}

		const TSharedPtr<FJsonObject>* RotationJson = nullptr;
		if ((*ElementJson)->TryGetObjectField(TEXT("rotation"), RotationJson))
		{
			FString Axis;
			double Angle = 0.0;
			if (!ReadVector(*RotationJson, TEXT("origin"), Element.RotationOrigin)
				|| !(*RotationJson)->TryGetStringField(TEXT("axis"), Axis)
				|| !(*RotationJson)->TryGetNumberField(TEXT("angle"), Angle))
			{
				OutError = TEXT("'rotation' treba 'origin', 'axis' i 'angle'");
				return false;
			}
			Element.bHasRotation = true;
			Element.RotationAxis = Axis == TEXT("x") ? 0 : Axis == TEXT("y") ? 1 : 2;
			Element.RotationAngle = (float)Angle;
		}

		const TSharedPtr<FJsonObject>* FacesJson = nullptr;
		if ((*ElementJson)->TryGetObjectField(TEXT("faces"), FacesJson))
		{
			for (int32 Face = 0; Face < 6; Face++)
			{
				const TSharedPtr<FJsonObject>* FaceJson = nullptr;
				if (!(*FacesJson)->TryGetObjectField(GFaceNames[Face], FaceJson))
				{
					continue;
				}
				Element.Faces[Face].bPresent = true;
				const TArray<TSharedPtr<FJsonValue>>* UV = nullptr;
				if ((*FaceJson)->TryGetArrayField(TEXT("uv"), UV) && UV->Num() == 4)
				{
					Element.Faces[Face].UV = FVector4f((float)(*UV)[0]->AsNumber(), (float)(*UV)[1]->AsNumber(),
						(float)(*UV)[2]->AsNumber(), (float)(*UV)[3]->AsNumber());
				}
			}
		}
		OutModel.Elements.Add(Element);
	}
	return true;
}

FVector BlockModel::McToLocal(const FVector3f& McPoint, float BlockSize)
{
	const float Scale = BlockSize / 16.f;
	return FVector((McPoint.X - 8.f) * Scale, (McPoint.Z - 8.f) * Scale, (McPoint.Y - 8.f) * Scale);
}

UStaticMesh* BlockModel::BuildStaticMesh(UObject* Outer, FName Name, const FBlockModel& Model,
	UMaterialInterface* Material, const FBox& CollisionBox, float BlockSize)
{
	FMeshDescription MeshDescription;
	FStaticMeshAttributes Attributes(MeshDescription);
	Attributes.Register();

	TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector3f> Tangents = Attributes.GetVertexInstanceTangents();
	TVertexInstanceAttributesRef<float> BinormalSigns = Attributes.GetVertexInstanceBinormalSigns();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
	TVertexInstanceAttributesRef<FVector4f> Colors = Attributes.GetVertexInstanceColors();

	const FName SlotName(TEXT("Block"));
	const FPolygonGroupID Group = MeshDescription.CreatePolygonGroup();
	Attributes.GetPolygonGroupMaterialSlotNames()[Group] = SlotName;

	for (const FBlockModelElement& Element : Model.Elements)
	{
		const FVector3f A = Element.From;
		const FVector3f B = Element.To;

		// Cetiri kuta svake stranice gledane izvana: gore-lijevo, gore-desno,
		// dolje-desno, dolje-lijevo (Minecraftove osi), i njezina normala
		struct FFaceCorners
		{
			FVector3f Corners[4];
			FVector3f Normal;
		};
		const FFaceCorners FaceCorners[6] = {
			{ { FVector3f(A.X, A.Y, B.Z), FVector3f(B.X, A.Y, B.Z), FVector3f(B.X, A.Y, A.Z), FVector3f(A.X, A.Y, A.Z) }, FVector3f(0, -1, 0) },
			{ { FVector3f(A.X, B.Y, A.Z), FVector3f(B.X, B.Y, A.Z), FVector3f(B.X, B.Y, B.Z), FVector3f(A.X, B.Y, B.Z) }, FVector3f(0, 1, 0) },
			{ { FVector3f(B.X, B.Y, A.Z), FVector3f(A.X, B.Y, A.Z), FVector3f(A.X, A.Y, A.Z), FVector3f(B.X, A.Y, A.Z) }, FVector3f(0, 0, -1) },
			{ { FVector3f(A.X, B.Y, B.Z), FVector3f(B.X, B.Y, B.Z), FVector3f(B.X, A.Y, B.Z), FVector3f(A.X, A.Y, B.Z) }, FVector3f(0, 0, 1) },
			{ { FVector3f(A.X, B.Y, A.Z), FVector3f(A.X, B.Y, B.Z), FVector3f(A.X, A.Y, B.Z), FVector3f(A.X, A.Y, A.Z) }, FVector3f(-1, 0, 0) },
			{ { FVector3f(B.X, B.Y, B.Z), FVector3f(B.X, B.Y, A.Z), FVector3f(B.X, A.Y, A.Z), FVector3f(B.X, A.Y, B.Z) }, FVector3f(1, 0, 0) },
		};

		for (int32 Face = 0; Face < 6; Face++)
		{
			if (!Element.Faces[Face].bPresent)
			{
				continue;
			}

			const FVector4f& UV = Element.Faces[Face].UV;
			const FVector2f CornerUVs[4] = {
				FVector2f(UV.X, UV.Y), FVector2f(UV.Z, UV.Y), FVector2f(UV.Z, UV.W), FVector2f(UV.X, UV.W)
			};

			FVector3f LocalCorners[4];
			FVertexInstanceID Instances[4];
			for (int32 Corner = 0; Corner < 4; Corner++)
			{
				FVector3f Point = FaceCorners[Face].Corners[Corner];
				if (Element.bHasRotation)
				{
					Point = RotateMc(Point, Element.RotationOrigin, Element.RotationAxis, Element.RotationAngle);
				}
				LocalCorners[Corner] = FVector3f(McToLocal(Point, BlockSize));
			}

			FVector3f Normal = FaceCorners[Face].Normal;
			if (Element.bHasRotation)
			{
				Normal = RotateMc(Normal, FVector3f::ZeroVector, Element.RotationAxis, Element.RotationAngle);
			}
			Normal = McDirToLocal(Normal).GetSafeNormal();
			const FVector3f Tangent = (LocalCorners[1] - LocalCorners[0]).GetSafeNormal();

			for (int32 Corner = 0; Corner < 4; Corner++)
			{
				const FVertexID Vertex = MeshDescription.CreateVertex();
				Positions[Vertex] = LocalCorners[Corner];
				Instances[Corner] = MeshDescription.CreateVertexInstance(Vertex);
				Normals[Instances[Corner]] = Normal;
				Tangents[Instances[Corner]] = Tangent;
				BinormalSigns[Instances[Corner]] = 1.f;
				Colors[Instances[Corner]] = FVector4f(1.f, 1.f, 1.f, 1.f);
				UVs.Set(Instances[Corner], 0, CornerUVs[Corner] / Model.TextureSize);
			}

			// Prednja strana je ona s koje su vrhovi suprotno od kazaljke
			MeshDescription.CreateTriangle(Group, { Instances[0], Instances[3], Instances[2] });
			MeshDescription.CreateTriangle(Group, { Instances[0], Instances[2], Instances[1] });
		}
	}

	UStaticMesh* Mesh = NewObject<UStaticMesh>(Outer, MakeUniqueObjectName(Outer, UStaticMesh::StaticClass(), Name), RF_Transient);
	Mesh->GetStaticMaterials().Add(FStaticMaterial(Material, SlotName, SlotName));

	UStaticMesh::FBuildMeshDescriptionsParams Params;
	Params.bBuildSimpleCollision = false;
	Params.bFastBuild = true;
	Params.bAllowCpuAccess = true;
	Params.bMarkPackageDirty = false;
	Params.bCommitMeshDescription = false;
	TArray<const FMeshDescription*> Descriptions;
	Descriptions.Add(&MeshDescription);
	Mesh->BuildFromMeshDescriptions(Descriptions, Params);

	// Kolizija = obris za ciljanje, ne vizual: tanku baklju bi bilo tesko pogoditi
	if (!Mesh->GetBodySetup())
	{
		Mesh->CreateBodySetup();
	}
	if (UBodySetup* Body = Mesh->GetBodySetup())
	{
		const FVector Size = CollisionBox.GetSize();
		FKBoxElem Box(Size.X, Size.Y, Size.Z);
		Box.Center = CollisionBox.GetCenter();
		Body->AggGeom.BoxElems.Reset();
		Body->AggGeom.BoxElems.Add(Box);
		Body->CollisionTraceFlag = CTF_UseSimpleAsComplex;
		Body->InvalidatePhysicsData();
		Body->CreatePhysicsMeshes();
	}
	return Mesh;
}
