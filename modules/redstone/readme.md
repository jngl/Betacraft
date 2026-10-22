# redstone

Beta 1.7.3 redstone (S8.5–S8.8): wire power propagation, torches with burnout, levers, buttons and pressure plates, and the consumers they switch. Like `fluids`, it sees the world only through a voxel interface, so it is tested on an in-memory grid.

## Public entry points

- `redstone/RedstoneEngine.hpp`: `RedstoneEngine` (per-tick update, scheduled torch and button ticks), `RedstoneTickStats`, the `RedstoneEvent`s the game turns into sounds and smoke.
- `redstone/RedstoneVoxelAccess.hpp`: `IRedstoneVoxelAccess`, the voxel view the engine needs.
- `redstone/TorchBurnoutTracker.hpp`: a torch that toggles too fast burns out for a while.

## What it does not do

No `World`: the adapter (`World::RedstoneAccess`) lives in `world`. No player interaction: right-clicking a lever goes through `game`.
