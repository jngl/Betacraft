# ai

Voxel A* for walking mobs: what a cell means to a mob of a given size, and the search over it. The ECS system that plans and follows paths is `ecs::PathPlanningSystem`.

## Public entry points

- `ai/NavVoxelQuery.hpp`: `NavCell` and `INavVoxelQuery`, the interface the search reads.
- `ai/VoxelPathfinder.hpp`: `NavAgent`, the moves (`canTraverse`, `moveCost`), `PathSearch` (A* run a node budget at a time) and `findPath`.
- `ai/WorldNavQuery.hpp`: `INavVoxelQuery` over a live `World` (doors, fences, liquids, hazards).
- `ai/GridNavQuery.hpp`: an in-memory grid for tests.

## What it does not do

No steering or entity state (`ecs`), no mob rules (`gameplay/MobDefs.hpp`).
