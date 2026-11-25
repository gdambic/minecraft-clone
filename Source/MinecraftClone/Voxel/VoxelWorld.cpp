#include "VoxelWorld.h"
#include "Block.h"
#include "ItemDrop.h"
#include "ChunkGenerator.h"
#include "TimerManager.h"
#include "Zombie.h"
#include "MobBase.h"
#include "BlockRegistry.h"
#include "BlockSoundLibrary.h"
#include "BlockCrackComponent.h"
#include "AI/NavigationSystemBase.h"
#include "NavigationSystem.h"
#include "NavMesh/RecastNavMesh.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "ProceduralMeshComponent.h"
#include "CreatureBase.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/PawnMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "Engine/HitResult.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Components/PostProcessComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "VoxelWaterVolume.h"
#include "FirstPersonCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Math/RandomStream.h"
#include "Tasks/Task.h"

using namespace VoxelGrid;

/**
 * voxel.tp <X> <Y> - teleportiraj igraca na grid stupac (X,Y), na visinu
 * terena. Za rucni test streaminga (20 chunkova hoda je sporo).
 */
static FAutoConsoleCommandWithWorldAndArgs GVoxelTeleportCmd(
	TEXT("voxel.tp"),
	TEXT("Teleportiraj igraca na grid (X Y), na visinu terena."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
	{
		if (Args.Num() < 2 || !World)
		{
			UE_LOG(LogTemp, Warning, TEXT("voxel.tp: ocekujem 2 argumenta (grid X Y)"));
			return;
		}
		APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0);
		AVoxelWorld* VoxelWorld = nullptr;
		for (TActorIterator<AVoxelWorld> It(World); It; ++It)
		{
			VoxelWorld = *It;
			break;
		}
		if (!Player || !VoxelWorld)
		{
			return;
		}
		const int32 GX = FCString::Atoi(*Args[0]);
		const int32 GY = FCString::Atoi(*Args[1]);
		const int32 GZ = VoxelWorld->GetTerrainHeightAt(GX, GY) + 3;
		Player->SetActorLocation(
			VoxelWorld->GridToWorld(GX, GY, GZ) + FVector(50.f, 50.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		UE_LOG(LogTemp, Log, TEXT("voxel.tp: igrac na grid (%d,%d,%d)"), GX, GY, GZ);
	}));

/**
 * voxel.swimtest - izmjeri plivanje ([WATER] swim u logu): pad u duboko more,
 * tonjenje, drzanje skoka. Headless run se nakon testa sam ugasi.
 */
static FAutoConsoleCommandWithWorldAndArgs GVoxelSwimTestCmd(
	TEXT("voxel.swimtest"),
	TEXT("Izmjeri plivanje: pad u najblize duboko more, tonjenje, plivanje gore."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (!World)
			{
				return;
			}
			for (TActorIterator<AVoxelWorld> It(World); It; ++It)
			{
				const float Duration = It->RunSwimTest();
				// Headless run se sam gasi da se log flusha; u editoru se ne gasi nista
				if (!GIsEditor && Duration > 0.f)
				{
					FTimerHandle QuitHandle;
					World->GetTimerManager().SetTimer(QuitHandle, FTimerDelegate::CreateLambda(
						[]() { FPlatformMisc::RequestExit(false); }), Duration + 2.0f, false);
				}
				break;
			}
		}));

AVoxelWorld::AVoxelWorld()
{
	// Tick samo za pogled ispod vode (UpdateUnderwater); nakon kamere da prati njen frame
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	// Default vrijednosti - mogu se override-ati u BP_VoxelWorld
	BlockSize = 100.0f;
	VisualRadius = 6;
	ChunkApplyBudgetMs = 8.0f;
	StreamingIntervalSeconds = 0.25f;
	BootstrapRadius = 2;
	WaterMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Water/MI_Water.MI_Water")));
	WaterSubdivisions = 2;
	WaterSurfaceHeight = 0.875f;
	UnderwaterMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/Water/MI_Underwater.MI_Underwater")));
	// More je na Z=0: kopno iznad, oceani ispod (Docs/PLAN_Oceans.md); kopanje
	// ide u minus do WorldMinZ (-128), zrak za stabla/gradnju do WorldMaxZ (127).
	SeaLevel = 0;
	WorldSeed = 1337;
	NoiseScale = 0.08f;
	HeightAmplitude = 6;
	Octaves = 3;
	BiomeNoiseScale = 0.012f;
	// Pragovi kontinentalnosti iz Minecrafta (ocean < -0.19, obala do -0.11);
	// krivulja: duboki ocean do -24, crta obale na pragu oceana, unutrasnjost do +10
	ContinentNoiseScale = 0.0015f;
	ContinentOctaves = 4;
	ContinentNoiseGain = 2.3f;
	ContinentHeightCurve = {
		FVector2D(-1.0f, -24.f), FVector2D(-0.455f, -16.f), FVector2D(-0.19f, -1.f),
		FVector2D(-0.11f, 2.f), FVector2D(0.03f, 4.f), FVector2D(0.3f, 7.f), FVector2D(1.0f, 10.f) };
	OceanContinentalness = -0.19f;
	CoastContinentalness = -0.11f;
	InlandContinentalness = 0.3f;
	UnderwaterAmplitudeScale = 0.3f;
	ShallowWaterDepth = 5;
	MountainNoiseScale = 0.0025f;
	MountainOctaves = 3;
	MountainNoiseStart = 0.15f;
	MountainNoiseFull = 0.55f;
	MountainMaxLift = 70.f;
	PeakBiomeLift = 30.f;
	PeakBorderJitter = 8.f;
	PeakBorderNoiseScale = 0.06f;
	StripeNoiseScale = 0.02f;
	StripeOctaves = 2;
	StripeHalfWidth = 0.025f;
	// ~0.25 stabla po chunku = gustoca starog svijeta (10 stabala na 100x100)
	TreeAttemptsPerChunk = 1;
	TreeChancePerChunk = 0.25f;

	CompletedChunkQueue = MakeShared<TQueue<FCompletedChunkData, EQueueMode::Mpsc>, ESPMode::ThreadSafe>();

	CrackComponent = CreateDefaultSubobject<UBlockCrackComponent>(TEXT("BlockCracks"));
}

void AVoxelWorld::BeginPlay()
{
	Super::BeginPlay();
	GenerateWorld();
	SetupUnderwater();
}

void AVoxelWorld::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateWaterVolume();
	UpdateUnderwater();
}

void AVoxelWorld::UpdateWaterVolume()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(this, 0);
	if (!Player)
	{
		return;
	}
	if (!WaterVolume)
	{
		FActorSpawnParameters Params;
		Params.Owner = this;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		WaterVolume = GetWorld()->SpawnActor<AVoxelWaterVolume>(Params);
		if (!WaterVolume)
		{
			return;
		}
	}

	// Ista odluka kao za plohu: je li stupac ispod sredine igraca more
	const FVector PlayerPos = Player->GetActorLocation();
	const FVector Rel = (PlayerPos - GetActorLocation()) / BlockSize;
	const int32 X = FMath::RoundToInt(Rel.X);
	const int32 Y = FMath::RoundToInt(Rel.Y);
	const bool bSea = FChunkGenerator::IsSeaWater(CachedGenParams, X, Y, SeaLevel);

	// Vrh na povrsini vode, dno na dnu svijeta; 3 bloka sirine
	const float SurfaceZ = GetWaterSurfaceZ();
	const float BottomZ = GetActorLocation().Z + (VoxelGrid::WorldMinZ - 0.5f) * BlockSize;
	const bool bChanged = bSea != WaterVolume->IsWaterActive();
	WaterVolume->SetWaterBox(FVector(PlayerPos.X, PlayerPos.Y, SurfaceZ), 1.5f * BlockSize, SurfaceZ - BottomZ, bSea);

	// Lik volumen provjerava samo kad se sam pomakne; kad se pomakne ili upali
	// kutija, a igrac stoji (npr. teleport na dno), ne bi je nasao. Zato
	// provjera svaki frame dok je kutija ukljucena i na iskljucenju.
	if (bSea || bChanged)
	{
		Player->GetRootComponent()->UpdatePhysicsVolume(true);
	}
	if (bChanged)
	{
		UE_LOG(LogTemp, Log, TEXT("[WATER] kutija vode %s: stupac (%d,%d), igrac Z=%.1f, povrsina Z=%.1f"),
			bSea ? TEXT("UKLJUCENA") : TEXT("iskljucena"), X, Y, PlayerPos.Z, SurfaceZ);
	}

	const ACharacter* PlayerCharacter = Cast<ACharacter>(Player);
	const bool bSwimming = PlayerCharacter && PlayerCharacter->GetCharacterMovement()->IsSwimming();
	if (bSwimming != bPlayerSwimming)
	{
		bPlayerSwimming = bSwimming;
		// vZ ~520 pri izlasku = skok na obalu (OutofWaterZ)
		UE_LOG(LogTemp, Log, TEXT("[WATER] igrac %s: sredina %.1f od povrsine, vZ=%.0f"),
			bSwimming ? TEXT("PLIVA") : TEXT("vise ne pliva"), PlayerPos.Z - SurfaceZ,
			PlayerCharacter ? PlayerCharacter->GetCharacterMovement()->Velocity.Z : 0.f);
	}
}

// === CHUNKS ===

FVoxelChunk* AVoxelWorld::FindChunk(const FIntPoint& Coord)
{
	const TUniquePtr<FVoxelChunk>* Found = Chunks.Find(Coord);
	return Found ? Found->Get() : nullptr;
}

const FVoxelChunk* AVoxelWorld::FindChunk(const FIntPoint& Coord) const
{
	const TUniquePtr<FVoxelChunk>* Found = Chunks.Find(Coord);
	return Found ? Found->Get() : nullptr;
}

EBlockType AVoxelWorld::GetBlockTypeAt(FIntVector GridPosition) const
{
	if (!IsValidZ(GridPosition.Z))
	{
		return EBlockType::Air;
	}

	const FVoxelChunk* Chunk = FindChunk(GridToChunk(GridPosition));
	if (!Chunk || Chunk->State == EChunkState::Generating)
	{
		return EBlockType::Air;
	}

	return Chunk->GetBlock(GridToLocal(GridPosition));
}

bool AVoxelWorld::SetBlockData(const FIntVector& GridPos, EBlockType Type, bool bMarkModified)
{
	if (!IsValidZ(GridPos.Z))
	{
		UE_LOG(LogTemp, Warning, TEXT("SetBlockData: Z=%d izvan svijeta (%d..%d)"), GridPos.Z, WorldMinZ, WorldMaxZ);
		return false;
	}

	FVoxelChunk* Chunk = FindChunk(GridToChunk(GridPos));
	if (!Chunk || Chunk->State != EChunkState::Applied)
	{
		UE_LOG(LogTemp, Warning, TEXT("SetBlockData: chunk za (%d,%d,%d) nije ucitan/primijenjen - mutacija odbijena"),
			GridPos.X, GridPos.Y, GridPos.Z);
		return false;
	}

	Chunk->SetBlock(GridToLocal(GridPos), Type);
	if (bMarkModified)
	{
		Chunk->bModified = true;
	}
	return true;
}

// === GENERACIJA ===

FChunkGenParams AVoxelWorld::MakeChunkGenParams() const
{
	FChunkGenParams Params;
	Params.WorldSeed = WorldSeed;
	Params.NoiseOffsetX = NoiseOffsetX;
	Params.NoiseOffsetY = NoiseOffsetY;
	Params.NoiseScale = NoiseScale;
	Params.Octaves = Octaves;
	Params.SeaLevel = SeaLevel;
	Params.HeightAmplitude = HeightAmplitude;
	Params.ContinentOffsetX = ContinentOffsetX;
	Params.ContinentOffsetY = ContinentOffsetY;
	Params.ContinentNoiseScale = ContinentNoiseScale;
	Params.ContinentOctaves = ContinentOctaves;
	Params.ContinentNoiseGain = ContinentNoiseGain;
	Params.ContinentHeightCurve = ContinentHeightCurve;
	Params.ContinentHeightCurve.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X; });
	Params.OceanContinentalness = OceanContinentalness;
	Params.CoastContinentalness = CoastContinentalness;
	Params.InlandContinentalness = InlandContinentalness;
	Params.UnderwaterAmplitudeScale = UnderwaterAmplitudeScale;
	Params.ShallowWaterDepth = ShallowWaterDepth;
	Params.BiomeOffsetX = BiomeOffsetX;
	Params.BiomeOffsetY = BiomeOffsetY;
	Params.BiomeNoiseScale = BiomeNoiseScale;
	Params.MountainOffsetX = MountainOffsetX;
	Params.MountainOffsetY = MountainOffsetY;
	Params.MountainNoiseScale = MountainNoiseScale;
	Params.MountainOctaves = MountainOctaves;
	Params.MountainNoiseStart = MountainNoiseStart;
	Params.MountainNoiseFull = MountainNoiseFull;
	Params.MountainMaxLift = MountainMaxLift;
	Params.PeakBiomeLift = PeakBiomeLift;
	Params.PeakBorderOffsetX = PeakBorderOffsetX;
	Params.PeakBorderOffsetY = PeakBorderOffsetY;
	Params.PeakBorderNoiseScale = PeakBorderNoiseScale;
	Params.PeakBorderJitter = PeakBorderJitter;
	Params.StripeOffsetX = StripeOffsetX;
	Params.StripeOffsetY = StripeOffsetY;
	Params.StripeNoiseScale = StripeNoiseScale;
	Params.StripeOctaves = StripeOctaves;
	Params.StripeHalfWidth = StripeHalfWidth;

	// Biomi po nacinu postavljanja: "noise" dijele biome noise jednakim
	// pragovima (redom iz JSON-a), "peaks" dobiva vrhove planina, "ocean" more
	if (UBlockRegistry* Registry = UBlockRegistry::Get(this))
	{
		Params.NoiseBiomes.Reset();
		const int32 BiomeCount = FMath::Min(Registry->GetBiomeCount(), 256);   // indeks se sprema kao uint8
		for (int32 Index = 0; Index < BiomeCount; Index++)
		{
			const FBiomeDefinition& Biome = Registry->GetBiome(Index);
			FBiomeGenData& GenData = Params.BiomeGenData.AddDefaulted_GetRef();
			GenData.Surface = Biome.SurfaceBlock;
			GenData.Subsurface = Biome.SubsurfaceBlock;
			GenData.SubsurfaceDepth = Biome.SubsurfaceDepth;
			GenData.Stripe = Biome.StripeBlock;
			GenData.TreeDensity = Biome.TreeDensity;

			const EBiomePlacement Placement = Biome.Placement;
			if (Placement == EBiomePlacement::Peaks)
			{
				if (Params.PeakBiome >= 0)
				{
					UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: vise bioma s placement 'peaks' - koristim prvi (%s)"),
						*Registry->GetBiome(Params.PeakBiome).Name.ToString());
					continue;
				}
				Params.PeakBiome = Index;
			}
			else if (Placement == EBiomePlacement::Ocean)
			{
				if (Params.OceanBiome >= 0)
				{
					UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: vise bioma s placement 'ocean' - koristim prvi (%s)"),
						*Registry->GetBiome(Params.OceanBiome).Name.ToString());
					continue;
				}
				Params.OceanBiome = Index;
			}
			else
			{
				Params.NoiseBiomes.Add((uint8)Index);
			}
		}
		if (Params.NoiseBiomes.IsEmpty())
		{
			Params.NoiseBiomes.Add(0);
		}
	}
	Params.TreeAttemptsPerChunk = TreeAttemptsPerChunk;
	Params.TreeChancePerChunk = TreeChancePerChunk;

	// Showcase red za chunk (0,0) - po jedan blok svake definicije,
	// sortirano po enum vrijednosti (TMap redoslijed nije garantiran)
	if (UBlockRegistry* Registry = UBlockRegistry::Get(this))
	{
		TArray<FBlockDefinition> Definitions = Registry->GetAllBlockDefinitions();
		Definitions.Sort([](const FBlockDefinition& A, const FBlockDefinition& B)
		{
			return (uint8)A.BlockType < (uint8)B.BlockType;
		});
		for (const FBlockDefinition& Def : Definitions)
		{
			Params.ShowcaseBlocks.Add(Def.BlockType);
		}
	}

	return Params;
}

void AVoxelWorld::GenerateWorld()
{
	// Provjeri je li registry dostupan
	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	if (!Registry)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelWorld: BlockRegistry not found!"));
		return;
	}

	// Profil visine (krivulja kontinenata +- amplituda + planine) mora stati u WorldMinZ..WorldMaxZ
	float CurveMin = 0.f;
	float CurveMax = 0.f;
	for (int32 i = 0; i < ContinentHeightCurve.Num(); i++)
	{
		CurveMin = i == 0 ? ContinentHeightCurve[i].Y : FMath::Min(CurveMin, ContinentHeightCurve[i].Y);
		CurveMax = i == 0 ? ContinentHeightCurve[i].Y : FMath::Max(CurveMax, ContinentHeightCurve[i].Y);
	}
	if (SeaLevel + CurveMax + HeightAmplitude + MountainMaxLift + 8 > WorldMaxZ + 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: SeaLevel=%d + kopno %.0f + amplituda %d + planine %.0f ostavlja premalo zraka do stropa %d (stabla trebaju ~8)"),
			SeaLevel, CurveMax, HeightAmplitude, MountainMaxLift, WorldMaxZ + 1);
	}
	if (SeaLevel + CurveMin - HeightAmplitude < WorldMinZ)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: SeaLevel=%d + dno oceana %.0f - amplituda %d pada ispod dna svijeta %d"),
			SeaLevel, CurveMin, HeightAmplitude, WorldMinZ);
	}

	// === [PERF] Početak mjerenja - vidi Docs/PLAN_UbrzanjePokretanja.md, sekcija 2 ===
	UE_LOG(LogTemp, Warning, TEXT("[PERF] ================================================"));
	UE_LOG(LogTemp, Warning, TEXT("[PERF] GenerateWorld  seed=%d, SeaLevel=%d, amplituda +/-%d, WorldHeight=%d, VisualRadius=%d (data %d, unload %d)"),
		WorldSeed, SeaLevel, HeightAmplitude, WorldHeight, VisualRadius, GetDataRadius(), GetUnloadRadius());

	const double T0 = FPlatformTime::Seconds();

	EnsureNavMeshStepHeight();

	// --- Faza 1: assets (fiksni trosak, NE skalira s velicinom svijeta) ---
	// Razrijesi mesh/materijale JEDNOM i spremi u cache - petlje ispod koriste gotove pointere
	BuildBlockAssetCache();

	const double T1 = FPlatformTime::Seconds();

	// Seed odreduje pomak uzorkovanja noisea; globalni RandInit ostaje SAMO za
	// spawn mobova (stabla su presla na deterministicki chunk stream).
	FMath::RandInit(WorldSeed);
	const FRandomStream NoiseSeedStream(WorldSeed);
	NoiseOffsetX = NoiseSeedStream.FRandRange(-100000.f, 100000.f);
	NoiseOffsetY = NoiseSeedStream.FRandRange(-100000.f, 100000.f);
	BiomeOffsetX = NoiseSeedStream.FRandRange(-100000.f, 100000.f);
	BiomeOffsetY = NoiseSeedStream.FRandRange(-100000.f, 100000.f);
	MountainOffsetX = NoiseSeedStream.FRandRange(-100000.f, 100000.f);
	MountainOffsetY = NoiseSeedStream.FRandRange(-100000.f, 100000.f);
	StripeOffsetX = NoiseSeedStream.FRandRange(-100000.f, 100000.f);
	StripeOffsetY = NoiseSeedStream.FRandRange(-100000.f, 100000.f);
	PeakBorderOffsetX = NoiseSeedStream.FRandRange(-100000.f, 100000.f);
	PeakBorderOffsetY = NoiseSeedStream.FRandRange(-100000.f, 100000.f);
	ContinentOffsetX = NoiseSeedStream.FRandRange(-100000.f, 100000.f);
	ContinentOffsetY = NoiseSeedStream.FRandRange(-100000.f, 100000.f);

	// Snapshot parametara NAKON postavljanja offseta - streaming ga koristi do kraja igre
	CachedGenParams = MakeChunkGenParams();
	LogBiomeDiagnostics();

	// --- Faza 2: bootstrap spawn podrucja (sinkrono - igrac ne smije propasti) ---
	// Centar je PlayerStart iz levele; podaci se generiraju prsten sire od
	// vizuala da je exposure na rubu bootstrapa tocan (data ring princip).
	FIntVector SpawnGrid(ChunkSize / 2, ChunkSize / 2, 0);
	for (TActorIterator<APlayerStart> It(GetWorld()); It; ++It)
	{
		SpawnGrid = WorldToGrid(It->GetActorLocation());
		break;
	}
	// PlayerStart u oceanu: kao Minecraft, igrac krece s najblizeg kopna
	bSpawnMovedToLand = false;
	if (!IsLandColumn(SpawnGrid.X, SpawnGrid.Y))
	{
		FIntPoint Land;
		if (FindNearestLandColumn(FIntPoint(SpawnGrid.X, SpawnGrid.Y), Land))
		{
			UE_LOG(LogTemp, Warning, TEXT("[BIOME] spawn (%d,%d) je u oceanu (Z=%d) -> najblize kopno (%d,%d) Z=%d"),
				SpawnGrid.X, SpawnGrid.Y, GetTerrainHeightAt(SpawnGrid.X, SpawnGrid.Y),
				Land.X, Land.Y, GetTerrainHeightAt(Land.X, Land.Y));
			SpawnGrid = FIntVector(Land.X, Land.Y, SpawnGrid.Z);
			SpawnLandColumn = Land;
			bSpawnMovedToLand = true;
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[BIOME] spawn (%d,%d) je u oceanu, a kopna nema unutar +-1024 - ostajem"),
				SpawnGrid.X, SpawnGrid.Y);
		}
	}
	SpawnCenterChunk = GridToChunk(SpawnGrid);

	for (int32 DY = -BootstrapRadius - 1; DY <= BootstrapRadius + 1; DY++)
	{
		for (int32 DX = -BootstrapRadius - 1; DX <= BootstrapRadius + 1; DX++)
		{
			LoadChunkData(SpawnCenterChunk + FIntPoint(DX, DY));
		}
	}

	const double T2 = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Warning, TEXT("[PERF] 2/4 Podaci     %8.1f ms   %d chunkova (bootstrap oko chunka (%d,%d))"),
		(T2 - T1) * 1000.0, Chunks.Num(), SpawnCenterChunk.X, SpawnCenterChunk.Y);

	// --- Faza 3: vizuali bootstrap podrucja ---
	for (int32 DY = -BootstrapRadius; DY <= BootstrapRadius; DY++)
	{
		for (int32 DX = -BootstrapRadius; DX <= BootstrapRadius; DX++)
		{
			if (FVoxelChunk* Chunk = FindChunk(SpawnCenterChunk + FIntPoint(DX, DY)))
			{
				ApplyChunkVisuals(*Chunk);
			}
		}
	}

	const double T3 = FPlatformTime::Seconds();
	int32 TotalInstances = 0;
	for (const TPair<FIntPoint, TUniquePtr<FVoxelChunk>>& Pair : Chunks)
	{
		for (const TPair<EBlockType, FChunkInstanceSet>& SetPair : Pair.Value->InstanceSets)
		{
			TotalInstances += SetPair.Value.InstanceToGrid.Num();
		}
	}
	UE_LOG(LogTemp, Warning, TEXT("[PERF] 3/4 Vizuali    %8.1f ms   %6d instanci"),
		(T3 - T2) * 1000.0, TotalInstances);
	LogWaterDiagnostics();

	// Drugi ispis: dio stupaca sad cita biom spremljen u ucitanim chunkovima - CRC mora ostati isti
	LogBiomeDiagnostics();

	// --- Faza 4: mobovi (oko spawna - dalje od toga zive buduci per-chunk spawn, vidi BACKLOG) ---
	SpawnEnemies();
	SpawnMobs();

	const double T4 = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Warning, TEXT("[PERF] 4/4 Mobovi     %8.1f ms"), (T4 - T3) * 1000.0);

	// --- Sazetak ---
	const double AssetMs = (T1 - T0) * 1000.0;
	const double TotalMs = (T4 - T0) * 1000.0;
	UE_LOG(LogTemp, Warning, TEXT("[PERF] ------------------------------------------------"));
	UE_LOG(LogTemp, Warning, TEXT("[PERF] UKUPNO        %8.1f ms   (%d chunkova, %d instanci; ostatak puni streaming)"),
		TotalMs, Chunks.Num(), TotalInstances);
	UE_LOG(LogTemp, Warning, TEXT("[PERF]   fiksni (assets)      %8.1f ms  (%.0f%%)"),
		AssetMs, TotalMs > 0.0 ? AssetMs / TotalMs * 100.0 : 0.0);
	UE_LOG(LogTemp, Warning, TEXT("[PERF] ================================================"));

	// Pokreni timer za leaf decay (svakih 2.5 sekundi)
	GetWorld()->GetTimerManager().SetTimer(
		LeafDecayTimerHandle,
		this,
		&AVoxelWorld::ProcessLeafDecay,
		2.5f,
		true // Looping
	);

	// Streaming petlja - puni svijet oko igraca do punih radijusa i dalje ga odrzava
	GetWorld()->GetTimerManager().SetTimer(
		StreamingTimerHandle,
		this,
		&AVoxelWorld::UpdateStreaming,
		StreamingIntervalSeconds,
		true // Looping
	);

	// Soak validacija (headless): -VoxelSoakTest teleportira igraca u hopovima
	// pa natrag i provjerava da regeneracija daje bit-identicne chunkove
	bSoakTest = FParse::Param(FCommandLine::Get(), TEXT("VoxelSoakTest"));
	if (bSoakTest)
	{
		// Iskopaj jedan blok na spawnu - na kraju testa (nakon unload + reload
		// spawn podrucja) mora i dalje biti Air (retained-modified put)
		const FIntVector Base = ChunkOrigin(SpawnCenterChunk);
		SoakDigPos = FIntVector(Base.X + 3, Base.Y + 3, GetTerrainHeightAt(Base.X + 3, Base.Y + 3));
		DestroyBlockAt(SoakDigPos);
		UE_LOG(LogTemp, Warning, TEXT("[SOAK] pokrecem soak test (hop svakih 12 s); iskopan blok na (%d,%d,%d)"),
			SoakDigPos.X, SoakDigPos.Y, SoakDigPos.Z);
		GetWorld()->GetTimerManager().SetTimer(
			SoakTimerHandle, this, &AVoxelWorld::SoakTestHop, 12.0f, true);
	}
}

// === SOAK VALIDACIJA ===

void AVoxelWorld::SoakVerifyChunkData(const FVoxelChunk& Chunk)
{
	if (!bSoakTest || Chunk.bModified)
	{
		return;
	}

	const uint32 Crc = FCrc::MemCrc32(Chunk.Blocks.GetData(), Chunk.Blocks.Num() * sizeof(EBlockType));
	if (const uint32* Known = SoakChunkHashes.Find(Chunk.Coord))
	{
		if (*Known == Crc)
		{
			SoakRehashOk++;
		}
		else
		{
			SoakRehashFail++;
			UE_LOG(LogTemp, Error, TEXT("[SOAK] DETERMINIZAM FAIL: chunk (%d,%d) regeneriran s drugim sadrzajem (crc %08x != %08x)"),
				Chunk.Coord.X, Chunk.Coord.Y, Crc, *Known);
		}
	}
	else
	{
		SoakChunkHashes.Add(Chunk.Coord, Crc);
	}
}

void AVoxelWorld::SoakTestHop()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!Player)
	{
		return;
	}

	// Stanje oko prethodnog hopa (12 s je dovoljno da se vizuali izgrade)
	LogWaterDiagnostics();

	SoakHop++;

	if (SoakHop > 5)
	{
		// Iskopani blok je preizivio unload spawn chunka i regeneraciju?
		const bool bRetainedOk = GetBlockTypeAt(SoakDigPos) == EBlockType::Air;
		UE_LOG(LogTemp, Warning, TEXT("[SOAK] RETAINED %s: iskopani blok (%d,%d,%d) je %s"),
			bRetainedOk ? TEXT("OK") : TEXT("FAIL"),
			SoakDigPos.X, SoakDigPos.Y, SoakDigPos.Z, bRetainedOk ? TEXT("Air") : TEXT("regeneriran!"));
		UE_LOG(LogTemp, Warning, TEXT("[PERF] najsporiji vizual chunka u streamingu: %.2f ms, chunk (%d,%d) (budzet %.1f ms)"),
			MaxChunkApplyMs, MaxChunkApplyCoord.X, MaxChunkApplyCoord.Y, ChunkApplyBudgetMs);
		UE_LOG(LogTemp, Warning, TEXT("[SOAK] GOTOVO: rehash OK=%d, FAIL=%d, hashirano %d chunkova, ucitano %d, zadrzano %d"),
			SoakRehashOk, SoakRehashFail, SoakChunkHashes.Num(), Chunks.Num(), RetainedModifiedChunks.Num());
		GetWorld()->GetTimerManager().ClearTimer(SoakTimerHandle);
		return;
	}

	// Hop 1: istok; hop 2: NEGATIVNE koordinate (floor-division test!); hop 3:
	// daleki istok; hop 4: najblizi vrh planine (streaming visokih chunkova);
	// hop 5: povratak na start (retained + rehash provjera)
	const FIntVector Base = ChunkOrigin(SpawnCenterChunk) + FIntVector(ChunkSize / 2, ChunkSize / 2, 0);
	int32 GX = Base.X;
	int32 GY = Base.Y;
	switch (SoakHop)
	{
	case 1: GX += 10 * ChunkSize; break;
	case 2: GX -= 12 * ChunkSize; GY -= 12 * ChunkSize; break;
	case 3: GX += 24 * ChunkSize; break;
	case 4:
	{
		FIntPoint Peak;
		if (FindNearestBiomeColumn(CachedGenParams.PeakBiome, Peak))
		{
			GX = Peak.X;
			GY = Peak.Y;
		}
		break;
	}
	default: break;
	}
	const int32 GZ = GetTerrainHeightAt(GX, GY) + 3;

	Player->SetActorLocation(GridToWorld(GX, GY, GZ) + FVector(50.f, 50.f, 0.f),
		false, nullptr, ETeleportType::TeleportPhysics);
	UE_LOG(LogTemp, Warning, TEXT("[SOAK] hop %d -> grid (%d,%d,%d), ucitano %d chunkova"),
		SoakHop, GX, GY, GZ, Chunks.Num());

	// Teren se async generira ispod igraca koji vec pada - za 1 s ga vrati na povrsinu
	GetWorld()->GetTimerManager().SetTimer(
		SoakSnapTimerHandle, this, &AVoxelWorld::SoakSnapPlayerToGround, 1.0f, false);
}

void AVoxelWorld::SoakSnapPlayerToGround()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!Player)
	{
		return;
	}

	const FIntVector Grid = WorldToGrid(Player->GetActorLocation());
	const int32 GZ = GetTerrainHeightAt(Grid.X, Grid.Y) + 2;
	Player->SetActorLocation(GridToWorld(Grid.X, Grid.Y, GZ) + FVector(50.f, 50.f, 0.f),
		false, nullptr, ETeleportType::TeleportPhysics);
	if (UPawnMovementComponent* Movement = Player->GetMovementComponent())
	{
		Movement->Velocity = FVector::ZeroVector;
	}
}

// === STREAMING ===

FIntVector AVoxelWorld::WorldToGrid(const FVector& WorldPos) const
{
	const FVector Rel = (WorldPos - GetActorLocation()) / BlockSize;
	return FIntVector(FMath::FloorToInt(Rel.X), FMath::FloorToInt(Rel.Y), FMath::FloorToInt(Rel.Z));
}

FVoxelChunk& AVoxelWorld::LoadChunkData(const FIntPoint& Coord)
{
	if (FVoxelChunk* Existing = FindChunk(Coord))
	{
		return *Existing;
	}

	TUniquePtr<FVoxelChunk> Chunk = MakeUnique<FVoxelChunk>(Coord);

	// Sacuvani izmijenjeni podaci POBJEDUJU regeneraciju (plan, rizik 6)
	if (TArray<EBlockType>* Retained = RetainedModifiedChunks.Find(Coord))
	{
		Chunk->Blocks = MoveTemp(*Retained);
		Chunk->Biomes = FChunkGenerator::GenerateChunkBiomes(CachedGenParams, Coord);
		Chunk->bModified = true;
		RetainedModifiedChunks.Remove(Coord);
	}
	else
	{
		Chunk->Blocks = FChunkGenerator::GenerateChunkData(CachedGenParams, Coord, Chunk->Biomes);
	}

	Chunk->State = EChunkState::DataReady;
	FVoxelChunk& Added = *Chunks.Add(Coord, MoveTemp(Chunk));
	SoakVerifyChunkData(Added);
	return Added;
}

void AVoxelWorld::RequestChunkDataAsync(const FIntPoint& Coord)
{
	if (Chunks.Contains(Coord))
	{
		return;
	}

	TUniquePtr<FVoxelChunk> NewChunk = MakeUnique<FVoxelChunk>(Coord);
	NewChunk->State = EChunkState::Generating;
	NewChunk->GenRequestId = NextGenRequestId++;
	const uint64 RequestId = NewChunk->GenRequestId;
	Chunks.Add(Coord, MoveTemp(NewChunk));

	// Worker dobiva KOPIJU parametara i shared pointer na queue - ne dira
	// actor ni UObjecte, pa mu ni smrt svijeta usred rada ne smeta (plan 3.4)
	const FChunkGenParams Params = CachedGenParams;
	TSharedPtr<TQueue<FCompletedChunkData, EQueueMode::Mpsc>, ESPMode::ThreadSafe> Queue = CompletedChunkQueue;

	UE::Tasks::Launch(UE_SOURCE_LOCATION, [Params, Coord, RequestId, Queue]()
	{
		FCompletedChunkData Done;
		Done.Coord = Coord;
		Done.RequestId = RequestId;

		const double Start = FPlatformTime::Seconds();
		Done.Blocks = FChunkGenerator::GenerateChunkData(Params, Coord, Done.Biomes);
		Done.GenMs = (FPlatformTime::Seconds() - Start) * 1000.0;

		Queue->Enqueue(MoveTemp(Done));
	});
}

void AVoxelWorld::DrainCompletedChunkData()
{
	FCompletedChunkData Done;
	while (CompletedChunkQueue->Dequeue(Done))
	{
		FVoxelChunk* Chunk = FindChunk(Done.Coord);
		if (!Chunk || Chunk->State != EChunkState::Generating || Chunk->GenRequestId != Done.RequestId)
		{
			// Zastarjeli rezultat: chunk unloadan (i mozda ponovno zatrazen) dok
			// je worker radio - id se ne poklapa, rezultat se baca (plan, rizik 6)
			continue;
		}

		Chunk->Blocks = MoveTemp(Done.Blocks);
		Chunk->Biomes = MoveTemp(Done.Biomes);
		Chunk->State = EChunkState::DataReady;
		SoakVerifyChunkData(*Chunk);
		UE_LOG(LogTemp, Verbose, TEXT("[PERF] Chunk (%d,%d) gen=%.2fms (worker)"),
			Done.Coord.X, Done.Coord.Y, Done.GenMs);
	}
}

bool AVoxelWorld::AreNeighborsDataReady(const FIntPoint& Coord) const
{
	static const FIntPoint Sides[4] = {
		FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1)
	};
	for (const FIntPoint& Offset : Sides)
	{
		const FVoxelChunk* Neighbor = FindChunk(Coord + Offset);
		if (!Neighbor || Neighbor->State == EChunkState::Generating)
		{
			return false;
		}
	}
	return true;
}

void AVoxelWorld::UnloadChunk(const FIntPoint& Coord)
{
	FVoxelChunk* Chunk = FindChunk(Coord);
	if (!Chunk)
	{
		return;
	}

	// Promovirani actor u chunku koji nestaje - unisti bez dropa (fokus je ionako daleko)
	for (auto It = Blocks.CreateIterator(); It; ++It)
	{
		if (GridToChunk(It.Key()) == Coord)
		{
			if (It.Value())
			{
				It.Value()->Destroy();
			}
			It.RemoveCurrent();
		}
	}

	if (CrackComponent)
	{
		CrackComponent->ClearCracksInChunk(Coord);
	}

	if (Chunk->WaterMesh)
	{
		Chunk->WaterMesh->DestroyComponent();
		Chunk->WaterMesh = nullptr;
	}

	// ISM komponente + ComponentToChunk unosi zive tocno koliko i chunk
	for (TPair<EBlockType, FChunkInstanceSet>& Pair : Chunk->InstanceSets)
	{
		if (Pair.Value.Component)
		{
			ComponentToChunk.Remove(Pair.Value.Component);
			Pair.Value.Component->DestroyComponent();
		}
	}

	// Decay provjere za pozicije koje vise nisu ucitane su bespredmetne
	LeavesToCheck.RemoveAll([&Coord](const FIntVector& P) { return GridToChunk(P) == Coord; });

	// "Samo memorija" perzistencija: izmijenjeni chunk ostavlja podatke,
	// neizmijenjeni se baca u cijelosti (formula ga vraca identicnog)
	if (Chunk->bModified)
	{
		RetainedModifiedChunks.Add(Coord, MoveTemp(Chunk->Blocks));
	}

	Chunks.Remove(Coord);
}

void AVoxelWorld::UpdateStreaming()
{
	APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!Player)
	{
		return;
	}

	if (!bPlayerPlacedOnSurface)
	{
		bPlayerPlacedOnSurface = true;
		if (bSpawnMovedToLand)
		{
			// Bootstrap je generiran oko kopna, a ne oko PlayerStarta u oceanu
			Player->SetActorLocation(GridToWorld(SpawnLandColumn.X, SpawnLandColumn.Y,
				GetTerrainHeightAt(SpawnLandColumn.X, SpawnLandColumn.Y) + 2) + FVector(50.f, 50.f, 0.f),
				false, nullptr, ETeleportType::TeleportPhysics);
		}
		const FIntVector Grid = WorldToGrid(Player->GetActorLocation());
		const int32 SurfaceZ = GetTerrainHeightAt(Grid.X, Grid.Y);
		if (Grid.Z <= SurfaceZ)
		{
			Player->SetActorLocation(GridToWorld(Grid.X, Grid.Y, SurfaceZ + 2) + FVector(50.f, 50.f, 0.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			UE_LOG(LogTemp, Warning, TEXT("[BIOME] spawn (%d,%d) je ispod povrsine Z=%d - igrac podignut na povrsinu"),
				Grid.X, Grid.Y, SurfaceZ);
		}
	}

	const FIntPoint PlayerChunk = GridToChunk(WorldToGrid(Player->GetActorLocation()));
	const bool bChunkChanged = !bHasLastPlayerChunk || PlayerChunk != LastPlayerChunk;

	// 0. Preuzmi gotove worker rezultate (Generating -> DataReady)
	DrainCompletedChunkData();

	// 1. Unload svega iza histereze
	int32 Unloaded = 0;
	TArray<FIntPoint> ToUnload;
	for (const TPair<FIntPoint, TUniquePtr<FVoxelChunk>>& Pair : Chunks)
	{
		if (ChunkDistance(Pair.Key, PlayerChunk) > GetUnloadRadius())
		{
			ToUnload.Add(Pair.Key);
		}
	}
	for (const FIntPoint& Coord : ToUnload)
	{
		UnloadChunk(Coord);
		Unloaded++;
	}

	// 2. Mobovi bez tla ispod sebe bi propali kroz svijet
	DespawnFarCreatures(PlayerChunk);

	// 3. Nav prostor prati igraca (samo na promjeni chunka)
	if (bChunkChanged)
	{
		UpdateNavBoundsVolume(PlayerChunk);
	}

	// 4. Data ring: zahtjevi za sve chunkove unutar DataRadius. Generacija ide
	// na worker (plan 3.4); iznimka su sacuvani izmijenjeni chunkovi - njihov
	// povratak je samo premjestanje arraya pa ide sinkrono.
	const int32 DataRadius = GetDataRadius();
	for (int32 DY = -DataRadius; DY <= DataRadius; DY++)
	{
		for (int32 DX = -DataRadius; DX <= DataRadius; DX++)
		{
			const FIntPoint Coord = PlayerChunk + FIntPoint(DX, DY);
			if (Chunks.Contains(Coord))
			{
				continue;
			}
			if (RetainedModifiedChunks.Contains(Coord))
			{
				LoadChunkData(Coord);
			}
			else
			{
				RequestChunkDataAsync(Coord);
			}
		}
	}

	// 5. Visual ring: izgradnja instanci je skupa (~4 ms/chunk) pa ide s
	// vremenskim budzetom, najblizi chunkovi prvi. Uvjet su podaci sva 4
	// bocna susjeda - inace bi exposure na rubu vidio Air i ostavio trajno
	// visak instanci na granici.
	TArray<FVoxelChunk*> PendingVisuals;
	for (int32 DY = -VisualRadius; DY <= VisualRadius; DY++)
	{
		for (int32 DX = -VisualRadius; DX <= VisualRadius; DX++)
		{
			const FIntPoint Coord = PlayerChunk + FIntPoint(DX, DY);
			FVoxelChunk* Chunk = FindChunk(Coord);
			if (Chunk && !Chunk->bVisualsBuilt && Chunk->State == EChunkState::DataReady
				&& AreNeighborsDataReady(Coord))
			{
				PendingVisuals.Add(Chunk);
			}
		}
	}
	PendingVisuals.Sort([&PlayerChunk](const FVoxelChunk& A, const FVoxelChunk& B)
	{
		return ChunkDistance(A.Coord, PlayerChunk) < ChunkDistance(B.Coord, PlayerChunk);
	});

	int32 Applied = 0;
	const double BudgetEnd = FPlatformTime::Seconds() + ChunkApplyBudgetMs / 1000.0;
	for (FVoxelChunk* Chunk : PendingVisuals)
	{
		const double ApplyStart = FPlatformTime::Seconds();
		ApplyChunkVisuals(*Chunk);
		const double ApplyMs = (FPlatformTime::Seconds() - ApplyStart) * 1000.0;
		if (ApplyMs > MaxChunkApplyMs)
		{
			MaxChunkApplyMs = ApplyMs;
			MaxChunkApplyCoord = Chunk->Coord;
		}
		Applied++;
		if (FPlatformTime::Seconds() >= BudgetEnd)
		{
			break;
		}
	}

	if (Unloaded > 0 || Applied > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("VoxelWorld streaming: igrac u chunku (%d,%d), +%d vizuala, -%d unload, %d ucitano (%d zadrzano)"),
			PlayerChunk.X, PlayerChunk.Y, Applied, Unloaded, Chunks.Num(), RetainedModifiedChunks.Num());
	}

	LastPlayerChunk = PlayerChunk;
	bHasLastPlayerChunk = true;
}

void AVoxelWorld::DespawnFarCreatures(const FIntPoint& PlayerChunk)
{
	int32 Despawned = 0;
	for (TActorIterator<ACreatureBase> It(GetWorld()); It; ++It)
	{
		// Igracev pawn nije ACreatureBase pa ovdje prolaze samo mobovi
		const FIntPoint CreatureChunk = GridToChunk(WorldToGrid(It->GetActorLocation()));
		if (ChunkDistance(CreatureChunk, PlayerChunk) > GetUnloadRadius())
		{
			It->Destroy();
			Despawned++;
		}
	}

	if (Despawned > 0)
	{
		UE_LOG(LogTemp, Log, TEXT("VoxelWorld streaming: despawnano %d mobova izvan radijusa"), Despawned);
	}
}

void AVoxelWorld::UpdateNavBoundsVolume(const FIntPoint& PlayerChunk)
{
	if (!bNavBoundsVolumeSearched)
	{
		bNavBoundsVolumeSearched = true;
		for (TActorIterator<ANavMeshBoundsVolume> It(GetWorld()); It; ++It)
		{
			NavBoundsVolume = *It;
			// Brush actor je Static - bez ovoga SetActorLocation tiho ne radi
			if (USceneComponent* Root = It->GetRootComponent())
			{
				Root->SetMobility(EComponentMobility::Movable);
			}
			break;
		}
		if (!NavBoundsVolume.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: NavMeshBoundsVolume nije naden - navmesh nece pratiti igraca"));
		}
		else
		{
			const FBox Bounds = NavBoundsVolume->GetComponentsBoundingBox(true);
			UE_LOG(LogTemp, Warning, TEXT("[BIOME] NavMesh volumen pokriva Z %.0f..%.0f (grid %d..%d)"),
				Bounds.Min.Z, Bounds.Max.Z, WorldToGrid(Bounds.Min).Z, WorldToGrid(Bounds.Max).Z);
		}
	}

	ANavMeshBoundsVolume* Volume = NavBoundsVolume.Get();
	if (!Volume)
	{
		return;
	}

	const FIntVector Center = ChunkOrigin(PlayerChunk) + FIntVector(ChunkSize / 2, ChunkSize / 2, 0);
	FVector NewLocation = GridToWorld(Center.X, Center.Y, 0);
	NewLocation.Z = Volume->GetActorLocation().Z;   // visinu odreduje levela
	Volume->SetActorLocation(NewLocation);

	// Sam SetActorLocation ne obavjestava nav sustav da se adresabilni prostor pomaknuo
	if (UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld()))
	{
		NavSys->OnNavigationBoundsUpdated(Volume);
	}
}

void AVoxelWorld::ApplyChunkVisuals(FVoxelChunk& Chunk)
{
	if (Chunk.State == EChunkState::Generating)
	{
		UE_LOG(LogTemp, Error, TEXT("ApplyChunkVisuals: chunk (%d,%d) jos nema podatke"), Chunk.Coord.X, Chunk.Coord.Y);
		return;
	}
	if (Chunk.bVisualsBuilt)
	{
		Chunk.State = EChunkState::Applied;
		return;
	}

	// ISM instance SAMO za izlozene blokove (zakopani ostaju samo podaci).
	// Batch AddInstances po tipu - jedan poziv umjesto tisuca.
	const FIntVector Origin = ChunkOrigin(Chunk.Coord);
	TMap<EBlockType, TArray<FIntVector>> ExposedByType;

	for (int32 Z = WorldMinZ; Z <= WorldMaxZ; Z++)
	{
		for (int32 LocalY = 0; LocalY < ChunkSize; LocalY++)
		{
			for (int32 LocalX = 0; LocalX < ChunkSize; LocalX++)
			{
				const EBlockType Type = Chunk.Blocks[LocalIndex(FIntVector(LocalX, LocalY, Z))];
				if (Type == EBlockType::Air)
				{
					continue;
				}
				if (IsBlockExposedInChunk(Chunk, FIntVector(LocalX, LocalY, Z)))
				{
					ExposedByType.FindOrAdd(Type).Add(Origin + FIntVector(LocalX, LocalY, Z));
				}
			}
		}
	}

	for (const TPair<EBlockType, TArray<FIntVector>>& Pair : ExposedByType)
	{
		AddChunkInstancesBatch(Chunk, Pair.Key, Pair.Value);
	}

	BuildChunkWater(Chunk);

	Chunk.bVisualsBuilt = true;
	Chunk.State = EChunkState::Applied;
}

void AVoxelWorld::BuildChunkWater(FVoxelChunk& Chunk)
{
	const double Start = FPlatformTime::Seconds();
	const FIntVector Origin = ChunkOrigin(Chunk.Coord);

	// More po stupcu za chunk + rub od jednog stupca oko njega: rubne tocke
	// mreze trebaju znati je li susjedni stupac (i u susjednom chunku) kopno.
	// IsSeaWater je formula, pa susjed ne mora biti ucitan.
	constexpr int32 Span = ChunkSize + 2;
	TArray<bool> Sea;
	Sea.SetNumUninitialized(Span * Span);
	int32 SeaColumns = 0;
	for (int32 CY = -1; CY <= ChunkSize; CY++)
	{
		for (int32 CX = -1; CX <= ChunkSize; CX++)
		{
			const bool bSea = FChunkGenerator::IsSeaWater(CachedGenParams, Origin.X + CX, Origin.Y + CY, SeaLevel);
			Sea[(CY + 1) * Span + (CX + 1)] = bSea;
			const bool bInside = CX >= 0 && CX < ChunkSize && CY >= 0 && CY < ChunkSize;
			SeaColumns += bInside && bSea ? 1 : 0;
		}
	}
	if (SeaColumns == 0)
	{
		return;
	}
	auto IsSea = [&Sea](int32 CX, int32 CY) { return Sea[(CY + 1) * Span + (CX + 1)]; };

	// Zajednicka mreza tocaka za cijeli chunk (susjedni kvadrati dijele tocke),
	// tocka nastaje tek kad je koristi neki kvadrat nad morem
	const int32 Sub = FMath::Clamp(WaterSubdivisions, 1, 8);
	const int32 Res = ChunkSize * Sub + 1;
	const float Step = BlockSize / Sub;
	// GridToWorld je sredina bloka: stupac X pokriva X*BlockSize +- BlockSize/2
	const FVector Corner((Origin.X - 0.5f) * BlockSize, (Origin.Y - 0.5f) * BlockSize,
		(SeaLevel - 0.5f + WaterSurfaceHeight) * BlockSize);

	TArray<int32> VertexIndex;
	VertexIndex.Init(INDEX_NONE, Res * Res);
	TArray<FVector> Vertices;
	TArray<FVector> Normals;
	TArray<FVector2D> UV0;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tangents;
	TArray<int32> Triangles;
	Triangles.Reserve(SeaColumns * Sub * Sub * 6);

	auto GetVertex = [&](int32 I, int32 J) -> int32
	{
		int32& Index = VertexIndex[J * Res + I];
		if (Index != INDEX_NONE)
		{
			return Index;
		}
		Index = Vertices.Add(Corner + FVector(I * Step, J * Step, 0.f));
		Normals.Add(FVector::UpVector);
		Tangents.Add(FProcMeshTangent(1.f, 0.f, 0.f));
		// UV u blokovima svijeta - normal mapa (faza 2) se ponavlja bez savova preko chunkova
		UV0.Add(FVector2D(Origin.X - 0.5f + (float)I / Sub, Origin.Y - 0.5f + (float)J / Sub));

		// Tocka na granici stupaca dodiruje dva ili cetiri stupca; obala je
		// ako je bilo koji od njih kopno
		const int32 MinCX = I % Sub == 0 ? I / Sub - 1 : I / Sub;
		const int32 MinCY = J % Sub == 0 ? J / Sub - 1 : J / Sub;
		bool bShore = false;
		for (int32 CY = MinCY; CY <= J / Sub && !bShore; CY++)
		{
			for (int32 CX = MinCX; CX <= I / Sub && !bShore; CX++)
			{
				bShore = !IsSea(CX, CY);
			}
		}
		Colors.Add(FLinearColor(bShore ? 1.f : 0.f, 0.f, 0.f, 1.f));
		return Index;
	};

	for (int32 CY = 0; CY < ChunkSize; CY++)
	{
		for (int32 CX = 0; CX < ChunkSize; CX++)
		{
			if (!IsSea(CX, CY))
			{
				continue;
			}
			for (int32 B = 0; B < Sub; B++)
			{
				for (int32 A = 0; A < Sub; A++)
				{
					const int32 I = CX * Sub + A;
					const int32 J = CY * Sub + B;
					const int32 V00 = GetVertex(I, J);
					const int32 V10 = GetVertex(I + 1, J);
					const int32 V01 = GetVertex(I, J + 1);
					const int32 V11 = GetVertex(I + 1, J + 1);
					// Redoslijed kao UKismetProceduralMeshLibrary::CreateGridMeshWelded (ploha prema gore)
					Triangles.Append({ V00, V01, V10, V10, V01, V11 });
				}
			}
		}
	}

	const FString BaseName = FString::Printf(TEXT("Water_%d_%d"), Chunk.Coord.X, Chunk.Coord.Y);
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(
		this, MakeUniqueObjectName(this, UProceduralMeshComponent::StaticClass(), FName(*BaseName)));
	Mesh->SetMobility(GetRootComponent()->Mobility);
	// Voda ne smije blokirati raycast kopanja ni navigaciju, a sjenu ne baca
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCanEverAffectNavigation(false);
	Mesh->SetCastShadow(false);
	Mesh->SetupAttachment(GetRootComponent());
	Mesh->CreateMeshSection_LinearColor(0, Vertices, Triangles, Normals, UV0, Colors, Tangents, /*bCreateCollision=*/false);
	if (WaterMaterialLoaded)
	{
		Mesh->SetMaterial(0, WaterMaterialLoaded);
	}
	Mesh->RegisterComponent();
	Chunk.WaterMesh = Mesh;

	WaterBuildMsTotal += (FPlatformTime::Seconds() - Start) * 1000.0;
	WaterBuildCount++;
}

float AVoxelWorld::RunSwimTest()
{
	ACharacter* Player = Cast<ACharacter>(UGameplayStatics::GetPlayerPawn(this, 0));
	UCharacterMovementComponent* Move = Player ? Player->GetCharacterMovement() : nullptr;
	if (!Move)
	{
		UE_LOG(LogTemp, Warning, TEXT("[WATER] swim: nema igraca"));
		return 0.f;
	}

	// Najblize more dublje od 8 blokova (dovoljno za tonjenje bez dna)
	const FIntVector PlayerGrid = WorldToGrid(Player->GetActorLocation());
	constexpr int32 MinDepth = 8;
	constexpr int32 SearchHalf = 512;
	constexpr int32 Step = 4;
	FIntPoint Target;
	int64 BestDistSq = TNumericLimits<int64>::Max();
	for (int32 DY = -SearchHalf; DY <= SearchHalf; DY += Step)
	{
		for (int32 DX = -SearchHalf; DX <= SearchHalf; DX += Step)
		{
			const int64 DistSq = (int64)DX * DX + (int64)DY * DY;
			if (DistSq < BestDistSq && GetTerrainHeightAt(PlayerGrid.X + DX, PlayerGrid.Y + DY) <= SeaLevel - MinDepth)
			{
				BestDistSq = DistSq;
				Target = FIntPoint(PlayerGrid.X + DX, PlayerGrid.Y + DY);
			}
		}
	}
	if (BestDistSq == TNumericLimits<int64>::Max())
	{
		UE_LOG(LogTemp, Warning, TEXT("[WATER] swim: nema mora dubljeg od %d blokova u krugu %d"), MinDepth, SearchHalf);
		return 0.f;
	}

	// Lebdi 3 bloka iznad povrsine dok streaming ne ucita dno (Flying ne pada)
	const float SurfaceZ = GetWaterSurfaceZ();
	Player->SetActorLocation(GridToWorld(Target.X, Target.Y, SeaLevel + 3), false, nullptr, ETeleportType::TeleportPhysics);
	Move->Velocity = FVector::ZeroVector;
	Move->SetMovementMode(MOVE_Flying);
	UE_LOG(LogTemp, Log, TEXT("[WATER] swim: stupac (%d,%d), dno Z=%d, povrsina Z=%.1f, gravitacija %.0f, MaxSwimSpeed %.0f, Buoyancy %.2f"),
		Target.X, Target.Y, GetTerrainHeightAt(Target.X, Target.Y), SurfaceZ, Move->GetGravityZ(), Move->MaxSwimSpeed, Move->Buoyancy);

	// Faze: 0..4 s lebdi, 4 s pusti (pada u vodu i tone), 10 s drzi skok do
	// povrsine i jos ~4 s na njoj, 18 s pusti
	constexpr float ReleaseAt = 4.f;
	constexpr float SwimUpAt = 10.f;
	constexpr float SwimUpEnd = 18.f;
	constexpr float EndAt = 21.f;
	constexpr float SampleEvery = 0.1f;
	TWeakObjectPtr<ACharacter> WeakPlayer = Player;
	auto Schedule = [this](float Delay, TFunction<void()> Fn)
	{
		FTimerHandle Handle;
		GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(MoveTemp(Fn)), Delay, false);
	};

	Schedule(ReleaseAt, [WeakPlayer]()
	{
		if (ACharacter* P = WeakPlayer.Get())
		{
			P->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
		}
	});
	Schedule(SwimUpAt, [WeakPlayer]()
	{
		if (AFirstPersonCharacter* P = Cast<AFirstPersonCharacter>(WeakPlayer.Get()))
		{
			P->SetJumpInputForTest(true);
		}
	});
	Schedule(SwimUpEnd, [WeakPlayer]()
	{
		if (AFirstPersonCharacter* P = Cast<AFirstPersonCharacter>(WeakPlayer.Get()))
		{
			P->SetJumpInputForTest(false);
		}
	});
	for (float T = ReleaseAt; T <= EndAt + KINDA_SMALL_NUMBER; T += SampleEvery)
	{
		Schedule(T + 0.01f, [WeakPlayer, T, SurfaceZ, SwimUpAt, SwimUpEnd]()
		{
			ACharacter* P = WeakPlayer.Get();
			if (!P)
			{
				return;
			}
			const UCharacterMovementComponent* M = P->GetCharacterMovement();
			const TCHAR* Phase = T < SwimUpAt ? TEXT("tone") : (T < SwimUpEnd ? TEXT("skok") : TEXT("pusten"));
			UE_LOG(LogTemp, Log, TEXT("[WATER] swim t=%5.2f %-6s mod=%-9s sredina-povrsina=%7.1f vZ=%7.1f vXY=%5.1f"),
				T - ReleaseAt, Phase, *StaticEnum<EMovementMode>()->GetNameStringByValue(M->MovementMode).RightChop(5),
				P->GetActorLocation().Z - SurfaceZ, M->Velocity.Z, M->Velocity.Size2D());
		});
	}
	return EndAt;
}

float AVoxelWorld::GetWaterSurfaceZ() const
{
	return GetActorLocation().Z + (SeaLevel - 0.5f + WaterSurfaceHeight) * BlockSize;
}

void AVoxelWorld::SetupUnderwater()
{
	UMaterialInterface* Material = UnderwaterMaterial.LoadSynchronous();
	if (!Material)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelWorld: materijal ispod vode %s ne postoji - pokreni Tools > MinecraftClone > Build Water Material"),
			*UnderwaterMaterial.ToString());
		return;
	}
	UnderwaterMID = UMaterialInstanceDynamic::Create(Material, this);
	UnderwaterPostProcess = NewObject<UPostProcessComponent>(this, TEXT("UnderwaterPostProcess"));
	// Bez shape roditelja je volumen unbound - vrijedi gdje god je kamera
	UnderwaterPostProcess->bUnbound = true;
	UnderwaterPostProcess->bEnabled = false;
	UnderwaterPostProcess->Settings.AddBlendable(UnderwaterMID, 1.f);
	UnderwaterPostProcess->SetupAttachment(GetRootComponent());
	UnderwaterPostProcess->RegisterComponent();
}

void AVoxelWorld::UpdateUnderwater()
{
	const APlayerCameraManager* CameraManager = UGameplayStatics::GetPlayerCameraManager(this, 0);
	if (!UnderwaterPostProcess || !CameraManager)
	{
		return;
	}

	// Valovi iz MI_Water: isti parametri pomicu plohu i crtu vode na ekranu
	static const FName WaveNames[] = { TEXT("Wave0"), TEXT("Wave1"), TEXT("Wave2"), TEXT("Wave3") };
	FLinearColor Waves[UE_ARRAY_COUNT(WaveNames)];
	float MaxCrest = 0.f;
	for (int32 i = 0; i < UE_ARRAY_COUNT(WaveNames); i++)
	{
		Waves[i] = FLinearColor::Black;
		if (WaterMaterialLoaded)
		{
			WaterMaterialLoaded->GetVectorParameterValue(FHashedMaterialParameterInfo(WaveNames[i]), Waves[i]);
		}
		MaxCrest += FMath::Abs(Waves[i].A);
	}

	// Ukljuci se dok kamera moze biti ispod vrha vala nad stupcem mora; near
	// plane je ispred kamere, pa se i on racuna kao dio kamere
	const FVector CameraPos = CameraManager->GetCameraLocation();
	const FVector Rel = (CameraPos - GetActorLocation()) / BlockSize;
	const int32 X = FMath::RoundToInt(Rel.X);
	const int32 Y = FMath::RoundToInt(Rel.Y);
	const float SurfaceZ = GetWaterSurfaceZ();
	const float NearDist = GNearClippingPlane;
	const bool bActive = FChunkGenerator::IsSeaWater(CachedGenParams, X, Y, SeaLevel)
		&& CameraPos.Z < SurfaceZ + MaxCrest + NearDist;

	if (bActive != bUnderwaterActive)
	{
		bUnderwaterActive = bActive;
		UnderwaterPostProcess->bEnabled = bActive;
		UE_LOG(LogTemp, Log, TEXT("[WATER] pogled ispod vode %s: stupac (%d,%d), kamera Z=%.1f, ploha Z=%.1f, vrh vala +%.1f"),
			bActive ? TEXT("UKLJUCEN") : TEXT("iskljucen"), X, Y, CameraPos.Z, SurfaceZ, MaxCrest);
	}
	if (!bActive)
	{
		return;
	}

	for (int32 i = 0; i < UE_ARRAY_COUNT(WaveNames); i++)
	{
		UnderwaterMID->SetVectorParameterValue(WaveNames[i], Waves[i]);
	}
	float WaveSpeed = 1.f;
	if (WaterMaterialLoaded)
	{
		WaterMaterialLoaded->GetScalarParameterValue(FHashedMaterialParameterInfo(TEXT("WaveSpeed")), WaveSpeed);
	}
	UnderwaterMID->SetScalarParameterValue(TEXT("WaveSpeed"), WaveSpeed);
	UnderwaterMID->SetScalarParameterValue(TEXT("SurfaceZ"), SurfaceZ);
	UnderwaterMID->SetScalarParameterValue(TEXT("NearDist"), NearDist);
}

void AVoxelWorld::LogWaterDiagnostics() const
{
	// Neovisna provjera mesha: stupac ima more ako je u PODACIMA chunka najvisi
	// blok ispod razine mora (stabla u moru ne rastu). Izmijenjeni chunkovi se
	// preskacu - njihovi podaci vise nisu generirani teren.
	int32 MeshChunks = 0;
	int32 MeshQuads = 0;
	int32 MeshVertices = 0;
	int32 DataColumns = 0;
	int32 CheckedChunks = 0;
	const int32 Sub = FMath::Clamp(WaterSubdivisions, 1, 8);
	for (const TPair<FIntPoint, TUniquePtr<FVoxelChunk>>& Pair : Chunks)
	{
		const FVoxelChunk* Chunk = Pair.Value.Get();
		if (!Chunk->bVisualsBuilt || Chunk->bModified)
		{
			continue;
		}
		CheckedChunks++;
		if (Chunk->WaterMesh)
		{
			MeshChunks++;
			if (const FProcMeshSection* Section = Chunk->WaterMesh->GetProcMeshSection(0))
			{
				MeshQuads += Section->ProcIndexBuffer.Num() / 6;
				MeshVertices += Section->ProcVertexBuffer.Num();
			}
		}
		for (int32 LocalY = 0; LocalY < ChunkSize; LocalY++)
		{
			for (int32 LocalX = 0; LocalX < ChunkSize; LocalX++)
			{
				int32 Top = WorldMinZ - 1;
				for (int32 Z = SeaLevel; Z >= WorldMinZ; Z--)
				{
					if (Chunk->Blocks[LocalIndex(FIntVector(LocalX, LocalY, Z))] != EBlockType::Air)
					{
						Top = Z;
						break;
					}
				}
				DataColumns += Top < SeaLevel ? 1 : 0;
			}
		}
	}

	const int32 MeshColumns = MeshQuads / (Sub * Sub);
	UE_LOG(LogTemp, Warning, TEXT("[WATER] %d chunkova s vizualima: ploha u %d chunkova, %d stupaca (%d kvadrata, %d tocaka) | iz podataka %d stupaca -> %s"),
		CheckedChunks, MeshChunks, MeshColumns, MeshQuads, MeshVertices,
		DataColumns, MeshColumns == DataColumns ? TEXT("OK") : TEXT("RAZLIKA"));
	UE_LOG(LogTemp, Warning, TEXT("[WATER] povrsina na Z=%.1f UU, materijal %s | izgradeno %d ploha, prosjek %.3f ms"),
		GetActorLocation().Z + (SeaLevel - 0.5f + WaterSurfaceHeight) * BlockSize,
		WaterMaterialLoaded ? *WaterMaterialLoaded->GetName() : TEXT("NEMA (default)"),
		WaterBuildCount, WaterBuildCount > 0 ? WaterBuildMsTotal / WaterBuildCount : 0.0);
}

void AVoxelWorld::EnsureNavMeshStepHeight() const
{
	UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!NavSys)
	{
		return;
	}

	ARecastNavMesh* RecastNavMesh = Cast<ARecastNavMesh>(NavSys->GetDefaultNavDataInstance());
	if (!RecastNavMesh)
	{
		return;
	}

	const float RequiredStepHeight = BlockSize + 10.0f;
	if (RecastNavMesh->GetAgentMaxStepHeight(ENavigationDataResolution::Default) < RequiredStepHeight)
	{
		RecastNavMesh->SetAgentMaxStepHeight(ENavigationDataResolution::Default, RequiredStepHeight);

		// SetAgentMaxStepHeight sam po sebi NE utjece na vec izgradjeni generator -
		// FRecastNavMeshGenerator::Init() svoju kopiju (walkableClimb) snimi JEDNOM
		// pri konstrukciji, prije nego ovaj kod uopce stigne promijeniti postavku.
		// Rekonstrukcija generatora tjera ga da ponovno procita novu vrijednost.
		RecastNavMesh->ConditionalConstructGenerator();

		UE_LOG(LogTemp, Log, TEXT("VoxelWorld: NavMesh AgentMaxStepHeight -> %.1f (BlockSize=%.1f), generator rekonstruiran"), RequiredStepHeight, BlockSize);
	}
}

bool AVoxelWorld::IsBlockExposed(FIntVector Pos) const
{
	static const FIntVector Neighbors[6] = {
		FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
		FIntVector(0, 1, 0), FIntVector(0, -1, 0),
		FIntVector(0, 0, 1), FIntVector(0, 0, -1)
	};

	for (const FIntVector& Offset : Neighbors)
	{
		const FIntVector N = Pos + Offset;

		// Donju stranu svijeta (ispod WorldMinZ) nitko ne vidi - tretiraj kao
		// cvrsto. Bocne strane prema neucitanom chunku se racunaju kao izlozene
		// (GetBlockTypeAt vraca Air) - data ring u fazi 2 garantira da
		// vizualni chunkovi uvijek imaju susjede s pravim podacima.
		if (N.Z < WorldMinZ)
		{
			continue;
		}

		if (GetBlockTypeAt(N) == EBlockType::Air)
		{
			return true;
		}
	}

	return false;
}

bool AVoxelWorld::IsBlockExposedInChunk(const FVoxelChunk& Chunk, const FIntVector& Local) const
{
	static const FIntVector Neighbors[6] = {
		FIntVector(0, 0, 1), FIntVector(0, 0, -1),
		FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
		FIntVector(0, 1, 0), FIntVector(0, -1, 0)
	};

	for (const FIntVector& Offset : Neighbors)
	{
		const FIntVector N = Local + Offset;

		// Ista pravila kao IsBlockExposed: dno svijeta je cvrsto, iznad
		// stropa je zrak (GetBlockTypeAt vraca Air za nevaljani Z)
		if (N.Z < WorldMinZ)
		{
			continue;
		}
		if (N.Z > WorldMaxZ)
		{
			return true;
		}

		const bool bInsideChunk = N.X >= 0 && N.X < ChunkSize && N.Y >= 0 && N.Y < ChunkSize;
		const EBlockType NeighborType = bInsideChunk
			? Chunk.Blocks[LocalIndex(N)]
			: GetBlockTypeAt(ChunkOrigin(Chunk.Coord) + N);
		if (NeighborType == EBlockType::Air)
		{
			return true;
		}
	}

	return false;
}

FBox AVoxelWorld::GetBlockOutlineBox(EBlockType Type)
{
	// Mesh je izvor istine o tome gdje blok vizualno pocinje i gdje zavrsava:
	// i ISM instanca i promovirani actor stoje na GridToWorld(Pos), pa su
	// bounds mesha u lokalnom prostoru upravo kutija koju treba obrubiti.
	BuildBlockAssetCache();
	if (const FBlockAssets* Assets = BlockAssetCache.Find(Type))
	{
		if (Assets->Mesh)
		{
			// Izmjereno za /Engine/BasicShapes/Cube: min (-50,-50,-50),
			// max (50,50,50) - kocka je centrirana na pivotu.
			return Assets->Mesh->GetBoundingBox();
		}
	}

	const float Half = BlockSize * 0.5f;
	return FBox(FVector(-Half), FVector(Half));
}

ABlock* AVoxelWorld::PromoteToActor(FIntVector Pos)
{
	if (ABlock* Existing = Blocks.FindRef(Pos))
	{
		return Existing;
	}

	const EBlockType Type = GetBlockTypeAt(Pos);
	if (Type == EBlockType::Air)
	{
		return nullptr;
	}

	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	if (!Registry)
	{
		return nullptr;
	}

	const FBlockDefinition* BlockDefinition = Registry->GetBlockDefinition(Type);
	if (!BlockDefinition)
	{
		UE_LOG(LogTemp, Warning, TEXT("BlockRegistry: No definition for block type %d"), (int32)Type);
		return nullptr;
	}

	// Gotovi pointeri iz cachea - bez TryLoad-a po bloku
	const FBlockAssets& BlockAssets = GetBlockAssets(Type, *BlockDefinition);

	FVector WorldPos = GridToWorld(Pos.X, Pos.Y, Pos.Z);

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// Spawn generički ABlock i inicijaliziraj iz registry-a
	ABlock* NewBlock = GetWorld()->SpawnActor<ABlock>(ABlock::StaticClass(), WorldPos, FRotator::ZeroRotator, SpawnParams);

	if (NewBlock)
	{
		// Invarijanta: instanca i actor nikad istovremeno - ukloni instancu
		// tek nakon uspjesnog spawna (neuspjeh ne smije "izbrisati" vizual)
		RemoveBlockInstance(Pos);

		// Actor nije ISM instanca pa custom data tint za njega ne postoji -
		// isti tint ide kroz TintFallback parametar (na ISM putu je bijel,
		// ovdje custom data pada na bijeli default: uvijek mnozi tocno jedna boja)
		UMaterialInterface* Material = BlockAssets.Material;
		if (Material && BlockDefinition->BiomeTint != EBiomeTintType::None)
		{
			UMaterialInstanceDynamic* TintedMID = UMaterialInstanceDynamic::Create(Material, NewBlock);
			TintedMID->SetVectorParameterValue(TEXT("TintFallback"),
				GetBiomeTintAt(Pos, BlockDefinition->BiomeTint));
			Material = TintedMID;
		}

		NewBlock->SetGridPosition(Pos);
		NewBlock->InitializeFromRegistry(Type, *BlockDefinition, BlockAssets.Mesh, Material);
		Blocks.Add(Pos, NewBlock);
	}

	return NewBlock;
}

void AVoxelWorld::DemoteToInstance(FIntVector Pos)
{
	ABlock* Actor = Blocks.FindRef(Pos);
	if (!Actor)
	{
		return;
	}

	Blocks.Remove(Pos);
	Actor->Destroy();

	// Blok jos postoji u podacima -> vrati instancu (fokus je samo otisao dalje)
	const EBlockType Type = GetBlockTypeAt(Pos);
	if (Type != EBlockType::Air)
	{
		AddBlockInstance(Pos, Type);
	}
}

// === ISM INSTANCE ===

FChunkInstanceSet* AVoxelWorld::EnsureChunkInstanceSet(FVoxelChunk& Chunk, EBlockType Type)
{
	if (FChunkInstanceSet* Existing = Chunk.InstanceSets.Find(Type))
	{
		// Component == nullptr je cache-irani negativni rezultat (tip bez mesha)
		return Existing->Component ? Existing : nullptr;
	}

	BuildBlockAssetCache();

	const FBlockAssets* Assets = BlockAssetCache.Find(Type);
	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	const FBlockDefinition* Def = Registry ? Registry->GetBlockDefinition(Type) : nullptr;
	if (!Assets || !Assets->Mesh || !Def)
	{
		UE_LOG(LogTemp, Error, TEXT("EnsureChunkInstanceSet: tip %d nema mesh/definiciju - bez vizuala"), (int32)Type);
		Chunk.InstanceSets.Add(Type);   // marker da ne pokusavamo ponovno
		return nullptr;
	}

	// Lazy kreiranje: ISM komponenta nastaje kad chunk prvi put dobije blok
	// ovog tipa. MakeUniqueObjectName jer ime unistene komponente ostaje
	// zauzeto do GC-a (unload pa brzi reload istog chunka).
	const FString BaseName = FString::Printf(TEXT("ISM_%s_%d_%d"),
		*StaticEnum<EBlockType>()->GetNameStringByValue((int64)Type), Chunk.Coord.X, Chunk.Coord.Y);
	UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(
		this, MakeUniqueObjectName(this, UInstancedStaticMeshComponent::StaticClass(), FName(*BaseName)));
	ISM->SetStaticMesh(Assets->Mesh);
	if (Assets->Material)
	{
		ISM->SetMaterial(0, Assets->Material);
	}
	ISM->SetMobility(GetRootComponent()->Mobility);
	ISM->SetCollisionProfileName(TEXT("BlockAll"));
	ISM->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	ISM->SetCanEverAffectNavigation(true);
	// RemoveInstance u 5.6 po defaultu radi order-preserving RemoveAt;
	// bookkeeping u RemoveBlockInstance racuna na RemoveAtSwap semantiku
	ISM->SetRemoveSwap();

	// Blokovi s biome tintom nose RGB tint kao 3 custom data floata po
	// instanci - mora se postaviti PRIJE prve AddInstance. Engine pri
	// RemoveAtSwap sam premjesta i custom data (InstancedStaticMesh.cpp:3827).
	if (Def->BiomeTint != EBiomeTintType::None)
	{
		ISM->SetNumCustomDataFloats(3);
	}

	ISM->SetupAttachment(GetRootComponent());
	ISM->RegisterComponent();

	FChunkInstanceSet& NewSet = Chunk.InstanceSets.Add(Type);
	NewSet.Component = ISM;
	NewSet.BiomeTint = Def->BiomeTint;
	ComponentToChunk.Add(ISM, TPair<FIntPoint, EBlockType>(Chunk.Coord, Type));
	return &NewSet;
}

void AVoxelWorld::AddBlockInstance(FIntVector Pos, EBlockType Type)
{
	FVoxelChunk* Chunk = FindChunk(GridToChunk(Pos));
	if (!Chunk)
	{
		return;
	}

	FChunkInstanceSet* Set = EnsureChunkInstanceSet(*Chunk, Type);
	if (!Set || Set->GridToInstance.Contains(Pos))
	{
		return;
	}

	const int32 Index = Set->Component->AddInstance(
		FTransform(GridToWorld(Pos.X, Pos.Y, Pos.Z)), /*bWorldSpace=*/true);

	// AddInstance appenda na kraj pa indeks mora pratiti nas paralelni niz
	if (Index != Set->InstanceToGrid.Num())
	{
		UE_LOG(LogTemp, Error, TEXT("AddBlockInstance: index desync (%d != %d) za tip %d"),
			Index, Set->InstanceToGrid.Num(), (int32)Type);
	}

	// RGB tint bioma - bez njega bi custom data ostao 0 (crna trava)
	if (Set->BiomeTint != EBiomeTintType::None)
	{
		const FLinearColor Tint = GetBiomeTintAt(Pos, Set->BiomeTint);
		const float TintFloats[3] = { Tint.R, Tint.G, Tint.B };
		Set->Component->SetCustomData(Index, MakeArrayView(TintFloats, 3));
	}

	Set->GridToInstance.Add(Pos, Index);
	Set->InstanceToGrid.Add(Pos);
}

void AVoxelWorld::AddChunkInstancesBatch(FVoxelChunk& Chunk, EBlockType Type, const TArray<FIntVector>& Positions)
{
	if (Positions.Num() == 0)
	{
		return;
	}

	FChunkInstanceSet* Set = EnsureChunkInstanceSet(Chunk, Type);
	if (!Set)
	{
		return;
	}

	TArray<FTransform> Transforms;
	Transforms.Reserve(Positions.Num());
	for (const FIntVector& Pos : Positions)
	{
		Transforms.Add(FTransform(GridToWorld(Pos.X, Pos.Y, Pos.Z)));
	}

	// Indeksi batcha su kontinuirani: AddInstances appenda na kraj
	const int32 StartIndex = Set->InstanceToGrid.Num();

	Set->Component->AddInstances(Transforms, /*bShouldReturnIndices=*/false, /*bWorldSpace=*/true);

	for (const FIntVector& Pos : Positions)
	{
		Set->GridToInstance.Add(Pos, Set->InstanceToGrid.Num());
		Set->InstanceToGrid.Add(Pos);
	}

	// RGB tint bioma za cijeli batch - range overload je jedan memcpy, a
	// delta tracker (CustomDataChanged) sam gura promjenu na GPU krajem framea
	if (Set->BiomeTint != EBiomeTintType::None)
	{
		TArray<float> TintFloats;
		TintFloats.Reserve(Positions.Num() * 3);
		for (const FIntVector& Pos : Positions)
		{
			const FLinearColor Tint = GetBiomeTintAt(Pos, Set->BiomeTint);
			TintFloats.Add(Tint.R);
			TintFloats.Add(Tint.G);
			TintFloats.Add(Tint.B);
		}
		Set->Component->SetCustomData(StartIndex, StartIndex + Positions.Num() - 1, TintFloats);
	}

	// Engine registrira komponentu u nav octree kod PRVE instance, s bounds
	// izracunatim od te jedne instance - batch nakon toga NE re-registrira
	// element, pa bi navmesh builder svugdje osim oko prve instance "vidio"
	// prazninu. Osvjezi bounds pa ponovno registriraj element s punim opsegom.
	Set->Component->UpdateBounds();
	FNavigationSystem::UpdateComponentData(*Set->Component);
}

void AVoxelWorld::RemoveBlockInstance(FIntVector Pos)
{
	FVoxelChunk* Chunk = FindChunk(GridToChunk(Pos));
	if (!Chunk)
	{
		return;
	}

	for (TPair<EBlockType, FChunkInstanceSet>& Pair : Chunk->InstanceSets)
	{
		FChunkInstanceSet& Set = Pair.Value;
		const int32* Found = Set.GridToInstance.Find(Pos);
		if (!Found || !Set.Component)
		{
			continue;
		}

		const int32 Removed = *Found;
		const int32 Last = Set.InstanceToGrid.Num() - 1;

		// Komponenta ima SetRemoveSwap(): zadnja instanca preuzima indeks Removed
		Set.Component->RemoveInstance(Removed);

		if (Removed != Last)
		{
			const FIntVector MovedPos = Set.InstanceToGrid[Last];
			Set.InstanceToGrid[Removed] = MovedPos;
			Set.GridToInstance[MovedPos] = Removed;
		}
		Set.InstanceToGrid.RemoveAt(Last);
		Set.GridToInstance.Remove(Pos);
		return;
	}
}

void AVoxelWorld::EnsureBlockVisual(FIntVector Pos)
{
	const EBlockType Type = GetBlockTypeAt(Pos);
	if (Type == EBlockType::Air || Blocks.Contains(Pos))
	{
		return;
	}

	// No-op ako instanca vec postoji
	AddBlockInstance(Pos, Type);
}

bool AVoxelWorld::ResolveHitToGrid(const FHitResult& Hit, FIntVector& OutPos, EBlockType& OutType) const
{
	// Promovirani actor
	if (const ABlock* Block = Cast<ABlock>(Hit.GetActor()))
	{
		OutPos = Block->GridPosition;
		OutType = Block->BlockType;
		return true;
	}

	// ISM instanca: Hit.Item je indeks instance u pogodenoj komponenti
	const TPair<FIntPoint, EBlockType>* Key = ComponentToChunk.Find(Hit.GetComponent());
	if (!Key)
	{
		return false;
	}

	const FVoxelChunk* Chunk = FindChunk(Key->Key);
	if (!Chunk)
	{
		return false;
	}

	const FChunkInstanceSet* Set = Chunk->InstanceSets.Find(Key->Value);
	if (!Set || !Set->InstanceToGrid.IsValidIndex(Hit.Item))
	{
		return false;
	}

	OutPos = Set->InstanceToGrid[Hit.Item];
	OutType = Key->Value;
	return true;
}

void AVoxelWorld::DestroyBlockAt(FIntVector Pos, bool bDropItems)
{
	const EBlockType Type = GetBlockTypeAt(Pos);
	if (Type == EBlockType::Air)
	{
		return;
	}

	// Drop sansa iz registry definicije
	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	const FBlockDefinition* Def = Registry ? Registry->GetBlockDefinition(Type) : nullptr;
	if (bDropItems && Def && Def->DropItemType != EItemType::None && FMath::FRand() < Def->DropChance)
	{
		// GridToWorld je srediste bloka (kocka centrirana na pivotu). Kao u
		// Minecraftu (Block.popResource) drop dobije pomak do ±0.25 bloka po osi.
		const float Jitter = BlockSize * 0.25f;
		const FVector DropLocation = GridToWorld(Pos.X, Pos.Y, Pos.Z)
			+ FVector(FMath::FRandRange(-Jitter, Jitter),
			          FMath::FRandRange(-Jitter, Jitter),
			          FMath::FRandRange(-Jitter, Jitter));
		AItemDrop::SpawnItemDrop(this, Def->DropItemType, DropLocation);
	}

	NotifyBlockDestroyed(Pos, Type);
}

void AVoxelWorld::SetBlockCrack(uint32 BreakerId, FIntVector Pos, int32 Stage)
{
	if (CrackComponent)
	{
		CrackComponent->SetCrack(BreakerId, Pos, GridToWorld(Pos.X, Pos.Y, Pos.Z), Stage);
	}
}

void AVoxelWorld::NotifyBlockDestroyed(FIntVector GridPosition, EBlockType DestroyedType)
{
	// Jedina tocka kroz koju prolazi svako unistenje (kopanje, DestroyBlockAt,
	// leaf decay), a chunk unload ne ide kroz nju - zato zvuk loma stoji ovdje.
	UBlockSoundLibrary::PlayBlockSound(this, DestroyedType, EBlockSoundEvent::Break,
		GridToWorld(GridPosition.X, GridPosition.Y, GridPosition.Z));

	SetBlockData(GridPosition, EBlockType::Air, /*bMarkModified=*/true);

	// Pukotina ne smije ostati u zraku, ni kad blok srusi netko drugi
	if (CrackComponent)
	{
		CrackComponent->ClearCracksAt(GridPosition);
	}

	// Vizual je bio ili instanca (npr. leaf decay) ili promovirani actor (kopanje)
	RemoveBlockInstance(GridPosition);
	if (ABlock* Actor = Blocks.FindRef(GridPosition))
	{
		Blocks.Remove(GridPosition);
		Actor->Destroy();
	}

	// Susjedi su mozda upravo postali izlozeni -> lazy instanca
	// (no-op za pozicije bez podataka ili s vec postojecim vizualom)
	static const FIntVector Neighbors[6] = {
		FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
		FIntVector(0, 1, 0), FIntVector(0, -1, 0),
		FIntVector(0, 0, 1), FIntVector(0, 0, -1)
	};
	for (const FIntVector& Offset : Neighbors)
	{
		EnsureBlockVisual(GridPosition + Offset);
	}

	// Leaf decay notifikacije
	if (DestroyedType == EBlockType::OakLog || DestroyedType == EBlockType::BirchLog)
	{
		OnLogDestroyed(GridPosition);
	}
	else if (DestroyedType == EBlockType::OakLeaves || DestroyedType == EBlockType::BirchLeaves)
	{
		OnLeafDecayed(GridPosition);
	}
}

ABlock* AVoxelWorld::GetBlock(int32 X, int32 Y, int32 Z)
{
	FIntVector GridPos(X, Y, Z);
	ABlock** FoundBlock = Blocks.Find(GridPos);
	return FoundBlock ? *FoundBlock : nullptr;
}

int32 AVoxelWorld::GetTerrainHeightAt(int32 X, int32 Y) const
{
	return FChunkGenerator::GetTerrainHeight(CachedGenParams, X, Y);
}

int32 AVoxelWorld::GetBiomeIndexAt(int32 X, int32 Y) const
{
	// Ucitani chunk nosi biom koji je zapisao generator; formula samo za neucitano
	const FVoxelChunk* Chunk = FindChunk(GridToChunk(X, Y));
	if (Chunk && Chunk->Biomes.Num() == ColumnsPerChunk)
	{
		return Chunk->Biomes[ColumnIndex(X & ChunkSizeMask, Y & ChunkSizeMask)];
	}
	return FChunkGenerator::GetBiomeIndex(CachedGenParams, X, Y);
}

FBiomeDefinition AVoxelWorld::GetBiomeAt(int32 X, int32 Y) const
{
	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	if (!Registry)
	{
		return FBiomeDefinition();
	}
	return Registry->GetBiome(GetBiomeIndexAt(X, Y));
}

FLinearColor AVoxelWorld::GetBiomeTintAt(FIntVector Pos, EBiomeTintType TintType) const
{
	if (TintType == EBiomeTintType::None)
	{
		return FLinearColor::White;
	}
	const FBiomeDefinition Biome = GetBiomeAt(Pos.X, Pos.Y);
	return TintType == EBiomeTintType::Foliage ? Biome.FoliageTint : Biome.GrassTint;
}

void AVoxelWorld::SetBlockType(int32 X, int32 Y, int32 Z, EBlockType NewType)
{
	FIntVector GridPos(X, Y, Z);

	if (NewType == EBlockType::Air)
	{
		// Air = prazna celija - ukloni podatke i vizual
		SetBlockData(GridPos, EBlockType::Air, /*bMarkModified=*/true);
		RemoveBlockInstance(GridPos);
		if (ABlock* Actor = Blocks.FindRef(GridPos))
		{
			Blocks.Remove(GridPos);
			Actor->Destroy();
		}
		return;
	}

	if (!SetBlockData(GridPos, NewType, /*bMarkModified=*/true))
	{
		return;
	}

	if (ABlock* Actor = Blocks.FindRef(GridPos))
	{
		Actor->SetBlockType(NewType);
		return;
	}

	// Tip se mozda promijenio -> instanca mora u drugi set
	RemoveBlockInstance(GridPos);
	if (IsBlockExposed(GridPos))
	{
		AddBlockInstance(GridPos, NewType);
	}
}

FVector AVoxelWorld::GridToWorld(int32 X, int32 Y, int32 Z)
{
	// X i Y su širina/duljina, Z je visina
	return GetActorLocation() + FVector(X * BlockSize, Y * BlockSize, Z * BlockSize);
}

bool AVoxelWorld::PlaceBlockAt(FIntVector GridPosition, EBlockType Type)
{
	if (!IsValidZ(GridPosition.Z))
	{
		return false;
	}

	// Zauzetost se provjerava u podacima (Air actori vise ne postoje)
	if (GetBlockTypeAt(GridPosition) != EBlockType::Air)
	{
		return false;
	}

	FVoxelChunk* Chunk = FindChunk(GridToChunk(GridPosition));
	if (!Chunk || Chunk->State != EChunkState::Applied)
	{
		UE_LOG(LogTemp, Warning, TEXT("PlaceBlockAt: chunk za (%d,%d,%d) nije ucitan - blok odbijen"),
			GridPosition.X, GridPosition.Y, GridPosition.Z);
		return false;
	}

	// Pokriva pozive prije GenerateWorld (no-op nakon prvog uspjesnog builda)
	BuildBlockAssetCache();

	// Bez mesha (nema registry definicije) ne ostavljaj "duh" podatke
	const FBlockAssets* Assets = BlockAssetCache.Find(Type);
	if (!Assets || !Assets->Mesh)
	{
		UE_LOG(LogTemp, Error, TEXT("PlaceBlockAt: No mesh for block type %d at (%d,%d,%d)"),
			(int32)Type, GridPosition.X, GridPosition.Y, GridPosition.Z);
		return false;
	}

	Chunk->SetBlock(GridToLocal(GridPosition), Type);
	Chunk->bModified = true;

	// Postavljeni blok je po definiciji izlozen - odmah dodaj instancu
	AddBlockInstance(GridPosition, Type);

	UBlockSoundLibrary::PlayBlockSound(this, Type, EBlockSoundEvent::Place,
		GridToWorld(GridPosition.X, GridPosition.Y, GridPosition.Z));

	return true;
}

TArray<EBlockType> AVoxelWorld::GetPlaceableBlockTypes() const
{
	TArray<EBlockType> Result;

	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	if (Registry)
	{
		TArray<FBlockDefinition> AllBlocks = Registry->GetAllBlockDefinitions();
		for (const FBlockDefinition& BlockDef : AllBlocks)
		{
			// Blok se može postaviti ako ima PlaceableFromItem definiran
			if (BlockDef.PlaceableFromItem != EItemType::None)
			{
				Result.Add(BlockDef.BlockType);
			}
		}
	}

	return Result;
}

// === [BIOME] DIJAGNOSTIKA ===

void AVoxelWorld::LogBiomeDiagnostics() const
{
	// Fiksno podrucje oko ishodista (ne oko PlayerStarta) - brojke se mogu
	// usporedivati izmedu runova i izmedu verzija koda.
	constexpr int32 HalfSize = 128;
	constexpr int32 TerrainChunkRadius = 2;

	TArray<int32> ColumnsPerBiome;
	uint32 ColumnCrc = 0;
	int32 MinHeight = TNumericLimits<int32>::Max();
	int32 MaxHeight = TNumericLimits<int32>::Lowest();
	int32 ColumnsFromChunks = 0;
	for (int32 Y = -HalfSize; Y < HalfSize; Y++)
	{
		for (int32 X = -HalfSize; X < HalfSize; X++)
		{
			ColumnsFromChunks += FindChunk(GridToChunk(X, Y)) ? 1 : 0;
			const int32 Column[2] = { GetTerrainHeightAt(X, Y), GetBiomeIndexAt(X, Y) };
			ColumnCrc = FCrc::MemCrc32(Column, sizeof(Column), ColumnCrc);
			MinHeight = FMath::Min(MinHeight, Column[0]);
			MaxHeight = FMath::Max(MaxHeight, Column[0]);
			if (!ColumnsPerBiome.IsValidIndex(Column[1]))
			{
				ColumnsPerBiome.SetNumZeroed(Column[1] + 1);
			}
			ColumnsPerBiome[Column[1]]++;
		}
	}

	// Teren + provjera da biom koji generator zapise u chunk odgovara formuli
	uint32 TerrainCrc = 0;
	int32 BiomeMismatches = 0;
	for (int32 CY = -TerrainChunkRadius; CY < TerrainChunkRadius; CY++)
	{
		for (int32 CX = -TerrainChunkRadius; CX < TerrainChunkRadius; CX++)
		{
			TArray<uint8> ChunkBiomes;
			const TArray<EBlockType> ChunkBlocks =
				FChunkGenerator::GenerateChunkData(CachedGenParams, FIntPoint(CX, CY), ChunkBiomes);
			TerrainCrc = FCrc::MemCrc32(ChunkBlocks.GetData(), ChunkBlocks.Num() * sizeof(EBlockType), TerrainCrc);

			const FIntVector Origin = ChunkOrigin(FIntPoint(CX, CY));
			for (int32 LocalY = 0; LocalY < ChunkSize; LocalY++)
			{
				for (int32 LocalX = 0; LocalX < ChunkSize; LocalX++)
				{
					const int32 Formula = FChunkGenerator::GetBiomeIndex(CachedGenParams, Origin.X + LocalX, Origin.Y + LocalY);
					BiomeMismatches += ChunkBiomes[ColumnIndex(LocalX, LocalY)] != Formula ? 1 : 0;
				}
			}
		}
	}

	FString Counts;
	const UBlockRegistry* Registry = UBlockRegistry::Get(this);
	for (int32 Index = 0; Index < ColumnsPerBiome.Num(); Index++)
	{
		const FString Name = Registry ? Registry->GetBiome(Index).Name.ToString() : FString::FromInt(Index);
		Counts += FString::Printf(TEXT(" %s=%d"), *Name, ColumnsPerBiome[Index]);
	}
	UE_LOG(LogTemp, Warning, TEXT("[BIOME] %dx%d stupaca oko (0,0):%s | visina %d..%d"),
		HalfSize * 2, HalfSize * 2, *Counts, MinHeight, MaxHeight);
	UE_LOG(LogTemp, Warning, TEXT("[BIOME] CRC stupci=%08X teren(%dx%d chunkova)=%08X | biom iz ucitanih chunkova za %d stupaca, chunk!=formula: %d"),
		ColumnCrc, TerrainChunkRadius * 2, TerrainChunkRadius * 2, TerrainCrc, ColumnsFromChunks, BiomeMismatches);

	// Siroko podrucje (planine su rijetke na 256x256): udio bioma, najvisa
	// tocka i najblizi vrh - koordinate za voxel.tp
	constexpr int32 WideHalf = 1024;
	constexpr int32 WideStep = 8;
	TArray<int32> WidePerBiome;
	WidePerBiome.SetNumZeroed(ColumnsPerBiome.Num());
	int32 WideSamples = 0;
	int32 LiftedSamples = 0;
	TMap<EBlockType, int32> PeakTopBlocks;
	FIntVector Highest(0, 0, TNumericLimits<int32>::Lowest());
	int32 UnderwaterSamples = 0;
	int32 LandBiomeUnderwater = 0;   // lokve: stupac kopnenog bioma s vrhom ispod mora
	int32 DeepestFloor = TNumericLimits<int32>::Max();
	TArray<float> ContinentSamples;
	ContinentSamples.Reserve((WideHalf * 2 / WideStep) * (WideHalf * 2 / WideStep));
	for (int32 Y = -WideHalf; Y < WideHalf; Y += WideStep)
	{
		for (int32 X = -WideHalf; X < WideHalf; X += WideStep)
		{
			WideSamples++;
			const int32 Biome = FChunkGenerator::GetBiomeIndex(CachedGenParams, X, Y);
			if (!WidePerBiome.IsValidIndex(Biome))
			{
				WidePerBiome.SetNumZeroed(Biome + 1);
			}
			WidePerBiome[Biome]++;
			LiftedSamples += FChunkGenerator::GetMountainLift(CachedGenParams, X, Y) > 0.f ? 1 : 0;
			const int32 Height = GetTerrainHeightAt(X, Y);
			ContinentSamples.Add(FChunkGenerator::GetContinentalness(CachedGenParams, X, Y));
			if (FChunkGenerator::IsSeaWater(CachedGenParams, X, Y, SeaLevel))
			{
				UnderwaterSamples++;
				LandBiomeUnderwater += Biome != CachedGenParams.OceanBiome ? 1 : 0;
				DeepestFloor = FMath::Min(DeepestFloor, Height);
			}
			if (Biome == CachedGenParams.PeakBiome)
			{
				const FBiomeGenData& Surface = FChunkGenerator::GetBiomeGenData(CachedGenParams, Biome);
				const EBlockType Top = FChunkGenerator::GetColumnBlock(Surface,
					FChunkGenerator::IsStripeColumn(CachedGenParams, X, Y), Height, Height);
				PeakTopBlocks.FindOrAdd(Top)++;
			}
			if (Height > Highest.Z)
			{
				Highest = FIntVector(X, Y, Height);
			}
		}
	}

	FString WideShares;
	for (int32 Index = 0; Index < WidePerBiome.Num(); Index++)
	{
		const FString Name = Registry ? Registry->GetBiome(Index).Name.ToString() : FString::FromInt(Index);
		WideShares += FString::Printf(TEXT(" %s=%.1f%%"), *Name, WidePerBiome[Index] * 100.f / WideSamples);
	}
	UE_LOG(LogTemp, Warning, TEXT("[BIOME] %dx%d (uzorak svakih %d):%s | planina dize %.1f%% | najvise Z=%d na (%d,%d)"),
		WideHalf * 2, WideHalf * 2, WideStep, *WideShares, LiftedSamples * 100.f / WideSamples,
		Highest.Z, Highest.X, Highest.Y);

	// More: udio stupaca pod vodom, lokve u kopnenim biomima i raspodjela
	// kontinentalnosti (za ugadanje pragova - nas noise nije Minecraftov)
	ContinentSamples.Sort();
	auto Percentile = [&ContinentSamples](float P)
	{
		return ContinentSamples[FMath::Clamp(FMath::FloorToInt32(P * ContinentSamples.Num()), 0, ContinentSamples.Num() - 1)];
	};
	UE_LOG(LogTemp, Warning, TEXT("[BIOME] more (SeaLevel=%d): pod vodom %.1f%% stupaca, od toga lokve u kopnenim biomima %.1f%% | najdublje dno Z=%d"),
		SeaLevel, UnderwaterSamples * 100.f / WideSamples, LandBiomeUnderwater * 100.f / WideSamples,
		UnderwaterSamples > 0 ? DeepestFloor : SeaLevel);
	UE_LOG(LogTemp, Warning, TEXT("[BIOME] kontinentalnost: min %.3f p10 %.3f p25 %.3f p50 %.3f p75 %.3f p90 %.3f max %.3f (ocean < %.2f, obala < %.2f)"),
		ContinentSamples[0], Percentile(0.1f), Percentile(0.25f), Percentile(0.5f), Percentile(0.75f), Percentile(0.9f),
		ContinentSamples.Last(), OceanContinentalness, CoastContinentalness);

	int32 PeakTopTotal = 0;
	for (const TPair<EBlockType, int32>& Pair : PeakTopBlocks)
	{
		PeakTopTotal += Pair.Value;
	}
	FString PeakTops;
	for (const TPair<EBlockType, int32>& Pair : PeakTopBlocks)
	{
		PeakTops += FString::Printf(TEXT(" %s=%.1f%%"),
			*StaticEnum<EBlockType>()->GetNameStringByValue((int64)Pair.Key), Pair.Value * 100.f / PeakTopTotal);
	}
	UE_LOG(LogTemp, Warning, TEXT("[BIOME] povrsina vrhova (%d uzoraka):%s"), PeakTopTotal, *PeakTops);

	TArray<int32> TreesPerBiome;
	TreesPerBiome.SetNumZeroed(WidePerBiome.Num());
	TArray<FTreeCandidate> Trees;
	for (int32 CY = -WideHalf / ChunkSize; CY < WideHalf / ChunkSize; CY++)
	{
		for (int32 CX = -WideHalf / ChunkSize; CX < WideHalf / ChunkSize; CX++)
		{
			FChunkGenerator::GetTreeCandidates(CachedGenParams, FIntPoint(CX, CY), Trees);
		}
	}
	for (const FTreeCandidate& Tree : Trees)
	{
		const int32 Biome = FChunkGenerator::GetBiomeIndex(CachedGenParams, Tree.Base.X, Tree.Base.Y);
		if (TreesPerBiome.IsValidIndex(Biome))
		{
			TreesPerBiome[Biome]++;
		}
	}
	FString TreeCounts;
	for (int32 Index = 0; Index < TreesPerBiome.Num(); Index++)
	{
		const FString Name = Registry ? Registry->GetBiome(Index).Name.ToString() : FString::FromInt(Index);
		TreeCounts += FString::Printf(TEXT(" %s=%d"), *Name, TreesPerBiome[Index]);
	}
	UE_LOG(LogTemp, Warning, TEXT("[BIOME] stabla u %dx%d:%s"), WideHalf * 2, WideHalf * 2, *TreeCounts);

	FIntPoint Ocean;
	if (FindNearestBiomeColumn(CachedGenParams.OceanBiome, Ocean))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BIOME] najblizi ocean od (0,0): (%d,%d) dno Z=%d, udaljenost %.0f blokova -> voxel.tp %d %d"),
			Ocean.X, Ocean.Y, GetTerrainHeightAt(Ocean.X, Ocean.Y), FVector2D(Ocean).Size(), Ocean.X, Ocean.Y);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BIOME] nema oceana unutar +-1024 od (0,0)"));
	}

	FIntPoint Peak;
	if (FindNearestBiomeColumn(CachedGenParams.PeakBiome, Peak))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BIOME] najblizi vrh od (0,0): (%d,%d) Z=%d, udaljenost %.0f blokova -> voxel.tp %d %d"),
			Peak.X, Peak.Y, GetTerrainHeightAt(Peak.X, Peak.Y), FVector2D(Peak).Size(), Peak.X, Peak.Y);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BIOME] nema bioma vrhova unutar +-1024 od (0,0)"));
	}
}

bool AVoxelWorld::FindNearestBiomeColumn(int32 Biome, FIntPoint& OutColumn) const
{
	if (Biome < 0)
	{
		return false;
	}

	constexpr int32 SearchHalf = 1024;
	constexpr int32 Step = 8;
	int64 BestDistSq = TNumericLimits<int64>::Max();
	for (int32 Y = -SearchHalf; Y <= SearchHalf; Y += Step)
	{
		for (int32 X = -SearchHalf; X <= SearchHalf; X += Step)
		{
			const int64 DistSq = (int64)X * X + (int64)Y * Y;
			if (DistSq < BestDistSq && FChunkGenerator::GetBiomeIndex(CachedGenParams, X, Y) == Biome)
			{
				BestDistSq = DistSq;
				OutColumn = FIntPoint(X, Y);
			}
		}
	}
	return BestDistSq != TNumericLimits<int64>::Max();
}

bool AVoxelWorld::FindNearestLandColumn(const FIntPoint& Center, FIntPoint& OutColumn) const
{
	constexpr int32 SearchHalf = 1024;
	constexpr int32 Step = 4;
	int64 BestDistSq = TNumericLimits<int64>::Max();
	for (int32 DY = -SearchHalf; DY <= SearchHalf; DY += Step)
	{
		for (int32 DX = -SearchHalf; DX <= SearchHalf; DX += Step)
		{
			const int64 DistSq = (int64)DX * DX + (int64)DY * DY;
			if (DistSq < BestDistSq && IsLandColumn(Center.X + DX, Center.Y + DY))
			{
				BestDistSq = DistSq;
				OutColumn = Center + FIntPoint(DX, DY);
			}
		}
	}
	return BestDistSq != TNumericLimits<int64>::Max();
}

bool AVoxelWorld::IsLandColumn(int32 X, int32 Y) const
{
	return GetTerrainHeightAt(X, Y) >= SeaLevel;
}

// === [PERF] DIJAGNOSTIKA ===

void AVoxelWorld::BuildBlockAssetCache()
{
	if (bAssetCacheReady)
	{
		return;
	}

	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	if (!Registry)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PERF] 1/4 Assets     -- registry nedostupan --"));
		return;
	}

	const TArray<FBlockDefinition> AllBlocks = Registry->GetAllBlockDefinitions();

	const double Start = FPlatformTime::Seconds();

	// ISM komponente chunkova se attachaju na root - osiguraj da postoji
	// (BP_VoxelWorld ima DefaultSceneRoot, ali cisti C++ spawn nema)
	if (!GetRootComponent())
	{
		USceneComponent* Root = NewObject<USceneComponent>(this, TEXT("VoxelWorldRoot"));
		SetRootComponent(Root);
		Root->RegisterComponent();
	}

	int32 Resolved = 0;
	int32 Failed = 0;
	for (const FBlockDefinition& Def : AllBlocks)
	{
		FBlockAssets Assets;
		Assets.Mesh = Cast<UStaticMesh>(Def.Mesh.TryLoad());
		Assets.Material = Cast<UMaterialInterface>(Def.Material.TryLoad());
		BlockAssetCache.Add(Def.BlockType, Assets);

		// Statistika za [PERF] log
		const FSoftObjectPath* Paths[2] = { &Def.Mesh, &Def.Material };
		const UObject* Loaded[2] = { Assets.Mesh, Assets.Material };
		for (int32 i = 0; i < 2; i++)
		{
			if (Paths[i]->IsNull())
			{
				continue;
			}
			if (Loaded[i])
			{
				Resolved++;
			}
			else
			{
				Failed++;
				UE_LOG(LogTemp, Warning, TEXT("[PERF]     NEUSPJEH: %s"), *Paths[i]->ToString());
			}
		}
	}

	WaterMaterialLoaded = WaterMaterial.LoadSynchronous();
	if (!WaterMaterialLoaded)
	{
		UE_LOG(LogTemp, Error, TEXT("VoxelWorld: materijal vode %s ne postoji - pokreni Tools > MinecraftClone > Build Water Material"),
			*WaterMaterial.ToString());
	}

	bAssetCacheReady = true;

	const double ElapsedMs = (FPlatformTime::Seconds() - Start) * 1000.0;
	UE_LOG(LogTemp, Warning, TEXT("[PERF] 1/4 Assets     %8.1f ms   %6d ucitano, %d neuspjelo (%d definicija)"),
		ElapsedMs, Resolved, Failed, AllBlocks.Num());
}

const FBlockAssets& AVoxelWorld::GetBlockAssets(EBlockType Type, const FBlockDefinition& BlockDefinition)
{
	if (const FBlockAssets* Cached = BlockAssetCache.Find(Type))
	{
		return *Cached;
	}

	// Lazy fallback - npr. ako se blok postavlja prije nego je GenerateWorld izgradio cache
	FBlockAssets Assets;
	Assets.Mesh = Cast<UStaticMesh>(BlockDefinition.Mesh.TryLoad());
	Assets.Material = Cast<UMaterialInterface>(BlockDefinition.Material.TryLoad());
	return BlockAssetCache.Add(Type, Assets);
}

// === ENEMIES ===

void AVoxelWorld::SpawnEnemies()
{
	if (!ZombieClass)
	{
		return;
	}

	// Spawn zombie na nasumičnoj poziciji na površini spawn podrucja
	// (uvijek unutar bootstrap chunkova - teren garantirano postoji)
	const FIntVector SpawnCenter = ChunkOrigin(SpawnCenterChunk) + FIntVector(ChunkSize / 2, ChunkSize / 2, 0);
	// Samo kopno - spawn podrucje moze dodirivati obalu
	int32 RandX = SpawnCenter.X;
	int32 RandY = SpawnCenter.Y;
	for (int32 Attempt = 0; Attempt < 10; Attempt++)
	{
		RandX = SpawnCenter.X + FMath::RandRange(-20, 20);
		RandY = SpawnCenter.Y + FMath::RandRange(-20, 20);
		if (IsLandColumn(RandX, RandY))
		{
			break;
		}
	}
	if (!IsLandColumn(RandX, RandY))
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: nema kopna za zombija oko spawna - preskacem"));
		return;
	}
	int32 SpawnZ = GetTerrainHeightAt(RandX, RandY) + 1;

	FVector WorldPos = GridToWorld(RandX, RandY, SpawnZ);
	// Podignuti malo iznad površine da izbjegnemo collision
	WorldPos.Z += 88.0f;

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	AZombie* Zombie = GetWorld()->SpawnActor<AZombie>(ZombieClass, WorldPos, FRotator::ZeroRotator, SpawnParams);

	if (Zombie)
	{
		UE_LOG(LogTemp, Log, TEXT("VoxelWorld: Spawned zombie at (%d, %d, %d)"), RandX, RandY, SpawnZ);
	}
}

// === MOBS ===

void AVoxelWorld::SpawnMobs()
{
	if (MobSpawns.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: MobSpawns list is empty, skipping mob spawn"));
		return;
	}

	for (const FMobSpawnEntry& Entry : MobSpawns)
	{
		if (!Entry.MobClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("VoxelWorld: MobSpawns entry has no class set, skipping"));
			continue;
		}

		int32 SpawnedCount = 0;

		const FIntVector SpawnCenter = ChunkOrigin(SpawnCenterChunk) + FIntVector(ChunkSize / 2, ChunkSize / 2, 0);

		for (int32 i = 0; i < Entry.Count; i++)
		{
			// Random pozicija na kopnu spawn podrucja (unutar bootstrap chunkova)
			int32 RandX = SpawnCenter.X;
			int32 RandY = SpawnCenter.Y;
			for (int32 Attempt = 0; Attempt < 10; Attempt++)
			{
				RandX = SpawnCenter.X + FMath::RandRange(-20, 20);
				RandY = SpawnCenter.Y + FMath::RandRange(-20, 20);
				if (IsLandColumn(RandX, RandY))
				{
					break;
				}
			}
			if (!IsLandColumn(RandX, RandY))
			{
				continue;
			}
			int32 SpawnZ = GetTerrainHeightAt(RandX, RandY) + 1;

			FVector WorldPos = GridToWorld(RandX, RandY, SpawnZ);
			// Podignuti iznad površine da izbjegnemo collision
			WorldPos.Z += 50.0f;

			FActorSpawnParameters SpawnParams;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;

			AMobBase* Mob = GetWorld()->SpawnActor<AMobBase>(Entry.MobClass, WorldPos, FRotator::ZeroRotator, SpawnParams);

			if (Mob)
			{
				SpawnedCount++;
			}
		}

		UE_LOG(LogTemp, Log, TEXT("VoxelWorld: Spawned %d/%d %s"),
			SpawnedCount, Entry.Count, *Entry.MobClass->GetName());
	}
}

// === LEAF DECAY ===

void AVoxelWorld::OnLogDestroyed(FIntVector LogPosition)
{
	// Dodaj svo lišće unutar 6 blokova (Manhattan distance) u listu za provjeru
	for (int32 X = -6; X <= 6; X++)
	{
		for (int32 Y = -6; Y <= 6; Y++)
		{
			for (int32 Z = -6; Z <= 6; Z++)
			{
				// Manhattan distance provjera
				if (FMath::Abs(X) + FMath::Abs(Y) + FMath::Abs(Z) > 6)
				{
					continue;
				}

				FIntVector CheckPos = LogPosition + FIntVector(X, Y, Z);
				EBlockType Type = GetBlockTypeAt(CheckPos);

				if (Type == EBlockType::OakLeaves || Type == EBlockType::BirchLeaves)
				{
					LeavesToCheck.AddUnique(CheckPos);
				}
			}
		}
	}
}

void AVoxelWorld::OnLeafDecayed(FIntVector LeafPosition)
{
	// Kad list propadne, dodaj sve listove unutar Manhattan 6 u listu za provjeru
	// Ovo hvata listove koji su možda već bili provjereni ali su izgubili vezu
	for (int32 X = -6; X <= 6; X++)
	{
		for (int32 Y = -6; Y <= 6; Y++)
		{
			for (int32 Z = -6; Z <= 6; Z++)
			{
				if (FMath::Abs(X) + FMath::Abs(Y) + FMath::Abs(Z) > 6)
				{
					continue;
				}

				FIntVector CheckPos = LeafPosition + FIntVector(X, Y, Z);
				EBlockType Type = GetBlockTypeAt(CheckPos);

				if (Type == EBlockType::OakLeaves || Type == EBlockType::BirchLeaves)
				{
					LeavesToCheck.AddUnique(CheckPos);
				}
			}
		}
	}
}

void AVoxelWorld::ProcessLeafDecay()
{
	if (LeavesToCheck.Num() == 0)
	{
		return;
	}

	// Procesiraj batch listova po tikcu (performance)
	int32 BatchSize = FMath::Min(10, LeavesToCheck.Num());

	for (int32 i = 0; i < BatchSize; i++)
	{
		FIntVector LeafPos = LeavesToCheck[0];
		LeavesToCheck.RemoveAt(0);

		// Provjeri je li još uvijek lišće (u podacima)
		EBlockType Type = GetBlockTypeAt(LeafPos);
		if (Type != EBlockType::OakLeaves && Type != EBlockType::BirchLeaves)
		{
			continue;
		}

		// Provjeri ima li vezu s logom
		if (!HasLogConnection(LeafPos))
		{
			// Nema veze - decay: instant unistenje s drop sansom (sapling)
			// iz registry definicije, bez promoviranja u actor
			DestroyBlockAt(LeafPos);
		}
	}
}

bool AVoxelWorld::HasLogConnection(FIntVector LeafPosition) const
{
	// BFS pretraga za log unutar 6 blokova Manhattan distance
	TSet<FIntVector> Visited;
	TArray<FIntVector> Queue;
	Queue.Add(LeafPosition);

	while (Queue.Num() > 0)
	{
		FIntVector Current = Queue[0];
		Queue.RemoveAt(0);

		if (Visited.Contains(Current))
		{
			continue;
		}
		Visited.Add(Current);

		// Provjeri Manhattan distance od početne pozicije
		int32 Distance = FMath::Abs(Current.X - LeafPosition.X) +
		                 FMath::Abs(Current.Y - LeafPosition.Y) +
		                 FMath::Abs(Current.Z - LeafPosition.Z);
		if (Distance > 6)
		{
			continue;
		}

		// BFS je dotakao neucitan chunk - prekini konzervativno kao "ima vezu"
		// (plan 3.8): decay je kozmetika, bolje lazno "povezano" nego rupa u
		// krosnji cije je deblo iza granice ucitanog svijeta.
		const FVoxelChunk* Chunk = FindChunk(GridToChunk(Current));
		if (!Chunk || Chunk->State == EChunkState::Generating)
		{
			return true;
		}

		EBlockType Type = GetBlockTypeAt(Current);
		if (Type == EBlockType::Air)
		{
			continue;
		}

		// Pronašli smo log - lišće je podržano
		if (Type == EBlockType::OakLog || Type == EBlockType::BirchLog)
		{
			return true;
		}

		// Nastavi BFS kroz lišće
		if (Type == EBlockType::OakLeaves || Type == EBlockType::BirchLeaves)
		{
			// Dodaj 6 susjeda
			Queue.Add(Current + FIntVector(1, 0, 0));
			Queue.Add(Current + FIntVector(-1, 0, 0));
			Queue.Add(Current + FIntVector(0, 1, 0));
			Queue.Add(Current + FIntVector(0, -1, 0));
			Queue.Add(Current + FIntVector(0, 0, 1));
			Queue.Add(Current + FIntVector(0, 0, -1));
		}
	}

	return false;
}
