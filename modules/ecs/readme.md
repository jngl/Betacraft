# ecs

The sparse-set entity component system (P2.1) and the systems that move dropped items, mobs and projectiles each tick. It sits above `world`, `physics` and `ai`.

## Public entry points

- `ecs/Entity.hpp`: `EntityId`, 24-bit index plus 8-bit generation.
- `ecs/Registry.hpp`: `Registry`, entity slots and one pool per component type.
- `ecs/SparseSet.hpp`, `ecs/ComponentPool.hpp`, `ecs/View.hpp`, `ecs/Group.hpp`: storage and iteration (views, owning groups).
- `ecs/CommandBuffer.hpp`: structural changes deferred out of an iteration.
- `ecs/Components.hpp`: the plain-data gameplay components.
- `ecs/Systems.hpp`: movement, voxel collision, health, item drops, `PathPlanningSystem`, `MobAISystem`, render interpolation, `PlayerControlSystem`, and `EntityFactory`.

## What it does not do

No game rules spanning the world and the registry (spawning, pickup, explosions): those are `game/EntitySimulation`.
