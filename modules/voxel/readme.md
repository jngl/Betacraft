# voxel

Block and chunk data: what a block is, how a chunk stores its blocks, light and metadata, and the plain data that rides along (block entities, scheduled ticks). It sits right above `core` and `math`, and almost every gameplay module only needs this one, not the whole world.

## Public entry points

- `voxel/Block.hpp`: `BlockId`, `RenderShape`, `BlockDef` and `BlockRegistry` (hardness, light, opacity per block).
- `voxel/BlockShapes.hpp`: metadata-dependent shapes in sixteenths of a block, one table for collision, selection and meshing.
- `voxel/BlockBoxes.hpp`: a block's collision and selection boxes in world space (`blockAABB`, `selectionAABB`, `forEachBlockAABB`).
- `voxel/Chunk.hpp`: `Chunk` (16×128×16 column of sections, biome, RLE serialisation), `BiomeId`.
- `voxel/PalettedSection.hpp`: `PalettedSection`, a 16³ section with a 0/4/8/16-bit palette and lazily allocated light and metadata arrays.
- `voxel/BlockEntity.hpp`, `voxel/BlockEntityStore.hpp`: furnace and chest payloads, keyed by chunk then cell.
- `voxel/ScheduledTicks.hpp`: `ScheduledTickQueue`, block updates due on a later tick.
- `voxel/SectionCoord.hpp`: `SectionCoord` and `SectionBox`, the unit of meshing and upload.

## What it does not do

No world orchestration, generation, light propagation or saving: that is `world`, `worldgen` and `storage`. No item ids or tool rules (`gameplay`).
