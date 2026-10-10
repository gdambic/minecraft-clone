#pragma once

#include "CoreMinimal.h"
#include "BlockType.h"
#include "BiomeDefinition.h"

class UInstancedStaticMeshComponent;

/**
 * Konstante i koordinatni helperi chunk sustava (vidi Docs/PLAN_Chunks.md).
 *
 * Chunk je stupac 16x16 blokova kroz CIJELU visinu svijeta (Minecraftov
 * model: jedinica ucitavanja je stupac; sekcije po visini se dodaju tek s
 * visokim svjetovima). Streaming je zato 2D - chunkovi se adresiraju samo
 * po (X,Y).
 *
 * PRAVILA SPREMNOSTI ZA SUBCHUNKOVE (iz plana, sekcija 3.1):
 *  1. Formulu indeksiranja Blocks arraya zna SAMO ovaj header - sav pristup
 *     ide kroz FVoxelChunk::GetBlock/SetBlock ili VoxelGrid helpere.
 *  2. WorldHeight je jedina konstanta visine - nikad hardkodirana brojka.
 */
namespace VoxelGrid
{
	/** log2 stranice chunka - stranica MORA biti potencija dvojke (bit-shift/maska za floor-division) */
	constexpr int32 ChunkSizeLog2 = 4;
	constexpr int32 ChunkSize = 1 << ChunkSizeLog2;   // 16
	constexpr int32 ChunkSizeMask = ChunkSize - 1;    // 15

	/**
	 * Visina svijeta = zbroj tri komponente (plan 3.1): dubina za kopanje
	 * + njihaj terena 2*HeightAmplitude + zrak iznad najviseg vrha za stabla
	 * i gradnju. Povrsina se njise oko Z=0 (SurfaceLevel): kopanje ide u
	 * minus do WorldMinZ, gradnja u plus do WorldMaxZ.
	 * Blokovi zive na Z = WorldMinZ..WorldMaxZ.
	 */
	constexpr int32 WorldHeight = 48;
	constexpr int32 WorldMinZ = -24;
	constexpr int32 WorldMaxZ = WorldMinZ + WorldHeight - 1;   // 23

	constexpr int32 BlocksPerChunk = ChunkSize * ChunkSize * WorldHeight;

	/**
	 * Grid -> chunk koordinata. Aritmeticki shift radi floor-division i za
	 * negativne koordinate: (-1 >> 4) == -1. Integer '/' bi dao 0 (bug:
	 * "zrcaljeni" teren oko origina) - zato NIKAD X / ChunkSize.
	 */
	FORCEINLINE FIntPoint GridToChunk(int32 X, int32 Y)
	{
		return FIntPoint(X >> ChunkSizeLog2, Y >> ChunkSizeLog2);
	}

	FORCEINLINE FIntPoint GridToChunk(const FIntVector& Pos)
	{
		return GridToChunk(Pos.X, Pos.Y);
	}

	/** Grid -> lokalna pozicija unutar chunka. Maska radi i za negativne koordinate ((-1 & 15) == 15). */
	FORCEINLINE FIntVector GridToLocal(const FIntVector& Pos)
	{
		return FIntVector(Pos.X & ChunkSizeMask, Pos.Y & ChunkSizeMask, Pos.Z);
	}

	/**
	 * Lokalna pozicija -> indeks u Blocks arrayu. Jedino mjesto koje zna
	 * formulu - ukljucivo offset kojim grid Z (WorldMinZ..WorldMaxZ) postaje
	 * 0-bazirani storage Z. Lokalni Z je dakle UVIJEK grid Z, nikad 0..47.
	 */
	FORCEINLINE int32 LocalIndex(const FIntVector& Local)
	{
		return ((Local.Z - WorldMinZ) * ChunkSize + Local.Y) * ChunkSize + Local.X;
	}

	/** Grid pozicija prvog (jugozapadnog) stupca chunka. */
	FORCEINLINE FIntVector ChunkOrigin(const FIntPoint& Coord)
	{
		return FIntVector(Coord.X << ChunkSizeLog2, Coord.Y << ChunkSizeLog2, 0);
	}

	FORCEINLINE bool IsValidZ(int32 Z)
	{
		return Z >= WorldMinZ && Z <= WorldMaxZ;
	}

	/** Chebyshev udaljenost chunk koordinata - metrika svih streaming radijusa. */
	FORCEINLINE int32 ChunkDistance(const FIntPoint& A, const FIntPoint& B)
	{
		return FMath::Max(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y));
	}

	/**
	 * Deterministicki seed chunka - jedini legalan izvor slucajnosti u
	 * generaciji (globalni RNG bi ovisio o redoslijedu ucitavanja chunkova).
	 * Salt razdvaja neovisne potrosace (stabla, buduci mob spawn...) da ne
	 * dijele isti niz.
	 */
	FORCEINLINE int32 ChunkSeed(int32 WorldSeed, const FIntPoint& Coord, uint32 Salt = 0)
	{
		return (int32)HashCombine(HashCombine(HashCombine(
			GetTypeHash(WorldSeed), GetTypeHash(Coord.X)), GetTypeHash(Coord.Y)), Salt);
	}
}

/**
 * Instance set jednog tipa bloka UNUTAR jednog chunka: ISM komponenta +
 * dvosmjerni bookkeeping grid pozicija <-> indeks instance. Komponenta radi
 * RemoveAtSwap pri uklanjanju (SetRemoveSwap) pa se indeksi mijenjaju -
 * NIKAD ih ne cuvati izvan ove strukture, uvijek ici kroz GridToInstance u
 * trenutku koristenja (PLAN_InstancedStaticMesh.md 5.1).
 *
 * GC napomena: komponenta je registrirana na AVoxelWorld pa je ziva drzi
 * njegov OwnedComponents - ovoj strukturi ne treba UPROPERTY.
 */
struct FChunkInstanceSet
{
	TObjectPtr<UInstancedStaticMeshComponent> Component = nullptr;

	/** Biome tint tipa (cache iz registry definicije). != None => komponenta ima 3 custom data floata (RGB). */
	EBiomeTintType BiomeTint = EBiomeTintType::None;

	/** GLOBALNA grid pozicija -> indeks instance u komponenti */
	TMap<FIntVector, int32> GridToInstance;

	/** Indeks instance -> globalna grid pozicija (paralelan s nizom instanci u komponenti) */
	TArray<FIntVector> InstanceToGrid;
};

/** Zivotni ciklus chunka: podaci se generiraju (worker), pa primjenjuju (game thread = vizuali). */
enum class EChunkState : uint8
{
	Generating,   // worker racuna Blocks array
	DataReady,    // podaci gotovi, vizuali jos ne postoje
	Applied       // podaci + vizuali (ako je u visual ringu) - jedini editabilan
};

/** Gotov rezultat workera - putuje kroz thread-safe queue natrag na game thread. */
struct FCompletedChunkData
{
	FIntPoint Coord = FIntPoint::ZeroValue;
	uint64 RequestId = 0;
	double GenMs = 0.0;
	TArray<EBlockType> Blocks;
};

/**
 * Jedan chunk: izvor istine (Blocks) + vizuali (InstanceSets).
 * Zivi u AVoxelWorld::Chunks; nikad ne postoji bez unosa u toj mapi.
 */
struct FVoxelChunk
{
	FIntPoint Coord = FIntPoint::ZeroValue;
	EChunkState State = EChunkState::Generating;

	/** Izvor istine: flat array BlocksPerChunk celija, Air = odsutnost bloka. Indeksira SAMO VoxelGrid::LocalIndex. */
	TArray<EBlockType> Blocks;

	/** Vizuali - ISM set SAMO za tipove prisutne u chunku (lazy kreiranje). */
	TMap<EBlockType, FChunkInstanceSet> InstanceSets;

	/** Je li igrac mijenjao chunk - odreduje cuva li se sadrzaj pri unloadu (plan 3.1). */
	bool bModified = false;

	/** Jesu li instance izgradene (chunk je u visual ringu). */
	bool bVisualsBuilt = false;

	/**
	 * Redni broj async zahtjeva generacije (plan, rizik 6): rezultat s workera
	 * se prihvaca samo ako se id poklapa - stiti od zastarjelog rezultata kad
	 * je chunk u meduvremenu unloadan pa ponovno zatrazen.
	 */
	uint64 GenRequestId = 0;

	FVoxelChunk() = default;
	explicit FVoxelChunk(FIntPoint InCoord)
		: Coord(InCoord)
	{
		Blocks.SetNumZeroed(VoxelGrid::BlocksPerChunk);
		static_assert((int32)EBlockType::Air == 0, "SetNumZeroed pretpostavlja Air == 0");
	}

	EBlockType GetBlock(const FIntVector& Local) const
	{
		if (!VoxelGrid::IsValidZ(Local.Z))
		{
			return EBlockType::Air;
		}
		return Blocks[VoxelGrid::LocalIndex(Local)];
	}

	void SetBlock(const FIntVector& Local, EBlockType Type)
	{
		if (VoxelGrid::IsValidZ(Local.Z))
		{
			Blocks[VoxelGrid::LocalIndex(Local)] = Type;
		}
	}
};
