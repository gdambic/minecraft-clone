// Svjetlo blokova po Minecraftu (Docs/PLAN_Torches.md): razina 0..15 po celiji,
// sirenje iz izvora s padom od jedne razine po bloku, bez prolaska kroz pune
// blokove. Dio AVoxelWorld-a izdvojen u zasebnu datoteku.

#include "VoxelWorld.h"
#include "Block.h"
#include "BlockLightReceiverComponent.h"
#include "BlockRegistry.h"
#include "Components/MeshComponent.h"
#include "EngineUtils.h"
#include "FirstPersonCharacter.h"
#include "ItemDrop.h"
#include "Kismet/GameplayStatics.h"
#include "MobBase.h"
#include "TimerManager.h"
#include "Zombie.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialParameterCollection.h"

using namespace VoxelGrid;

namespace
{
	/** Red odgovara custom data floatovima 3-8: +X, +Y, +Z, -X, -Y, -Z */
	const FIntVector GLightDirections[6] = {
		FIntVector(1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, 0, 1),
		FIntVector(-1, 0, 0), FIntVector(0, -1, 0), FIntVector(0, 0, -1)
	};

	/**
	 * Razina svjetla -> svjetlina, kao Minecraftov lightmap.fsh: get_brightness
	 * (level / (4 - 3 * level)) puta BlockFactor 1.5 iz LightTexture. Boju
	 * (toplo zuto pri srednjim razinama) racuna materijal iz ove vrijednosti.
	 */
	float LightLevelToBrightness(int32 Level)
	{
		const float Normalized = FMath::Clamp(Level, 0, 15) / 15.f;
		return 1.5f * Normalized / (4.f - 3.f * Normalized);
	}

	TAutoConsoleVariable<float> CVarVoxelLightIntensity(
		TEXT("voxel.light.intensity"), 1.0f,
		TEXT("Jacina sjaja kojim svjetlo blokova (baklje) osvjetljava stranice blokova."));
}

/**
 * voxel.entitylighttest - provjeri svjetlo baklje na stvorenjima i dropovima
 * ([ENTITYLIGHT] u logu). Headless run se nakon testa sam ugasi.
 */
static FAutoConsoleCommandWithWorldAndArgs GVoxelEntityLightTestCmd(
	TEXT("voxel.entitylighttest"),
	TEXT("Provjeri da stvorenja, dropovi i igrac dobivaju svjetlinu mjesta na kojem su."),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args, UWorld* World)
		{
			for (TActorIterator<AVoxelWorld> It(World); It; ++It)
			{
				const float Duration = It->RunEntityLightTest();
				if (!GIsEditor)
				{
					FTimerHandle QuitHandle;
					World->GetTimerManager().SetTimer(QuitHandle, FTimerDelegate::CreateLambda(
						[]() { FPlatformMisc::RequestExit(false); }), Duration + 2.0f, false);
				}
				break;
			}
		}));

float AVoxelWorld::GetBlockLightBrightnessAt(const FVector& WorldLocation) const
{
	// Celija koja sadrzi tocku: sredista celija su na cijelim visekratnicima BlockSize
	const FVector Rel = (WorldLocation - GetActorLocation()) / BlockSize;
	const FIntVector Cell(FMath::FloorToInt(Rel.X + 0.5), FMath::FloorToInt(Rel.Y + 0.5), FMath::FloorToInt(Rel.Z + 0.5));
	return LightLevelToBrightness(GetBlockLightAt(Cell));
}

int32 AVoxelWorld::GetBlockLightAt(FIntVector GridPosition) const
{
	const FVoxelChunk* Chunk = IsValidZ(GridPosition.Z) ? FindChunk(GridToChunk(GridPosition)) : nullptr;
	if (!Chunk || Chunk->State == EChunkState::Generating)
	{
		return 0;
	}
	return Chunk->GetLight(GridToLocal(GridPosition));
}

void AVoxelWorld::SetBlockLightAt(const FIntVector& Pos, uint8 Level)
{
	FVoxelChunk* Chunk = IsValidZ(Pos.Z) ? FindChunk(GridToChunk(Pos)) : nullptr;
	if (!Chunk || Chunk->State == EChunkState::Generating)
	{
		return;
	}
	Chunk->SetLight(GridToLocal(Pos), Level);
	DirtyLightCells.Add(Pos);
}

void AVoxelWorld::PropagateLight(TArray<FIntVector>& Queue)
{
	// Niz sluzi kao red: nove celije se dodaju na kraj dok Head ide naprijed
	for (int32 Head = 0; Head < Queue.Num(); Head++)
	{
		const FIntVector Cell = Queue[Head];
		const int32 Level = GetBlockLightAt(Cell);
		if (Level <= 1)
		{
			continue;
		}

		for (const FIntVector& Direction : GLightDirections)
		{
			const FIntVector Neighbor = Cell + Direction;
			FVoxelChunk* Chunk = IsValidZ(Neighbor.Z) ? FindChunk(GridToChunk(Neighbor)) : nullptr;
			if (!Chunk || Chunk->State == EChunkState::Generating)
			{
				continue;
			}
			const FIntVector Local = GridToLocal(Neighbor);
			if (bBlocksLightByType[(uint8)Chunk->GetBlock(Local)] || Chunk->GetLight(Local) >= Level - 1)
			{
				continue;
			}
			Chunk->SetLight(Local, (uint8)(Level - 1));
			DirtyLightCells.Add(Neighbor);
			Queue.Add(Neighbor);
		}
	}
}

void AVoxelWorld::RemoveLight(const FIntVector& Pos, TArray<FIntVector>& OutRelight)
{
	const int32 StartLevel = GetBlockLightAt(Pos);
	if (StartLevel == 0)
	{
		return;
	}

	TArray<TPair<FIntVector, int32>> Queue;
	TArray<FIntVector> Emitters;
	SetBlockLightAt(Pos, 0);
	Queue.Emplace(Pos, StartLevel);

	for (int32 Head = 0; Head < Queue.Num(); Head++)
	{
		const FIntVector Cell = Queue[Head].Key;
		const int32 Level = Queue[Head].Value;
		for (const FIntVector& Direction : GLightDirections)
		{
			const FIntVector Neighbor = Cell + Direction;
			const int32 NeighborLevel = GetBlockLightAt(Neighbor);
			if (NeighborLevel == 0)
			{
				continue;
			}
			if (NeighborLevel < Level)
			{
				// Slabije svjetlo je moglo doci iz celije koja se gasi
				SetBlockLightAt(Neighbor, 0);
				Queue.Emplace(Neighbor, NeighborLevel);
				if (EmissionByType[(uint8)GetBlockTypeAt(Neighbor)] > 0)
				{
					Emitters.Add(Neighbor);
				}
			}
			else
			{
				// Jednako ili jace svjetlo ima drugi izvor - iz njega se praznina ponovno puni
				OutRelight.Add(Neighbor);
			}
		}
	}

	// Izvor koji je usput ugasen opet svijetli svojom razinom
	for (const FIntVector& Emitter : Emitters)
	{
		SetBlockLightAt(Emitter, EmissionByType[(uint8)GetBlockTypeAt(Emitter)]);
		OutRelight.Add(Emitter);
	}
}

void AVoxelWorld::UpdateLightForBlockChange(const FIntVector& Pos, EBlockType NewType)
{
	TArray<FIntVector> Relight;
	RemoveLight(Pos, Relight);

	const uint8 Emission = EmissionByType[(uint8)NewType];
	if (Emission > 0)
	{
		SetBlockLightAt(Pos, Emission);
		Relight.Add(Pos);
	}

	// Celija koja propusta svjetlo puni se iz osvijetljenih susjeda
	if (!bBlocksLightByType[(uint8)NewType])
	{
		for (const FIntVector& Direction : GLightDirections)
		{
			if (GetBlockLightAt(Pos + Direction) > 1)
			{
				Relight.Add(Pos + Direction);
			}
		}
	}

	if (Relight.Num() > 0)
	{
		PropagateLight(Relight);
	}
}

void AVoxelWorld::InitChunkLight(FVoxelChunk& Chunk)
{
	TArray<FIntVector> Queue;
	const FIntVector Origin = ChunkOrigin(Chunk.Coord);

	// Izvori svjetla u chunku: baklje koje je postavio igrac (sacuvani chunk) i
	// blokovi koje stvori generator (izlozba blokova kod spawna)
	for (int32 Z = WorldMinZ; Z <= WorldMaxZ; Z++)
	{
		for (int32 LocalY = 0; LocalY < ChunkSize; LocalY++)
		{
			for (int32 LocalX = 0; LocalX < ChunkSize; LocalX++)
			{
				const FIntVector Local(LocalX, LocalY, Z);
				const uint8 Emission = EmissionByType[(uint8)Chunk.Blocks[LocalIndex(Local)]];
				if (Emission > 0)
				{
					Chunk.SetLight(Local, Emission);
					DirtyLightCells.Add(Origin + Local);
					Queue.Add(Origin + Local);
				}
			}
		}
	}

	// Svjetlo s ruba susjednih chunkova ulazi u ovaj
	static const FIntPoint Sides[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
	for (const FIntPoint& Side : Sides)
	{
		const FVoxelChunk* Neighbor = FindChunk(Chunk.Coord + Side);
		if (!Neighbor || Neighbor->State == EChunkState::Generating || Neighbor->BlockLight.Num() == 0)
		{
			continue;
		}
		const FIntVector NeighborOrigin = ChunkOrigin(Neighbor->Coord);
		for (int32 Z = WorldMinZ; Z <= WorldMaxZ; Z++)
		{
			for (int32 Along = 0; Along < ChunkSize; Along++)
			{
				// Ravnina susjeda koja dodiruje ovaj chunk
				const FIntVector Local(
					Side.X == 1 ? 0 : Side.X == -1 ? ChunkSize - 1 : Along,
					Side.Y == 1 ? 0 : Side.Y == -1 ? ChunkSize - 1 : Along,
					Z);
				if (Neighbor->BlockLight[LocalIndex(Local)] > 1)
				{
					Queue.Add(NeighborOrigin + Local);
				}
			}
		}
	}

	if (Queue.Num() > 0)
	{
		PropagateLight(Queue);
	}
}

bool AVoxelWorld::ChunkMayHaveLight(const FVoxelChunk& Chunk) const
{
	if (Chunk.BlockLight.Num() > 0)
	{
		return true;
	}
	static const FIntPoint Sides[4] = { FIntPoint(1, 0), FIntPoint(-1, 0), FIntPoint(0, 1), FIntPoint(0, -1) };
	for (const FIntPoint& Side : Sides)
	{
		const FVoxelChunk* Neighbor = FindChunk(Chunk.Coord + Side);
		if (Neighbor && Neighbor->BlockLight.Num() > 0)
		{
			return true;
		}
	}
	return false;
}

void AVoxelWorld::ComputeFaceLights(const FIntVector& Pos, EBlockType Type, float* OutSix) const
{
	// Blok koji nije puna kocka (baklja) osvijetljen je razinom vlastite celije
	if (!IsFullCube(Type))
	{
		const float Own = LightLevelToBrightness(GetBlockLightAt(Pos));
		for (int32 Face = 0; Face < 6; Face++)
		{
			OutSix[Face] = Own;
		}
		return;
	}

	// Stranica pune kocke je svijetla koliko i celija ispred nje
	for (int32 Face = 0; Face < 6; Face++)
	{
		OutSix[Face] = LightLevelToBrightness(GetBlockLightAt(Pos + GLightDirections[Face]));
	}
}

void AVoxelWorld::RefreshBlockLightVisual(const FIntVector& Pos)
{
	const EBlockType Type = GetBlockTypeAt(Pos);
	if (Type == EBlockType::Air)
	{
		return;
	}

	float Lights[6];

	// Promovirani actor: svjetlo ide kroz parametre njegove instance materijala
	if (ABlock* Actor = Blocks.FindRef(Pos))
	{
		UMaterialInstanceDynamic* MID = Actor->MeshComponent
			? Cast<UMaterialInstanceDynamic>(Actor->MeshComponent->GetMaterial(0)) : nullptr;
		if (MID)
		{
			ComputeFaceLights(Pos, Type, Lights);
			MID->SetVectorParameterValue(TEXT("LightFallbackPos"), FLinearColor(Lights[0], Lights[1], Lights[2]));
			MID->SetVectorParameterValue(TEXT("LightFallbackNeg"), FLinearColor(Lights[3], Lights[4], Lights[5]));
		}
		return;
	}

	FVoxelChunk* Chunk = FindChunk(GridToChunk(Pos));
	if (!Chunk)
	{
		return;
	}

	// Instanca je u povrsinskom setu svoje vrste ili u setu svog odsjecka podzemlja
	FChunkInstanceSet* Set = Chunk->InstanceSets.Find(Type);
	const int32* Index = Set && Set->Component ? Set->GridToInstance.Find(Pos) : nullptr;
	if (!Index && Chunk->DeepSets.IsValidIndex(SectionIndex(Pos.Z)))
	{
		Set = Chunk->DeepSets[SectionIndex(Pos.Z)].Find(Type);
		Index = Set && Set->Component ? Set->GridToInstance.Find(Pos) : nullptr;
	}
	if (!Index)
	{
		return;
	}

	ComputeFaceLights(Pos, Type, Lights);
	for (int32 Face = 0; Face < 6; Face++)
	{
		Set->Component->SetCustomDataValue(*Index, 3 + Face, Lights[Face]);
	}
}

void AVoxelWorld::FlushLightDirty()
{
	if (DirtyLightCells.Num() == 0)
	{
		return;
	}

	// Promjena svjetla celije mijenja izgled bloka u njoj (baklja) i stranica 6 susjednih blokova
	TSet<FIntVector> BlocksToRefresh;
	BlocksToRefresh.Reserve(DirtyLightCells.Num() * 4);
	for (const FIntVector& Cell : DirtyLightCells)
	{
		BlocksToRefresh.Add(Cell);
		for (const FIntVector& Direction : GLightDirections)
		{
			BlocksToRefresh.Add(Cell + Direction);
		}
	}
	DirtyLightCells.Reset();

	for (const FIntVector& Pos : BlocksToRefresh)
	{
		RefreshBlockLightVisual(Pos);
	}
}

void AVoxelWorld::UpdateLightIntensity()
{
	const float Intensity = CVarVoxelLightIntensity.GetValueOnGameThread();
	if (Intensity != AppliedLightIntensity && BlockLightCollection)
	{
		AppliedLightIntensity = Intensity;
		UKismetMaterialLibrary::SetScalarParameterValue(this, BlockLightCollection, TEXT("Intensity"), Intensity);
	}
}

float AVoxelWorld::RunEntityLightTest()
{
	AFirstPersonCharacter* Player = Cast<AFirstPersonCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0));
	if (!Player)
	{
		UE_LOG(LogTemp, Warning, TEXT("[ENTITYLIGHT] test: nema igraca"));
		return 0.f;
	}

	int32 Failures = 0;
	const auto Check = [&Failures](bool bOk, const FString& What)
	{
		UE_LOG(LogTemp, Log, TEXT("[ENTITYLIGHT] %s -> %s"), *What, bOk ? TEXT("OK") : TEXT("FAIL"));
		Failures += bOk ? 0 : 1;
	};

	// Poligon visoko nad igracem: kamen i na njemu baklja
	const FIntVector PlayerGrid = WorldToGrid(Player->GetActorLocation());
	const FIntVector Stone(PlayerGrid.X, PlayerGrid.Y, FMath::Min(PlayerGrid.Z + 30, WorldMaxZ - 12));
	const FIntVector Torch = Stone + FIntVector(0, 0, 1);
	PlaceBlockAt(Stone, EBlockType::Stone);
	const bool bTorch = PlaceItemAt(Torch, EItemType::Torch, FIntVector(0, 0, 1), FVector(0.f, 0.f, -1.f));
	Check(bTorch && GetBlockLightAt(Torch) == 14, FString::Printf(TEXT("poligon: baklja na (%d,%d,%d), svjetlo %d (14)"), Torch.X, Torch.Y, Torch.Z, GetBlockLightAt(Torch)));

	// Stvorenja i dropovi uz baklju: svaki u svojoj celiji, na razlicitoj udaljenosti
	struct FSubject
	{
		FString Name;
		AActor* Actor = nullptr;
	};
	TArray<FSubject> Subjects;
	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const auto LocationOf = [this, Torch](int32 DX, int32 DY) { return GridToWorld(Torch.X + DX, Torch.Y + DY, Torch.Z); };

	Subjects.Add({ TEXT("drop Coal (slicica)"), AItemDrop::SpawnItemDrop(this, EItemType::Coal, LocationOf(1, 0)) });
	Subjects.Add({ TEXT("drop Dirt (blok)"), AItemDrop::SpawnItemDrop(this, EItemType::Dirt, LocationOf(-2, 0)) });
	if (ZombieClass)
	{
		Subjects.Add({ TEXT("zombi"), GetWorld()->SpawnActor<AZombie>(ZombieClass, LocationOf(0, 3), FRotator::ZeroRotator, SpawnParams) });
	}
	for (const FMobSpawnEntry& Entry : MobSpawns)
	{
		if (Entry.MobClass)
		{
			Subjects.Add({ Entry.MobClass->GetName(), GetWorld()->SpawnActor<AMobBase>(Entry.MobClass, LocationOf(0, -4), FRotator::ZeroRotator, SpawnParams) });
		}
	}

	const auto ReadMaterialLight = [](const AActor* Actor)
	{
		// Najveca vrijednost parametra svjetla u materijalima actora (-1 = nijedan ga nema)
		float Value = -1.f;
		TInlineComponentArray<UMeshComponent*> Meshes(Actor);
		for (const UMeshComponent* Mesh : Meshes)
		{
			for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); Slot++)
			{
				FLinearColor Light;
				const UMaterialInterface* Material = Mesh->GetMaterial(Slot);
				if (Material && Material->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("LightFallbackPos")), Light))
				{
					Value = FMath::Max(Value, Light.R);
				}
			}
		}
		return Value;
	};

	for (int32 Pass = 0; Pass < 2; Pass++)
	{
		// Prvi prolaz uz baklju, drugi nakon sto je baklja uklonjena
		if (Pass == 1)
		{
			DestroyBlockAt(Torch, /*bDropItems=*/false);
		}
		for (const FSubject& Subject : Subjects)
		{
			UBlockLightReceiverComponent* Receiver = Subject.Actor ? Subject.Actor->FindComponentByClass<UBlockLightReceiverComponent>() : nullptr;
			if (!Receiver)
			{
				Check(false, FString::Printf(TEXT("%s: %s"), *Subject.Name, Subject.Actor ? TEXT("nema komponentu svjetla") : TEXT("nije stvoren")));
				continue;
			}
			Receiver->UpdateNow();

			// Ocekivana svjetlina: razina celije u kojoj su oci pawna, odnosno srediste dropa
			const APawn* Pawn = Cast<APawn>(Subject.Actor);
			const FVector Probe = Pawn ? Pawn->GetPawnViewLocation() : Subject.Actor->GetActorLocation();
			const FVector Rel = (Probe - GetActorLocation()) / BlockSize;
			const int32 Level = GetBlockLightAt(FIntVector(FMath::RoundToInt(Rel.X), FMath::RoundToInt(Rel.Y), FMath::RoundToInt(Rel.Z)));
			const float Expected = LightLevelToBrightness(Level);
			const float InMaterial = ReadMaterialLight(Subject.Actor);
			const bool bLevelOk = Pass == 0 ? Level >= 9 : Level == 0;
			Check(bLevelOk && FMath::IsNearlyEqual(Receiver->GetBrightness(), Expected, 0.001f) && FMath::IsNearlyEqual(InMaterial, Expected, 0.001f)
				&& Receiver->GetLitMaterialCount() > 0,
				FString::Printf(TEXT("%s %s: razina mjesta %d, svjetlina %.3f, u materijalu %.3f (ocekivano %.3f), materijala sa svjetlom %d, bez %d"),
					*Subject.Name, Pass == 0 ? TEXT("uz baklju") : TEXT("bez baklje"), Level, Receiver->GetBrightness(), InMaterial, Expected,
					Receiver->GetLitMaterialCount(), Receiver->GetUnlitMaterialCount()));
		}
	}

	// Igrac: ruka i ono sto drzi moraju imati materijal koji prima svjetlo
	if (UBlockLightReceiverComponent* Receiver = Player->FindComponentByClass<UBlockLightReceiverComponent>())
	{
		Receiver->UpdateNow();

		// Popis materijala po meshu, da se vidi sto na igracu ne prima svjetlo
		TInlineComponentArray<UMeshComponent*> PlayerMeshes(Player);
		for (const UMeshComponent* Mesh : PlayerMeshes)
		{
			for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); Slot++)
			{
				FLinearColor Light;
				UMaterialInterface* Material = Mesh->GetMaterial(Slot);
				const bool bLit = Material && Material->GetVectorParameterValue(FHashedMaterialParameterInfo(TEXT("LightFallbackPos")), Light);
				UE_LOG(LogTemp, Log, TEXT("[ENTITYLIGHT] igrac: %s [%d] %s, %s, %s"), *Mesh->GetName(), Slot,
					Material ? *Material->GetBaseMaterial()->GetName() : TEXT("bez materijala"),
					Mesh->IsVisible() ? TEXT("vidljiv") : TEXT("skriven"), bLit ? TEXT("prima svjetlo") : TEXT("NE prima svjetlo"));
			}
		}
		Check(Receiver->GetLitMaterialCount() > 0,
			FString::Printf(TEXT("igrac: materijala sa svjetlom %d, bez %d, svjetlina %.3f"),
				Receiver->GetLitMaterialCount(), Receiver->GetUnlitMaterialCount(), Receiver->GetBrightness()));
	}
	else
	{
		Check(false, TEXT("igrac: nema komponentu svjetla"));
	}

	for (const FSubject& Subject : Subjects)
	{
		if (Subject.Actor)
		{
			Subject.Actor->Destroy();
		}
	}
	DestroyBlockAt(Stone, /*bDropItems=*/false);

	UE_LOG(LogTemp, Log, TEXT("[ENTITYLIGHT] test GOTOVO: neuspjelih provjera %d -> %s"), Failures, Failures == 0 ? TEXT("OK") : TEXT("FAIL"));
	return 1.0f;
}
