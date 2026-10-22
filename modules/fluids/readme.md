# fluids

Beta 1.7.3 water and lava (BlockFlowing / BlockStationary) as a deterministic cellular automaton on scheduled ticks (P2.3). It reads and writes the world through a small voxel interface, so it runs on an in-memory grid in the tests and on a `World` adapter in the game.

## Public entry points

- `fluids/FluidSimulator.hpp`: `FluidSimulator` (tick, spread, retraction, infinite sources, lava/water interactions), `FluidConfig`, the `FluidEvent`s the game turns into drops and sounds.
- `fluids/FluidState.hpp`: `FluidType` and the 4-bit per-voxel level/falling nibble.
- `fluids/FluidVoxelAccess.hpp`: `IFluidVoxelAccess`, the voxel view the simulator needs, and `blocksFlow`.
- `fluids/GridFluidVoxelAccess.hpp`: an in-memory box implementing that view (tests, reference behaviour).
- `fluids/ActiveFluidQueue.hpp`: the queue of cells still flowing, so settled oceans cost nothing.

## What it does not do

No `World`: the adapter (`World::FluidAccess`) lives in `world`. No drops or sounds, only events describing them.
