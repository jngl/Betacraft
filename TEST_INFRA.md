# E2E Test Infra: BetaCraft 1.7.3 Modernization (Phase 1)

## Test Philosophy
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
- Test Runner: CTest executing GoogleTest binaries (`tests/unit/`, `tests/integration/`, `tests/e2e/`).
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

# Phase 2 Test Infra (`docs/IMPROVEMENTS_ROADMAP.md` §9.2)

## Phase 2 Test Philosophy
- Each Phase 2 module was developed test-first against its own GoogleTest executable, using only the module's public headers plus test doubles (`PipelineTestDoubles.hpp`, `FluidTestUtils.hpp`, `GridFluidVoxelAccess`, `AcousticsTestUtils.hpp`, `WorldgenTestSupport.hpp`, `GlslMiniInterpreter.hpp`).
- Beta 1.7.3 parity is pinned by explicit value tests: tool durability, weapon damage, fluid tick rates and decay, recipe shapes, and cave spec constants.
- Integration tests in `tests/integration/phase2/` drive the real `World`, `WorldSession`, `EntitySimulation`, `WorldMeshStreamer`, `AudioEngine` and renderer-side helpers headlessly.

## Phase 2 Module Suites (`tests/phase2/<key>/`)
`CMakeLists.txt` (`BETACRAFT_PHASE2_MODULES`) globs `tests/phase2/<key>/*.cpp` into `betacraft_p2_<key>_tests`, which links `betacraft_p2_<key>`, an INTERFACE target over `betacraft_core`. It registers the executable in CTest as `Phase2_<key>`.

| CTest name | Executable | Module (`src/`) | Roadmap | Sprint | Test files | Test cases |
|------------|------------|-----------------|---------|--------|:----------:|:----------:|
| `Phase2_streaming` | `betacraft_p2_streaming_tests` | `streaming` | P1.1 AsyncChunkPipeline | 2.1 | 15 | 176 |
| `Phase2_ecs` | `betacraft_p2_ecs_tests` | `ecs` | P2.1 Sparse-set ECS | 2.2 | 20 | 224 |
| `Phase2_combat` | `betacraft_p2_combat_tests` | `gameplay` | P2.5 Tools, durability, melee | 2.2 | 15 | 217 |
| `Phase2_crafting` | `betacraft_p2_crafting_tests` | `crafting` | P2.6 2x2 / 3x3 crafting | 2.2 | 9 | 141 |
| `Phase2_fluids` | `betacraft_p2_fluids_tests` | `fluids` | P2.3 Cellular fluids | 2.3 | 13 | 131 |
| `Phase2_worldgen` | `betacraft_p2_worldgen_tests` | `worldgen` | P3.3 caves + P3.4 decoration | 2.3 | 17 | 162 |
| `Phase2_dynlight` | `betacraft_p2_dynlight_tests` | `lighting` | P4.6 Dynamic light UBO | 2.4 | 10 | 158 |
| `Phase2_acoustics` | `betacraft_p2_acoustics_tests` | `acoustics` | P4.2 DDA occlusion + reverb bus | 2.4 | 14 | 265 |

Test-case counts are `TEST`/`TEST_F`/`TEST_P` macros per directory. A parameterised case can expand to more test instances at run time.

Coverage highlights per suite:
- **streaming**: section coords, 18³ snapshot capture and fast paths, `SnapshotMesher` parity with `ChunkMesher`, ping-pong staging, upload budget, chunk lifecycle, cancellation, eviction, supersession and staleness, view-cone priority, robustness and shutdown, and `World` integration.
- **ecs**: sparse set, component pool, registry and slot recycling, views, owning groups, command buffer, iteration and exception safety, systems, collision hardening, mob AI and knockback, entity budget, Beta parity, spec-name aliases.
- **combat**: item ids and stacks, tool definitions, harvesting and dig rates, block break and drops, weapon damage, melee, knockback, health, survival, death drops, item appearance, item bridge.
- **crafting**: ingredients, item ids, grid, shaped and shapeless recipes, registry, Beta recipe book (legacy and vanilla), inventory crafting screen, HUD inventory layout.
- **fluids**: `FluidState` encoding, active queue, spread, falling, infinite sources, retraction, settled detection, flow-cost pathfinding, interactions (hardening, fizz, wash-away), tick rates, budgets, determinism, grid adapter.
- **worldgen**: Simplex noise, seeded random, column sampling and context, cave envelope, carver, chunk carving and sampler, aquifers, fill palette, spec pinning, cave properties, decoration features, pipeline, hooks and shores, feature-plan codec.
- **dynlight**: attenuation, `DynamicLight`, light manager, tile culling, std140 and UBO packing, frame pipeline, shader source and GLSL contract, GLSL execution through a mini interpreter.
- **acoustics**: voxel DDA, world solidity, occlusion and its scheduler, one-pole filter, enclosure detector and monitor, FDN reverb, aux bus, voice bank, end-to-end pipeline, no allocation on the real-time path, `AudioEngine` acoustics, `AcousticsController`.

## Phase 2 Integration Suites (`tests/integration/phase2/`)
`CMakeLists.txt` globs these files into `INTEGRATION_TEST_SOURCES`, so they run inside `betacraft_integration_tests` (CTest `IntegrationTests`) and `betacraft_all_tests` (CTest `AllTests`). All fixtures are prefixed `P2` (156 test cases in 20 files, with shared helpers in `P2GameplaySupport.hpp` and `P2WorldgenSupport.hpp`).

| File | Fixtures | Scope |
|------|----------|-------|
| `test_p2_foundations_blocks.cpp`, `test_p2_foundations_items.cpp`, `test_p2_foundations_metadata.cpp` | `P2FoundationsBlocks`, `P2FoundationsItems`, `P2FoundationsMetadata` | Lava block ids, item id scheme, per-voxel fluid metadata in `Chunk`/`World`/storage |
| `test_p2_streaming_world.cpp`, `test_p2_streaming_dirty.cpp`, `test_p2_fixes_streaming.cpp` | `P2StreamingWorld`, `P2StreamingDirty`, `P2StreamingChunkDirty`, `P2FixesStreaming`, `P2FixesStreamingChunk`, `P2FixesRenderStats` | `WorldMeshStreamer` against a live `World`: dirty marking, lifecycle reporting, unload deletions, render stats |
| `test_p2_gameplay_mining.cpp`, `test_p2_gameplay_entities.cpp`, `test_p2_gameplay_crafting.cpp`, `test_p2_fixes_gameplay.cpp` | `P2GameplayMining`, `P2GameplayEntities`, `P2GameplayCrafting`, `P2FixesGameplay`, `P2FixesHardness`, `P2FixesRecipes`, `P2FixesSupport` | `EntitySimulation` mining with wear, item entities and pickup, melee on spawned mobs, inventory crafting, unsupported drops |
| `test_p2_fluids_world.cpp`, `test_p2_fluids_game.cpp` | `P2FluidsWorld`, `P2FluidsStreaming`, `P2FluidsGame`, `P2FluidsInput` | `World`-backed fluid simulation, chunk-load scheduling, game-tick events (drops, fizz), placing sources |
| `test_p2_worldgen_caves.cpp`, `test_p2_worldgen_decoration.cpp`, `test_p2_worldgen_persistence.cpp` | `P2WorldgenCaves`, `P2WorldgenDecoration`, `P2WorldgenPersistence` | `CaveCarver` in `WorldGen`, cross-border two-pass decoration, decoration-record save/load |
| `test_p2_dynlight_collect.cpp`, `test_p2_dynlight_frame.cpp`, `test_p2_dynlight_shader.cpp` | `P2DynLightCollect`, `P2DynLightFrame`, `P2DynLightShader` | `collectDynamicLights` from player and ECS item drops, frame build, injected chunk shader source |
| `test_p2_acoustics_mixing.cpp`, `test_p2_acoustics_game.cpp` | `P2AcousticsMixing`, `P2AcousticsGame`, `P2AcousticsGameWorld` | `AudioEngine` mixing through `AcousticVoiceBank`, `AcousticsController` against a `World` |

## How to Run Phase 2 Tests
- Build: `cmake --build build --parallel`
- All CTest entries (Phase 1 and Phase 2): `ctest --test-dir build --output-on-failure`
- Phase 2 module suites only: `ctest --test-dir build -R '^Phase2_' --output-on-failure`
- One module: `ctest --test-dir build -R '^Phase2_fluids$' --output-on-failure`, or run `build/betacraft_p2_fluids_tests --gtest_filter='FluidSpread*'` directly.
- Phase 2 integration tests only: `build/betacraft_integration_tests --gtest_filter='P2*'`
- Pass/fail semantics are the same as Phase 1: 100% CTest pass, zero failed assertions, exit code 0.

## ThreadSanitizer Run (streaming)
`AsyncChunkPipeline` is the only Phase 2 module that runs its own worker threads, so `Phase2_streaming` also runs under TSan in a separate Debug build directory (`build-p2-streaming-tsan`, configured with `-fsanitize=thread`):
```
cmake -S . -B build-p2-streaming-tsan -DCMAKE_BUILD_TYPE=Debug \
      -DCMAKE_CXX_FLAGS=-fsanitize=thread -DCMAKE_EXE_LINKER_FLAGS=-fsanitize=thread
cmake --build build-p2-streaming-tsan --target betacraft_p2_streaming_tests --parallel
ctest --test-dir build-p2-streaming-tsan -R '^Phase2_streaming$' --output-on-failure
```
- The streaming tests synchronise with the pipeline through `waitIdle()`, `pause()`/`resume()` and gated mesher doubles (`PipelineTestDoubles.hpp`), never through sleeps. Inline mode (`workerCount = 0` + `runPendingInline`) is used where ordering must be deterministic.
- Timing targets (capture, mesh, swap) are not asserted in Debug/TSan builds, and the capture benchmark enforces its 100 µs ceiling only under `NDEBUG`.

## Phase 2 Test Gaps
- The GL uploader (`WorldRenderer::uploadSection`) and `DynamicLightUniformBuffer` have no GPU-side tests, because a second in-process `Application` stalls (a known issue). They are covered only by the e2e binary runs and a manual stats check. The GLSL is checked through source/contract tests and the mini interpreter, not by a driver compile in CI.
- Mesh time and staging swap time are not benchmarked.
