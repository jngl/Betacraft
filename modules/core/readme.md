# core

The vocabulary every other module shares: block and chunk coordinates (`BlockPos`, `ChunkCoord`), the engine configuration read from the command line, and logging. It sits at the bottom of the module graph and knows nothing about the game.

## Public entry points

- `core/Types.hpp`: `BlockPos`, `ChunkCoord`, `NonCopyable`, hashes for coordinate maps.
- `core/Config.hpp`: `EngineConfig`, `FeatureFlags` (`--no-<flag>` switches), `GameMode`, `NewWorldParams`.
- `core/Logger.hpp`: `Logger` (`debug`/`info`/`warn`/`error` per subsystem, minimum level).
- `core/MpscRingBuffer.hpp`: the lock-free multi-producer queue that game threads use to post commands to the audio callback.

## What it does not do

No blocks, chunks or world state (that is `voxel` and `world`), no boxes, frustum or noise (that is `math`), and nothing that touches SDL, OpenGL or audio.
