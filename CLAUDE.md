# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

BetaCraft is a self-contained Minecraft Beta 1.7.3 clone in C++20 / OpenGL 3.3 Core / SDL2 / miniaudio. Requirements and design docs (mostly in French): `ORIGINAL_REQUEST.md`, `PROJECT.md` (architecture, interface contracts, deferred items), `docs/IMPROVEMENTS_ROADMAP.md` (technical proposals P1.x–P4.x), `docs/GAMEPLAY_ROADMAP.md` (sprints S1–S8 and the cross-cutting conventions summarised below). Code comments cite these IDs (`P1.1`, `S7.4`, `§9.2`).

## Build, run, test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release     # needs SDL2 (pkg-config); libzstd and libpng are optional
cmake --build build -j
ctest --test-dir build --output-on-failure -LE benchmark   # what CI runs
ctest --test-dir build -L benchmark                       # wall-clock budget tests (sets BETACRAFT_PERF_TESTS=1)
./build/betacraft --help
./build/betacraft --frames 300 --save-dir /tmp/bc-smoke   # headless-ish smoke test required before merging
```

Run a single test by invoking a GoogleTest binary directly with a filter:

```bash
./build/betacraft_unit_tests --gtest_filter='PalettedSection*'
./build/betacraft_p2_fluids_tests --gtest_filter='*Lava*'
ctest --test-dir build -R Phase2_storage --output-on-failure
```

Test executables: `betacraft_unit_tests`, `betacraft_integration_tests`, `betacraft_e2e_tests` (spawns the real `betacraft` binary), `betacraft_mock_tests`, `betacraft_all_tests` (everything combined), `betacraft_legacy_tests`, and one `betacraft_p2_<key>_tests` per Phase 2 module. Phase 2 test keys differ from some source dirs: `combat`→`src/gameplay`, `dynlight`→`src/lighting`. Frozen reference saves live in `tests/data/saves/vN/` (`BETACRAFT_TEST_DATA_DIR`).

Optional build switches: `-DBETACRAFT_WITH_ZSTD=OFF` (region files stored uncompressed). Runtime feature flags turn off risky mechanics: `--no-async-gen`, `--no-region-storage`, `--no-pathfinding`, `--no-redstone`, `--no-weather`.

## What CMake compiles automatically vs. by hand

All engine code goes into one static library, `betacraft_core`; `betacraft` is just `src/main.cpp`.

- **Globbed (no CMake edit needed):** `src/{streaming,ecs,gameplay,crafting,fluids,worldgen,lighting,acoustics,storage,ai,redstone}/*.cpp` (`BETACRAFT_PHASE2_DIRS`), `tests/phase2/<key>/*.cpp`, `tests/integration/phase2/*.cpp`.
- **Listed by hand (otherwise never compiled):** any `.cpp` in `src/{core,math,world,physics,render,audio,ui,game}` → `BETACRAFT_CORE_SOURCES`; `tests/unit/*` → `UNIT_TEST_SOURCES`; non-phase2 `tests/integration/*.cpp` → `INTEGRATION_TEST_SOURCES`; `tests/e2e/*` → `E2E_TEST_SOURCES`.
- A new Phase 2 module needs both a `BETACRAFT_PHASE2_DIRS` entry and a `BETACRAFT_PHASE2_MODULES` `"key:dir"` entry.

`betacraft_core` builds with `-Wall -Wextra -Wpedantic` (CI adds `-Werror`, ASan/UBSan, TSan on `Phase2_streaming`/`storage`/`ai`, and a no-zstd job). miniaudio is isolated in its own warning-free TU (`src/audio/miniaudio_impl.cpp`).

## Architecture

**Main loop (`game/Application`).** Polls SDL → `InputManager` → fixed-timestep accumulator (20 TPS game ticks, decoupled from render rate) → per tick: `WorldSession::tick` in Beta 1.7.3 order (input: mining/attack → entity phase via `EntitySimulation::tick`: ECS systems, pickup, TNT and explosions, falling-block landing, then block entities, and the death scatter → world phase: weather, sleep, `spawnMobs`, time of day +1 tick, `World::tick()` (scheduled ticks, redstone, fluids), random ticks, the lightning roll → World queues emptied: falling-block spawns, unsupported breaks, removed block entities, fluid wash-away drops), returning `WorldTickEvents` that `Application` turns into effects → render with interpolation alpha. A screen state machine (`ScreenState`, `ui/ScreenKind.hpp`, `ui/*Screen`) picks title / world select / options / in-game / death screens.

**World data (`world/`).** `World` owns chunks; each `Chunk` is a stack of 16³ `PalettedSection`s with dynamic 0/4/8/16-bit palettes and lazily allocated light arrays. Per-voxel 4-bit metadata stores fluid level and orientation. `DifferentialLightingEngine` does BFS add/remove light propagation (bounded to 4991 voxels) plus vertical skylight extinction. Block entities (furnace, chest) live in `BlockEntityStore`.

**Chunk lifecycle.** `World::ensureChunkLoaded` loads from disk or runs pass 1 (`WorldGen::generateChunk` → `worldgen/CaveCarver`), registers with `worldgen/DecorationPipeline` (pass 2 runs once the 3×3 neighbourhood is resident), relights, schedules unsettled fluids, and dirties neighbour meshes. `streaming/ChunkGenPipeline` moves generation off-thread unless `--no-async-gen`.

**Meshing & rendering.** Each frame `WorldRenderer` calls `streaming::WorldMeshStreamer::update`, which captures immutable 18³ `SectionNeighborhoodSnapshot18`s of dirty sections and submits them to `AsyncChunkPipeline` (jthread pool). Workers run `SnapshotMesher`, results land in `PingPongStaging`, and the render thread uploads them under a per-frame budget (`--upload-budget-ms`). Vertices are 8-byte `PackedChunkVertex` with one shared static EBO. Dynamic lights (held/dropped torches) are tile-culled and uploaded as a std140 UBO (binding 3), injected into the chunk shader via `lighting/ShaderInclude`. GLSL sources are in `shaders/`; GL entry points are loaded by `render/GLHeaders`.

**Entities & gameplay.** `EntitySimulation` owns the sparse-set `ecs::Registry` (dropped items, mobs, projectiles). `gameplay/` holds tools, durability, harvesting, combat, health; `crafting/` holds 2×2/3×3 grids and the Beta recipe book; `ai/` voxel A*; `redstone/` wire/torches/levers. `game/WorldSession` owns one open world (World, Player, `EntitySimulation`, weather, time of day, spawn points, crafting grid) and its open/save/close lifecycle, GL-free; `Application` holds one `unique_ptr<WorldSession>`. `game/` is the glue between these modules and `Application`.

**Audio.** `AudioEngine` takes commands from game threads via a lock-free `MpscRingBuffer` and drains them in the miniaudio callback (no locks or allocations there). Sounds are synthesised procedurally. `game/AcousticsController` runs 10 Hz DDA occlusion and enclosure detection and posts `AcousticCommand`s applied by `acoustics/AcousticVoiceBank` (per-voice low-pass + shared FDN reverb bus).

**Persistence.** `world/WorldStorage` + `storage/` (region files `region/r.X.Z.bcr`, 4 KiB sectors, zstd chunk bundles; legacy `chunks/*.chk|.dec|.be` are migrated to `chunks.migrated/`). `level.dat` is a `key value` text file with header `BETACRAFT_LEVEL_V2`. Textures are generated procedurally by `render/TextureAtlas` at startup and exported to `assets/terrain.png` (a custom `assets/terrain.png` overrides it).

## Conventions (from `docs/GAMEPLAY_ROADMAP.md`, "Conventions transverses")

- **Saves:** every file carries a format id + version; readers accept versions ≤ their own and refuse (never overwrite) newer ones. New data = new key, no version bump. Write atomically (`.tmp` → rename old to `_old` → rename). Unreadable chunks are quarantined (`.chk.corrupt`), not silently regenerated.
- **Enums grow only at the end:** `ScreenState`, `BlockId`, `SoundEffect`, `GameAction`, `RenderShape`, `InventoryRegion`, `MobKind`, `DamageCause`.
- **Public signatures used by tests don't change** (add an overload or defaulted parameter instead): `WorldStorage::saveChunk/loadChunk`, `World::ensureChunkLoaded`, `SkyRenderer::evaluateAtmosphere`, `MobAISystem::update`, `Raycaster::cast`, `HudRenderer::hitTestInventory`, `EntitySimulation::tick`, `WorldSession::open`, `WorldSession::tick`, `renderWorld`.
- **All player damage goes through `EntitySimulation::damagePlayer`**; never write `Health::current` directly.
- **All cheats** (palette, fly, time skip, creative piston push, weather cycle) gate on the single predicate `Player::isCreative()`.
- **Right-click order** (`handleRightClickAction`): targeted entity → activate targeted block (unless sneaking) → held item on block → held item in air.
- **Tests:** unit tests instantiate modules directly with no `Application` or GL context. No `sleep` in concurrency tests — use `waitIdle`, `runPendingInline`, or `workerCount=0`. Wall-clock budgets get the `benchmark` ctest label.

## Agent skills

### Issue tracker

Issues are tracked in GitHub Issues on jngl/Betacraft (via `gh`). See `docs/agents/issue-tracker.md`.

### Triage labels

Default vocabulary: `needs-triage`, `needs-info`, `ready-for-agent`, `ready-for-human`, `wontfix`. See `docs/agents/triage-labels.md`.

### Domain docs

Single-context: one root `GLOSSARY.md` plus `docs/adr/`. See `docs/agents/domain.md`.
