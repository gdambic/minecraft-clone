// Svjetlo blokova po Minecraftu (Docs/PLAN_Torches.md): razina 0..15 po celiji,
// sirenje iz izvora s padom od jedne razine po bloku, bez prolaska kroz pune
// blokove. Dio AVoxelWorld-a izdvojen u zasebnu datoteku.

#include "VoxelWorld.h"
#include "Block.h"
#include "BlockRegistry.h"
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
