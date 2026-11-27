#include "CaveGenerator.h"
#include "VoxelChunk.h"
#include "Math/RandomStream.h"

namespace
{
	FORCEINLINE double Fade(double T)
	{
		return T * T * T * (T * (T * 6.0 - 15.0) + 10.0);
	}

	FORCEINLINE double Grad(int32 Hash, double X, double Y, double Z)
	{
		const int32 H = Hash & 15;
		const double U = H < 8 ? X : Y;
		const double V = H < 4 ? Y : (H == 12 || H == 14 ? X : Z);
		return ((H & 1) == 0 ? U : -U) + ((H & 2) == 0 ? V : -V);
	}

	/** Minecraftov y_clamped_gradient: linearno od (FromY, FromValue) do (ToY, ToValue), izvan toga rubna vrijednost. */
	FORCEINLINE double YGradient(double Y, double FromY, double FromValue, double ToY, double ToValue)
	{
		return FMath::GetMappedRangeValueClamped(FVector2D(FromY, ToY), FVector2D(FromValue, ToValue), Y);
	}

	/** Rijetkost 3D tunela (NoiseRouterData.QuantizedSpaghettiRarity.getSpaghettiRarity3D) */
	FORCEINLINE double SpaghettiRarity3D(double Value)
	{
		if (Value < -0.5) { return 0.75; }
		if (Value < 0.0) { return 1.0; }
		if (Value < 0.5) { return 1.5; }
		return 2.0;
	}

	/** Rijetkost 2D tunela (getSphaghettiRarity2D) */
	FORCEINLINE double SpaghettiRarity2D(double Value)
	{
		if (Value < -0.75) { return 0.5; }
		if (Value < -0.5) { return 0.75; }
		if (Value < 0.5) { return 1.0; }
		if (Value < 0.75) { return 2.0; }
		return 3.0;
	}

	/** Minecraftov weird_scaled_sampler: |noise| uzorkovan na koordinatama podijeljenim s rijetkoscu. */
	FORCEINLINE double WeirdScaled(const FVoxelNormalNoise& Noise, double Rarity, double X, double Y, double Z)
	{
		return Rarity * FMath::Abs(Noise.Sample(X / Rarity, Y / Rarity, Z / Rarity));
	}

	/** Minecraftov Y za nas Z (razina mora 63). */
	FORCEINLINE int32 ToMinecraftY(const FCaveGenData& Data, int32 Z)
	{
		return Z + 63 - Data.SeaLevel;
	}
}

void FImprovedNoise::Init(FRandomStream& Stream)
{
	OffsetX = Stream.FRand() * 256.0;
	OffsetY = Stream.FRand() * 256.0;
	OffsetZ = Stream.FRand() * 256.0;
	for (int32 i = 0; i < 256; i++)
	{
		Permutation[i] = (uint8)i;
	}
	for (int32 i = 0; i < 256; i++)
	{
		const int32 j = Stream.RandRange(i, 255);
		Swap(Permutation[i], Permutation[j]);
	}
}

double FImprovedNoise::Sample(double X, double Y, double Z) const
{
	X += OffsetX;
	Y += OffsetY;
	Z += OffsetZ;
	const double FloorX = FMath::FloorToDouble(X);
	const double FloorY = FMath::FloorToDouble(Y);
	const double FloorZ = FMath::FloorToDouble(Z);
	const int32 IX = (int32)FloorX;
	const int32 IY = (int32)FloorY;
	const int32 IZ = (int32)FloorZ;
	const double FX = X - FloorX;
	const double FY = Y - FloorY;
	const double FZ = Z - FloorZ;

	const auto P = [this](int32 Index) { return (int32)Permutation[Index & 255]; };
	const int32 A = P(IX);
	const int32 B = P(IX + 1);
	const int32 AA = P(A + IY);
	const int32 AB = P(A + IY + 1);
	const int32 BA = P(B + IY);
	const int32 BB = P(B + IY + 1);

	const double U = Fade(FX);
	const double V = Fade(FY);
	const double W = Fade(FZ);

	const double Low = FMath::Lerp(
		FMath::Lerp(Grad(P(AA + IZ), FX, FY, FZ), Grad(P(BA + IZ), FX - 1.0, FY, FZ), U),
		FMath::Lerp(Grad(P(AB + IZ), FX, FY - 1.0, FZ), Grad(P(BB + IZ), FX - 1.0, FY - 1.0, FZ), U), V);
	const double High = FMath::Lerp(
		FMath::Lerp(Grad(P(AA + IZ + 1), FX, FY, FZ - 1.0), Grad(P(BA + IZ + 1), FX - 1.0, FY, FZ - 1.0), U),
		FMath::Lerp(Grad(P(AB + IZ + 1), FX, FY - 1.0, FZ - 1.0), Grad(P(BB + IZ + 1), FX - 1.0, FY - 1.0, FZ - 1.0), U), V);
	return FMath::Lerp(Low, High, W);
}

void FVoxelNormalNoise::Init(int32 Seed, int32 FirstOctave, TConstArrayView<double> InAmplitudes)
{
	NumOctaves = FMath::Min(InAmplitudes.Num(), MaxOctaves);
	FRandomStream Stream(Seed);

	int32 FirstUsed = MAX_int32;
	int32 LastUsed = MIN_int32;
	for (int32 i = 0; i < NumOctaves; i++)
	{
		Amplitudes[i] = InAmplitudes[i];
		Octaves[0][i].Init(Stream);
		Octaves[1][i].Init(Stream);
		if (Amplitudes[i] != 0.0)
		{
			FirstUsed = FMath::Min(FirstUsed, i);
			LastUsed = FMath::Max(LastUsed, i);
		}
	}

	InputFactor = FMath::Pow(2.0, (double)FirstOctave);
	OctaveValueFactor = FMath::Pow(2.0, (double)(NumOctaves - 1)) / (FMath::Pow(2.0, (double)NumOctaves) - 1.0);

	// NormalNoise: valueFactor = (1/6) / expectedDeviation(raspon oktava), expectedDeviation(n) = 0.1 * (1 + 1/(n+1))
	const int32 Span = LastUsed >= FirstUsed ? LastUsed - FirstUsed : 0;
	ValueFactor = (1.0 / 6.0) / (0.1 * (1.0 + 1.0 / (Span + 1)));
}

double FVoxelNormalNoise::Sample(double X, double Y, double Z) const
{
	static constexpr double SecondInputFactor = 1.0181268882175227;

	double Total = 0.0;
	double Input = InputFactor;
	double Value = OctaveValueFactor;
	for (int32 i = 0; i < NumOctaves; i++)
	{
		if (Amplitudes[i] != 0.0)
		{
			const double First = Octaves[0][i].Sample(X * Input, Y * Input, Z * Input);
			const double Second = Octaves[1][i].Sample(X * Input * SecondInputFactor, Y * Input * SecondInputFactor,
				Z * Input * SecondInputFactor);
			Total += Amplitudes[i] * (First + Second) * Value;
		}
		Input *= 2.0;
		Value *= 0.5;
	}
	return Total * ValueFactor;
}

TSharedPtr<const FCaveGenData, ESPMode::ThreadSafe> FCaveGenerator::Build(int32 WorldSeed, int32 SeaLevel, const FCaveConfig& Config)
{
	TSharedPtr<FCaveGenData, ESPMode::ThreadSafe> Data = MakeShared<FCaveGenData, ESPMode::ThreadSafe>();
	Data->Config = Config;
	Data->Config.SizeScale = FMath::Max(Config.SizeScale, 0.1f);
	Data->Config.SeaGuardRadius = FMath::Clamp(Config.SeaGuardRadius, 0, 4);
	Data->Config.SeaCrustDepth = FMath::Max(Config.SeaCrustDepth, 1);
	Data->SeaLevel = SeaLevel;

	// firstOctave i amplitude iz data/minecraft/worldgen/noise/<ime>.json; svaki
	// noise dobiva vlastiti seed iz WorldSeed i rednog broja
	int32 Index = 0;
	const auto Init = [&Index, WorldSeed](FVoxelNormalNoise& Noise, int32 FirstOctave, std::initializer_list<double> Amplitudes)
	{
		Noise.Init((int32)HashCombine(GetTypeHash(WorldSeed), GetTypeHash(0xCA7E0000 + Index++)), FirstOctave,
			MakeArrayView(Amplitudes.begin(), (int32)Amplitudes.size()));
	};
	Init(Data->CaveEntrance, -7, { 0.4, 0.5, 1.0 });
	Init(Data->Spaghetti3DRarity, -11, { 1.0 });
	Init(Data->Spaghetti3D1, -7, { 1.0 });
	Init(Data->Spaghetti3D2, -7, { 1.0 });
	Init(Data->Spaghetti3DThickness, -8, { 1.0 });
	Init(Data->SpaghettiRoughness, -5, { 1.0 });
	Init(Data->SpaghettiRoughnessModulator, -8, { 1.0 });
	Init(Data->CaveLayer, -8, { 1.0 });
	Init(Data->CaveCheese, -8, { 0.5, 1.0, 2.0, 1.0, 2.0, 1.0, 0.0, 2.0, 0.0 });
	Init(Data->Spaghetti2D, -7, { 1.0 });
	Init(Data->Spaghetti2DModulator, -11, { 1.0 });
	Init(Data->Spaghetti2DThickness, -11, { 1.0 });
	Init(Data->Spaghetti2DElevation, -8, { 1.0 });
	Init(Data->Pillar, -7, { 1.0, 1.0 });
	Init(Data->PillarRareness, -8, { 1.0 });
	Init(Data->PillarThickness, -8, { 1.0 });
	Init(Data->Noodle, -8, { 1.0 });
	Init(Data->NoodleThickness, -8, { 1.0 });
	Init(Data->NoodleRidgeA, -7, { 1.0 });
	Init(Data->NoodleRidgeB, -7, { 1.0 });
	return Data;
}

void FCaveGenerator::SampleNode(const FCaveGenData& Data, int32 X, int32 Y, int32 Z, FCaveFields& Out)
{
	const FCaveConfig& Config = Data.Config;

	// Minecraftove osi: x i z vodoravno, y okomito. SizeScale razvlaci samo
	// uzorkovanje noisea; pragovi vezani uz visinu ostaju na pravom Y.
	const double BlockY = ToMinecraftY(Data, Z);
	const double Inv = 1.0 / Config.SizeScale;
	const double NX = X * Inv;
	const double NY = BlockY * Inv;
	const double NZ = Y * Inv;

	// spaghetti_roughness_function: dijele ga tuneli i ulazi
	const double Roughness = (Config.bTunnels || Config.bEntrances)
		? (-0.05 - 0.05 * Data.SpaghettiRoughnessModulator.Sample(NX, NY, NZ))
			* (-0.4 + FMath::Abs(Data.SpaghettiRoughness.Sample(NX, NY, NZ)))
		: 0.0;

	// caves/entrances.json: min(veliki otvori, 3D tuneli)
	double Entrances = 1.0;
	if (Config.bEntrances)
	{
		Entrances = Config.EntranceOffset + Data.CaveEntrance.Sample(NX * 0.75, NY * 0.5, NZ * 0.75)
			+ YGradient(BlockY, -10.0, 0.3, 30.0, 0.0);
	}
	if (Config.bTunnels)
	{
		const double Rarity = SpaghettiRarity3D(Data.Spaghetti3DRarity.Sample(NX * 2.0, NY, NZ * 2.0));
		const double Tunnel = FMath::Max(WeirdScaled(Data.Spaghetti3D1, Rarity, NX, NY, NZ),
			WeirdScaled(Data.Spaghetti3D2, Rarity, NX, NY, NZ));
		const double Thickness = -0.0765 - 0.0115 * Data.Spaghetti3DThickness.Sample(NX, NY, NZ);
		Entrances = FMath::Min(Entrances, Roughness + FMath::Clamp(Tunnel + Thickness, -1.0, 1.0));
	}
	Out.Entrances = (float)Entrances;

	// final_density, grana dvorana: 4 * cave_layer^2 + clamp(0.27 + cave_cheese)
	Out.Cheese = 1.f;
	if (Config.bChambers)
	{
		const double Layer = Data.CaveLayer.Sample(NX, NY * 8.0, NZ);
		Out.Cheese = (float)(4.0 * Layer * Layer
			+ FMath::Clamp(Config.ChamberOffset + Data.CaveCheese.Sample(NX, NY * (2.0 / 3.0), NZ), -1.0, 1.0));
	}

	// caves/spaghetti_2d.json + roughness
	Out.Spaghetti = 1.f;
	if (Config.bTunnels)
	{
		const double ThicknessModulator = -0.95 - 0.35 * Data.Spaghetti2DThickness.Sample(NX * 2.0, NY, NZ * 2.0);
		const double Rarity = SpaghettiRarity2D(Data.Spaghetti2DModulator.Sample(NX * 2.0, NY, NZ * 2.0));
		const double Ribbon = WeirdScaled(Data.Spaghetti2D, Rarity, NX, NY, NZ) + 0.083 * ThicknessModulator;
		const double Elevation = FMath::Abs(8.0 * Data.Spaghetti2DElevation.Sample(NX, 0.0, NZ)
			+ YGradient(BlockY, -64.0, 8.0, 320.0, -40.0)) + ThicknessModulator;
		Out.Spaghetti = (float)(FMath::Clamp(FMath::Max(Ribbon, Elevation * Elevation * Elevation), -1.0, 1.0) + Roughness);
	}

	// caves/pillars.json
	Out.Pillars = -1.f;
	if (Config.bChambers && Config.bPillars)
	{
		const double Thickness = 0.55 + 0.55 * Data.PillarThickness.Sample(NX, NY, NZ);
		Out.Pillars = (float)((2.0 * Data.Pillar.Sample(NX * 25.0, NY * 0.3, NZ * 25.0)
			+ (-1.0 - Data.PillarRareness.Sample(NX, NY, NZ))) * Thickness * Thickness * Thickness);
	}

	// caves/noodle.json: cetiri zasebno interpolirana polja
	Out.NoodleToggle = -1.f;
	if (Config.bNoodles)
	{
		static constexpr double RidgeScale = 2.6666666666666665;
		Out.NoodleToggle = (float)Data.Noodle.Sample(NX, NY, NZ);
		Out.NoodleThickness = (float)(-0.075 - 0.025 * Data.NoodleThickness.Sample(NX, NY, NZ));
		Out.NoodleRidgeA = (float)Data.NoodleRidgeA.Sample(NX * RidgeScale, NY * RidgeScale, NZ * RidgeScale);
		Out.NoodleRidgeB = (float)Data.NoodleRidgeB.Sample(NX * RidgeScale, NY * RidgeScale, NZ * RidgeScale);
	}
}

FCaveFields FCaveGenerator::Interpolate(const FCaveFields Nodes[8], float FX, float FY, float FZ)
{
	const auto Tri = [&](float FCaveFields::* Field)
	{
		const float X00 = FMath::Lerp(Nodes[0].*Field, Nodes[1].*Field, FX);
		const float X10 = FMath::Lerp(Nodes[2].*Field, Nodes[3].*Field, FX);
		const float X01 = FMath::Lerp(Nodes[4].*Field, Nodes[5].*Field, FX);
		const float X11 = FMath::Lerp(Nodes[6].*Field, Nodes[7].*Field, FX);
		return FMath::Lerp(FMath::Lerp(X00, X10, FY), FMath::Lerp(X01, X11, FY), FZ);
	};

	FCaveFields Out;
	Out.Entrances = Tri(&FCaveFields::Entrances);
	Out.Cheese = Tri(&FCaveFields::Cheese);
	Out.Spaghetti = Tri(&FCaveFields::Spaghetti);
	Out.Pillars = Tri(&FCaveFields::Pillars);
	Out.NoodleToggle = Tri(&FCaveFields::NoodleToggle);
	Out.NoodleThickness = Tri(&FCaveFields::NoodleThickness);
	Out.NoodleRidgeA = Tri(&FCaveFields::NoodleRidgeA);
	Out.NoodleRidgeB = Tri(&FCaveFields::NoodleRidgeB);
	return Out;
}

bool FCaveGenerator::IsCarved(const FCaveGenData& Data, const FCaveFields& Fields, int32 Z, int32 Depth)
{
	// Najdonji sloj svijeta uvijek ostaje pun
	if (!Data.Config.bEnabled || Z <= VoxelGrid::WorldMinZ)
	{
		return false;
	}

	// Zamjena za Minecraftov sloped_cheese (gustoca terena, raste s dubinom ispod
	// povrsine): depth pada 3/384 po bloku, sloped_cheese = 4 * depth * factor.
	const float SlopedCheese = (Depth + 0.5f) * (4.f * 3.f / 384.f) * Data.Config.SurfaceFactor;

	float Density;
	if (SlopedCheese < 1.5625f)
	{
		// Tik ispod povrsine buse samo ulazi
		Density = FMath::Min(SlopedCheese, 5.f * Fields.Entrances);
	}
	else
	{
		const float Cheese = Fields.Cheese + FMath::Clamp(1.5f - 0.64f * SlopedCheese, 0.f, 0.5f);
		Density = FMath::Min(FMath::Min(Cheese, Fields.Entrances), Fields.Spaghetti);
		if (Fields.Pillars >= 0.03f)
		{
			Density = FMath::Max(Density, Fields.Pillars);
		}
	}

	// Donji "slide": od Y=-40 prema Y=-64 gustoca se gura prema punom kamenu
	const int32 BlockY = ToMinecraftY(Data, Z);
	const float Slide = FMath::Clamp((BlockY + 64) / 24.f, 0.f, 1.f);
	Density = 0.1171875f + Slide * (Density - 0.1171875f);

	// Uski prolazi: samo gdje je noodle >= 0 i Y >= -60
	if (BlockY >= -60 && Fields.NoodleToggle >= 0.f)
	{
		const float Noodle = Fields.NoodleThickness
			+ 1.5f * FMath::Max(FMath::Abs(Fields.NoodleRidgeA), FMath::Abs(Fields.NoodleRidgeB));
		Density = FMath::Min(Density, Noodle);
	}

	return Density < 0.f;
}

bool FCaveGenerator::IsCarvedAt(const FCaveGenData& Data, int32 X, int32 Y, int32 Z, int32 Depth)
{
	if (!Data.Config.bEnabled || Z <= VoxelGrid::WorldMinZ)
	{
		return false;
	}

	// Aritmeticki shift = floor i za negativne koordinate (isto kao VoxelGrid::GridToChunk)
	const int32 BaseX = (X >> CellShiftXY) << CellShiftXY;
	const int32 BaseY = (Y >> CellShiftXY) << CellShiftXY;
	const int32 BaseZ = (Z >> CellShiftZ) << CellShiftZ;
	const int32 CellXY = 1 << CellShiftXY;
	const int32 CellZ = 1 << CellShiftZ;

	FCaveFields Nodes[8];
	for (int32 i = 0; i < 8; i++)
	{
		SampleNode(Data, BaseX + (i & 1) * CellXY, BaseY + ((i >> 1) & 1) * CellXY, BaseZ + ((i >> 2) & 1) * CellZ, Nodes[i]);
	}
	const FCaveFields Fields = Interpolate(Nodes, (float)(X - BaseX) / CellXY, (float)(Y - BaseY) / CellXY,
		(float)(Z - BaseZ) / CellZ);
	return IsCarved(Data, Fields, Z, Depth);
}

FCaveLattice::FCaveLattice(const FCaveGenData& InData, const FIntVector& Origin, int32 MaxZ)
	: Data(InData)
{
	using namespace VoxelGrid;

	if (!Data.Config.bEnabled)
	{
		return;
	}

	const int32 CellXY = 1 << FCaveGenerator::CellShiftXY;
	const int32 CellZ = 1 << FCaveGenerator::CellShiftZ;
	NodesXY = ChunkSize / CellXY + 1;
	NodesZ = ((FMath::Clamp(MaxZ, WorldMinZ, WorldMaxZ) - WorldMinZ) >> FCaveGenerator::CellShiftZ) + 2;
	Nodes.SetNum(NodesXY * NodesXY * NodesZ);

	for (int32 IZ = 0; IZ < NodesZ; IZ++)
	{
		for (int32 IY = 0; IY < NodesXY; IY++)
		{
			for (int32 IX = 0; IX < NodesXY; IX++)
			{
				FCaveGenerator::SampleNode(Data, Origin.X + IX * CellXY, Origin.Y + IY * CellXY, WorldMinZ + IZ * CellZ,
					Nodes[(IZ * NodesXY + IY) * NodesXY + IX]);
			}
		}
	}
}

bool FCaveLattice::IsCarved(int32 LocalX, int32 LocalY, int32 Z, int32 Depth) const
{
	using namespace VoxelGrid;

	if (Nodes.IsEmpty() || Z <= WorldMinZ)
	{
		return false;
	}

	const int32 CellXY = 1 << FCaveGenerator::CellShiftXY;
	const int32 CellZ = 1 << FCaveGenerator::CellShiftZ;
	const int32 IX = LocalX >> FCaveGenerator::CellShiftXY;
	const int32 IY = LocalY >> FCaveGenerator::CellShiftXY;
	const int32 IZ = (Z - WorldMinZ) >> FCaveGenerator::CellShiftZ;
	if (IZ + 1 >= NodesZ)
	{
		return false;
	}

	FCaveFields Cell[8];
	for (int32 i = 0; i < 8; i++)
	{
		Cell[i] = Nodes[((IZ + ((i >> 2) & 1)) * NodesXY + IY + ((i >> 1) & 1)) * NodesXY + IX + (i & 1)];
	}
	const FCaveFields Fields = FCaveGenerator::Interpolate(Cell, (float)(LocalX - IX * CellXY) / CellXY,
		(float)(LocalY - IY * CellXY) / CellXY, (float)(Z - WorldMinZ - IZ * CellZ) / CellZ);
	return FCaveGenerator::IsCarved(Data, Fields, Z, Depth);
}
