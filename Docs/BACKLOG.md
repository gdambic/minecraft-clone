# Backlog

Popis poznatih dugova i poboljšanja koja nisu hitna, ali ih treba odraditi.

## 1. NavMeshBoundsVolume u leveli je divovski

**Problem:** NavMeshBoundsVolume u `Lvl_ThirdPerson` je višestruko veći od
svijeta (~28 km stranica). S tileom od 1000uu to traži 8,2M tileova — iznad
`TileNumberHardLimit` (1M), pa engine odsijeca adresabilni prostor i dijelovi
svijeta ostaju bez navmesha (log: `Navmesh bounds are too large!`).

**Trenutni workaround:** `TileSizeUU=3000` u `DefaultEngine.ini`
(`[/Script/NavigationSystem.RecastNavMesh]`) — 9× manje tileova (~912k < 1M),
pa cijeli svijet stane u limit. Vidi
[PLAN_InstancedStaticMesh.md](PLAN_InstancedStaticMesh.md), sekcija 5.2.

**Pravi fix (editor):** U leveli smanjiti NavMeshBoundsVolume na veličinu
svijeta + margina (za 100×100 svijet: ~12.000 × 12.000 × ~1.500 UU, centriran
na teren — BP_VoxelWorld je na ~(-460, -1110), teren ide do ~(9540, 8890),
površina na Z≈400). Nakon toga se `TileSizeUU` override u configu može maknuti
(vratiti na default 1000) — manji tileovi znače finiju granulaciju rebuilda
pri kopanju/postavljanju blokova.

**Napomena:** ako svijet naraste (npr. 300×300 = 30.000 UU), volumen treba
pratiti veličinu svijeta, a tile size po potrebi opet povećati.
