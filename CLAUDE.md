# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

Betacraft is a game inspired by Minecraft in C++20 / OpenGL 3.3 Core / SDL2 / miniaudio.

## Build, run, test

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release     # needs SDL2 (pkg-config) and GLM headers; libzstd and libpng are optional
cmake --build build -j
ctest --test-dir build --output-on-failure -LE benchmark   # what CI runs (plus -E AllTests, under xvfb)
ctest --test-dir build -L benchmark                       # wall-clock budget tests (sets BETACRAFT_PERF_TESTS=1)
./build/betacraft --help
./build/betacraft --frames 300 --save-dir /tmp/bc-smoke   # headless-ish smoke test required before merging
```

Run a single test by invoking a GoogleTest binary directly with a filter:

```bash
./build/betacraft_voxel_tests --gtest_filter='PalettedSection*'
./build/betacraft_fluids_tests --gtest_filter='*Lava*'
ctest --test-dir build -R Module_storage --output-on-failure
```

Test executables: one `betacraft_<m>_tests` per module with tests (ctest `Module_<m>`), `betacraft_integration_tests` (`tests/integration/<domain>/`, fixtures in `tests/integration/support/`), `betacraft_e2e_tests` (spawns the real `betacraft` binary), `betacraft_mock_tests`, `betacraft_all_tests` (everything combined, except each module's `STANDALONE_TESTS`), `betacraft_legacy_tests`. Frozen reference saves live in `tests/data/saves/vN/` (`BETACRAFT_TEST_DATA_DIR`).

Optional build switches: `-DBETACRAFT_WITH_ZSTD=OFF` (region files stored uncompressed), `-DBETACRAFT_WITH_PNG=OFF` (`assets/terrain.png` neither exported nor loaded). Runtime feature flags turn off risky mechanics: `--no-async-gen`, `--no-region-storage`, `--no-pathfinding`, `--no-redstone`, `--no-weather`.

## What CMake compiles automatically vs. by hand

The engine is one static library per module (`docs/adr/0001-modules-as-static-libraries.md`, issue #24), under `modules/<m>/`, from bottom to top: `core`, `math`, `voxel`, `gameplay`, `fluids`, `redstone`, `worldgen`, `storage`, `lighting`, `world`, `physics`, `crafting`, `ai`, `ecs`, `streaming`, `acoustics`, `audio`, `render`, `ui`, `game`. The executable `betacraft` is `app/main.cpp` and links only `game`.

- **Modules:** `modules/<m>/{include/<m>/,src/,test/,CMakeLists.txt,readme.md}`. `include/<m>/` holds the headers other modules use, `src/` the sources and internal headers. `modules/<m>/CMakeLists.txt` is a `betacraft_add_module(<m> DEPS … [STANDALONE_TESTS <file>…])` call, plus module-specific settings (e.g. zstd in `storage`, miniaudio in `audio`, libpng in `render`) (`cmake/BetacraftModule.cmake`): it globs `src/*.cpp` and `test/*.cpp`, builds the library `<m>` (alias `betacraft::<m>`) and `betacraft_<m>_tests`, and adds the test sources to `betacraft_all_tests`, except `STANDALONE_TESTS` (tests that replace a global such as `operator new`). Module tests use only that module and its `DEPS` (plus `betacraft_test_support`: `tests/support/test_support/`, e.g. `TempDir.hpp`, `GLTestContext.hpp`, and helpers shared by several test executables). Multi-module tests stay in `tests/` (`integration`, `e2e`, `mock`) and link `betacraft::game`. A module only sees its own `include/` and those of its `DEPS`, so a missing dependency is a compile error. Add a new module with `add_subdirectory(modules/<m>)` in the root `CMakeLists.txt`.
- **Globbed (no CMake edit needed):** everything under `modules/<m>/{src,test}`, and `tests/integration/**/*.cpp` → `INTEGRATION_TEST_SOURCES`.
- **Listed by hand (otherwise never compiled):** `tests/e2e/*` → `E2E_TEST_SOURCES`; `tests/mock/*` → `MOCK_TEST_SOURCES`.

Every module, `betacraft` and the test executables (through the `betacraft_warnings` interface target) build with `-Wall -Wextra -Wpedantic` (CI adds `-Werror`). miniaudio is isolated in its own warning-free TU (`modules/audio/miniaudio_impl.cpp`, target `miniaudio_lib`), linked `PRIVATE` by `audio`: `AudioEngine.hpp` keeps the device behind a pimpl.

## CI

`.github/workflows/ci.yml` (GitHub Actions, `ubuntu-latest`, GCC) builds Debug and Release with `-Werror`, runs `ctest -LE benchmark -E AllTests` and the `--frames 300` smoke test under `xvfb-run` with Mesa, so GL and e2e tests really run. It sets `BETACRAFT_REQUIRE_GL=1`: a test guarded by `BETACRAFT_REQUIRE_GL_CONTEXT()` (`test_support/GLTestContext.hpp`) then fails instead of skipping when no OpenGL 3.3 context exists. Guard every new GL test with that macro.

## Architecture

**Main loop (`game/Application`).** Polls SDL → `InputManager` → fixed-timestep accumulator (20 TPS game ticks, decoupled from render rate) → per tick: `WorldSession::tick` in Beta 1.7.3 order (input: mining/attack → entity phase via `EntitySimulation::tick`: ECS systems, pickup, TNT and explosions, falling-block landing, then block entities, and the death scatter → world phase: weather, sleep, `spawnMobs`, time of day +1 tick, `World::tick()` (scheduled ticks, redstone, fluids), random ticks, the lightning roll → World queues emptied: falling-block spawns, unsupported breaks, removed block entities, fluid wash-away drops), returning `WorldTickEvents` that `Application` turns into effects → render with interpolation alpha. A screen state machine (`ScreenState`, `ui/ScreenKind.hpp`, `ui/*Screen`) picks title / world select / options / in-game / death screens.

**World data (`voxel/`, `world/`).** `voxel/` holds block and chunk data (and `BlockBoxes.hpp`, a block's collision and selection boxes) and depends only on `core` and `math`; `world/` holds `World` and the orchestration above it. `World` owns chunks; each `Chunk` is a stack of 16³ `PalettedSection`s with dynamic 0/4/8/16-bit palettes and lazily allocated light arrays. Per-voxel 4-bit metadata stores fluid level and orientation. `DifferentialLightingEngine` does BFS add/remove light propagation (bounded to 4991 voxels) plus vertical skylight extinction. Block entities (furnace, chest) live in `BlockEntityStore`.

**Chunk lifecycle.** `World::ensureChunkLoaded` loads from disk or runs pass 1 (`WorldGen::generateChunk` → `worldgen/CaveCarver`), registers with `worldgen/DecorationPipeline` (pass 2 runs once the 3×3 neighbourhood is resident), relights, schedules unsettled fluids, and dirties neighbour meshes. `world/ChunkGenPipeline` moves generation off-thread unless `--no-async-gen`.

**Meshing & rendering.** Each frame `WorldRenderer` calls `streaming::WorldMeshStreamer::update`, which captures immutable 18³ `SectionNeighborhoodSnapshot18`s of dirty sections and submits them to `AsyncChunkPipeline` (jthread pool). Workers run `SnapshotMesher`, results land in `PingPongStaging`, and the render thread uploads them under a per-frame budget (`--upload-budget-ms`). Vertices are 8-byte `PackedChunkVertex` with one shared static EBO. Dynamic lights (held/dropped torches) are tile-culled and uploaded as a std140 UBO (binding 3), injected into the chunk shader via `lighting/ShaderInclude`. GLSL sources are in `shaders/`; GL entry points are loaded by `render/GLHeaders`.

**Entities & gameplay.** `EntitySimulation` owns the sparse-set `ecs::Registry` (dropped items, mobs, projectiles). `gameplay/` holds tools, durability, harvesting, combat, health; `crafting/` holds 2×2/3×3 grids and the Beta recipe book; `ai/` voxel A*; `redstone/` wire/torches/levers. `game/WorldSession` owns one open world (World, Player, `EntitySimulation`, weather, time of day, spawn points, crafting grid) and its open/save/close lifecycle, GL-free; `Application` holds one `unique_ptr<WorldSession>`. `game/` is the glue between these modules and `Application`.

**Audio.** `AudioEngine` takes commands from game threads via a lock-free `MpscRingBuffer` and drains them in the miniaudio callback (no locks or allocations there). Sounds are synthesised procedurally. `game/AcousticsController` runs 10 Hz DDA occlusion and enclosure detection and posts `AcousticCommand`s applied by `acoustics/AcousticVoiceBank` (per-voice low-pass + shared FDN reverb bus).

**Persistence.** `storage/WorldStorage` facade over `storage/` (region files `region/r.X.Z.bcr`, 4 KiB sectors, zstd chunk bundles; legacy `chunks/*.chk|.dec|.be` are migrated to `chunks.migrated/`). `level.dat` is a `key value` text file with header `BETACRAFT_LEVEL_V2`. Textures are generated procedurally by `render/TextureAtlas` at startup and exported to `assets/terrain.png` (a custom `assets/terrain.png` overrides it); both need libpng, otherwise the generated atlas is used as is.

## Conventions

- **Inspired by, not a clone:** Minecraft Beta 1.7.3 is the default reference for any mechanic not deliberately redesigned; departures are allowed and update the pinning tests with a comment (`docs/adr/0002-beta-as-behaviour-reference.md`). Player-facing text (title, HUD, `--help`, logs) says "Betacraft" with `core::kGameTitle`, never Minecraft.
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
