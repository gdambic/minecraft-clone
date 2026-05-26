# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

MinecraftClone is an **Unreal Engine 5.6** voxel game recreating Minecraft mechanics.

## Build Commands

```bash
# Build via Visual Studio 2022
# 1. Open MinecraftClone.sln
# 2. Configuration: Development Editor | Win64
# 3. Build solution

# Run: Open MinecraftClone.uproject with UE5.6 Editor
# In World Settings: GameMode Override → BP_FirstPersonGameMode
```

No automated test framework is configured. Testing is manual through the editor.

## Architecture

### Coordinate System
- **X** = Width, **Y** = Length, **Z** = Height (Z is UP, unlike Minecraft's Y-up)
- Block Size = 100 Unreal Units

### Core Classes (Source/MinecraftClone/Voxel/)

| Class | Purpose |
|-------|---------|
| `AVoxelWorld` | World manager, grid storage (`TMap<FIntVector, ABlock*>`), generation |
| `ABlock` | Individual voxel actor with mesh, destruction system, drop spawning |
| `AItemDrop` | Physics-enabled dropped items with auto-pickup |
| `UInventoryComponent` | Slot-based player inventory (36 slots: 27 main + 9 hotbar) |
| `AFirstPersonCharacter` | Player with camera, input handling, block raycast |
| `FTreeGenerator` | Static utility for procedural Oak/Birch tree generation |

### Key Enums

```cpp
// EBlockType: Air, Dirt, Stone, Grass, OakLog, BirchLog, OakLeaves, BirchLeaves
// EItemType: None, Dirt, Stone, Grass, OakLog, BirchLog, OakSapling, BirchSapling
```

### Block Interaction Flow
1. Character raycasts from camera (500 units = 5 blocks range)
2. LMB: Destroy block → spawns AItemDrop → auto-pickup adds to inventory
3. RMB: Place block from inventory at adjacent grid position
4. Scroll: Cycle through inventory items

### Leaf Decay System
Leaves without log connection within 6 blocks (Manhattan distance via BFS) decay and may drop saplings (5% chance).

## Source Structure

```
Source/MinecraftClone/
├── MinecraftClone.h/.cpp     # Module definition
├── MinecraftClone.Build.cs   # Build configuration
└── Voxel/                    # Core voxel system
```

## Content Organization

- `Content/Blueprints/Blocks/` - Block blueprints (BP_Dirt, BP_Stone, etc.)
- `Content/Input/Actions/` - Enhanced Input actions
- `Content/FirstPerson/Blueprints/` - Character and controller BPs

## Module Dependencies

Configured in `MinecraftClone.Build.cs`:
- Core, CoreUObject, Engine, InputCore
- EnhancedInput (modern input system)
- UMG, Slate (UI)

## Key APIs

```cpp
// VoxelWorld
ABlock* GetBlock(int32 X, int32 Y, int32 Z);
void SetBlockType(int32 X, int32 Y, int32 Z, EBlockType Type);
void PlaceBlockAt(FIntVector GridPosition, EBlockType Type);
FVector GridToWorld(int32 X, int32 Y, int32 Z);

// InventoryComponent - Slot API
FInventorySlot GetSlot(int32 SlotIndex);
void SetSlot(int32 SlotIndex, EItemType Type, int32 Quantity);
bool SwapSlots(int32 SlotA, int32 SlotB);
const TArray<FInventorySlot>& GetAllSlots();

// InventoryComponent - Utility
void AddItem(EItemType Type, int32 Amount = 1);  // Smart add for pickup
int32 RemoveItem(EItemType Type, int32 Amount = 1);
int32 GetItemCount(EItemType Type);
static EBlockType ItemTypeToBlockType(EItemType ItemType);
static EItemType BlockTypeToItemType(EBlockType BlockType);
```

## Documentation

Detailed docs in Croatian available in `Docs/`:
- `PROJECT_OVERVIEW.md` - Full architecture reference
- `PLAN_Trees.md` - Tree system implementation (complete)
- `PLAN_InventoryUI.md` - Inventory UI design
- `TODO_InventoryUI_Blueprints.md` - Blueprint implementation steps
