// Pravila blokova po Minecraftu (Docs/PLAN_Torches.md): oslonac, opstanak,
// postavljanje iz predmeta i reakcija na promjenu susjeda, te voxel.torchtest.
// Dio AVoxelWorld-a izdvojen u zasebnu datoteku.

#include "VoxelWorld.h"
#include "Block.h"
#include "BlockMiningComponent.h"
#include "BlockRegistry.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "CraftingRecipeRegistry.h"
#include "Engine/HitResult.h"
#include "EngineUtils.h"
#include "FirstPersonCharacter.h"
#include "HAL/IConsoleManager.h"
#include "ItemDrop.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

using namespace VoxelGrid;

namespace
{
	const FIntVector GBlockDirections[6] = {
		FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
		FIntVector(0, 1, 0), FIntVector(0, -1, 0),
		FIntVector(0, 0, 1), FIntVector(0, 0, -1)
	};

	AVoxelWorld* FindVoxelWorldForRules(UWorld* World)
	{
		for (TActorIterator<AVoxelWorld> It(World); It; ++It)
		{
			return *It;
		}
		return nullptr;
	}
}

/**
 * voxel.torchtest - provjeri baklje ([TORCH] u logu): postavljanje, oslonac,
 * prolaznost, svjetlo i recept. Headless run se nakon testa sam ugasi.
 */
static FAutoConsoleCommandWithWorldAndArgs GVoxelTorchTestCmd(
	TEXT("voxel.torchtest"),
	TEXT("Provjeri postavljanje baklji, njihov oslonac i razine svjetla."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			if (AVoxelWorld* VoxelWorld = World ? FindVoxelWorldForRules(World) : nullptr)
			{
				const float Duration = VoxelWorld->RunTorchTest();
				if (!GIsEditor)
				{
					FTimerHandle QuitHandle;
					World->GetTimerManager().SetTimer(QuitHandle, FTimerDelegate::CreateLambda(
						[]() { FPlatformMisc::RequestExit(false); }), Duration + 2.0f, false);
				}
			}
		}));

FTransform AVoxelWorld::GetBlockInstanceTransform(const FIntVector& Pos, EBlockType Type, uint8 State)
{
	const FVector Location = GridToWorld(Pos.X, Pos.Y, Pos.Z);
	if (bStateTransformByType[(uint8)Type])
	{
		UBlockRegistry* Registry = UBlockRegistry::Get(this);
		if (const FBlockDefinition* Def = Registry ? Registry->GetBlockDefinition(Type) : nullptr)
		{
			return FTransform(FRotator(0.f, Def->GetStateYaw(State), 0.f), Location);
		}
	}
	return FTransform(Location);
}

bool AVoxelWorld::IsFaceSturdy(FIntVector GridPosition, FIntVector Face) const
{
	// Minecraft isFaceSturdy: puna kocka je oslonac sa svih strana; lisce
	// (prazan oblik za oslonac) i blokovi bez kolizije nisu ni s jedne
	const EBlockType Type = GetBlockTypeAt(GridPosition);
	if (Type == EBlockType::Air)
	{
		return false;
	}
	const UBlockRegistry* Registry = UBlockRegistry::Get(this);
	const FBlockDefinition* Def = Registry ? Registry->GetBlockDefinition(Type) : nullptr;
	return Def && Def->bHasCollision && Def->bSturdy;
}

bool AVoxelWorld::CanBlockSurvive(EBlockType Type, uint8 State, FIntVector GridPosition) const
{
	switch (BehaviorByType[(uint8)Type])
	{
	case EBlockBehavior::Torch:
		// BaseTorchBlock.canSurvive: sredina gornje stranice bloka ispod
		return IsFaceSturdy(GridPosition + FIntVector(0, 0, -1), FIntVector(0, 0, 1));

	case EBlockBehavior::WallTorch:
	{
		// WallTorchBlock.canSurvive: stranica bloka iza baklje, okrenuta prema njoj
		const UBlockRegistry* Registry = UBlockRegistry::Get(this);
		const FBlockDefinition* Def = Registry ? Registry->GetBlockDefinition(Type) : nullptr;
		const FIntVector Facing = Def ? Def->GetFacing(State) : FIntVector::ZeroValue;
		return Facing != FIntVector::ZeroValue && IsFaceSturdy(GridPosition - Facing, Facing);
	}

	default:
		return true;
	}
}

void AVoxelWorld::NotifyNeighborsChanged(const FIntVector& Pos)
{
	for (const FIntVector& Direction : GBlockDirections)
	{
		const FIntVector Neighbor = Pos + Direction;
		const EBlockType Type = GetBlockTypeAt(Neighbor);
		if (Type != EBlockType::Air && BehaviorByType[(uint8)Type] != EBlockBehavior::None
			&& !CanBlockSurvive(Type, GetBlockStateAt(Neighbor), Neighbor))
		{
			DestroyBlockAt(Neighbor, /*bDropItems=*/true);
		}
	}
}

bool AVoxelWorld::PlaceItemAt(FIntVector GridPosition, EItemType Item, FIntVector ClickedFace, FVector LookDirection)
{
	const UBlockRegistry* Registry = UBlockRegistry::Get(this);
	const FBlockDefinition* BlockDef = Registry ? Registry->GetBlockForItem(Item) : nullptr;
	if (!BlockDef)
	{
		return false;
	}

	const FItemDefinition* ItemDef = Registry->GetItemDefinition(Item);
	const EBlockType WallType = ItemDef ? ItemDef->WallBlock : EBlockType::Air;
	const FBlockDefinition* WallDef = WallType != EBlockType::Air ? Registry->GetBlockDefinition(WallType) : nullptr;
	if (!WallDef)
	{
		return PlaceBlockAt(GridPosition, BlockDef->BlockType, 0);
	}

	// BlockPlaceContext.getNearestLookingDirections: 6 smjerova od najblizeg
	// pogledu do najdaljeg, pa smjer suprotan kliknutoj stranici na prvo mjesto
	TArray<FIntVector> Directions(GBlockDirections, 6);
	const FVector Look = LookDirection.GetSafeNormal();
	Directions.Sort([&Look](const FIntVector& A, const FIntVector& B)
	{
		return (FVector(A) | Look) > (FVector(B) | Look);
	});
	const FIntVector Behind = ClickedFace * -1;
	if (ClickedFace != FIntVector::ZeroValue && Directions.Remove(Behind) > 0)
	{
		Directions.Insert(Behind, 0);
	}

	// WallTorchBlock.getStateForPlacement: prvi vodoravni smjer s popisa u
	// kojem je cvrst zid; baklja gleda od zida
	bool bWallStateFound = false;
	uint8 WallState = 0;
	for (const FIntVector& Direction : Directions)
	{
		if (Direction.Z != 0)
		{
			continue;
		}
		const uint8 Candidate = WallDef->WithStatePropertyValue(0, TEXT("facing"), BlockFacing::NameFromDirection(Direction * -1));
		if (CanBlockSurvive(WallType, Candidate, GridPosition))
		{
			bWallStateFound = true;
			WallState = Candidate;
			break;
		}
	}

	// StandingAndWallBlockItem.getPlacementState: prolazi istim popisom, preskace
	// smjer prema gore (baklja ne visi sa stropa), za smjer prema dolje nudi
	// stojeci blok, za ostale zidni; uzima prvi koji moze opstati
	for (const FIntVector& Direction : Directions)
	{
		if (Direction == FIntVector(0, 0, 1))
		{
			continue;
		}
		if (Direction == FIntVector(0, 0, -1))
		{
			if (CanBlockSurvive(BlockDef->BlockType, 0, GridPosition))
			{
				return PlaceBlockAt(GridPosition, BlockDef->BlockType, 0);
			}
		}
		else if (bWallStateFound)
		{
			return PlaceBlockAt(GridPosition, WallType, WallState);
		}
	}
	return false;
}

float AVoxelWorld::RunTorchTest()
{
	AFirstPersonCharacter* Player = Cast<AFirstPersonCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0));
	UBlockRegistry* Registry = UBlockRegistry::Get(this);
	if (!Player || !Registry)
	{
		UE_LOG(LogTemp, Warning, TEXT("[TORCH] test: nema igraca ili registryja"));
		return 0.f;
	}

	int32 Failures = 0;
	const auto Check = [&Failures](bool bOk, const FString& What)
	{
		UE_LOG(LogTemp, Log, TEXT("[TORCH] %s -> %s"), *What, bOk ? TEXT("OK") : TEXT("FAIL"));
		Failures += bOk ? 0 : 1;
	};
	const auto CountTorchDrops = [this]()
	{
		int32 Count = 0;
		for (TActorIterator<AItemDrop> It(GetWorld()); It; ++It)
		{
			Count += It->ItemType == EItemType::Torch ? 1 : 0;
		}
		return Count;
	};
	const UEnum* BlockEnum = StaticEnum<EBlockType>();
	const auto Describe = [this, BlockEnum, Registry](const FIntVector& Pos)
	{
		const EBlockType Type = GetBlockTypeAt(Pos);
		const FBlockDefinition* Def = Registry->GetBlockDefinition(Type);
		const FString Facing = Def ? Def->GetStatePropertyValue(GetBlockStateAt(Pos), TEXT("facing")) : FString();
		return BlockEnum->GetNameStringByValue((int64)Type) + (Facing.IsEmpty() ? FString() : TEXT(" facing=") + Facing);
	};

	// 0. Izlozba kod spawna: stojeca baklja na tlu i stup s cetiri zidne baklje, svaka s osloncem
	{
		int32 Standing = 0;
		int32 Wall = 0;
		int32 Unsupported = 0;
		TSet<FString> Facings;
		const FIntVector Origin = ChunkOrigin(FIntPoint::ZeroValue);
		if (const FVoxelChunk* Showcase = FindChunk(FIntPoint::ZeroValue))
		{
			for (int32 Z = WorldMinZ; Z <= WorldMaxZ; Z++)
			{
				for (int32 LocalY = 0; LocalY < ChunkSize; LocalY++)
				{
					for (int32 LocalX = 0; LocalX < ChunkSize; LocalX++)
					{
						const FIntVector Local(LocalX, LocalY, Z);
						const EBlockType Type = Showcase->GetBlock(Local);
						if (Type != EBlockType::Torch && Type != EBlockType::WallTorch)
						{
							continue;
						}
						Standing += Type == EBlockType::Torch ? 1 : 0;
						Wall += Type == EBlockType::WallTorch ? 1 : 0;
						Unsupported += CanBlockSurvive(Type, Showcase->GetState(Local), Origin + Local) ? 0 : 1;
						if (Type == EBlockType::WallTorch)
						{
							Facings.Add(Registry->GetBlockDefinition(Type)->GetStatePropertyValue(Showcase->GetState(Local), TEXT("facing")));
						}
					}
				}
			}
		}
		Check(Standing == 1 && Wall == 4 && Facings.Num() == 4 && Unsupported == 0,
			FString::Printf(TEXT("izlozba: stojecih baklji %d (1), zidnih %d (4), razlicitih smjerova %d (4), bez oslonca %d (0)"),
				Standing, Wall, Facings.Num(), Unsupported));
	}

	// 0a. Tint u custom data: materijal gornju stranicu mnozi tintom, pa instanca
	// s tintom nula ima crnu gornju plohu (blokovi bez tinta moraju imati bijelo)
	{
		int32 Instances = 0;
		int32 BlackTint = 0;
		for (const TPair<FIntPoint, TUniquePtr<FVoxelChunk>>& ChunkPair : Chunks)
		{
			for (const TPair<EBlockType, FChunkInstanceSet>& SetPair : ChunkPair.Value->InstanceSets)
			{
				const UInstancedStaticMeshComponent* Component = SetPair.Value.Component;
				if (!Component || Component->NumCustomDataFloats != NumBlockCustomFloats)
				{
					continue;
				}
				for (int32 Index = 0; Index < SetPair.Value.InstanceToGrid.Num(); Index++)
				{
					const float* Tint = &Component->PerInstanceSMCustomData[Index * NumBlockCustomFloats];
					Instances++;
					BlackTint += Tint[0] <= 0.f && Tint[1] <= 0.f && Tint[2] <= 0.f ? 1 : 0;
				}
			}
		}
		Check(Instances > 0 && BlackTint == 0,
			FString::Printf(TEXT("tint instanci: pregledano %d, s crnim tintom %d (0)"), Instances, BlackTint));
	}

	// Poligon visoko u zraku iznad igraca, da teren i svjetlo s tla ne utjecu: pod 5x5 od kamena
	const FIntVector PlayerGrid = WorldToGrid(Player->GetActorLocation());
	const FIntVector Base(PlayerGrid.X, PlayerGrid.Y, FMath::Min(PlayerGrid.Z + 30, WorldMaxZ - 12));
	TArray<FIntVector> Built;
	const auto Build = [this, &Built](const FIntVector& Pos, EBlockType Type)
	{
		if (PlaceBlockAt(Pos, Type))
		{
			Built.Add(Pos);
		}
	};
	for (int32 DY = -2; DY <= 2; DY++)
	{
		for (int32 DX = -2; DX <= 2; DX++)
		{
			Build(Base + FIntVector(DX, DY, 0), EBlockType::Stone);
		}
	}
	Check(Built.Num() == 25, FString::Printf(TEXT("poligon: pod 5x5 na (%d,%d,%d), postavljeno %d blokova"), Base.X, Base.Y, Base.Z, Built.Num()));

	// Neovisna provjera svjetla: razine u kocki oko poligona izracunate iznova,
	// sirenjem iz svih izvora u njoj, moraju biti jednake spremljenima
	const auto VerifyLight = [this, &Check, Base](const TCHAR* When)
	{
		constexpr int32 Reach = 20;
		constexpr int32 Side = 2 * Reach + 1;
		TArray<uint8> Expected;
		Expected.SetNumZeroed(Side * Side * Side);
		const auto IndexOf = [Base](const FIntVector& Cell)
		{
			const FIntVector Rel = Cell - Base + FIntVector(Reach);
			return Rel.X < 0 || Rel.Y < 0 || Rel.Z < 0 || Rel.X >= Side || Rel.Y >= Side || Rel.Z >= Side
				? INDEX_NONE : (Rel.Z * Side + Rel.Y) * Side + Rel.X;
		};
		TArray<FIntVector> Queue;
		for (int32 DZ = -Reach; DZ <= Reach; DZ++)
		{
			for (int32 DY = -Reach; DY <= Reach; DY++)
			{
				for (int32 DX = -Reach; DX <= Reach; DX++)
				{
					const FIntVector Cell = Base + FIntVector(DX, DY, DZ);
					if (const uint8 Emission = EmissionByType[(uint8)GetBlockTypeAt(Cell)])
					{
						Expected[IndexOf(Cell)] = Emission;
						Queue.Add(Cell);
					}
				}
			}
		}
		for (int32 Head = 0; Head < Queue.Num(); Head++)
		{
			const FIntVector Cell = Queue[Head];
			const int32 Level = Expected[IndexOf(Cell)];
			for (const FIntVector& Direction : GBlockDirections)
			{
				const FIntVector Next = Cell + Direction;
				const int32 Index = IndexOf(Next);
				if (Index != INDEX_NONE && IsValidZ(Next.Z) && !bBlocksLightByType[(uint8)GetBlockTypeAt(Next)] && Expected[Index] < Level - 1)
				{
					Expected[Index] = (uint8)(Level - 1);
					Queue.Add(Next);
				}
			}
		}

		int32 Mismatches = 0;
		FString First;
		for (int32 DZ = -Reach; DZ <= Reach; DZ++)
		{
			for (int32 DY = -Reach; DY <= Reach; DY++)
			{
				for (int32 DX = -Reach; DX <= Reach; DX++)
				{
					const FIntVector Cell = Base + FIntVector(DX, DY, DZ);
					const int32 Actual = GetBlockLightAt(Cell);
					if (Actual != Expected[IndexOf(Cell)] && Mismatches++ == 0)
					{
						First = FString::Printf(TEXT(", prva na (%d,%d,%d): spremljeno %d, izracunato %d"),
							Cell.X, Cell.Y, Cell.Z, Actual, Expected[IndexOf(Cell)]);
					}
				}
			}
		}
		Check(Mismatches == 0, FString::Printf(TEXT("svjetlo %s: razlika prema neovisnom izracunu %d (0)%s"), When, Mismatches, *First));
	};

	// 1. Klik na gornju stranicu poda -> stojeca baklja
	const FIntVector FloorTorch = Base + FIntVector(0, 0, 1);
	const bool bFloorPlaced = PlaceItemAt(FloorTorch, EItemType::Torch, FIntVector(0, 0, 1), FVector(0.6f, 0.f, -0.8f));
	Check(bFloorPlaced && GetBlockTypeAt(FloorTorch) == EBlockType::Torch,
		FString::Printf(TEXT("klik na vrh bloka: %s (ocekivano Torch)"), *Describe(FloorTorch)));

	// 2. Svjetlo: 14 u celiji baklje, 13 pokraj nje, 0 u kamenu ispod. Ispod
	// sredine poda svjetlo mora obici rub: 3 u stranu, 2 dolje, 3 natrag = 14 - 8
	FlushLightDirty();
	const int32 LightAt = GetBlockLightAt(FloorTorch);
	const int32 LightBeside = GetBlockLightAt(FloorTorch + FIntVector(1, 0, 0));
	const int32 LightInStone = GetBlockLightAt(Base);
	const int32 LightUnder = GetBlockLightAt(Base + FIntVector(0, 0, -1));
	Check(LightAt == 14 && LightBeside == 13 && LightInStone == 0 && LightUnder == 6,
		FString::Printf(TEXT("svjetlo: baklja %d (14), pokraj %d (13), u kamenu %d (0), ispod poda %d (6)"),
			LightAt, LightBeside, LightInStone, LightUnder));

	// 3. Vizuali: baklja i blok ispod nje imaju instancu; gornja stranica bloka nosi svjetlinu razine 14
	float TopFaceLight = -1.f;
	if (const FVoxelChunk* Chunk = FindChunk(GridToChunk(Base)))
	{
		const FChunkInstanceSet* Set = Chunk->InstanceSets.Find(EBlockType::Stone);
		const int32* Index = Set && Set->Component ? Set->GridToInstance.Find(Base) : nullptr;
		if (Index && Set->Component->PerInstanceSMCustomData.IsValidIndex(*Index * NumBlockCustomFloats + 5))
		{
			TopFaceLight = Set->Component->PerInstanceSMCustomData[*Index * NumBlockCustomFloats + 5];
		}
	}
	Check(HasBlockInstance(FloorTorch) && HasBlockInstance(Base) && FMath::IsNearlyEqual(TopFaceLight, 1.1667f, 0.001f),
		FString::Printf(TEXT("vizuali: baklja %s, blok ispod %s, svjetlina njegove gornje stranice %.4f (1.1667)"),
			HasBlockInstance(FloorTorch) ? TEXT("ima instancu") : TEXT("NEMA instancu"),
			HasBlockInstance(Base) ? TEXT("ima instancu") : TEXT("NEMA instancu"), TopFaceLight));

	// 4. Prolaznost: zraka igraca (ECC_Pawn) prolazi kroz baklju, zraka nisana (ECC_Visibility) je pogada
	const FVector TorchCenter = GridToWorld(FloorTorch.X, FloorTorch.Y, FloorTorch.Z) + FVector(0.f, 0.f, -20.f);
	const FVector TraceStart = TorchCenter + FVector(-150.f, 0.f, 0.f);
	const FVector TraceEnd = TorchCenter + FVector(150.f, 0.f, 0.f);
	FHitResult PawnHit;
	FHitResult SightHit;
	FCollisionQueryParams TraceParams;
	TraceParams.AddIgnoredActor(Player);
	const bool bPawnBlocked = GetWorld()->LineTraceSingleByChannel(PawnHit, TraceStart, TraceEnd, ECC_Pawn, TraceParams);
	const bool bSightBlocked = GetWorld()->LineTraceSingleByChannel(SightHit, TraceStart, TraceEnd, ECC_Visibility, TraceParams);
	FIntVector SightPos = FIntVector::ZeroValue;
	EBlockType SightType = EBlockType::Air;
	const bool bSightIsTorch = bSightBlocked && ResolveHitToGrid(SightHit, SightPos, SightType) && SightPos == FloorTorch;
	Check(!bPawnBlocked && bSightIsTorch,
		FString::Printf(TEXT("prolaznost: zraka igraca %s, zraka nisana %s"),
			bPawnBlocked ? TEXT("ZAUSTAVLJENA") : TEXT("prolazi"), bSightIsTorch ? TEXT("pogada baklju") : TEXT("NE pogada baklju")));

	// 5. Obris i trenutno razbijanje
	const FBox Outline = GetBlockOutlineBox(EBlockType::Torch, 0);
	const UBlockMiningComponent* Mining = Player->FindComponentByClass<UBlockMiningComponent>();
	const float BreakTime = Mining ? Mining->GetBreakTime(FloorTorch, EItemType::None) : -2.f;
	Check(Outline.GetSize().Equals(FVector(25.f, 25.f, 62.5f), 0.01f) && BreakTime == 0.f,
		FString::Printf(TEXT("obris %.1f x %.1f x %.1f (25 x 25 x 62.5), vrijeme kopanja %.2f s (0)"),
			Outline.GetSize().X, Outline.GetSize().Y, Outline.GetSize().Z, BreakTime));

	// 6. Zid istocno od sredine: klik na njegovu zapadnu stranicu -> zidna baklja koja gleda na zapad
	const FIntVector Wall = Base + FIntVector(2, 1, 1);
	Build(Wall, EBlockType::Stone);
	const FIntVector WallTorchPos = Wall + FIntVector(-1, 0, 0);
	const bool bWallPlaced = PlaceItemAt(WallTorchPos, EItemType::Torch, FIntVector(-1, 0, 0), FVector(0.9f, 0.f, -0.1f));
	const FBox WallOutline = GetBlockOutlineBox(EBlockType::WallTorch, GetBlockStateAt(WallTorchPos));
	Check(bWallPlaced && Describe(WallTorchPos) == TEXT("WallTorch facing=west")
		&& FMath::IsNearlyEqual(WallOutline.Max.X, 50.f, 0.01f) && FMath::IsNearlyEqual(WallOutline.Min.X, 18.75f, 0.01f),
		FString::Printf(TEXT("klik na bok bloka: %s (ocekivano WallTorch facing=west), obris X %.2f..%.2f (18.75..50)"),
			*Describe(WallTorchPos), WallOutline.Min.X, WallOutline.Max.X));

	VerifyLight(TEXT("s dvije baklje"));

	// 6a. Fokus: baklja postaje actor zakrenut prema stanju, pa se vraca u instancu
	ABlock* Promoted = PromoteToActor(WallTorchPos);
	const float PromotedYaw = Promoted ? FRotator::ClampAxis(Promoted->GetActorRotation().Yaw) : -1.f;
	const bool bInstanceWhilePromoted = HasBlockInstance(WallTorchPos);
	DemoteToInstance(WallTorchPos);
	Check(Promoted && FMath::IsNearlyEqual(PromotedYaw, 180.f, 0.1f) && !bInstanceWhilePromoted && HasBlockInstance(WallTorchPos),
		FString::Printf(TEXT("fokus: actor %s, zakret %.0f (180), nakon fokusa %s"),
			Promoted ? TEXT("stvoren") : TEXT("NIJE stvoren"), PromotedYaw,
			HasBlockInstance(WallTorchPos) ? TEXT("opet instanca") : TEXT("NEMA instance")));

	// 6b. Unload i ponovno ucitavanje chunka: stanje bloka, svjetlo i vizuali se vracaju
	const FIntPoint TorchChunk = GridToChunk(WallTorchPos);
	UnloadChunk(TorchChunk);
	const bool bGoneWhileUnloaded = GetBlockTypeAt(WallTorchPos) == EBlockType::Air;
	ApplyChunkVisuals(LoadChunkData(TorchChunk));
	FlushLightDirty();
	Check(bGoneWhileUnloaded && Describe(WallTorchPos) == TEXT("WallTorch facing=west") && GetBlockTypeAt(FloorTorch) == EBlockType::Torch
		&& GetBlockLightAt(FloorTorch) == 14 && GetBlockLightAt(WallTorchPos) == 14 && HasBlockInstance(WallTorchPos) && HasBlockInstance(FloorTorch),
		FString::Printf(TEXT("ponovno ucitan chunk (%d,%d): %s, svjetlo baklji %d i %d (14 i 14), instance %s"),
			TorchChunk.X, TorchChunk.Y, *Describe(WallTorchPos), GetBlockLightAt(FloorTorch), GetBlockLightAt(WallTorchPos),
			HasBlockInstance(WallTorchPos) && HasBlockInstance(FloorTorch) ? TEXT("postoje") : TEXT("NEDOSTAJU")));

	VerifyLight(TEXT("nakon ponovnog ucitavanja chunka"));

	// 7. Klik na strop bez zida i poda u blizini -> nista
	const FIntVector Ceiling = Base + FIntVector(-1, -1, 4);
	Build(Ceiling, EBlockType::Stone);
	const FIntVector UnderCeiling = Ceiling + FIntVector(0, 0, -1);
	const bool bCeilingPlaced = PlaceItemAt(UnderCeiling, EItemType::Torch, FIntVector(0, 0, -1), FVector(0.f, 0.f, 1.f));
	Check(!bCeilingPlaced && GetBlockTypeAt(UnderCeiling) == EBlockType::Air,
		FString::Printf(TEXT("klik na strop bez oslonca: %s (ocekivano odbijeno)"), bCeilingPlaced ? TEXT("POSTAVLJENO") : TEXT("odbijeno")));

	// 8. Lisce nije oslonac
	const FIntVector Leaves = Base + FIntVector(-2, 2, 1);
	Build(Leaves, EBlockType::OakLeaves);
	const bool bOnLeaves = PlaceItemAt(Leaves + FIntVector(0, 0, 1), EItemType::Torch, FIntVector(0, 0, 1), FVector(0.f, 0.f, -1.f));
	Check(!bOnLeaves, FString::Printf(TEXT("baklja na liscu: %s (ocekivano odbijeno)"), bOnLeaves ? TEXT("POSTAVLJENA") : TEXT("odbijeno")));

	// 9. Bez oslonca baklja otpada i ispusta jedan predmet; svjetlo nestaje
	const int32 DropsBefore = CountTorchDrops();
	DestroyBlockAt(Wall, /*bDropItems=*/false);
	Built.Remove(Wall);
	const int32 DropsAfterWall = CountTorchDrops();
	DestroyBlockAt(Base, /*bDropItems=*/false);
	Built.Remove(Base);
	const int32 DropsAfterFloor = CountTorchDrops();
	FlushLightDirty();
	Check(GetBlockTypeAt(WallTorchPos) == EBlockType::Air && GetBlockTypeAt(FloorTorch) == EBlockType::Air
		&& DropsAfterWall - DropsBefore == 1 && DropsAfterFloor - DropsAfterWall == 1,
		FString::Printf(TEXT("oslonac: zidna baklja %s, stojeca %s, ispusteno predmeta %d i %d (1 i 1)"),
			GetBlockTypeAt(WallTorchPos) == EBlockType::Air ? TEXT("otpala") : TEXT("OSTALA"),
			GetBlockTypeAt(FloorTorch) == EBlockType::Air ? TEXT("otpala") : TEXT("OSTALA"),
			DropsAfterWall - DropsBefore, DropsAfterFloor - DropsAfterWall));

	VerifyLight(TEXT("nakon uklanjanja baklji"));

	Check(GetBlockLightAt(FloorTorch) == 0 && GetBlockLightAt(WallTorchPos) == 0,
		FString::Printf(TEXT("svjetlo na mjestu uklonjenih baklji: %d i %d (0 i 0)"), GetBlockLightAt(FloorTorch), GetBlockLightAt(WallTorchPos)));

	// 10. Recept: ugljen iznad stapa daje 4 baklje
	const UCraftingRecipeRegistry* Recipes = UCraftingRecipeRegistry::Get(this);
	const TArray<EItemType> Grid = { EItemType::Coal, EItemType::None, EItemType::Stick, EItemType::None };
	const FCraftingRecipe* Recipe = Recipes ? Recipes->FindMatchingRecipe(Grid, 2) : nullptr;
	Check(Recipe && Recipe->Result.ItemType == EItemType::Torch && Recipe->Result.Count == 4,
		FString::Printf(TEXT("recept ugljen iznad stapa: %s"),
			Recipe ? *FString::Printf(TEXT("%s x%d (Torch x4)"),
				*StaticEnum<EItemType>()->GetNameStringByValue((int64)Recipe->Result.ItemType), Recipe->Result.Count) : TEXT("NEMA recepta")));

	// Poligon se uklanja da ne ostane u svijetu
	for (const FIntVector& Pos : Built)
	{
		DestroyBlockAt(Pos, /*bDropItems=*/false);
	}

	UE_LOG(LogTemp, Log, TEXT("[TORCH] test GOTOVO: neuspjelih provjera %d -> %s"), Failures, Failures == 0 ? TEXT("OK") : TEXT("FAIL"));
	return 1.0f;
}
