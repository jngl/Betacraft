# storage

Everything about saves on disk: region files of 4 KiB sectors with zstd-compressed chunk bundles, the read-only legacy `chunks/` layout and its one-way migration, the background I/O worker, and `level.dat`. Without libzstd (`-DBETACRAFT_WITH_ZSTD=OFF`) chunks are stored uncompressed, which every build can read.

## Public entry points

- `storage/WorldStorage.hpp`: `WorldStorage`, the facade the game uses (`saveChunk`/`loadChunk`, `level.dat`, format versions).
- `storage/ChunkBundleCodec.hpp`: one chunk's blocks, decoration record and block entities as a single entry.
- `storage/RegionFile.hpp`, `storage/RegionChunkStore.hpp`: `region/r.X.Z.bcr` files and the store over them.
- `storage/LegacyChunkStore.hpp`, `storage/SaveMigration.hpp`: the pre-Sprint 7 layout and its conversion to regions.
- `storage/IChunkStore.hpp`, `storage/ChunkIoWorker.hpp`: the store interface and the thread that writes and reads bundles off the main thread.
- `storage/WeatherState.hpp`: the weather fields persisted in `level.dat`.

## What it does not do

No decision about when a chunk is loaded or saved (that is `world`), and no player data format: `game/PlayerPersistence` writes its own keys through `WorldStorage`.
