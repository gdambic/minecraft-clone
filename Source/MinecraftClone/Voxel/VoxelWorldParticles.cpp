// Cestice blokova po Minecraftu (Docs/PLAN_Torches.md): plamen i dim baklje.
// Koji blok i koliko cesto ispusta cestice ponavlja ClientLevel.animateTick,
// a gibanje cestica klase Particle, RisingParticle, FlameParticle i
// BaseAshSmokeParticle (procitano iz 1.21.11 jara). Dio AVoxelWorld-a.

#include "VoxelWorld.h"
#include "BlockRegistry.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Pawn.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "TimerManager.h"

using namespace VoxelGrid;

namespace
{
	/** Minecraftov tick: 20 u sekundi */
	constexpr float ParticleTickSeconds = 0.05f;

	/** ClientLevel.animateTick: toliko nasumicnih celija po ticku, jednom u dometu 16 i jednom u dometu 32 */
	constexpr int32 AnimateSamplesPerTick = 667;

	/** Broj slicica dima (particles/smoke.json: generic_7 ... generic_0) */
	constexpr int32 SmokeFrames = 8;

	/** Custom data cestice: slicica, pa RGB boja */
	constexpr int32 NumParticleCustomFloats = 4;

	const TCHAR* GParticleMaterialPaths[2] = {
		TEXT("/Game/Blocks/Particles/MI_ParticleFlame.MI_ParticleFlame"),
		TEXT("/Game/Blocks/Particles/MI_ParticleSmoke.MI_ParticleSmoke")
	};

	TAutoConsoleVariable<int32> CVarVoxelParticles(
		TEXT("voxel.particles"), 1,
		TEXT("Cestice blokova (plamen i dim baklje): 1 = ukljuceno, 0 = iskljuceno."));

	AVoxelWorld* FindVoxelWorldForParticles(UWorld* World)
	{
		for (TActorIterator<AVoxelWorld> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}

	/**
	 * Kvadrat 1x1 u ravnini YZ: Y = desno, Z = gore, U raste udesno, V prema
	 * dolje. Instanca zakrenuta kao kamera (X naprijed, Y desno, Z gore) zato
	 * stoji uspravno i licem prema kameri.
	 */
	UStaticMesh* BuildParticleQuad(UObject* Outer)
	{
		FMeshDescription MeshDescription;
		FStaticMeshAttributes Attributes(MeshDescription);
		Attributes.Register();

		const FName SlotName(TEXT("Particle"));
		const FPolygonGroupID Group = MeshDescription.CreatePolygonGroup();
		Attributes.GetPolygonGroupMaterialSlotNames()[Group] = SlotName;

		const FVector3f Corners[4] = {
			FVector3f(0.f, -0.5f, 0.5f), FVector3f(0.f, 0.5f, 0.5f), FVector3f(0.f, 0.5f, -0.5f), FVector3f(0.f, -0.5f, -0.5f)
		};
		const FVector2f CornerUVs[4] = { FVector2f(0.f, 0.f), FVector2f(1.f, 0.f), FVector2f(1.f, 1.f), FVector2f(0.f, 1.f) };
		FVertexInstanceID Instances[4];
		for (int32 Corner = 0; Corner < 4; Corner++)
		{
			const FVertexID Vertex = MeshDescription.CreateVertex();
			Attributes.GetVertexPositions()[Vertex] = Corners[Corner];
			Instances[Corner] = MeshDescription.CreateVertexInstance(Vertex);
			Attributes.GetVertexInstanceNormals()[Instances[Corner]] = FVector3f(-1.f, 0.f, 0.f);
			Attributes.GetVertexInstanceTangents()[Instances[Corner]] = FVector3f(0.f, 1.f, 0.f);
			Attributes.GetVertexInstanceBinormalSigns()[Instances[Corner]] = 1.f;
			Attributes.GetVertexInstanceColors()[Instances[Corner]] = FVector4f(1.f, 1.f, 1.f, 1.f);
			Attributes.GetVertexInstanceUVs().Set(Instances[Corner], 0, CornerUVs[Corner]);
		}
		MeshDescription.CreateTriangle(Group, { Instances[0], Instances[3], Instances[2] });
		MeshDescription.CreateTriangle(Group, { Instances[0], Instances[2], Instances[1] });

		UStaticMesh* Mesh = NewObject<UStaticMesh>(Outer, MakeUniqueObjectName(Outer, UStaticMesh::StaticClass(), TEXT("SM_BlockParticleQuad")), RF_Transient);
		Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, SlotName, SlotName));

		UStaticMesh::FBuildMeshDescriptionsParams Params;
		Params.bFastBuild = true;
		Params.bAllowCpuAccess = true;
		Params.bMarkPackageDirty = false;
		Params.bCommitMeshDescription = false;
		TArray<const FMeshDescription*> Descriptions;
		Descriptions.Add(&MeshDescription);
		Mesh->BuildFromMeshDescriptions(Descriptions, Params);
		return Mesh;
	}
}

/**
 * voxel.particletest - provjeri cestice baklje ([PARTICLE] u logu): mjesto
 * nastanka, trajanje, gibanje i ucestalost. Headless run se nakon testa sam ugasi.
 */
static FAutoConsoleCommandWithWorldAndArgs GVoxelParticleTestCmd(
	TEXT("voxel.particletest"),
	TEXT("Provjeri plamen i dim baklje: mjesto nastanka, trajanje, gibanje i ucestalost."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AVoxelWorld* VoxelWorld = World ? FindVoxelWorldForParticles(World) : nullptr)
			{
				const float Duration = VoxelWorld->RunParticleTest();
				if (!GIsEditor)
				{
					FTimerHandle QuitHandle;
					World->GetTimerManager().SetTimer(QuitHandle, FTimerDelegate::CreateLambda(
						[]() { FPlatformMisc::RequestExit(false); }), Duration + 2.0f, false);
				}
			}
		}));

void AVoxelWorld::EnsureParticleComponents()
{
	if (ParticleComponents[0])
	{
		return;
	}
	if (!GetRootComponent())
	{
		BuildBlockAssetCache();
	}

	ParticleQuadMesh = BuildParticleQuad(this);
	for (int32 Kind = 0; Kind < 2; Kind++)
	{
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, GParticleMaterialPaths[Kind]);
		if (!Material)
		{
			UE_LOG(LogTemp, Error, TEXT("VoxelWorld: nema %s - pokreni Tools > MinecraftClone > Import Torch"), GParticleMaterialPaths[Kind]);
		}

		UInstancedStaticMeshComponent* ISM = NewObject<UInstancedStaticMeshComponent>(
			this, Kind == 0 ? TEXT("ISM_ParticleFlame") : TEXT("ISM_ParticleSmoke"));
		ISM->SetStaticMesh(ParticleQuadMesh);
		if (Material)
		{
			ISM->SetMaterial(0, Material);
		}
		ISM->SetMobility(EComponentMobility::Movable);
		ISM->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ISM->SetCanEverAffectNavigation(false);
		ISM->SetCastShadow(false);
		ISM->SetNumCustomDataFloats(NumParticleCustomFloats);
		ISM->SetupAttachment(GetRootComponent());
		ISM->RegisterComponent();
		ParticleComponents[Kind] = ISM;
	}
}

void AVoxelWorld::SpawnBlockParticle(int32 Kind, const FVector& Position)
{
	FBlockParticle Particle;

	// Particle(level, x, y, z, 0, 0, 0): nasumican smjer, brzina (r + r + 1) * 0.15 * 0.4, plus 0.1 prema gore
	FVector Velocity(
		(ParticleRandom.FRand() * 2.f - 1.f) * 0.4f,
		(ParticleRandom.FRand() * 2.f - 1.f) * 0.4f,
		(ParticleRandom.FRand() * 2.f - 1.f) * 0.4f);
	const double Speed = (ParticleRandom.FRand() + ParticleRandom.FRand() + 1.f) * 0.15f;
	const double Length = FMath::Max(Velocity.Size(), UE_DOUBLE_SMALL_NUMBER);
	Velocity = Velocity / Length * Speed * 0.4;
	Velocity.Z += 0.1;

	// SingleQuadParticle: quadSize = 0.1 * (r * 0.5 + 0.5) * 2
	Particle.QuadSize = 0.1f * (ParticleRandom.FRand() * 0.5f + 0.5f) * 2.f;
	Particle.Position = Position;

	if (Kind == 0)
	{
		// RisingParticle: brzina na stotinku, pomak mjesta do 0.05 po osi, trajanje 8 / (r * 0.8 + 0.2) + 4 ticka
		Velocity *= 0.01;
		Particle.Position += FVector(
			(ParticleRandom.FRand() - ParticleRandom.FRand()) * 0.05f,
			(ParticleRandom.FRand() - ParticleRandom.FRand()) * 0.05f,
			(ParticleRandom.FRand() - ParticleRandom.FRand()) * 0.05f);
		Particle.Lifetime = (int32)(8.0 / (ParticleRandom.FRand() * 0.8 + 0.2)) + 4;
		Particle.Gravity = 0.f;
		Particle.bCollides = false;
		Particle.Color = FLinearColor::White;
	}
	else
	{
		// SmokeParticle -> BaseAshSmokeParticle(0.1, 0.1, 0.1, ..., scale 1, boja do 0.3, trajanje 8, gravitacija -0.1)
		Velocity *= 0.1;
		// Minecraft boju (siva do 0.3) mnozi u prikaznom prostoru, a nas materijal u
		// linearnom; bez pretvorbe bi dim bio svijetlosiv umjesto gotovo crn
		const float Grey = FMath::Pow(ParticleRandom.FRand() * 0.3f, 2.2f);
		Particle.Color = FLinearColor(Grey, Grey, Grey);
		Particle.QuadSize *= 0.75f;
		Particle.Lifetime = FMath::Max((int32)(8.0 / (ParticleRandom.FRand() * 0.8 + 0.2)), 1);
		Particle.Gravity = -0.1f;
		Particle.bCollides = true;
	}

	Particle.Velocity = Velocity;
	Particle.PreviousPosition = Particle.Position;
	BlockParticles[Kind].Add(Particle);
}

bool AVoxelWorld::AnimateBlock(const FIntVector& Pos)
{
	const EBlockType Type = GetBlockTypeAt(Pos);
	const EBlockBehavior Behavior = BehaviorByType[(uint8)Type];
	if (Behavior != EBlockBehavior::Torch && Behavior != EBlockBehavior::WallTorch)
	{
		return false;
	}

	// TorchBlock.animateTick: sredina celije, 0.7 bloka iznad dna (srediste celije je nasa pozicija, pa +0.2).
	// WallTorchBlock.animateTick: jos 0.22 gore i 0.27 prema zidu.
	FVector Position(Pos.X, Pos.Y, Pos.Z + 0.2);
	if (Behavior == EBlockBehavior::WallTorch)
	{
		const UBlockRegistry* Registry = UBlockRegistry::Get(this);
		const FBlockDefinition* Def = Registry ? Registry->GetBlockDefinition(Type) : nullptr;
		const FIntVector Facing = Def ? Def->GetFacing(GetBlockStateAt(Pos)) : FIntVector::ZeroValue;
		Position += FVector(-0.27 * Facing.X, -0.27 * Facing.Y, 0.22);
	}

	SpawnBlockParticle(1, Position);
	SpawnBlockParticle(0, Position);
	return true;
}

int32 AVoxelWorld::AnimateTickAround(const FIntVector& Center)
{
	// ClientLevel.animateTick: 667 puta po jedna nasumicna celija u dometu 16 i
	// u dometu 32. Pomak po osi je nextInt(domet) - nextInt(domet), pa su
	// celije blize igracu cesce na redu.
	int32 Animated = 0;
	for (int32 Sample = 0; Sample < AnimateSamplesPerTick; Sample++)
	{
		for (const int32 Range : { 16, 32 })
		{
			const FIntVector Offset(
				ParticleRandom.RandHelper(Range) - ParticleRandom.RandHelper(Range),
				ParticleRandom.RandHelper(Range) - ParticleRandom.RandHelper(Range),
				ParticleRandom.RandHelper(Range) - ParticleRandom.RandHelper(Range));
			Animated += AnimateBlock(Center + Offset) ? 1 : 0;
		}
	}
	return Animated;
}

void AVoxelWorld::StepBlockParticles()
{
	for (int32 Kind = 0; Kind < 2; Kind++)
	{
		TArray<FBlockParticle>& Particles = BlockParticles[Kind];
		for (int32 Index = Particles.Num() - 1; Index >= 0; Index--)
		{
			FBlockParticle& Particle = Particles[Index];

			// Particle.tick
			Particle.PreviousPosition = Particle.Position;
			if (Particle.Age++ >= Particle.Lifetime)
			{
				Particles.RemoveAtSwap(Index);
				continue;
			}
			Particle.Velocity.Z -= 0.04 * Particle.Gravity;

			// Particle.move: os po os; puna kocka zaustavlja cesticu koja ima fiziku (dim)
			bool bVerticalBlocked = false;
			for (int32 Axis = 0; Axis < 3; Axis++)
			{
				FVector Next = Particle.Position;
				Next[Axis] += Particle.Velocity[Axis];
				const FIntVector Cell(FMath::FloorToInt(Next.X + 0.5), FMath::FloorToInt(Next.Y + 0.5), FMath::FloorToInt(Next.Z + 0.5));
				if (Particle.bCollides && IsFullCube(GetBlockTypeAt(Cell)))
				{
					if (Axis == 2)
					{
						bVerticalBlocked = true;
					}
					else
					{
						Particle.Velocity[Axis] = 0.0;
					}
					continue;
				}
				Particle.Position = Next;
			}

			// speedUpWhenYMotionIsBlocked: dim se pod stropom razlijeva u stranu
			if (Particle.bCollides && bVerticalBlocked)
			{
				Particle.Velocity.X *= 1.1;
				Particle.Velocity.Y *= 1.1;
			}
			Particle.Velocity *= 0.96;
		}
	}
}

void AVoxelWorld::UpdateBlockParticleVisuals(float PartialTick)
{
	const APlayerCameraManager* Camera = UGameplayStatics::GetPlayerCameraManager(this, 0);
	const FQuat CameraRotation = Camera ? Camera->GetCameraRotation().Quaternion() : FQuat::Identity;
	const FVector WorldOrigin = GetActorLocation();

	for (int32 Kind = 0; Kind < 2; Kind++)
	{
		UInstancedStaticMeshComponent* ISM = ParticleComponents[Kind];
		const TArray<FBlockParticle>& Particles = BlockParticles[Kind];
		if (!ISM)
		{
			continue;
		}

		// Broj instanci prati broj cestica; koja instanca crta koju cesticu nije bitno
		while (ISM->GetInstanceCount() > Particles.Num())
		{
			ISM->RemoveInstance(ISM->GetInstanceCount() - 1);
		}
		if (ISM->GetInstanceCount() < Particles.Num())
		{
			TArray<FTransform> NewInstances;
			NewInstances.Init(FTransform::Identity, Particles.Num() - ISM->GetInstanceCount());
			ISM->AddInstances(NewInstances, /*bShouldReturnIndices=*/false, /*bWorldSpace=*/true);
		}
		if (Particles.Num() == 0)
		{
			continue;
		}

		TArray<FTransform> Transforms;
		TArray<float> Custom;
		Transforms.Reserve(Particles.Num());
		Custom.Reserve(Particles.Num() * NumParticleCustomFloats);
		for (const FBlockParticle& Particle : Particles)
		{
			const float Progress = FMath::Clamp((Particle.Age + PartialTick) / FMath::Max(Particle.Lifetime, 1), 0.f, 1.f);

			// FlameParticle.getQuadSize: smanjuje se na pola; BaseAshSmokeParticle: naraste u prvoj 1/32 trajanja.
			// SingleQuadParticle crta kvadrat od -size do +size, pa je stranica dvostruka.
			const float Size = Kind == 0
				? Particle.QuadSize * (1.f - Progress * Progress * 0.5f)
				: Particle.QuadSize * FMath::Clamp(Progress * 32.f, 0.f, 1.f);
			const FVector Position = FMath::Lerp(Particle.PreviousPosition, Particle.Position, (double)PartialTick);
			Transforms.Emplace(CameraRotation, WorldOrigin + Position * BlockSize, FVector(2.f * Size * BlockSize));

			// SpriteSet.get(age, lifetime): slicica = age * (broj - 1) / lifetime
			const int32 Frame = Kind == 0 ? 0 : FMath::Min(Particle.Age * (SmokeFrames - 1) / FMath::Max(Particle.Lifetime, 1), SmokeFrames - 1);
			Custom.Add((float)Frame);
			Custom.Add(Particle.Color.R);
			Custom.Add(Particle.Color.G);
			Custom.Add(Particle.Color.B);
		}
		ISM->BatchUpdateInstancesTransforms(0, Transforms, /*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/true);
		ISM->SetCustomData(0, Particles.Num() - 1, Custom);
	}
}

void AVoxelWorld::TickBlockParticles(float DeltaSeconds)
{
	const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!Player || !bAssetCacheReady)
	{
		return;
	}
	if (CVarVoxelParticles.GetValueOnGameThread() == 0)
	{
		if (BlockParticles[0].Num() + BlockParticles[1].Num() > 0)
		{
			BlockParticles[0].Reset();
			BlockParticles[1].Reset();
			UpdateBlockParticleVisuals(0.f);
		}
		return;
	}
	EnsureParticleComponents();

	// Simulacija ide Minecraftovim tickom; nakon zastoja se ne nadoknaduje vise od 5 tickova
	ParticleTickAccumulator = FMath::Min(ParticleTickAccumulator + DeltaSeconds, 5 * ParticleTickSeconds);
	const FIntVector PlayerGrid = WorldToGrid(Player->GetActorLocation() + FVector(BlockSize * 0.5f));
	while (ParticleTickAccumulator >= ParticleTickSeconds)
	{
		ParticleTickAccumulator -= ParticleTickSeconds;
		StepBlockParticles();
		AnimateTickAround(PlayerGrid);
	}
	UpdateBlockParticleVisuals(ParticleTickAccumulator / ParticleTickSeconds);
}

float AVoxelWorld::RunParticleTest()
{
	const APawn* Player = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	if (!Player)
	{
		UE_LOG(LogTemp, Warning, TEXT("[PARTICLE] test: nema igraca"));
		return 0.f;
	}

	int32 Failures = 0;
	const auto Check = [&Failures](bool bOk, const FString& What)
	{
		UE_LOG(LogTemp, Log, TEXT("[PARTICLE] %s -> %s"), *What, bOk ? TEXT("OK") : TEXT("FAIL"));
		Failures += bOk ? 0 : 1;
	};

	// Poligon visoko nad igracem: kamen, na njemu stojeca baklja, na njegovom boku zidna, nad stojecom strop
	const FIntVector PlayerGrid = WorldToGrid(Player->GetActorLocation());
	const FIntVector Stone(PlayerGrid.X, PlayerGrid.Y, FMath::Min(PlayerGrid.Z + 30, WorldMaxZ - 12));
	const FIntVector Standing = Stone + FIntVector(0, 0, 1);
	const FIntVector Hanging = Stone + FIntVector(1, 0, 0);
	const FIntVector Ceiling = Standing + FIntVector(0, 0, 3);
	PlaceBlockAt(Stone, EBlockType::Stone);
	PlaceBlockAt(Ceiling, EBlockType::Stone);
	const bool bPlaced = PlaceItemAt(Standing, EItemType::Torch, FIntVector(0, 0, 1), FVector(0.f, 0.f, -1.f))
		&& PlaceItemAt(Hanging, EItemType::Torch, FIntVector(1, 0, 0), FVector(-1.f, 0.f, 0.f));
	Check(bPlaced && GetBlockTypeAt(Standing) == EBlockType::Torch && GetBlockTypeAt(Hanging) == EBlockType::WallTorch,
		TEXT("poligon: stojeca i zidna baklja postavljene"));

	EnsureParticleComponents();
	BlockParticles[0].Reset();
	BlockParticles[1].Reset();

	// 1. Mjesto nastanka: stojeca 0.2 bloka iznad sredista celije; zidna jos 0.22 gore i 0.27 prema zidu.
	// Plamen se od tog mjesta pomakne najvise 0.05 po osi, dim nimalo.
	const FVector ExpectedStanding(Standing.X, Standing.Y, Standing.Z + 0.2);
	const FVector ExpectedHanging(Hanging.X - 0.27, Hanging.Y, Hanging.Z + 0.42);
	double WorstFlame = 0.0;
	double WorstSmoke = 0.0;
	for (int32 Round = 0; Round < 200; Round++)
	{
		for (const TPair<FIntVector, FVector>& Case : { TPair<FIntVector, FVector>(Standing, ExpectedStanding), TPair<FIntVector, FVector>(Hanging, ExpectedHanging) })
		{
			BlockParticles[0].Reset();
			BlockParticles[1].Reset();
			AnimateBlock(Case.Key);
			if (BlockParticles[0].Num() == 1 && BlockParticles[1].Num() == 1)
			{
				WorstFlame = FMath::Max(WorstFlame, (BlockParticles[0][0].Position - Case.Value).GetAbsMax());
				WorstSmoke = FMath::Max(WorstSmoke, (BlockParticles[1][0].Position - Case.Value).GetAbsMax());
			}
			else
			{
				WorstFlame = WorstSmoke = 1.0;
			}
		}
	}
	Check(WorstFlame <= 0.05001 && WorstSmoke < 1e-6 && !AnimateBlock(Stone),
		FString::Printf(TEXT("mjesto nastanka: plamen najvise %.3f bloka od ocekivanog (do 0.05), dim %.3f (0); kamen ne ispusta cestice"),
			WorstFlame, WorstSmoke));

	// 2. Trajanje i gibanje: 400 parova cestica stojece baklje, simulacija dok sve ne nestanu
	BlockParticles[0].Reset();
	BlockParticles[1].Reset();
	for (int32 Round = 0; Round < 400; Round++)
	{
		AnimateBlock(Standing);
	}
	int32 MinFlameLife = MAX_int32, MaxFlameLife = 0, MinSmokeLife = MAX_int32, MaxSmokeLife = 0;
	for (const FBlockParticle& Particle : BlockParticles[0])
	{
		MinFlameLife = FMath::Min(MinFlameLife, Particle.Lifetime);
		MaxFlameLife = FMath::Max(MaxFlameLife, Particle.Lifetime);
	}
	for (const FBlockParticle& Particle : BlockParticles[1])
	{
		MinSmokeLife = FMath::Min(MinSmokeLife, Particle.Lifetime);
		MaxSmokeLife = FMath::Max(MaxSmokeLife, Particle.Lifetime);
	}
	double FlameDrift = 0.0;
	double SmokeTop = -1e9;
	int32 Ticks = 0;
	while (BlockParticles[0].Num() + BlockParticles[1].Num() > 0 && Ticks < 200)
	{
		StepBlockParticles();
		Ticks++;
		for (const FBlockParticle& Particle : BlockParticles[0])
		{
			FlameDrift = FMath::Max(FlameDrift, (Particle.Position - ExpectedStanding).Size());
		}
		for (const FBlockParticle& Particle : BlockParticles[1])
		{
			SmokeTop = FMath::Max(SmokeTop, Particle.Position.Z);
		}
	}
	// Plamen 8 / (0.2..1) + 4 = 12..44 ticka, dim 8 / (0.2..1) = 8..40. Dim se dize, ali ne kroz strop
	// (donja ploha stropa je pola bloka ispod njegovog sredista).
	Check(MinFlameLife >= 12 && MaxFlameLife <= 44 && MinSmokeLife >= 8 && MaxSmokeLife <= 40 && Ticks <= 46,
		FString::Printf(TEXT("trajanje: plamen %d..%d tickova (12..44), dim %d..%d (8..40), sve nestalo nakon %d tickova"),
			MinFlameLife, MaxFlameLife, MinSmokeLife, MaxSmokeLife, Ticks));
	Check(FlameDrift < 0.2 && SmokeTop > ExpectedStanding.Z + 0.5 && SmokeTop <= Ceiling.Z - 0.5,
		FString::Printf(TEXT("gibanje: plamen se udalji najvise %.3f bloka (ispod 0.2), dim se digne do %.2f (iznad %.2f, ne iznad stropa %.2f)"),
			FlameDrift, SmokeTop, ExpectedStanding.Z + 0.5, Ceiling.Z - 0.5));

	// 3. Ucestalost: baklja u celiji oko koje se uzorkuje dolazi na red 667 * (1/16^3 + 1/32^3) = 0.183 puta po ticku
	BlockParticles[0].Reset();
	BlockParticles[1].Reset();
	DestroyBlockAt(Hanging, /*bDropItems=*/false);
	constexpr int32 SampleTicks = 2000;
	int32 Animated = 0;
	for (int32 Tick = 0; Tick < SampleTicks; Tick++)
	{
		Animated += AnimateTickAround(Standing);
		BlockParticles[0].Reset();
		BlockParticles[1].Reset();
	}
	const double PerTick = (double)Animated / SampleTicks;
	Check(PerTick > 0.14 && PerTick < 0.23,
		FString::Printf(TEXT("ucestalost: %.3f puta po ticku kroz %d tickova (ocekivano 0.183)"), PerTick, SampleTicks));

	// 4. Vizuali: broj instanci prati broj cestica
	AnimateBlock(Standing);
	UpdateBlockParticleVisuals(0.5f);
	const int32 FlameInstances = ParticleComponents[0] ? ParticleComponents[0]->GetInstanceCount() : -1;
	const int32 SmokeInstances = ParticleComponents[1] ? ParticleComponents[1]->GetInstanceCount() : -1;
	BlockParticles[0].Reset();
	BlockParticles[1].Reset();
	UpdateBlockParticleVisuals(0.f);
	const int32 InstancesAfter = (ParticleComponents[0] ? ParticleComponents[0]->GetInstanceCount() : -1)
		+ (ParticleComponents[1] ? ParticleComponents[1]->GetInstanceCount() : -1);
	Check(FlameInstances == 1 && SmokeInstances == 1 && InstancesAfter == 0
		&& ParticleComponents[0]->GetMaterial(0) && ParticleComponents[0]->GetMaterial(0)->GetName() == TEXT("MI_ParticleFlame")
		&& ParticleComponents[1]->GetMaterial(0) && ParticleComponents[1]->GetMaterial(0)->GetName() == TEXT("MI_ParticleSmoke"),
		FString::Printf(TEXT("vizuali: instanci plamena %d (1), dima %d (1), nakon brisanja %d (0), materijali %s i %s"),
			FlameInstances, SmokeInstances, InstancesAfter,
			ParticleComponents[0] && ParticleComponents[0]->GetMaterial(0) ? *ParticleComponents[0]->GetMaterial(0)->GetName() : TEXT("NEMA"),
			ParticleComponents[1] && ParticleComponents[1]->GetMaterial(0) ? *ParticleComponents[1]->GetMaterial(0)->GetName() : TEXT("NEMA")));

	// Poligon se uklanja da ne ostane u svijetu
	DestroyBlockAt(Standing, /*bDropItems=*/false);
	DestroyBlockAt(Stone, /*bDropItems=*/false);
	DestroyBlockAt(Ceiling, /*bDropItems=*/false);

	UE_LOG(LogTemp, Log, TEXT("[PARTICLE] test GOTOVO: neuspjelih provjera %d -> %s"), Failures, Failures == 0 ? TEXT("OK") : TEXT("FAIL"));
	return 1.0f;
}
