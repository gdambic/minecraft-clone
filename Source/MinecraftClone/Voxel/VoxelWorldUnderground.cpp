// Alati za provjeru podzemlja (Docs/PLAN_Underground.md): voxel.slice i voxel.cavetest.
// Dio AVoxelWorld-a izdvojen u zasebnu datoteku da VoxelWorld.cpp ne raste.

#include "VoxelWorld.h"
#include "ChunkGenerator.h"
#include "Async/ParallelFor.h"
#include "EngineUtils.h"
#include "FirstPersonCharacter.h"
#include "GameFramework/Pawn.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include <atomic>

using namespace VoxelGrid;

namespace
{
	/** Boja bloka na presjeku. Tipovi bez unosa dobivaju boju iz hasha, da se razlikuju. */
	FColor SliceColor(EBlockType Type)
	{
		switch (Type)
		{
		case EBlockType::Air: return FColor(255, 255, 255);
		case EBlockType::Stone: return FColor(125, 125, 125);
		case EBlockType::Dirt: return FColor(134, 96, 67);
		case EBlockType::Grass: return FColor(88, 160, 50);
		case EBlockType::Gravel: return FColor(160, 150, 170);
		case EBlockType::Andesite: return FColor(150, 155, 150);
		case EBlockType::Basalt: return FColor(70, 70, 80);
		case EBlockType::Limestone: return FColor(205, 195, 160);
		case EBlockType::Calcite: return FColor(225, 215, 235);
		case EBlockType::Cobblestone: return FColor(110, 110, 105);
		case EBlockType::Deepslate: return FColor(72, 72, 80);
		case EBlockType::CobbledDeepslate: return FColor(60, 60, 66);
		case EBlockType::Bedrock: return FColor(10, 10, 10);
		case EBlockType::Granite: return FColor(165, 110, 92);
		case EBlockType::Diorite: return FColor(200, 200, 205);
		case EBlockType::Tuff: return FColor(98, 104, 92);
		case EBlockType::CoalOre:
		case EBlockType::DeepslateCoalOre: return FColor(40, 40, 40);
		case EBlockType::IronOre:
		case EBlockType::DeepslateIronOre: return FColor(216, 175, 147);
		case EBlockType::CopperOre:
		case EBlockType::DeepslateCopperOre: return FColor(224, 115, 60);
		case EBlockType::GoldOre:
		case EBlockType::DeepslateGoldOre: return FColor(252, 238, 75);
		case EBlockType::RedstoneOre:
		case EBlockType::DeepslateRedstoneOre: return FColor(210, 20, 20);
		case EBlockType::LapisOre:
		case EBlockType::DeepslateLapisOre: return FColor(20, 40, 160);
		case EBlockType::DiamondOre:
		case EBlockType::DeepslateDiamondOre: return FColor(90, 225, 220);
		case EBlockType::EmeraldOre:
		case EBlockType::DeepslateEmeraldOre: return FColor(20, 200, 80);
		case EBlockType::OakLog:
		case EBlockType::BirchLog: return FColor(100, 75, 45);
		case EBlockType::OakLeaves:
		case EBlockType::BirchLeaves: return FColor(40, 110, 40);
		default:
		{
			const uint32 Hash = GetTypeHash((uint8)Type) * 2654435761u;
			return FColor(64 + (Hash & 127), 64 + ((Hash >> 8) & 127), 64 + ((Hash >> 16) & 127));
		}
		}
	}

	const FColor SeaColor(40, 90, 200);
	const FColor PlayerColor(255, 0, 255);

	AVoxelWorld* FindVoxelWorld(UWorld* World)
	{
		for (TActorIterator<AVoxelWorld> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	/** Headless run se nakon komande sam gasi da se log flusha; u editoru se ne gasi nista. */
	void QuitIfHeadless(UWorld* World, float Delay)
	{
		if (!GIsEditor && World)
		{
			FTimerHandle QuitHandle;
			World->GetTimerManager().SetTimer(QuitHandle, FTimerDelegate::CreateLambda(
				[]() { FPlatformMisc::RequestExit(false); }), Delay, false);
		}
	}
}

/**
 * voxel.slice [x|y|z] [Z] - snimi presjek svijeta kroz igraca kao PNG u
 * Saved/VoxelSlices. Bez argumenata: okomiti rez duz osi X. "z -40" =
 * vodoravni rez na visini -40. Headless run se nakon toga sam ugasi.
 */
static FAutoConsoleCommandWithWorldAndArgs GVoxelSliceCmd(
	TEXT("voxel.slice"),
	TEXT("Snimi presjek svijeta kroz igraca kao PNG: [x|y] okomito, [z <visina>] vodoravno."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			AVoxelWorld* VoxelWorld = World ? FindVoxelWorld(World) : nullptr;
			if (!VoxelWorld)
			{
				return;
			}
			const TCHAR Axis = Args.Num() > 0 && !Args[0].IsEmpty() ? FChar::ToLower(Args[0][0]) : TEXT('x');
			const int32 Level = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : -40;
			VoxelWorld->SaveSlice(Axis, Level);
			QuitIfHeadless(World, 2.0f);
		}));

/**
 * voxel.cavetest - provjeri spilje ([CAVE] u logu). Headless run se nakon
 * testa sam ugasi.
 */
static FAutoConsoleCommandWithWorldAndArgs GVoxelCaveTestCmd(
	TEXT("voxel.cavetest"),
	TEXT("Provjeri spilje: udio supljina po dubini, otvori, dodir s morem, stabla nad rupom."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AVoxelWorld* VoxelWorld = World ? FindVoxelWorld(World) : nullptr)
			{
				VoxelWorld->RunCaveTest();
				QuitIfHeadless(World, 2.0f);
			}
		}));

/**
 * voxel.deeptest - provjeri vizuale podzemlja ([DEEP] u logu): igrac se spusti
 * u spilju, pa vrati na povrsinu. Headless run se nakon testa sam ugasi.
 */
static FAutoConsoleCommandWithWorldAndArgs GVoxelDeepTestCmd(
	TEXT("voxel.deeptest"),
	TEXT("Provjeri da se vizuali spilja grade oko igraca i uklanjaju kad ode."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AVoxelWorld* VoxelWorld = World ? FindVoxelWorld(World) : nullptr)
			{
				const float Duration = VoxelWorld->RunDeepVisualTest();
				if (Duration > 0.f)
				{
					QuitIfHeadless(World, Duration + 2.0f);
				}
			}
		}));

float AVoxelWorld::RunDeepVisualTest()
{
	AFirstPersonCharacter* Player = Cast<AFirstPersonCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0));
	if (!Player)
	{
		UE_LOG(LogTemp, Warning, TEXT("[DEEP] test: nema igraca"));
		return 0.f;
	}
	const FIntVector Start = WorldToGrid(Player->GetActorLocation());
	const FIntPoint StartChunk = GridToChunk(Start);

	// Najblize mjesto u spilji: dvije prazne celije nad cvrstom, barem 30
	// blokova ispod praga podzemlja (trazi se u ucitanim chunkovima oko igraca)
	FIntVector Cave = FIntVector::ZeroValue;
	int64 BestDistSq = TNumericLimits<int64>::Max();
	for (const TPair<FIntPoint, TUniquePtr<FVoxelChunk>>& Pair : Chunks)
	{
		const FVoxelChunk& Chunk = *Pair.Value;
		if (ChunkDistance(Chunk.Coord, StartChunk) > 4 || Chunk.DeepThreshold.Num() != ColumnsPerChunk)
		{
			continue;
		}
		const FIntVector Origin = ChunkOrigin(Chunk.Coord);
		for (int32 LocalY = 0; LocalY < ChunkSize; LocalY++)
		{
			for (int32 LocalX = 0; LocalX < ChunkSize; LocalX++)
			{
				const int64 DX = Origin.X + LocalX - Start.X;
				const int64 DY = Origin.Y + LocalY - Start.Y;
				if (DX * DX + DY * DY >= BestDistSq)
				{
					continue;
				}
				for (int32 Z = Chunk.DeepThreshold[ColumnIndex(LocalX, LocalY)] - 30; Z > WorldMinZ + 8; Z--)
				{
					if (Chunk.GetBlock(FIntVector(LocalX, LocalY, Z)) == EBlockType::Air
						&& Chunk.GetBlock(FIntVector(LocalX, LocalY, Z + 1)) == EBlockType::Air
						&& Chunk.GetBlock(FIntVector(LocalX, LocalY, Z - 1)) != EBlockType::Air)
					{
						BestDistSq = DX * DX + DY * DY;
						Cave = Origin + FIntVector(LocalX, LocalY, Z);
						break;
					}
				}
			}
		}
	}
	if (BestDistSq == TNumericLimits<int64>::Max())
	{
		UE_LOG(LogTemp, Warning, TEXT("[DEEP] test: nema spilje u ucitanim chunkovima oko igraca"));
		return 0.f;
	}

	const auto CountSections = [this](int32& OutBuilt, int32& OutInstances)
	{
		OutBuilt = 0;
		OutInstances = 0;
		for (const TPair<FIntPoint, TUniquePtr<FVoxelChunk>>& Pair : Chunks)
		{
			OutBuilt += FMath::CountBits(Pair.Value->DeepBuiltMask);
			OutInstances += CountChunkInstances(*Pair.Value, false, true);
		}
	};
	int32 BuiltBefore = 0;
	int32 InstancesBefore = 0;
	CountSections(BuiltBefore, InstancesBefore);
	UE_LOG(LogTemp, Log, TEXT("[DEEP] test: igrac na povrsini (%d,%d,%d): %d odsjecaka podzemlja, %d instanci; spilja na (%d,%d,%d); UndergroundVisualRadius %d"),
		Start.X, Start.Y, Start.Z, BuiltBefore, InstancesBefore, Cave.X, Cave.Y, Cave.Z, UndergroundVisualRadius);

	TWeakObjectPtr<AVoxelWorld> WeakThis(this);
	TWeakObjectPtr<AFirstPersonCharacter> WeakPlayer(Player);
	TSharedRef<bool> bCaveOk = MakeShared<bool>(false);

	// Teleport tek nakon 1 s: prvi streaming tick nakon starta vraca igraca
	// koji je ispod povrsine natrag na nju. Let ga drzi na mjestu u spilji.
	FTimerHandle TeleportHandle;
	GetWorldTimerManager().SetTimer(TeleportHandle, FTimerDelegate::CreateLambda([WeakThis, WeakPlayer, Cave]()
	{
		if (WeakThis.IsValid() && WeakPlayer.IsValid())
		{
			WeakPlayer->SetCreativeFlying(true);
			WeakPlayer->SetActorLocation(WeakThis->GridToWorld(Cave.X, Cave.Y, Cave.Z) + FVector(0.f, 0.f, 50.f),
				false, nullptr, ETeleportType::TeleportPhysics);
		}
	}), 1.0f, false);

	// Nakon 3 s u spilji: svaki izlozeni blok u krugu 24 bloka mora imati vizual
	FTimerHandle InCaveHandle;
	GetWorldTimerManager().SetTimer(InCaveHandle, FTimerDelegate::CreateLambda([WeakThis, WeakPlayer, Cave, CountSections, bCaveOk]()
	{
		AVoxelWorld* World = WeakThis.Get();
		AFirstPersonCharacter* P = WeakPlayer.Get();
		if (!World || !P)
		{
			return;
		}
		constexpr int32 Reach = 24;
		int32 Exposed = 0;
		int32 Missing = 0;
		for (int32 Z = FMath::Max(Cave.Z - Reach, WorldMinZ); Z <= FMath::Min(Cave.Z + Reach, WorldMaxZ); Z++)
		{
			for (int32 Y = Cave.Y - Reach; Y <= Cave.Y + Reach; Y++)
			{
				for (int32 X = Cave.X - Reach; X <= Cave.X + Reach; X++)
				{
					const FIntVector Pos(X, Y, Z);
					if (World->GetBlockTypeAt(Pos) != EBlockType::Air && World->IsBlockExposed(Pos) && !World->Blocks.Contains(Pos))
					{
						Exposed++;
						Missing += World->HasBlockInstance(Pos) ? 0 : 1;
					}
				}
			}
		}

		// Nijedan izgradeni odsjecak ne smije biti dalje od radijusa + 1
		const FIntVector PlayerGrid = World->WorldToGrid(P->GetActorLocation());
		const FIntPoint PlayerChunk = GridToChunk(PlayerGrid);
		const int32 PlayerSection = SectionIndex(FMath::Clamp(PlayerGrid.Z, WorldMinZ, WorldMaxZ));
		int32 FarBuilt = 0;
		for (const TPair<FIntPoint, TUniquePtr<FVoxelChunk>>& Pair : World->Chunks)
		{
			for (int32 Section = 0; Section < SectionsPerChunk; Section++)
			{
				if ((Pair.Value->DeepBuiltMask & (1u << Section))
					&& FMath::Max(ChunkDistance(Pair.Key, PlayerChunk), FMath::Abs(Section - PlayerSection)) > World->UndergroundVisualRadius + 1)
				{
					FarBuilt++;
				}
			}
		}
		int32 Built = 0;
		int32 Instances = 0;
		CountSections(Built, Instances);
		*bCaveOk = Missing == 0 && FarBuilt == 0 && Exposed > 0;
		UE_LOG(LogTemp, Log, TEXT("[DEEP] u spilji: igrac na Z=%d (spilja Z=%d), %d odsjecaka podzemlja, %d instanci; izlozenih blokova u krugu %d: %d, bez vizuala %d (ocekivano 0); predalekih odsjecaka %d (ocekivano 0)"),
			PlayerGrid.Z, Cave.Z, Built, Instances, Reach, Exposed, Missing, FarBuilt);

		// Natrag nad povrsinu, vodoravno dovoljno daleko da odsjecak spilje izade
		// iz radijusa + 1 pri bilo kojoj postavci UndergroundVisualRadius
		const int32 AwayX = Cave.X + (World->UndergroundVisualRadius + 3) * ChunkSize;
		P->SetActorLocation(World->GridToWorld(AwayX, Cave.Y, World->GetGroundHeightAt(AwayX, Cave.Y) + 12),
			false, nullptr, ETeleportType::TeleportPhysics);
	}), 4.0f, false);

	// Nakon jos 3 s na povrsini: odsjecak spilje mora biti uklonjen
	FTimerHandle SurfaceHandle;
	GetWorldTimerManager().SetTimer(SurfaceHandle, FTimerDelegate::CreateLambda([WeakThis, WeakPlayer, Cave, CountSections, bCaveOk]()
	{
		AVoxelWorld* World = WeakThis.Get();
		AFirstPersonCharacter* P = WeakPlayer.Get();
		if (!World || !P)
		{
			return;
		}
		const FVoxelChunk* CaveChunk = World->FindChunk(GridToChunk(Cave));
		const bool bCaveSectionBuilt = CaveChunk && (CaveChunk->DeepBuiltMask & (1u << SectionIndex(Cave.Z)));
		const int32 PlayerZ = World->WorldToGrid(P->GetActorLocation()).Z;
		int32 Built = 0;
		int32 Instances = 0;
		CountSections(Built, Instances);
		UE_LOG(LogTemp, Log, TEXT("[DEEP] opet na povrsini: igrac na Z=%d, %d odsjecaka podzemlja, %d instanci; odsjecak spilje %s"),
			PlayerZ, Built, Instances, bCaveSectionBuilt ? TEXT("JOS IZGRADEN") : TEXT("uklonjen"));
		P->SetCreativeFlying(false);
		UE_LOG(LogTemp, Log, TEXT("[DEEP] test GOTOVO: vizuali u spilji %s, uklanjanje %s -> %s"),
			*bCaveOk ? TEXT("potpuni") : TEXT("NEPOTPUNI"), bCaveSectionBuilt ? TEXT("NIJE radilo") : TEXT("radi"),
			*bCaveOk && !bCaveSectionBuilt ? TEXT("OK") : TEXT("FAIL"));
	}), 7.0f, false);

	return 7.5f;
}

FString AVoxelWorld::SaveSlice(TCHAR Axis, int32 Level)
{
	if (Axis != TEXT('x') && Axis != TEXT('y') && Axis != TEXT('z'))
	{
		UE_LOG(LogTemp, Warning, TEXT("[CAVE] voxel.slice: os mora biti x, y ili z"));
		return FString();
	}

	const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	const FIntVector Center = Player ? WorldToGrid(Player->GetActorLocation())
		: ChunkOrigin(SpawnCenterChunk) + FIntVector(ChunkSize / 2, ChunkSize / 2, 0);

	constexpr int32 Extent = 512;
	constexpr int32 Half = Extent / 2;
	const bool bHorizontal = Axis == TEXT('z');
	const int32 Width = Extent;
	const int32 Height = bHorizontal ? Extent : WorldHeight;
	Level = FMath::Clamp(Level, WorldMinZ, WorldMaxZ);

	// Chunkovi koje rez sijece: ucitani daju svoje podatke (s igracevim
	// izmjenama), ostali se generiraju samo za sliku
	const FIntPoint MinColumn = Axis == TEXT('y') ? FIntPoint(Center.X, Center.Y - Half) : FIntPoint(Center.X - Half, Axis == TEXT('x') ? Center.Y : Center.Y - Half);
	const FIntPoint MaxColumn = Axis == TEXT('y') ? FIntPoint(Center.X, Center.Y + Half - 1)
		: FIntPoint(Center.X + Half - 1, Axis == TEXT('x') ? Center.Y : Center.Y + Half - 1);
	const FIntPoint MinChunk = GridToChunk(MinColumn.X, MinColumn.Y);
	const FIntPoint MaxChunk = GridToChunk(MaxColumn.X, MaxColumn.Y);
	const int32 ChunksX = MaxChunk.X - MinChunk.X + 1;
	const int32 ChunksY = MaxChunk.Y - MinChunk.Y + 1;

	TArray<TArray<EBlockType>> Generated;
	Generated.SetNum(ChunksX * ChunksY);
	TArray<const TArray<EBlockType>*> ChunkBlocks;
	ChunkBlocks.SetNumZeroed(ChunksX * ChunksY);
	for (int32 Index = 0; Index < ChunkBlocks.Num(); Index++)
	{
		const FVoxelChunk* Loaded = FindChunk(MinChunk + FIntPoint(Index % ChunksX, Index / ChunksX));
		if (Loaded && Loaded->State != EChunkState::Generating)
		{
			ChunkBlocks[Index] = &Loaded->Blocks;
		}
	}
	const FChunkGenParams& Params = CachedGenParams;
	ParallelFor(ChunkBlocks.Num(), [&](int32 Index)
	{
		if (!ChunkBlocks[Index])
		{
			TArray<uint8> Biomes;
			Generated[Index] = FChunkGenerator::GenerateChunkData(Params, MinChunk + FIntPoint(Index % ChunksX, Index / ChunksX), Biomes);
			ChunkBlocks[Index] = &Generated[Index];
		}
	});

	const auto PixelAt = [&](int32 X, int32 Y, int32 Z)
	{
		const FIntPoint Chunk = GridToChunk(X, Y) - MinChunk;
		const EBlockType Type = (*ChunkBlocks[Chunk.Y * ChunksX + Chunk.X])[LocalIndex(GridToLocal(FIntVector(X, Y, Z)))];
		if (Type == EBlockType::Air && FChunkGenerator::IsSeaWater(Params, X, Y, Z))
		{
			return SeaColor;
		}
		return SliceColor(Type);
	};

	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(Width * Height);
	for (int32 Row = 0; Row < Height; Row++)
	{
		for (int32 Col = 0; Col < Width; Col++)
		{
			FIntVector Pos;
			if (bHorizontal)
			{
				// Sjever (+Y) je gore
				Pos = FIntVector(Center.X - Half + Col, Center.Y + Half - 1 - Row, Level);
			}
			else
			{
				// Vrh svijeta je gore
				const int32 Along = -Half + Col;
				Pos = Axis == TEXT('x') ? FIntVector(Center.X + Along, Center.Y, WorldMaxZ - Row)
					: FIntVector(Center.X, Center.Y + Along, WorldMaxZ - Row);
			}
			const bool bPlayer = Pos.X == Center.X && Pos.Y == Center.Y && (bHorizontal || Pos.Z == Center.Z);
			Pixels[Row * Width + Col] = bPlayer ? PlayerColor : PixelAt(Pos.X, Pos.Y, Pos.Z);
		}
	}

	TArray64<uint8> Png;
	FImageUtils::PNGCompressImageArray(Width, Height, TArrayView64<const FColor>(Pixels.GetData(), Pixels.Num()), Png);

	const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("VoxelSlices"));
	IFileManager::Get().MakeDirectory(*Directory, true);
	const FString FileName = bHorizontal
		? FString::Printf(TEXT("slice_seed%d_z%d_at_%d_%d.png"), WorldSeed, Level, Center.X, Center.Y)
		: FString::Printf(TEXT("slice_seed%d_%c_at_%d_%d.png"), WorldSeed, Axis, Center.X, Center.Y);
	const FString FullPath = Directory / FileName;
	if (!FFileHelper::SaveArrayToFile(Png, *FullPath))
	{
		UE_LOG(LogTemp, Error, TEXT("[CAVE] presjek NIJE spremljen: %s"), *FullPath);
		return FString();
	}
	UE_LOG(LogTemp, Log, TEXT("[CAVE] presjek spremljen: %s (%dx%d, igrac na (%d,%d,%d) ljubicasto)"),
		*FullPath, Width, Height, Center.X, Center.Y, Center.Z);
	return FullPath;
}

void AVoxelWorld::RunCaveTest()
{
	const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	const FIntPoint CenterChunk = Player ? GridToChunk(WorldToGrid(Player->GetActorLocation())) : SpawnCenterChunk;
	const FChunkGenParams& Params = CachedGenParams;

	constexpr int32 Radius = 4;
	constexpr int32 Side = 2 * Radius + 1;
	const FIntPoint MinChunk = CenterChunk - FIntPoint(Radius, Radius);
	const FIntVector RegionOrigin = ChunkOrigin(MinChunk);
	constexpr int32 RegionColumns = Side * ChunkSize;

	UE_LOG(LogTemp, Log, TEXT("[CAVE] test: %dx%d chunkova oko chunka (%d,%d), seed %d"),
		Side, Side, CenterChunk.X, CenterChunk.Y, WorldSeed);

	// --- Trajanje generacije: sredisnjih 3x3 chunka jedan po jedan, s podzemljem i bez ---
	FChunkGenParams NoCaves = Params;
	NoCaves.Caves.Reset();
	NoCaves.Underground.Reset();
	double WithMs = 0.0;
	double WithMaxMs = 0.0;
	double WithoutMs = 0.0;
	for (int32 DY = -1; DY <= 1; DY++)
	{
		for (int32 DX = -1; DX <= 1; DX++)
		{
			TArray<uint8> Biomes;
			const double T0 = FPlatformTime::Seconds();
			FChunkGenerator::GenerateChunkData(Params, CenterChunk + FIntPoint(DX, DY), Biomes);
			const double T1 = FPlatformTime::Seconds();
			FChunkGenerator::GenerateChunkData(NoCaves, CenterChunk + FIntPoint(DX, DY), Biomes);
			const double T2 = FPlatformTime::Seconds();
			WithMs += (T1 - T0) * 1000.0;
			WithMaxMs = FMath::Max(WithMaxMs, (T1 - T0) * 1000.0);
			WithoutMs += (T2 - T1) * 1000.0;
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[CAVE] generacija chunka: %.2f ms s podzemljem (najsporiji %.2f), %.2f ms bez"),
		WithMs / 9.0, WithMaxMs, WithoutMs / 9.0);

	// --- Podaci cijelog podrucja (cista generacija, bez igracevih izmjena) ---
	TArray<TArray<EBlockType>> Region;
	Region.SetNum(Side * Side);
	ParallelFor(Region.Num(), [&](int32 Index)
	{
		TArray<uint8> Biomes;
		Region[Index] = FChunkGenerator::GenerateChunkData(Params, MinChunk + FIntPoint(Index % Side, Index / Side), Biomes);
	});
	TArray<int32> Heights;
	Heights.SetNumUninitialized(RegionColumns * RegionColumns);
	ParallelFor(RegionColumns, [&](int32 RY)
	{
		for (int32 RX = 0; RX < RegionColumns; RX++)
		{
			Heights[RY * RegionColumns + RX] = FChunkGenerator::GetTerrainHeight(Params, RegionOrigin.X + RX, RegionOrigin.Y + RY);
		}
	});

	// RX, RY = stupac unutar podrucja (0..RegionColumns-1)
	const auto BlockAt = [&](int32 RX, int32 RY, int32 Z)
	{
		return Region[(RY >> ChunkSizeLog2) * Side + (RX >> ChunkSizeLog2)]
			[LocalIndex(FIntVector(RX & ChunkSizeMask, RY & ChunkSizeMask, Z))];
	};
	const auto InRegion = [&](int32 RX, int32 RY)
	{
		return RX >= 0 && RX < RegionColumns && RY >= 0 && RY < RegionColumns;
	};

	// --- Supljine po pojasevima visine, otvori, dodir s morem, stabla nad rupom ---
	constexpr int32 BandSize = 32;
	constexpr int32 NumBands = WorldHeight / BandSize;
	int64 BandCells[NumBands] = {};
	int64 BandCarved[NumBands] = {};
	int32 Openings = 0;
	int32 LandColumns = 0;
	int32 SeaContacts = 0;
	int32 FloatingLogs = 0;
	static const FIntPoint Sides[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };

	for (int32 RY = 0; RY < RegionColumns; RY++)
	{
		for (int32 RX = 0; RX < RegionColumns; RX++)
		{
			const int32 Height = FMath::Min(Heights[RY * RegionColumns + RX], WorldMaxZ);
			for (int32 Z = WorldMinZ; Z <= Height; Z++)
			{
				const int32 Band = (Z - WorldMinZ) / BandSize;
				BandCells[Band]++;
				BandCarved[Band] += BlockAt(RX, RY, Z) == EBlockType::Air ? 1 : 0;
			}
			if (Height >= SeaLevel)
			{
				LandColumns++;
				Openings += BlockAt(RX, RY, Height) == EBlockType::Air ? 1 : 0;
			}

			// More (prazne celije iznad povrsine do SeaLevel) ne smije dodirivati izdubljenu celiju
			for (int32 Z = Height + 1; Z <= SeaLevel; Z++)
			{
				if (Z - 1 == Height && BlockAt(RX, RY, Height) == EBlockType::Air)
				{
					SeaContacts++;
				}
				for (const FIntPoint& D : Sides)
				{
					if (InRegion(RX + D.X, RY + D.Y) && Z <= Heights[(RY + D.Y) * RegionColumns + RX + D.X]
						&& BlockAt(RX + D.X, RY + D.Y, Z) == EBlockType::Air)
					{
						SeaContacts++;
					}
				}
			}

			// Deblo ciji je donji blok zrak = stablo posadeno nad ulazom
			for (int32 Z = Height + 1; Z <= FMath::Min(Height + 2, WorldMaxZ); Z++)
			{
				const EBlockType Type = BlockAt(RX, RY, Z);
				if ((Type == EBlockType::OakLog || Type == EBlockType::BirchLog) && BlockAt(RX, RY, Z - 1) == EBlockType::Air)
				{
					FloatingLogs++;
				}
			}
		}
	}

	// --- Slojevi kamena i nakupine: broj blokova svakog tipa i pravila slojeva ---
	int64 TypeCounts[256] = {};
	int32 BedrockFloorErrors = 0;
	int32 DeepLayerErrors = 0;
	const FStoneLayers StoneLayers = Params.Underground ? Params.Underground->Layers : FStoneLayers();
	for (int32 RY = 0; RY < RegionColumns; RY++)
	{
		for (int32 RX = 0; RX < RegionColumns; RX++)
		{
			const int32 Height = FMath::Min(Heights[RY * RegionColumns + RX], WorldMaxZ);
			for (int32 Z = WorldMinZ; Z <= Height; Z++)
			{
				const EBlockType Type = BlockAt(RX, RY, Z);
				TypeCounts[(uint8)Type]++;
				if (StoneLayers.DeepBlock != EBlockType::Air && Z <= StoneLayers.DeepFullAtAndBelow && Type == StoneLayers.DeepReplaces)
				{
					DeepLayerErrors++;
				}
			}
			if (StoneLayers.BedrockBlock != EBlockType::Air && BlockAt(RX, RY, WorldMinZ) != StoneLayers.BedrockBlock)
			{
				BedrockFloorErrors++;
			}
		}
	}
	const UEnum* BlockEnum = StaticEnum<EBlockType>();
	for (int32 Type = 1; Type < 256; Type++)
	{
		if (TypeCounts[Type] > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("[CAVE] blok %-22s %9lld ukupno, %8.1f po chunku"),
				*BlockEnum->GetNameStringByValue(Type), TypeCounts[Type], (double)TypeCounts[Type] / (Side * Side));
		}
	}
	if (Params.Underground)
	{
		UE_LOG(LogTemp, Log, TEXT("[CAVE] slojevi: najdonji sloj koji nije %s: %d stupaca (ocekivano 0); %s na ili ispod Z %d: %d (ocekivano 0)"),
			*BlockEnum->GetNameStringByValue((int64)StoneLayers.BedrockBlock), BedrockFloorErrors,
			*BlockEnum->GetNameStringByValue((int64)StoneLayers.DeepReplaces), StoneLayers.DeepFullAtAndBelow, DeepLayerErrors);
	}

	int64 TotalCells = 0;
	int64 TotalCarved = 0;
	for (int32 Band = NumBands - 1; Band >= 0; Band--)
	{
		TotalCells += BandCells[Band];
		TotalCarved += BandCarved[Band];
		if (BandCells[Band] > 0)
		{
			UE_LOG(LogTemp, Log, TEXT("[CAVE] Z %4d..%4d: supljine %5.2f %% (%lld od %lld celija terena)"),
				WorldMinZ + Band * BandSize, WorldMinZ + (Band + 1) * BandSize - 1,
				100.0 * BandCarved[Band] / BandCells[Band], BandCarved[Band], BandCells[Band]);
		}
	}
	UE_LOG(LogTemp, Log, TEXT("[CAVE] ukupno supljina %.2f %%; otvori na povrsini: %d od %d stupaca kopna (%.2f %%)"),
		TotalCells > 0 ? 100.0 * TotalCarved / TotalCells : 0.0, Openings, LandColumns,
		LandColumns > 0 ? 100.0 * Openings / LandColumns : 0.0);

	// --- Upit za jednu tocku mora dati isto sto i generacija chunka (sredisnji chunk) ---
	const int32 CenterBase = Radius * ChunkSize;
	std::atomic<int32> PointMismatches{ 0 };
	std::atomic<int32> GroundMismatches{ 0 };
	std::atomic<int32> PointsChecked{ 0 };
	ParallelFor(ColumnsPerChunk, [&](int32 Column)
	{
		const int32 RX = CenterBase + (Column & ChunkSizeMask);
		const int32 RY = CenterBase + (Column >> ChunkSizeLog2);
		const int32 X = RegionOrigin.X + RX;
		const int32 Y = RegionOrigin.Y + RY;
		const int32 Height = FMath::Min(Heights[RY * RegionColumns + RX], WorldMaxZ);
		int32 Ground = Height;
		bool bGroundFound = false;
		for (int32 Z = Height; Z >= WorldMinZ; Z--)
		{
			const bool bAir = BlockAt(RX, RY, Z) == EBlockType::Air;
			if (FChunkGenerator::IsCaveCell(Params, X, Y, Z) != bAir)
			{
				PointMismatches++;
			}
			if (!bGroundFound && !bAir)
			{
				Ground = Z;
				bGroundFound = true;
			}
			PointsChecked++;
		}
		if (FChunkGenerator::GetGroundHeight(Params, X, Y) != Ground)
		{
			GroundMismatches++;
		}
	});
	UE_LOG(LogTemp, Log, TEXT("[CAVE] tocka = chunk: %d celija provjereno, %d razlika; visina tla: %d razlika od %d stupaca"),
		PointsChecked.load(), PointMismatches.load(), GroundMismatches.load(), ColumnsPerChunk);

	const bool bCavesOn = Params.Caves && Params.Caves->Config.bEnabled;
	const bool bOk = SeaContacts == 0 && FloatingLogs == 0 && PointMismatches == 0 && GroundMismatches == 0
		&& BedrockFloorErrors == 0 && DeepLayerErrors == 0 && (!bCavesOn || TotalCarved > 0);
	UE_LOG(LogTemp, Log, TEXT("[CAVE] test GOTOVO: dodir mora i spilje %d (ocekivano 0), stabla nad rupom %d (ocekivano 0), supljih celija %lld -> %s"),
		SeaContacts, FloatingLogs, TotalCarved, bOk ? TEXT("OK") : TEXT("FAIL"));
}
