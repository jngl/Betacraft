# world

The live world: `World` owns the loaded chunks and runs everything that touches several of them (generation, decoration, light, fluids, redstone, scheduled and random ticks, block entities, saving). It sits above `worldgen`, `storage`, `fluids`, `redstone` and `gameplay`, and below everything that plays in a world (`physics`, `ai`, `ecs`, `streaming`, `acoustics`).

## Public entry points

- `world/World.hpp`: `World` (chunk loading and eviction, block and metadata edits, `tick()`, the queues `WorldSession` empties each tick).
- `world/WorldGen.hpp`: `WorldGen`, pass-1 terrain of one chunk (heightmap, biomes, caves through `worldgen/CaveCarver`).
- `world/ChunkGenPipeline.hpp`: pass 1 on worker threads, or inline with `workerCount = 0`.
- `world/DifferentialLightingEngine.hpp`: BFS add/remove block light and skylight, bounded per edit.
- `world/BlockTicks.hpp`: random ticks (crops, grass, leaves, fire...).
- `world/Weather.hpp`: rain and thunder cycle, lightning rolls.

## What it does not do

No meshing (`streaming`, `render`), no entities (`ecs`) and no player (`physics`). No file formats: those are `storage`.
