# game

The top of the engine: the glue between the modules and the SDL application. `app/main.cpp` only parses the command line and runs `Application`.

## Public entry points

- `game/Application.hpp`: SDL window and GL context, main loop (20 TPS fixed step, interpolated render), screen transitions, effects of each tick.
- `game/WorldSession.hpp`: one open world (World, Player, `EntitySimulation`, weather, time of day, spawn, crafting grid), its open/save/close lifecycle and `tick` in Beta 1.7.3 order. GL-free.
- `game/EntitySimulation.hpp`: the `ecs::Registry` and the entity phase of a tick; `damagePlayer` is the only way to hurt the player.
- `game/InventoryCrafting.hpp`, `game/ItemUse.hpp`, `game/MobSpawner.hpp`, `game/MobBehaviour.hpp`, `game/FallingBlocks.hpp`, `game/Projectiles.hpp`, `game/StarterKit.hpp`, `game/PlayerPersistence.hpp`.
- `game/DynamicLightCollector.hpp`, `game/AcousticsController.hpp`: per-frame feeds for `lighting` and `acoustics`.
- `game/ItemBridge.hpp`, `game/FluidEffects.hpp`.

## What it does not do

Nothing sits above it: no other module may include `game/`.
