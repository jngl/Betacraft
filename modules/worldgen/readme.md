# worldgen

The generation passes that work on one chunk or a chunk neighbourhood: simplex-noise caves and aquifers (P3.3), ore veins and dungeons, and the two-pass decoration of trees, flowers and shore features (P3.4). Every step is a pure function of the seed and the chunk, so the result is the same whatever the generation order.

## Public entry points

- `worldgen/CaveCarver.hpp`, `worldgen/ColumnSampling.hpp`: `CaveCarver` and the per-column terrain facts (`ColumnContext`, `ColumnSampler`) it carves from.
- `worldgen/OreVeins.hpp`, `worldgen/Dungeons.hpp`: pass-1 ore veins and dungeon rooms with their chest loot.
- `worldgen/DecorationFeatures.hpp`: feature planning (`FeaturePlanner`, `FeaturePlan`) and rasterisation into a chunk.
- `worldgen/DecorationPipeline.hpp`: runs pass 2 once a chunk's 3×3 neighbourhood is resident (`ChunkAccess`).
- `worldgen/FeaturePlanCodec.hpp`: `DecorationRecord`, what is saved next to a chunk about its decoration.
- `worldgen/SeededRandom.hpp`, `worldgen/SimplexNoise.hpp`: the platform-independent PRNG and noise everything above draws from.

## What it does not do

No terrain heightmap or chunk loading: `WorldGen` (base terrain) and the chunk lifecycle are in `world`. No file I/O.
