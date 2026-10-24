# E2E Test Infra: BetaCraft 1.7.3 Modernization Spec Contracts

## Test Philosophy
- The contracts live in `tests/support/test_support/SpecContracts.hpp` (Tiers 1-3 in `tests/integration/test_integration_spec_contracts.cpp`, Tier 4 scenarios in `tests/e2e/test_e2e_spec_scenarios.cpp`).
- Opaque-box, requirement-driven. Derived from `ORIGINAL_REQUEST.md` and `docs/IMPROVEMENTS_ROADMAP.md`.
- No dependency on private implementation details; tests verify behavior via public APIs, deterministic inputs/outputs, and invariant assertions.
- Methodology: Category-Partition + Boundary Value Analysis (BVA) + Pairwise Combinatorial Testing + Workload / Scenario Testing.

## Feature Inventory & Test Matrix
| # | Feature | Source (Requirement) | Tier 1 (Feature >=5) | Tier 2 (Boundary >=5) | Tier 3 (Cross-Feature) |
|---|---------|----------------------|:--------------------:|:---------------------:|:----------------------:|
| 1 | Fiedler Fixed Timestep Physics | P1.6 / Fiche 2 | 5 | 5 | ✓ |
| 2 | PalettedSection 16x16x16 Layout | P3.1 / Fiche 2 | 5 | 5 | ✓ |
| 3 | Dynamic Palette Transitions (0/4/8/16-bit) | P3.1 / Fiche 2 | 5 | 5 | ✓ |
| 4 | Lazy DenseLightArray Allocation | P3.1 / Fiche 2 | 5 | 5 | ✓ |
| 5 | Chunk / Section Interface Conformance | P3.1 / Baseline | 5 | 5 | ✓ |
| 6 | PackedChunkVertex 8-Byte Layout | P1.2 / Fiche 1 | 5 | 5 | ✓ |
| 7 | Static Shared EBO Buffer & Indexing | P1.2 / Fiche 1 | 5 | 5 | ✓ |
| 8 | Fractal UV Tiling / Planar Mapping | P1.2 / Fiche 1 | 5 | 5 | ✓ |
| 9 | GLHeaders Dynamic Loader (`glVertexAttribIPointer`) | Baseline / Sprint 1.2 | 5 | 5 | ✓ |
| 10 | Certified BFS Differential Lighting (<=4991 voxels) | P3.2 / Fiche 2 | 5 | 5 | ✓ |
| 11 | Dual-Queue Propagation (Removal & Addition) | P3.2 / Fiche 2 | 5 | 5 | ✓ |
| 12 | 1D Skylight Vertical Raycast Extinction | P3.2 / Fiche 2 | 5 | 5 | ✓ |
| 13 | Lockless MpscRingBuffer Concurrency | P4.1 / Fiche 5 | 5 | 5 | ✓ |
| 14 | Lock-Free Audio Callback Execution | P4.1 / Fiche 5 | 5 | 5 | ✓ |
| 15 | MSDF Vector Text Rendering & Median Filter | P4.3 / Baseline | 5 | 5 | ✓ |
| 16 | HiDPI & Virtual GUI Scale Handling | P4.3 / Baseline | 5 | 5 | ✓ |
| 17 | Screen State Machine Navigation | P4.4 / Baseline | 5 | 5 | ✓ |
| 18 | Title & Options Menus Controls | P4.4 / Baseline | 5 | 5 | ✓ |
| 19 | Dynamic Key Rebinding & `options.txt` Persistence | P4.4 / Baseline | 5 | 5 | ✓ |

## Test Architecture
- Test Runner: CTest executing GoogleTest binaries: one `betacraft_<m>_tests` per module (`modules/<m>/test/`), plus the multi-module suites in `tests/integration/`, `tests/e2e/` and `tests/mock/`.
- Build Command: `cmake --build build --parallel`
- Test Execution: `ctest --test-dir build --output-on-failure`
- Pass/Fail Semantics: 100% CTest pass, zero failed assertions, clean exit code 0.

## Real-World Application Scenarios (Tier 4)
| # | Scenario | Features Exercised | Complexity |
|---|----------|--------------------|------------|
| 1 | Variable Framerate Physics Simulation (30 vs 60 vs 144 FPS) | Fiedler accumulator, Player movement, collision resolution | High |
| 2 | Heavy Voxel Modification & Dynamic Palette Upgrades/Downgrades | PalettedSection, 0->4->8->16 bit transitions, lazy light alloc/free | High |
| 3 | Rapid Torch Placement & Removal in Enclosed and Open Spaces | BFS Differential Lighting, Manhattan sphere limit <= 4991, chunk borders | High |
| 4 | Burst Audio Producer Contention (512 commands) without Glitches | MpscRingBuffer, multi-thread push, lockless drain | High |
| 5 | Complete User Journey: Launch -> Options Rebind -> InGame -> Move & Build | Screen states, options.txt persistence, MSDF text, GUI scale | High |

## Coverage Thresholds
- Tier 1: >=5 per feature
- Tier 2: >=5 per feature (where boundaries exist)
- Tier 3: pairwise coverage of major feature interactions
- Tier 4: >=5 realistic application scenarios

---

# Module and Integration Test Infra

## Test Philosophy
- Each module is developed test-first against its own GoogleTest executable, using only the module's public headers plus test doubles (`PipelineTestDoubles.hpp`, `FluidTestUtils.hpp`, `GridFluidVoxelAccess`, `AcousticsTestUtils.hpp`, `WorldgenTestSupport.hpp`, `GlslMiniInterpreter.hpp`).
- Beta 1.7.3 parity is pinned by explicit value tests: tool durability, weapon damage, fluid tick rates and decay, recipe shapes, and cave spec constants.
- Integration tests in `tests/integration/<domain>/` drive the real `World`, `WorldSession`, `EntitySimulation`, `WorldMeshStreamer`, `AudioEngine` and renderer-side helpers headlessly.

## Module Suites (`modules/<m>/test/`)
Each module (`modules/<m>`, ADR 0001) builds `modules/<m>/test/*.cpp` into `betacraft_<m>_tests`, linked against that module and its `DEPS` only, and run as CTest `Module_<m>`. Its tests that need `game`, `render` or `ui` are in `tests/integration/<domain>/`. Helpers shared by several executables (`StreamingTestHelpers.hpp`, `AcousticsTestUtils.hpp`, `AiTestSupport.hpp`, `TempDir.hpp`) are in `tests/support/test_support/`. A test that replaces a global (`operator new` in `ecs/test/test_exception_safety.cpp` and `audio/test/test_realtime_no_alloc.cpp`) is a module's `STANDALONE_TESTS`: it stays out of `betacraft_all_tests`.

| CTest name | Executable | Roadmap | Test files | Test cases |
|------------|------------|---------|:----------:|:----------:|
| `Module_streaming` | `betacraft_streaming_tests` | P1.1 AsyncChunkPipeline | 16 | 177 |
| `Module_ecs` | `betacraft_ecs_tests` | P2.1 Sparse-set ECS | 22 | 233 |
| `Module_gameplay` | `betacraft_gameplay_tests` | P2.5 Tools, durability, melee | 15 | 230 |
| `Module_crafting` | `betacraft_crafting_tests` | P2.6 2x2 / 3x3 crafting | 8 | 121 |
| `Module_fluids` | `betacraft_fluids_tests` | P2.3 Cellular fluids | 13 | 131 |
| `Module_worldgen` | `betacraft_worldgen_tests` | P3.3 caves + P3.4 decoration | 18 | 174 |
| `Module_lighting` | `betacraft_lighting_tests` | P4.6 Dynamic light UBO | 10 | 158 |
| `Module_storage` | `betacraft_storage_tests` | S7.1-S7.6 Region files, migration | 6 | 37 |
| `Module_redstone` | `betacraft_redstone_tests` | S8.5-S8.8 Redstone | 4 | 13 |
| `Module_ai` | `betacraft_ai_tests` | S8.1-S8.4 Voxel A* | 3 | 14 |
| `Module_acoustics` | `betacraft_acoustics_tests` | P4.2 DDA occlusion + reverb bus | 11 | 241 |

The other modules (`core`, `math`, `voxel`, `world`, `physics`, `audio`, `render`, `ui`) follow the same scheme. Test-case counts are `TEST`/`TEST_F`/`TEST_P` macros per directory. A parameterised case can expand to more test instances at run time.

Coverage highlights per suite:
- **streaming**: section coords, 18³ snapshot capture and fast paths, packed vertices and the shared EBO, ping-pong staging, upload budget, chunk lifecycle, `ChunkGenPipeline`, cancellation, eviction, supersession and staleness, view-cone priority, robustness and shutdown. `SnapshotMesher` parity with `ChunkMesher` and the pipeline against a `World` need `render` and run in `IntegrationTests` (`integration/streaming/`).
- **ecs**: sparse set, component pool, registry and slot recycling, views, owning groups, command buffer, iteration and exception safety, systems, collision hardening, mob AI and knockback, A* path planning and following, entity budget, Beta parity, spec-name aliases. The player proxy and projectiles need `game` (`integration/ecs/`).
- **ai**: octile heuristic, `canTraverse` for each mob size, A* on a `GridNavQuery`.
- **gameplay**: item stacks, tool definitions, harvesting and dig rates, block break and drops, weapon damage, melee, knockback, health, survival, crops, explosions, mob definitions, death drops. Armor, food, item ids, item appearance, item bridge and spawn rules need `game` or `render` and run in `IntegrationTests` (`integration/gameplay/`).
- **crafting**: ingredients, item ids, grid, shaped and shapeless recipes, registry, furnace and smelting. The Beta recipe book, inventory crafting and HUD inventory layout need `game` or `ui` (`integration/crafting/`).
- **fluids**: `FluidState` encoding, active queue, spread, falling, infinite sources, retraction, settled detection, flow-cost pathfinding, interactions (hardening, fizz, wash-away), tick rates, budgets, determinism, grid adapter.
- **worldgen**: Simplex noise, seeded random, column sampling and context, cave envelope, carver, chunk carving and sampler, aquifers, fill palette, spec pinning, cave properties, decoration features, pipeline, hooks and shores, feature-plan codec.
- **lighting**: attenuation, `DynamicLight`, light manager, tile culling, std140 and UBO packing, frame pipeline, shader source and GLSL contract, GLSL execution through a mini interpreter.
- **acoustics**: voxel DDA, world solidity, occlusion and its scheduler, one-pole filter, enclosure detector and monitor, FDN reverb, aux bus, voice bank, end-to-end pipeline. No allocation on the real-time path and `AudioEngine` acoustics are in `Module_audio`, `AcousticsController` in `IntegrationTests` (`integration/acoustics/test_acoustics_controller.cpp`).

## Integration Suites (`tests/integration/`)
`CMakeLists.txt` globs every `tests/integration/**/*.cpp` into `INTEGRATION_TEST_SOURCES`, so they run inside `betacraft_integration_tests` (CTest `IntegrationTests`) and `betacraft_all_tests` (CTest `AllTests`). A new file in any folder needs no CMake edit. Shared fixtures are in `tests/integration/support/`: `WorldgenSupport.hpp` (test seed, chunk loading), `GameplaySupport.hpp` (`GameplayWorld`: a `World` and an `EntitySimulation`), `WorldSessionSupport.hpp` (a real `WorldSession`) and `GlassFloorSupport.hpp`.

| Folder | Fixtures | Scope |
|--------|----------|-------|
| `acoustics/` | `AcousticsControllerTest`, `AcousticsMixing`, `AcousticsGame`, `AcousticsGameWorld` | `AcousticsController`, `AudioEngine` mixing through `AcousticVoiceBank`, acoustics against a `World` |
| `ai/` | `AiWorld` | Zombie pathfinding around walls on a real `World`, `--no-pathfinding` fallback |
| `crafting/` | `BetaRecipeBook`, `BetaRecipes`, `InventoryCraftingTest`, `ContainerScreenTest`, `HudInventoryLayout`, `GameplayCrafting` | Beta recipe book, inventory/furnace/chest screens, HUD inventory layout, crafting in the game |
| `dynlight/` | `DynLightCollect`, `DynLightFrameIntegration`, `DynLightShader` | `collectDynamicLights` from player and ECS item drops, frame build, injected chunk shader source |
| `lighting/` | `AdversarialDifferentialLighting`, `AdversarialMultiChunkLighting`, `AdversarialChunkLighting` | Analytical BFS bound N(15) = 4,991 on every edit, toggled and overlapping torches, chunk corners, partially loaded neighbourhoods, world limits |
| `ecs/` | `PlayerProxy`, `ProjectilesTest`, `ArrowHeading` | Player proxy targeted by mobs, arrows |
| `fluids/` | `FluidsWorld`, `FluidsStreaming`, `FluidsGame`, `FluidsInput`, `FluidPersistence` | `World`-backed fluid simulation, chunk-load scheduling, game-tick events (drops, fizz), placing sources, saving simulated water |
| `gameplay/` | `GameplayMining`, `GameplayEntities`, `SurvivalRules`, `EnvironmentDamage`, `BetaContent`, `MobsSession`, `Armor`, `Food`, `*Regressions`, … | `EntitySimulation` mining with wear, item entities and pickup, melee, survival and environmental damage, mob spawning, armor, food, item ids, TNT and buckets |
| `physics/` | `AdversarialPhysics`, `AdversarialPlayerCollision` | Determinism across frame rates and jitter, interpolation, lag spikes, walls, inner corners, sneak ledges, the void safety net |
| `redstone/` | `RedstoneWorld` | Redstone on a real `World`: levers, doors, plates, pistons, save and reload |
| `session/` | `WorldSession*`, `Progression` | `WorldSession` lifecycle, tick order, crafting, respawn, sleep, a scripted Survival progression |
| `storage/` | `ChunkBundleCodec`, `StorageRegionWorld`, `StorageLegacyMigration` | Chunk bundles, region files under `World`, migration of the frozen `tests/data/saves/v1` |
| `streaming/` | `StreamingWorld`, `StreamingDirty`, `StreamingAsyncWorld`, `PipelineWorldIntegration`, `SnapshotMesher*`, `*Regressions`, `AdversarialPackedChunkVertex`, `AdversarialSharedEBO`, `AdversarialChunkMesher` | `WorldMeshStreamer` against a live `World`: dirty marking, lifecycle reporting, unload deletions, async generation, mesher parity, render stats; packed vertex bit fields, the shared EBO past 65,536 quads, concurrent meshing |
| `ui/` | `AdversarialOptions`, `AdversarialKeyBindings`, `AdversarialMsdfText`, `AdversarialGuiScale`, `AdversarialOptionsMenu` | Corrupted, empty, overflowing and read-only `options.txt`, duplicate and cancelled bindings, MSDF text at every byte and extreme scales, GUI scale limits, options menu navigation |
| `voxel/` | `AdversarialPalettedSection`, `AdversarialChunkSections` | Palette promotion at its exact limits, nibble crosstalk, random writes against an oracle, lazy light arrays, RLE round trips and malformed buffers |
| `world/` | `FluidBlockIds`, `MetadataNibble`, `MetadataShapes`, `RandomTicks`, `FallingSand`, `BedBlock`, `DoorBlock`, `LadderBlock`, `Farming`, `BlockEntities`, `Weather*` | Block ids and metadata, metadata shapes, random and scheduled ticks, falling blocks, survival blocks, block entities, weather |
| `worldgen/` | `WorldgenCaves`, `WorldgenDecoration`, `WorldgenPersistence` | `CaveCarver` in `WorldGen`, cross-border two-pass decoration, decoration-record save/load |

## How to Run
- Build: `cmake --build build --parallel`
- All CTest entries: `ctest --test-dir build --output-on-failure`
- Module suites only: `ctest --test-dir build -R '^Module_' --output-on-failure`
- One module: `ctest --test-dir build -R '^Module_fluids$' --output-on-failure`, or run `build/betacraft_fluids_tests --gtest_filter='FluidSpread*'` directly.
- One integration domain: `build/betacraft_integration_tests --gtest_filter='Fluid*'`
- Pass/fail semantics: 100% CTest pass, zero failed assertions, exit code 0.

## ThreadSanitizer Run (streaming)
`AsyncChunkPipeline` runs its own worker threads, so `Module_streaming` also runs under TSan in a separate Debug build directory (`build-streaming-tsan`, configured with `-fsanitize=thread`):
```
cmake -S . -B build-streaming-tsan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS=-fsanitize=thread -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build build-streaming-tsan --target betacraft_streaming_tests --parallel
ctest --test-dir build-streaming-tsan -R '^Module_streaming$' --output-on-failure
# The pipeline against a live World checks parity with render::ChunkMesher, so it lives in IntegrationTests:
cmake --build build-streaming-tsan --target betacraft_integration_tests --parallel
build-streaming-tsan/betacraft_integration_tests --gtest_filter='PipelineWorldIntegration.*'
```
- The streaming tests synchronise with the pipeline through `waitIdle()`, `pause()`/`resume()` and gated mesher doubles (`PipelineTestDoubles.hpp`), never through sleeps. Inline mode (`workerCount = 0` + `runPendingInline`) is used where ordering must be deterministic.
- Timing targets (capture, mesh, swap) are not asserted in Debug/TSan builds, and the capture benchmark enforces its 100 µs ceiling only under `NDEBUG`.

## Known Test Gaps
- The GL uploader (`WorldRenderer::uploadSection`) and `DynamicLightUniformBuffer` have no GPU-side tests, because a second in-process `Application` stalls (a known issue). They are covered only by the e2e binary runs and a manual stats check. The GLSL is checked through source/contract tests and the mini interpreter, not by a driver compile in CI.
- Mesh time and staging swap time are not benchmarked.
