# Backlog

Popis poznatih dugova i poboljšanja koja nisu hitna, ali ih treba odraditi.

## 0. Per-chunk mob spawn (beskonačan svijet)

**Problem:** Mobovi se spawnaju samo jednom, oko igračevog starta
(PLAN_Chunks.md 3.6). Odlaskom izvan unload radijusa se despawnaju — istraženi
svijet je zato trajno prazan.

**Posao:** spawn pravila po chunku pri prvom učitavanju (deterministički iz
chunk seeda, salt različit od stabala), cap po području, respawn ciklus.
Vidi Minecraftov model: spawn unutar 128 blokova od igrača.

## 0b. ItemDrop nije vezan uz chunkove

Ako se chunk unloada ispod ItemDropa (igrač pobjegne odmah nakon kopanja),
drop propada kroz svijet dok ga KillZ ne počisti. Kozmetički problem —
po potrebi vezati dropove uz chunk (despawn pri unloadu, kao mobovi).

## 1. NavMeshBoundsVolume u leveli je divovski

**Problem:** NavMeshBoundsVolume u `Lvl_ThirdPerson` je višestruko veći od
svijeta (~28 km stranica). S tileom od 1000uu to traži 8,2M tileova — iznad
`TileNumberHardLimit` (1M), pa engine odsijeca adresabilni prostor i dijelovi
svijeta ostaju bez navmesha (log: `Navmesh bounds are too large!`).

**Trenutni workaround:** `TileSizeUU=3000` u `DefaultEngine.ini`
(`[/Script/NavigationSystem.RecastNavMesh]`) — 9× manje tileova (~912k < 1M),
pa cijeli svijet stane u limit. Vidi
[PLAN_InstancedStaticMesh.md](PLAN_InstancedStaticMesh.md), sekcija 5.2.

**Novo stanje (chunk streaming, 2026-09-27):** volumen sada PRATI igrača
(`AVoxelWorld::UpdateNavBoundsVolume` ga svaki prelazak chunka centrira na
igračev chunk), ali mu je VELIČINA i dalje autorska iz levele (~28 km).

**Pravi fix (editor):** smanjiti volumen na streaming područje: stranica
~`2 * UnloadRadius * 16 * 100` UU (uz default VisualRadius=6 → unload 8 →
~25.600 UU), visina ~5.000 UU (svijet je visok 4.800). Nakon toga se
`TileSizeUU` override u configu može maknuti (vratiti na default 1000) —
manji tileovi znače finiju granulaciju rebuilda pri kopanju.

## 2. BTTask_FindWanderLocation masovno failanje

**Problem:** `BTTask_FindWanderLocation: Could not find valid wander
location` ispisuje se ogromnom brzinom u logu - u headless testu (`-nullrhi`,
vidi sekcija "Headless Test Run" u CLAUDE.md) izmjereno ~10.000-13.000 failova
u 40-ak sekundi, za sve mobove zajedno.

**Dijagnoza:** Potvrđeno (2026-08-23), kad je uveden noise-based teren, da
problem NIJE vezan uz visinske razlike terena - identičan volumen failova
izmjeren i na potpuno ravnom testnom terenu (`HeightAmplitude=0`), znači
postojao je i prije. Uzrok je vjerojatno u
`BTTask_FindWanderLocation.cpp`: `NavSys->GetRandomReachablePointInRadius(
Origin, Creature->GetWanderRadius(), RandomLocation)` bez ikakvog cooldowna/
retry-limita - ako je `WanderRadius` velik u odnosu na dostupni navmesh oko
moba (rubovi svijeta, litice), većina nasumičnih pokušaja promašuje, a
Behavior Tree ih ponavlja gotovo bez pauze.

**Mogući fix:** smanjiti `WanderRadius` na nešto realnije u odnosu na
gustoću navmesha, i/ili dodati kratki cooldown/retry-limit u
`BTTask_FindWanderLocation` da neuspjeli pokušaji ne pune log iz frame u
frame. Treba prvo utvrditi je li ovo samo log-šum (BT ionako čeka do sljedće
prilike) ili stvarno degradira ponašanje mobova (stoje u mjestu jer nikad ne
dobiju validnu wander lokaciju).
