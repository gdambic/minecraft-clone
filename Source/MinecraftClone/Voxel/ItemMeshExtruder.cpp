#include "ItemMeshExtruder.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"
#if WITH_EDITORONLY_DATA
#include "ImageCore.h"
#endif
#include "MinecraftClone.h"

namespace
{
	// Dodaj quad (4 verteksa, 2 trokuta). Winding nije bitan za vidljivost jer
	// je materijal Two Sided - normala sluzi samo osvjetljenju
	void AddQuad(FItemExtrudedMeshData& Mesh,
		const FVector& A, const FVector& B, const FVector& C, const FVector& D,
		const FVector& Normal,
		const FVector2D& UVA, const FVector2D& UVB, const FVector2D& UVC, const FVector2D& UVD)
	{
		const int32 Base = Mesh.Vertices.Num();
		Mesh.Vertices.Add(A);
		Mesh.Vertices.Add(B);
		Mesh.Vertices.Add(C);
		Mesh.Vertices.Add(D);
		for (int32 i = 0; i < 4; ++i)
		{
			Mesh.Normals.Add(Normal);
		}
		Mesh.UVs.Add(UVA);
		Mesh.UVs.Add(UVB);
		Mesh.UVs.Add(UVC);
		Mesh.UVs.Add(UVD);
		Mesh.Triangles.Append({ Base, Base + 1, Base + 2, Base, Base + 2, Base + 3 });
	}

	// Put za cooked/packaged build: mip 0 iz platform data (BGRA8 +
	// NeverStream garantiraju dostupnost - postavlja Build Block Materials)
	bool ReadAlphaFromPlatformData(UTexture2D* Texture, TArray<uint8>& OutAlpha, int32& OutW, int32& OutH)
	{
		FTexturePlatformData* PlatformData = Texture->GetPlatformData();
		if (!PlatformData || PlatformData->Mips.Num() == 0 || PlatformData->PixelFormat != PF_B8G8R8A8)
		{
			return false;
		}

		FTexture2DMipMap& Mip = PlatformData->Mips[0];
		const int32 W = Mip.SizeX;
		const int32 H = Mip.SizeY;
		const FColor* Data = static_cast<const FColor*>(Mip.BulkData.LockReadOnly());
		if (!Data || W <= 0 || H <= 0)
		{
			Mip.BulkData.Unlock();
			return false;
		}

		OutAlpha.SetNumUninitialized(W * H);
		for (int32 i = 0; i < W * H; ++i)
		{
			OutAlpha[i] = Data[i].A;
		}
		Mip.BulkData.Unlock();
		OutW = W;
		OutH = H;
		return true;
	}

#if WITH_EDITORONLY_DATA
	// Put za editor build (i -game bez cookanja): import source podaci.
	// Platform data se bez renderiranja (-nullrhi) uopce ne mora izgraditi.
	bool ReadAlphaFromSource(UTexture2D* Texture, TArray<uint8>& OutAlpha, int32& OutW, int32& OutH)
	{
		FImage Image;
		if (!Texture->Source.IsValid() || !Texture->Source.GetMipImage(Image, 0))
		{
			return false;
		}
		if (Image.Format != ERawImageFormat::BGRA8)
		{
			// PNG import je uvijek BGRA8; konverzija pokriva ostale formate
			FImage Converted;
			Image.CopyTo(Converted, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
			Image = MoveTemp(Converted);
		}

		const int32 W = Image.SizeX;
		const int32 H = Image.SizeY;
		if (W <= 0 || H <= 0 || Image.RawData.Num() < static_cast<int64>(W) * H * 4)
		{
			return false;
		}

		OutAlpha.SetNumUninitialized(W * H);
		for (int32 i = 0; i < W * H; ++i)
		{
			OutAlpha[i] = Image.RawData[static_cast<int64>(i) * 4 + 3]; // BGRA -> A
		}
		OutW = W;
		OutH = H;
		return true;
	}
#endif
}

FItemExtrudedMeshData FItemMeshExtruder::BuildFlatQuad()
{
	FItemExtrudedMeshData Mesh;
	const float Half = PlateSize * 0.5f;
	AddQuad(Mesh,
		FVector(-Half, 0.0f, Half), FVector(Half, 0.0f, Half),
		FVector(Half, 0.0f, -Half), FVector(-Half, 0.0f, -Half),
		FVector(0.0f, -1.0f, 0.0f),
		FVector2D(0.0f, 0.0f), FVector2D(1.0f, 0.0f), FVector2D(1.0f, 1.0f), FVector2D(0.0f, 1.0f));
	return Mesh;
}

FItemExtrudedMeshData FItemMeshExtruder::Extrude(UTexture2D* Texture)
{
	FItemExtrudedMeshData Mesh;
	if (!Texture)
	{
		return Mesh;
	}

	int32 W = 0;
	int32 H = 0;
	TArray<uint8> Alpha;
	bool bPixelsRead = ReadAlphaFromPlatformData(Texture, Alpha, W, H);
#if WITH_EDITORONLY_DATA
	if (!bPixelsRead)
	{
		bPixelsRead = ReadAlphaFromSource(Texture, Alpha, W, H);
	}
#endif
	if (!bPixelsRead)
	{
		UE_LOG(LogMinecraftClone, Error,
			TEXT("ItemMeshExtruder: pikseli teksture %s nedostupni (compression mora biti TC_EDITOR_ICON + NeverStream) - pokreni Tools > MinecraftClone > Build Block Materials"),
			*Texture->GetName());
		return Mesh;
	}

	const float PxX = PlateSize / W;
	const float PxZ = PlateSize / H;
	const float Half = PlateSize * 0.5f;
	const float HalfT = PxX * 0.5f;   // debljina ploce = 1 piksel (kao Minecraft)

	auto IsOpaque = [&Alpha, W, H](int32 C, int32 R)
	{
		return C >= 0 && C < W && R >= 0 && R < H && Alpha[R * W + C] >= AlphaThreshold;
	};
	auto XAt = [PxX, Half](int32 C) { return C * PxX - Half; };
	auto ZAt = [PxZ, Half](int32 R) { return Half - R * PxZ; };

	// Prednja (-Y) i straznja (+Y) ploca preko cijele teksture; prozirne
	// piksele reze Masked materijal
	for (const float Y : { -HalfT, HalfT })
	{
		AddQuad(Mesh,
			FVector(XAt(0), Y, ZAt(0)), FVector(XAt(W), Y, ZAt(0)),
			FVector(XAt(W), Y, ZAt(H)), FVector(XAt(0), Y, ZAt(H)),
			FVector(0.0f, Y > 0.0f ? 1.0f : -1.0f, 0.0f),
			FVector2D(0.0f, 0.0f), FVector2D(1.0f, 0.0f), FVector2D(1.0f, 1.0f), FVector2D(0.0f, 1.0f));
	}

	// Okomite bocne stranice: na granici stupaca C-1|C gdje je tocno jedan
	// piksel neprozirn. Uzastopni redovi s istim stanjem spajaju se u jedan
	// quad; U je prikvacen na centar neprozirnog piksela a V prati redove, pa
	// uz Nearest filter svaki fragment dobije boju "svog" rubnog piksela.
	for (int32 C = 0; C <= W; ++C)
	{
		int32 R = 0;
		while (R < H)
		{
			const bool bLeft = IsOpaque(C - 1, R);
			const bool bRight = IsOpaque(C, R);
			if (bLeft == bRight)
			{
				++R;
				continue;
			}

			int32 RunEnd = R + 1;
			while (RunEnd < H && IsOpaque(C - 1, RunEnd) == bLeft && IsOpaque(C, RunEnd) == bRight)
			{
				++RunEnd;
			}

			const float X = XAt(C);
			const float U = ((bLeft ? C - 1 : C) + 0.5f) / W;
			const float VTop = static_cast<float>(R) / H;
			const float VBottom = static_cast<float>(RunEnd) / H;

			AddQuad(Mesh,
				FVector(X, -HalfT, ZAt(R)), FVector(X, HalfT, ZAt(R)),
				FVector(X, HalfT, ZAt(RunEnd)), FVector(X, -HalfT, ZAt(RunEnd)),
				FVector(bLeft ? 1.0f : -1.0f, 0.0f, 0.0f),
				FVector2D(U, VTop), FVector2D(U, VTop), FVector2D(U, VBottom), FVector2D(U, VBottom));

			R = RunEnd;
		}
	}

	// Vodoravne bocne stranice: granica redova R-1|R po stupcima, isto spajanje
	for (int32 R = 0; R <= H; ++R)
	{
		int32 C = 0;
		while (C < W)
		{
			const bool bAbove = IsOpaque(C, R - 1);
			const bool bBelow = IsOpaque(C, R);
			if (bAbove == bBelow)
			{
				++C;
				continue;
			}

			int32 RunEnd = C + 1;
			while (RunEnd < W && IsOpaque(RunEnd, R - 1) == bAbove && IsOpaque(RunEnd, R) == bBelow)
			{
				++RunEnd;
			}

			const float Z = ZAt(R);
			const float V = ((bAbove ? R - 1 : R) + 0.5f) / H;
			const float ULeft = static_cast<float>(C) / W;
			const float URight = static_cast<float>(RunEnd) / W;

			AddQuad(Mesh,
				FVector(XAt(C), -HalfT, Z), FVector(XAt(RunEnd), -HalfT, Z),
				FVector(XAt(RunEnd), HalfT, Z), FVector(XAt(C), HalfT, Z),
				FVector(0.0f, 0.0f, bAbove ? -1.0f : 1.0f),
				FVector2D(ULeft, V), FVector2D(URight, V), FVector2D(URight, V), FVector2D(ULeft, V));

			C = RunEnd;
		}
	}

	UE_LOG(LogMinecraftClone, Log, TEXT("ItemMeshExtruder: %s (%dx%d) -> %d verteksa, %d trokuta"),
		*Texture->GetName(), W, H, Mesh.Vertices.Num(), Mesh.Triangles.Num() / 3);
	return Mesh;
}
