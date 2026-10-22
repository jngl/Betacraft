# Project: BetaCraft 1.7.3 Technical Modernization (Phase 1)

## Architecture
BetaCraft 1.7.3 is a high-performance voxel engine in C++20 and OpenGL 3.3 Core.
Phase 1 modernizes the core architectural foundations across three main pillars:
- **Simulation & Core Memory**: Fixed timestep physics with Fiedler accumulator decoupling tick rate (20 TPS / 60 TPS) from render framerate, combined with a 64-byte aligned `PalettedSection` $16 \times 16 \times 16$ sub-chunk voxel storage with dynamic 4/8/16-bit palettes and lazy `DenseLightArray` allocation.
- **Render Geometry & Lighting**: 8-byte `PackedChunkVertex` (two 32-bit words) backed by a static shared GPU index buffer (EBO) and fractal UV planar projection shader, paired with a certified BFS differential lighting engine bounded by $N(15) = 4,991$ voxels with 1D vertical skylight raycast extinction.
- **Audio Concurrency & Modern UI**: Lockless atomic `MpscRingBuffer<AudioCommand, 512>` eliminating mutex contention on the real-time audio thread, MSDF vector text rendering with dynamic HiDPI/GUI scale support, and a complete screen state machine with title screen, options menu, and persistent dynamic key rebinding (`options.txt`).

Data Flow:
1. `Application` runs the main loop: polls SDL events -> updates `InputManager` -> feeds fixed timestep accumulator -> ticks physics/world -> computes alpha interpolation -> renders active `ScreenState` (WorldRenderer or UI).
2. `World` manages chunks composed of 8 `PalettedSection` instances ($16 \times 16 \times 16$), handling dynamic palette transitions and delegating lighting updates to `DifferentialLightingEngine`.
3. `ChunkMesher` constructs compact mesh data using `PackedChunkVertex`, submitted to GPU with the static shared EBO.
4. `AudioEngine` receives commands via lock-free `MpscRingBuffer` and drains them in the real-time miniaudio callback.
5. `TextRenderer` and UI screens draw anti-aliased vector glyphs via MSDF shader.

## Feature Inventory
| # | Feature | Description | Milestone | Source |
|---|---------|-------------|-----------|--------|
| 1 | FiedlerFixedTimestep | Fixed timestep physics loop with Fiedler accumulator (60 TPS / 20 TPS), max accumulator guard (0.25s), and convex alpha interpolation (`glm::mix(prev, curr, alpha)`) for render transforms | M1 | P1.6, Fiche 2, survey_sprint11 |
| 2 | PalettedSection16 | 16x16x16 sub-chunk partitioning (8 sections per chunk column for Y=0..128), `sizeof(PalettedSection) <= 64` and `alignas(64)` L1 cache line alignment | M1 | P3.1, Fiche 2, survey_sprint11 |
| 3 | DynamicPalette | Dynamic palette bit-packing across 4 tiers: 0-bit (homogeneous single block), 4-bit (<=16 blocks), 8-bit (<=256 blocks), 16-bit (>256 blocks direct `BlockId`) | M1 | P3.1, Fiche 2, survey_sprint11 |
| 4 | LazyDenseLightArray | Lazy unified `DenseLightArray` (4096 bytes: 4-bit sky, 4-bit block) with `m_lightData = nullptr` for homogeneous sections (pure air or pure stone), zero heap allocation on uniform sections | M1 | P3.1, Fiche 2, survey_sprint11 |
| 5 | ChunkSubchunkIntegration | Seamless backward-compatible chunk public interface (`getBlock`, `setBlock`, `getSkyLight`, `getBlockLight`, `setSkyLight`, `setBlockLight`, `serializeRLE`, `deserializeRLE`) | M1 | P3.1, survey_sprint11 |
| 6 | PackedChunkVertex8B | 8-byte bit-packed vertex struct (`word0` and `word1` 32-bit words), `static_assert(sizeof(PackedChunkVertex) == 8)`, `alignas(8)`, POD/trivially copyable | M2 | P1.2, Fiche 1, survey_sprint12 |
| 7 | StaticSharedEBO | Static global shared GPU index buffer (EBO) repeating pattern `[4q, 4q+1, 4q+2, 4q, 4q+2, 4q+3]` for up to 65,536 quads, reducing VRAM per quad from 240 bytes to 32 bytes (-86.67%) | M2 | P1.2, Fiche 1, survey_sprint12 |
| 8 | FractalUVTiling | Planar orthogonal projection in vertex shader and lossless fractal repeating UV tiling `fract(v_planarPos.xy)` in fragment shader | M2 | P1.2, Fiche 1, survey_sprint12 |
| 9 | GLHeadersVertexAttribIPointer | Add `bc_glVertexAttribIPointer` declaration and dynamic loader via `SDL_GL_GetProcAddress` in `GLHeaders.hpp/cpp` | M2 | survey_sprint12 |
| 10 | CertifiedBFSDifferentialLighting | Bidirectional differential BFS lighting engine with analytical sphere of Manhattan $B_1(15)$ bounding worst-case open-air exploration to exactly $N(15) = 4,991$ voxels | M2 | P3.2, Fiche 2, survey_sprint12 |
| 11 | DualQueueLightEngine | `LightRemovalQueue` (cascade extinction & independent source reinjection) and `LightAdditionQueue` (attenuation-based wave propagation) | M2 | P3.2, Fiche 2, survey_sprint12 |
| 12 | Skylight1DRaycastExtinction | 3-pass hybrid skylight extinction with 1D vertical raycast ($O(\Delta H) < 0.5\ \mu\text{s}$), perimeter boundary queueing, and bounded BFS | M2 | P3.2, Fiche 2, survey_sprint12 |
| 13 | LocklessMpscRingBuffer | Lockless bounded Multi-Producer Single-Consumer circular queue (`MpscRingBuffer<AudioCommand, 512>`) with atomic sequence counters, 64-byte cache alignment, no `std::mutex`, power-of-2 capacity assertion | M3 | P4.1, Fiche 5, survey_sprint13 |
| 14 | LockFreeAudioCallback | Non-blocking miniaudio real-time callback consuming from `MpscRingBuffer` with zero mutex acquisition and zero dynamic allocations | M3 | P4.1, Fiche 5, survey_sprint13 |
| 15 | MSDFVectorTextRendering | Multi-channel Signed Distance Field font rendering with median filter GLSL fragment shader, generating 1 quad per glyph (>96% vertex reduction over legacy bitmap font) | M3 | P4.3, survey_sprint13 |
| 16 | HiDPIGUIScaleVirtualCanvas | Resolution-independent virtual GUI scale canvas (Auto, 1x, 2x, 3x, 4x) for HUD and menus adapting dynamically to window resize without blur | M3 | P4.3, survey_sprint13 |
| 17 | ScreenStateMachine | Screen state machine with `ScreenState` enum/states (`TitleScreen`, `InGame`, `PauseMenu`, `OptionsMenu`, `KeybindingMenu`) | M3 | P4.4, survey_sprint13 |
| 18 | ClassicTitleAndOptionsMenu | Title screen with Singleplayer/Options/Quit, pause menu, and options sliders/toggles (FOV, audio, render distance, GUI scale) | M3 | P4.4, survey_sprint13 |
| 19 | DynamicKeyRebinding | `InputManager` action abstraction (`GameAction`), interactive click-to-listen key rebinding UI, and persistent configuration in `options.txt` | M3 | P4.4, survey_sprint13 |
| 20 | E2ETestHarnessAndInfra | Comprehensive opaque-box test runner covering Tiers 1-4 for all Phase 1 features | E2E | Dual Track requirement |

## Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| E2E | E2E Testing Track | Design and implement opaque-box test suite across Tiers 1-4, publish `TEST_READY.md` | none | DONE |
| M1 | Sprint 1.1: Physics & PalettedSection | Fixed timestep physics (Fiedler 60 TPS / 20 TPS) and 64B PalettedSection with 4/8/16-bit dynamic palette and lazy light array | none | DONE |
| M2 | Sprint 1.2: PackedChunkVertex & BFS Lighting | 8-byte PackedChunkVertex, shared static EBO, fractal UV tiling, certified BFS differential lighting ($N(15) = 4,991$) | M1 | DONE |
| M3 | Sprint 1.3: Audio MPSC, MSDF & Menus | Lockless atomic MpscRingBuffer, MSDF vector text rendering with HiDPI/GUI scale, screen state machine & options menu with persistent key rebinding | none | DONE |
| M4 | Final Milestone: E2E Test Pass & Hardening | Pass 100% of E2E test suite (Tiers 1-4) and execute Tier 5 adversarial coverage hardening | E2E, M1, M2, M3 | IN_PROGRESS |

## Interface Contracts

### M1 ↔ Application / Player
- `void Application::update(float dt)`: Uses `m_accumulator += dt; while (m_accumulator >= FIXED_DT) { fixedUpdate(FIXED_DT); m_accumulator -= FIXED_DT; }`.
- `void Player::fixedUpdate(float dt, const PlayerInput& input, const world::World& world)`: Deterministic physics integration.
- `glm::vec3 Player::getInterpolatedPosition(float alpha) const`: Returns `glm::mix(m_previousPosition, m_position, alpha)`.

### M1 ↔ Chunk / PalettedSection
- `PalettedSection`: `sizeof(PalettedSection) <= 64`, `alignas(64)`.
- `BlockId PalettedSection::getBlock(int x, int y, int z) const` (x, y, z in [0..15]).
- `void PalettedSection::setBlock(int x, int y, int z, BlockId id)`.
- `uint8_t PalettedSection::getSkyLight(int x, int y, int z) const`.
- `uint8_t PalettedSection::getBlockLight(int x, int y, int z) const`.
- `void PalettedSection::setSkyLight(int x, int y, int z, uint8_t level)`.
- `void PalettedSection::setBlockLight(int x, int y, int z, uint8_t level)`.
- `Chunk`: Contains `std::array<PalettedSection, 8> m_sections`.

### M2 ↔ ChunkMesher / PackedChunkVertex
- `struct alignas(8) PackedChunkVertex { uint32_t word0; uint32_t word1; };`
- `static_assert(sizeof(PackedChunkVertex) == 8);`
- `static uint32_t packWord0(uint32_t x, uint32_t y, uint32_t z, uint32_t norm, uint32_t ao, uint32_t uvCorner, uint32_t matLayer);`
- `static uint32_t packWord1(uint32_t sky, uint32_t block, uint32_t tint, uint32_t anim, uint32_t sway, uint32_t trans, uint32_t reserved);`
- `SharedEBO`: Global `GLuint` buffer loaded once with index pattern `[0, 1, 2, 0, 2, 3]`.

### M2 ↔ DifferentialLightingEngine
- `void DifferentialLightingEngine::setBlockAndUpdateLighting(world::World& world, int wx, int wy, int wz, voxel::BlockId newBlock);`
- `void DifferentialLightingEngine::updateLightSource(world::World& world, int wx, int wy, int wz, uint8_t newEmission);`
- Analytical bound: `max_voxels <= 4991`.

### M3 ↔ AudioEngine
- `template<typename T, size_t Capacity = 512> class MpscRingBuffer;`
- `bool AudioEngine::postCommand(const AudioCommand& cmd) noexcept;`
- Callback mixes audio frames consuming commands without `std::mutex`.

### M3 ↔ UI & Input
- `enum class ScreenState { TitleScreen, InGame, PauseMenu, OptionsMenu, KeybindingMenu };`
- `class InputManager`: Maps `GameAction` to `SDL_Scancode`, loaded/saved from `options.txt`.
- `class TextRenderer`: MSDF shader and atlas rendering text quads.

## Code Layout
- `modules/<m>/{include/<m>,src,test}`: extracted modules, one static library and test executable each (ADR 0001). `modules/core/`: engine configuration, coordinate types, `Logger`, `MpscRingBuffer.hpp`. `modules/math/`: `AABB`, `Frustum`, `PerlinNoise`. `modules/voxel/`: block and chunk data (`Block`, `BlockShapes.hpp`, `BlockBoxes.hpp`, `BlockEntity.hpp`, `BlockEntityStore`, `PalettedSection`, `Chunk`, `ScheduledTicks`, `SectionCoord.hpp`). Above it, `modules/gameplay/`, `fluids/`, `redstone/`, `lighting/`, then `worldgen/` (needs `gameplay`) and `storage/` (needs `worldgen`); each `readme.md` lists its entry points.
- `src/physics/`: Player physics, bounding boxes, fixed timestep integration.
- `src/world/`: the orchestrator above `worldgen`, `storage`, `fluids`, `redstone` and `gameplay`: `World`, `WorldGen`, `BlockTicks`, `Weather`, `DifferentialLightingEngine`, `ChunkGenPipeline`.
- `src/streaming/`: GL-free asynchronous meshing: `PackedChunkVertex.hpp/cpp`, `BlockShapeMesher.hpp`, `SnapshotMesher`, `AsyncChunkPipeline`, `WorldMeshStreamer`.
- `src/render/`: `ChunkMesher.hpp/cpp`, `WorldRenderer.hpp/cpp`, `TextRenderer.hpp/cpp`, `GLHeaders.hpp/cpp`.
- `src/audio/`: `AudioEngine.hpp/cpp`.
- `src/ui/`: `ScreenState.hpp`, `TitleScreen.hpp/cpp`, `OptionsMenu.hpp/cpp`, `InputManager.hpp/cpp`.
- `shaders/`: `chunk_vertex.glsl`, `chunk_fragment.glsl`, `msdf_vertex.glsl`, `msdf_fragment.glsl`.
- `tests/unit/`: Dedicated GoogleTest unit test suites (`test_physics_fixed_step.cpp`, `test_paletted_section.cpp`, `test_packed_chunk_vertex.cpp`, `test_shared_ebo.cpp`, `test_differential_lighting.cpp`, `test_msdf_font.cpp`, `test_input_rebind.cpp`).
- `tests/e2e/`: Opaque-box acceptance test suites.

---

# Phase 2: Engine Evolutions & Major Systems (`docs/IMPROVEMENTS_ROADMAP.md` §9.2)

## Phase 2 Architecture
Phase 2 adds eight modules under `src/` (`streaming`, `ecs`, `gameplay`, `crafting`, `fluids`, `worldgen`, `lighting`, `acoustics`). `CMakeLists.txt` (`BETACRAFT_PHASE2_DIRS`) compiles them into `betacraft_core`, so `World`, `Application`, the renderers and `AudioEngine` use them directly. Game-side glue lives in `src/game/` (`WorldSession`, `EntitySimulation`, `InventoryCrafting`, `DynamicLightCollector`, `AcousticsController`, `ItemBridge.hpp`, `FluidEffects.hpp`) and `src/render/ItemAppearance.*`.
- **Async World Streaming (Sprint 2.1, P1.1)**: `AsyncChunkPipeline` runs section meshing on a `std::jthread` pool (N-1 cores, at least one worker). Each job reads an immutable `SectionNeighborhoodSnapshot18` (18³ voxels) captured on the main thread. Finished meshes go to a `PingPongStaging` double buffer, and the render thread uploads them under a time budget (`drainUploads`, `Config::meshUploadBudgetMs = 4.0`). `WorldMeshStreamer` connects the live `World` to the pipeline. Terrain generation, lighting and decoration still run synchronously on the main thread.
- **ECS & Survival (Sprint 2.2, P2.1/P2.5/P2.6)**: The sparse-set `ecs::Registry` has views, owning groups and a `CommandBuffer`. `EntitySimulation` (formerly `GameSession`) owns the registry, which holds dropped items and, in tests only, mobs. `gameplay` adds tools with durability, harvest levels, Beta hold-to-mine (`MiningProgress`), melee damage, knockback and health. `crafting` adds 2x2/3x3 grids, shaped/shapeless matching and a Beta recipe book (`makeBetaRecipeBook`).
- **World Simulation & Geology (Sprint 2.3, P2.3/P3.3/P3.4)**: `FluidSimulator` is a Beta-style cellular automaton for water and lava, owned by `World` behind an `IFluidVoxelAccess` adapter. `World` keeps fluid levels in a per-voxel 4-bit metadata nibble. `CaveCarver` carves 3D Simplex caves with the vertical envelope `A(y)`, aquifers and a lava band at Y <= 10, called from `WorldGen::generateChunk`. `DecorationPipeline` places trees and flora in a second pass once a chunk's 3x3 neighbourhood is resident, and `WorldStorage` persists a per-chunk decoration record.
- **Ambience & Dynamic Lighting (Sprint 2.4, P4.6/P4.2)**: Held and dropped light emitters (up to `kMaxDynamicLights = 16`) are culled into a 16x9 screen-tile grid. They are uploaded through a std140 UBO (`DynamicLightBlock`, binding 3), and the chunk fragment shader evaluates them through `injectDynamicLights(PACKED_CHUNK_FS)`. Acoustic occlusion uses voxel DDA ray-marching on a 10 Hz `OcclusionScheduler`, plus per-voice low-pass filters. Enclosure detection drives a shared FDN reverb on an auxiliary bus (`AuxReverbBus`). `AcousticVoiceBank` in `AudioEngine` applies both.

Phase 2 Data Flow:
1. `World::ensureChunkLoaded` loads a chunk from disk or generates it with pass 1 (`WorldGen::generateChunk`, density, surface, ores, then `CaveCarver::carveChunk`). It registers the chunk with `DecorationPipeline` (`onTerrainReady` / `on(Un)DecoratedChunkLoaded`) and relights each chunk that becomes decorated. It then schedules unsettled fluids (`scheduleUnsettledFluids`) and marks the neighbouring meshes dirty.
2. On each frame, `WorldRenderer` calls `WorldMeshStreamer::update(world, eye, look)`. It reports `TerrainGen`/`Lit` on chunk arrival and `Decorated` after pass 2, captures 18³ snapshots of dirty sections and submits them to `AsyncChunkPipeline`. Workers run `SnapshotMesher` and stage the results. `drainUploads(budget, uploadSection)` then uploads them, and an empty buffer deletes a section's GPU mesh.
3. On each 20 TPS game tick, `Application` runs `WorldSession::tick`: `EntitySimulation::tick` (attack/mining, entity systems, item pickup, block entities), then the world phase with `World::tick()` (fluids), then the World queues, where `takeFluidEvents()` goes to `EntitySimulation::spawnFluidDrops` (washed-away blocks). The fluid events come back in `WorldTickEvents`, and `Application` passes them to `loudestFizzSound` (fizz audio).
4. On each frame, `collectDynamicLights(manager, player, &simulation.entities(), alpha)` gathers emitters. `DynamicLightFrame::build` culls them and packs the UBO bytes, and `DynamicLightUniformBuffer` uploads them before the chunk draws.
5. On each frame, `AcousticsController::update(now, eye, query)` runs the occlusion scheduler and enclosure monitor on the game thread. It posts `AcousticCommand`s through `AudioEngine`'s MPSC queue, and the audio callback applies them through `AcousticVoiceBank::processBlock`.

## Phase 2 Feature Inventory
| # | Feature | Description | Milestone | Source | Status |
|---|---------|-------------|-----------|--------|--------|
| 21 | AsyncChunkPipeline | `std::jthread` meshing pool with a bounded priority queue (`maxQueuedJobs` 4096, distance and view-cone ordering, eviction reports), per-section version/ticket staleness checks, cooperative `stop_token` cancellation, inline mode (`workerCount = 0`) | Sprint 2.1 | P1.1, Fiche 1 | DONE |
| 22 | SectionNeighborhoodSnapshot18 | Immutable 18³ block/light capture (`alignas(64)`) taken on the main thread, with fast paths for contiguous runs | Sprint 2.1 | P1.1, Fiche 1 | DONE |
| 23 | PingPongStaging & UploadBudget | Mutex-guarded back buffer with an O(1) swap to the render-thread front buffer. `drainUploads` respects a time budget and always uploads at least one item per call | Sprint 2.1 | P1.1, Fiche 1 | DONE |
| 24 | ChunkLifecycle | `Unloaded -> TerrainGen -> Lit -> Decorated -> Meshed -> Uploaded` state machine (`canTransition`, `ChunkLifecycleTracker`) that gates meshing (`RejectedNotReady`) | Sprint 2.1 | P1.1 | PARTIAL (only meshing runs on workers; the other stages are reported after `World` finishes them synchronously) |
| 25 | WorldMeshStreamer | Main-thread glue between `World` dirty bits and the pipeline. Used by `WorldRenderer`, which no longer has a synchronous meshing path | Sprint 2.1 | P1.1 | DONE |
| 26 | SparseSetECS | `ecs::Registry` (24-bit index / 8-bit generation ids, FIFO slot recycling, entity cap), `ComponentPool`, `View`, owning `Group`, `CommandBuffer` for deferred structural changes | Sprint 2.2 | P2.1, Fiche 3 | DONE |
| 27 | ECSSystems | `MovementPhysicsSystem`, `VoxelCollisionSystem` (swept AABB), `HealthSystem`, `MobAISystem` (straight-line steering), `ItemDropSystem`, `EntityRenderInterpolationSystem`, `PlayerControlSystem`, `EntityFactory` | Sprint 2.2 | P2.1 | PARTIAL (the player is not an entity, `PlayerControlSystem` is not run in game, no mobs spawn) |
| 28 | ToolsDurabilityHarvesting | `gameplay::ItemId` (16-bit) shared item ids, `ToolDef`/`materialStats` (Beta durability 59/131/250/32/1561), `harvestInfo`/`canHarvest`/`computeDig`, `MiningProgress` with crack stages, `breakBlockWith` with drops and wear | Sprint 2.2 | P2.5 | DONE |
| 29 | MeleeCombat & Health | `weaponDamage` (Beta 1.7.3 values), `performMeleeAttack`, `pickMeleeTarget`, knockback, hurt window, corpse expiry, `fallDamage` | Sprint 2.2 | P2.5 | DONE (no mobs in the live game) |
| 30 | ItemEntities | Dropped items as ECS entities, rendered as spinning quarter-size cubes or cards (`render::ItemEntityInstance`). Picked up by the player, and they glow if their item emits light | Sprint 2.2 | P2.1/P2.5 | DONE |
| 31 | CraftingGrids | `CraftingGrid` (`Inventory2x2`, `Workbench3x3`), `ShapedRecipe` (mirroring, any offset), `ShapelessRecipe`, `RecipeRegistry::match/craft` with remainders/byproducts, `makeBetaRecipeBook` (legacy-preserving default, `BetaRecipeOptions::vanilla()`), `game::InventoryCrafting` screen | Sprint 2.2 | P2.6 | DONE |
| 32 | WidenedInventory | `physics::InventorySlot` holds `gameplay::ItemId` + count + wear, so tools at 256 and above and glowstone dust (348) can be held. `render::ItemAppearance` provides names and icons | Sprint 2.2 | P2.5/P2.6 | DONE |
| 33 | CellularFluids | `FluidSimulator`: Beta tick rates (water 5, lava 30), decay steps, flow-cost search (depth 4), infinite sources, falling fluid, retraction, lava/water hardening (obsidian/cobblestone/stone), per-tick update and search budgets, deterministic seeded lava thinning, `FluidEvent` (BlockWashedAway, Fizz) | Sprint 2.3 | P2.3, Fiche 4 | DONE |
| 34 | FluidWorldIntegration | `BlockId::Lava`/`StillLava` (10/11), per-voxel metadata nibble in `Chunk`/`World`, `World::FluidAccess` adapter, `World::tick()` in the game tick, load-time scheduling of unsettled fluid, washed-away drops, fizz sound | Sprint 2.3 | P2.3 | DONE (see deferred list: flat rendering, buckets) |
| 35 | SimplexCaves | `CaveCarver`: spaghetti and cheese caves (cheese only below Y=50), vertical envelope `A(y)` (0 at Y <= 5 for bedrock, fade below the surface and sea floor), aquifers at Y 11..32 with a rock seal, lava fill at Y <= 10. Replaces the legacy tube caves in `WorldGen` | Sprint 2.3 | P3.3, Fiche 4 | DONE |
| 36 | TwoPassDecoration | `DecorationPipeline`: deterministic per-chunk `FeaturePlan` (pass 1) applied once all 9 chunks of the 3x3 are resident (pass 2), so features cross chunk borders. Plans are retained on unload, and `FeaturePlanCodec`/`WorldStorage` persist the decoration record | Sprint 2.3 | P3.4, Fiche 4 | DONE |
| 37 | DynamicLightUBO | `DynamicLight`, `DynamicLightManager` (held item, dropped items, selection by distance/frustum), `TileLightCuller` (16x9 tiles), std140 `DynamicLightUbo` packing, `DynamicLightUniformBuffer`, GLSL `computeDynamicLight` injected into the chunk shader, first-person hand light | Sprint 2.4 | P4.6, Fiche 5 | DONE |
| 38 | VoxelAcousticOcclusion | `VoxelDda` ray-march, `measureOcclusion` / `occlusionTargetFor` / `computeVoxelOcclusion` (LPF cutoff in Hz), 10 Hz `OcclusionScheduler` (32 sources, interpolated targets), `VoiceAcousticFilter` (one-pole LPF + gain) | Sprint 2.4 | P4.2, Fiche 5 | DONE |
| 39 | SharedReverbBus | `EnclosureDetector`/`EnclosureMonitor` (teleport-aware), `FdnReverb` on one `AuxReverbBus` shared by all voices, `AcousticVoiceBank` in the `AudioEngine` callback, `AcousticsController` in `Application` | Sprint 2.4 | P4.2, Fiche 5 | DONE |
| 40 | Phase2TestSuites | Per-module GoogleTest executables `Phase2_<key>` and `tests/integration/phase2/` (see `TEST_INFRA.md`) | Sprint 2.x | Dual Track requirement | DONE |

## Phase 2 Milestones
| # | Name | Scope | Dependencies | Status |
|---|------|-------|-------------|--------|
| S2.1 | Sprint 2.1: Async World Streaming | `AsyncChunkPipeline`, 18³ snapshots, ping-pong staging, budgeted GPU upload, `WorldMeshStreamer` | M2 | DONE (meshing only on workers) |
| S2.2 | Sprint 2.2: ECS & Complete Survival | Sparse-set ECS with owning groups, tools/durability/harvest levels/melee, 2x2 and 3x3 crafting | M1 | DONE (player not migrated to ECS, no mobs spawned) |
| S2.3 | Sprint 2.3: World Simulation & Geology | Water/lava cellular automaton, Simplex 3D caves with `A(y)`, two-pass decoration | M1, M2 | DONE |
| S2.4 | Sprint 2.4: Ambience & Dynamic Lighting | Forward+ UBO held-torch lighting, DDA acoustic occlusion and shared reverb bus | M2, M3 | DONE |

## Phase 2 Interface Contracts

### Sprint 2.1 ↔ AsyncChunkPipeline (`src/streaming/AsyncChunkPipeline.hpp`)
- `explicit AsyncChunkPipeline(PipelineConfig config = {});` with `PipelineConfig{ workerCount = N-1 (0 = inline), maxQueuedJobs = 4096, meshOptions, mesher, clock, enforceChunkLifecycle = true, authoritativeVersion, viewConeCos = 0.5, outOfViewDistanceScale = 2, nearRadiusBlocks = 32 }`.
- `SubmitResult requestSectionMesh(SectionCoord, std::shared_ptr<const SectionNeighborhoodSnapshot18>, uint64_t version, TaskPriority = HighNear);` and `requestSectionMeshFromWorld(const world::World&, SectionCoord, uint64_t version, TaskPriority = HighNear);`. `SubmitResult` is one of `Accepted`, `Replaced`, `AcceptedEvictedOther`, `RejectedQueueFull`, `RejectedStale`, `RejectedInvalid`, `RejectedNotReady` or `RejectedShutdown`.
- `void setViewer(const glm::vec3& pos, const glm::vec3& forward);`, `void noteSectionVersion(SectionCoord, uint64_t);`, `void cancelSection(SectionCoord);`, `void cancelChunk(core::ChunkCoord);`, `std::vector<SectionCoord> takeEvictedSections();`.
- `bool advanceChunkStage(core::ChunkCoord, ChunkStage to);`: accepts only the strict next stage. `Meshed`/`Uploaded` are derived by the pipeline.
- `DrainStats drainUploads(std::chrono::nanoseconds budget, const MeshUploader&);`: render thread only, one consumer. `MeshUploader = std::function<void(ChunkMeshResult&)>`, and empty `buffers` means delete the section's mesh.
- `size_t runPendingInline(size_t);`, `bool waitIdle(std::chrono::nanoseconds);`, `void shutdown();` (idempotent, joins workers, also run by the destructor). `PipelineStats stats() const;`.
- Threading: submit/cancel/viewer calls are internally synchronised. Workers read only snapshots. The pipeline never reads the live `World` on its own.

### Sprint 2.1 ↔ WorldMeshStreamer (`src/streaming/WorldMeshStreamer.hpp`)
- `void update(world::World&, const glm::vec3& viewerPos, const glm::vec3& viewerForward);`: main thread, once per frame, after chunk streaming and edits. Clears the dirty bits it submitted.
- `DrainStats drainUploads(std::chrono::nanoseconds budget, const MeshUploader&);`, `void setSmoothLighting(bool);` (rebuilds the pipeline), `void shutdown();` (must run before the `World` and GL context are destroyed).

### Sprint 2.2 ↔ ECS (`src/ecs/`)
- `explicit Registry(uint32_t maxEntities = MAX_ENTITIES, uint32_t recycleThreshold = 1024) noexcept;`: `create()` returns `NULL_ENTITY` only at the cap, and `destroy`, `valid`, `alive`, `slotCount` are available.
- `emplaceOrReplace<T>`, `get<T>`, `tryGet<T>`, `remove<T>`, `has<Ts...>`, `view<Ts...>()`, `group<Ts...>()`: a component type can be owned by only one group (`std::logic_error` otherwise).
- Structural changes are not allowed inside `View::each` / `Group::each`. Record them in `ecs::CommandBuffer` and call `flush(reg)` afterwards.
- Components: `TransformComp`, `VelocityComp`, `HealthComp`, `AABBColliderComp`, `ItemDropComp`, `MobAIComp`, `PlayerInputComp`, `PlayerTagComp`. Teleports must use `ecs::teleport(TransformComp&, pos)`.
- Systems: `MovementPhysicsSystem::update(reg, dt)`, `VoxelCollisionSystem::update(reg, world)`, `HealthSystem::applyDamage(reg, e, amount, knockback)` / `update(reg, dt) -> dead list`, `MobAISystem::update(reg, dt)`, `ItemDropSystem`, `EntityRenderInterpolationSystem::collect(reg, alpha, out)`, `PlayerControlSystem::update(reg, dt)`. `EntityFactory::spawnPlayer / spawnItemDrop / spawnMob`. Roadmap-name aliases also exist, for example `CollisionResolutionSystem` and `CombatDamageSystem`.

### Sprint 2.2 ↔ Gameplay (`modules/gameplay/`)
- `BlockHarvestInfo harvestInfo(voxel::BlockId);`, `bool canHarvest(const BlockHarvestInfo&, const ItemStack&);`, `DigResult computeDig(voxel::BlockId, const ItemStack& held, DigConditions);`.
- `MiningProgress::click(target, dig)` / `advance(target, dig)` / `reset()` / `crackStage()` (-1 or 0..9).
- `BlockBreakOutcome breakBlockWith(voxel::BlockId, ItemStack& held, uint64_t dropSeed = 0);`: drop, wear and `meltsIntoWater`. A tool breaks when `damage > maxDamage` (maxDamage + 1 uses, as in Beta).
- `int32_t weaponDamage(const ItemStack&);`, `MeleeResult performMeleeAttack(const AttackerState&, ItemStack& weapon, TargetState&, ...);`, `std::optional<TargetHit> pickMeleeTarget(eye, look, std::span<const math::AABB>, float maxDistance);`.
- `gameplay::ItemId` (`uint16_t`): `toItemId(BlockId)`, `canonicalize`, `toVanillaId`, `toolItemId(kind, material)`, `toolInfo`, `maxStackSize`.

### Sprint 2.2 ↔ Crafting (`src/crafting/`)
- `CraftingGrid{GridSize::Inventory2x2 | GridSize::Workbench3x3}` with `get/at/set`, `bounds()`, `isEmpty()`.
- `std::optional<CraftMatch> RecipeRegistry::match(const CraftingGrid&) const noexcept;` and `std::optional<CraftOutcome> RecipeRegistry::craft(CraftingGrid&) const noexcept;` (consumes inputs, returns result plus byproducts).
- `RecipeRegistry makeBetaRecipeBook(const BetaRecipeOptions& = {});`. `BetaRecipeOptions::vanilla()` gives a strict Beta 1.7.3 book.
- `crafting::isPlaceableBlock(ItemId)` decides whether an item can be placed. `maxDurability` mirrors `gameplay::materialStats`.

### Sprint 2.3 ↔ Fluids (`modules/fluids/`)
- `class IFluidVoxelAccess { isLoaded, getBlock, getFluid, setFluid, setBlock, blocksFlow }`: unloaded cells act as walls. Nibble 0 is never decoded as dry, and the fluid type comes from the block id.
- `FluidState{ type, level 0..7 (7 = source), fallingFlag }` with `toNibble/fromNibble` and `betaDecay/fromBetaDecay`. `World` stores Beta's native metadata (0 = source, 1..7 flowing, 8 = falling).
- `explicit FluidSimulator(IFluidVoxelAccess&, FluidConfig = {}) noexcept;` with `FluidTickStats tick();`, `placeSource`, `removeFluid`, `notifyBlockChanged`, `notifyRegionLoaded(min, max, wakeInterior)`, `scheduleUpdate`, `isSettled`, `takeEvents()`.
- `World` facade: `FluidTickStats World::tick();`, `std::vector<FluidEvent> World::takeFluidEvents();`, `FluidState World::getFluid(BlockPos) const;`. `World` configures `maxUpdatesPerTick = 256` and `randomSeed = world seed`.

### Sprint 2.3 ↔ Worldgen (`modules/worldgen/`)
- `explicit CaveCarver(uint64_t seed);`, `CarveStats carveChunk(voxel::Chunk&, const ColumnSampler&, ...);` (pass 1, after surface and ores), `static ColumnContext scanColumn(const voxel::Chunk&, lx, lz);`, `static constexpr float computeVerticalEnvelope(int32_t wy, const ColumnContext&);`. Constants: `BEDROCK_GUARD_MAX_Y = 5`, `CHEESE_MAX_Y = 50`, `LAVA_MAX_Y = 10`, aquifers Y 11..32, `AQUIFER_THRESHOLD = 0.42`.
- `DecorationPipeline(uint64_t worldSeed, ChunkAccess&)`: `onTerrainReady(coord)`, `onUndecoratedChunkLoaded(coord, plan)`, `onDecoratedChunkLoaded(coord, plan)`, `onChunkUnloaded(coord, PlanRetention)`, `isDecorated`, `findPlan`. Every returned coord needs a relight and remesh.
- `WorldStorage::saveDecorationRecord / loadDecorationRecord(core::ChunkCoord)` with `worldgen::DecorationRecord` (`FeaturePlanCodec`).

### Sprint 2.4 ↔ Dynamic Lighting (`modules/lighting/`, `src/game/DynamicLightCollector.hpp`)
- `DynamicLightManager::clear()`, `submit(const DynamicLight&)`, `submitHeldItem(eyePos, lookDir, BlockId)`, `submitDroppedItem(pos, BlockId)`. Selection is capped at `kMaxDynamicLights = 16`.
- `void DynamicLightFrame::build(const DynamicLightManager&, const glm::vec3& cameraPos, const glm::mat4& viewProj, viewportPx);`: `viewportPx` is the drawable (framebuffer) size. Also `uboBytes()`, `blockLightLevelAt(worldPos)`.
- `DynamicLightUniformBuffer::initialize()`, `static bindProgram(GLuint)` (block `DynamicLightBlock`, binding point 3), `upload(bytes)`, `bindBase()`, `destroy()`.
- `std::string lighting::injectDynamicLights(std::string_view fragmentShader);`: the GLSL entry point is `computeDynamicLight(v_worldPos, gl_FragCoord.xy)`.
- `void game::collectDynamicLights(DynamicLightManager&, const physics::Player&, const ecs::Registry*, float alpha);`.

### Sprint 2.4 ↔ Acoustics (`src/acoustics/`, `src/game/AcousticsController.hpp`)
- `OcclusionSample measureOcclusion(const VoxelSolidityQuery&, listener, source);`, `OcclusionTarget occlusionTargetFor(const OcclusionSample&, const OcclusionParams&);`, `float computeVoxelOcclusion(const world::World&, listener, soundPos);` (LPF cutoff in Hz).
- `OcclusionScheduler` (`CAPACITY = 32`): `addSource(handle, pos)`, `setSourcePosition`, `removeSource`, `update(now, listener, query) -> std::span<const OcclusionUpdate>`.
- `EnclosureMonitor::update(now, listener, query) -> std::optional<EnvironmentUpdate>`, `reset()`. It detects teleports above 16 blocks.
- `AcousticVoiceBank` (32 voices, inside `AudioEngine`): `apply(const AcousticCommand&)`, `startVoice`/`stopVoice`, `processVoiceSample`, `processBlock`. `AuxReverbBus` has `MAX_BLOCK_FRAMES = 512`.
- `AcousticsController::update(now, listener, query)`, `acousticsAt(source, ...) -> std::optional<audio::VoiceAcoustics>` (`occlusionGain`, `lpfCutoffHz`, `reverbSend`), `reset()`. World-backed queries run on the game thread only.

## Phase 2 Code Layout
- `src/streaming/`: `AsyncChunkPipeline`, `ChunkLifecycle.hpp`, `PingPongStaging.hpp`, `UploadBudget.hpp`, `SectionNeighborhoodSnapshot18`, `SnapshotMesher`, `WorldMeshStreamer`.
- `src/ecs/`: `Entity.hpp`, `SparseSet`, `ComponentPool.hpp`, `Registry`, `View.hpp`, `Group`, `CommandBuffer.hpp`, `Components.hpp`, `Systems`.
- `modules/gameplay/`: `ItemIds.hpp`, `ItemStack`, `ToolDefinitions`, `Harvesting`, `Combat`, `MeleeCombat`, `Knockback`, `Health`, `Survival`, `DeathDrops`, `SplitMix64.hpp`.
- `src/crafting/`: `ItemIds.hpp`, `Ingredient.hpp`, `RecipeTypes.hpp`, `CraftingGrid`, `ShapedRecipe`, `ShapelessRecipe`, `RecipeRegistry`, `BetaRecipes`.
- `modules/fluids/`: `FluidState.hpp`, `FluidVoxelAccess.hpp`, `GridFluidVoxelAccess`, `ActiveFluidQueue`, `FluidSimulator`.
- `modules/worldgen/`: `SimplexNoise`, `SeededRandom.hpp`, `ColumnSampling`, `CaveCarver`, `DecorationFeatures`, `DecorationPipeline`, `FeaturePlanCodec`.
- `modules/lighting/`: `DynamicLight`, `DynamicLightManager`, `TileLightCuller`, `Std140.hpp`, `DynamicLightUbo`, `DynamicLightFrame`, `DynamicLightsGlsl`, `ShaderInclude` (`DynamicLightUniformBuffer` is in `src/render/`).
- `src/acoustics/`: `VoxelDda.hpp`, `AcousticOcclusion`, `WorldSolidity`, `OcclusionScheduler`, `OnePoleFilter`, `VoiceAcousticFilter`, `EnclosureDetector`, `EnclosureMonitor`, `FdnReverb`, `AuxReverbBus`, `AcousticVoiceBank`, `AcousticCommand.hpp`.
- `src/game/`: `WorldSession`, `EntitySimulation`, `InventoryCrafting`, `DynamicLightCollector`, `AcousticsController`, `ItemBridge.hpp`, `FluidEffects.hpp`.

## Phase 2 Known Deferred Items
Gathered from the module integration hooks and the integration steps, then checked against the current code.

### Streaming (Sprint 2.1)
- Terrain generation, lighting and decoration still run synchronously in `World::ensureChunkLoaded` on the main thread. Only meshing runs on workers. Moving `TerrainGen`/`Lit`/`Decorated` to workers needs detached chunk generation plus a main-thread commit in `World`. `ChunkLifecycle` has no `Decorated -> Lit` edge: `World` relights after decoration before reporting `Decorated`.
- `WorldRenderer` has no synchronous fallback, and the pipeline always has at least one worker. Headless tests use `PipelineConfig::workerCount = 0` with `WorldMeshStreamer::runPendingInline`. `ChunkMesher::buildMesh` remains only as the parity reference for Phase 1 tests.
- Neighbour sections are not re-meshed when a chunk unloads. A remaining chunk keeps its culled border faces, so you can see into the unloaded area at the seam. The old renderer behaved the same way.
- Border light sync in `Chunk::spreadSkyLight/spreadBlockLight` marks the whole chunk. Light that propagates through a neighbour to that neighbour's far seam does not mark the third chunk, a gap the old renderer also had.
- `TaskPriority` is always `HighNear` (`WorldMeshStreamer` submits with the default). Edits get no `ImmediateCamera` priority and rely on distance ordering alone.
- There is no GPU-side test of the GL uploader, because a second in-process `Application` stalls (a known issue). It is covered only by the e2e binary runs and a manual stats check.
- Performance targets (<1.5 µs capture, <0.4 ms per section mesh, <10 ns swap) are not asserted. Timing tests in Debug/TSan would be flaky, and `PalettedSection` has no raw-span accessor for memcpy-style capture. The capture benchmark enforces a 100 µs ceiling only in `NDEBUG` builds.
- Out of scope: greedy meshing and the Dual-Tier MDI backend (Phase 3).

### ECS, Tools & Crafting (Sprint 2.2)
- The player is not an ECS entity (this was optional). `physics::Player` keeps its 60 Hz movement, which leaves the Phase 1 fixed-timestep tests untouched, and `PlayerControlSystem` is not run. Flying and swimming exist only in `physics::Player`.
- No mobs spawn in the running game, because there are no mob types, models or spawning rules. Melee, knockback, hurt frames and corpse removal work on any entity with `HealthComp` (tested with `EntityFactory::spawnMob`). In the live game the only entities are item drops, which are not attackable, as in Beta.
- A* voxel pathfinding (P2.2) is deferred to Phase 3. `MobAISystem` steers in a straight line toward `MobAIComp::goal`/`target`.
- Per-block slipperiness (ice) is not modelled for item entities.
- Item entities are rendered in the particle pass and are not lit by dynamic lights there, although glowing items do emit dynamic light.
- Crafting UI gaps: no shift-click (quick-move) on the result or inventory slots, no drag-distribute, and the workbench screen does not close automatically when the player walks away. On close, grid and cursor contents go back to the inventory, and any overflow is thrown with `dropFromPlayer`.
- The inventory is not saved in level data (it was not in Phase 1 either), so tool wear is lost on restart.
- Items at 256 and above other than tools and glowstone dust (buckets, ingots, paper, chest...) share one generic tinted sprite (`kGenericItemTile = 158`). Buckets have no behaviour: the starter inventory has 16 water and 16 lava sources (slots 22/23) as stand-ins. A source is removed by placing a block into it (water is replaceable), not by a bucket pickup with `FluidSimulator::removeFluid`.
- Sounds reuse existing procedural effects: an entity hit is `StepCloth` at pitch 0.8, a tool break is `BreakWood` at pitch 1.6, and a pickup is `UiClick` at pitch 1.7. There is no dedicated hurt, pop or break sample.
- There is no red hurt flash on entities, because entity bodies are not rendered. Armor, mob-specific health/attack tables and a respawn screen are also deferred.
- `ui::CraftingSystem` and `BlockRegistry::getPickaxeSpeedMultiplier` are kept only for Phase 1 tests (`test_ui`, `test_main`, `test_integration_mining_crafting`). The game and UI no longer use them.
- `crafting::kMaterialDurability` duplicates `gameplay::materialStats` (the values match and are pinned by tests). A cross-module `static_assert` has not been added.

### Fluids, Caves & Decoration (Sprint 2.3)
- Liquids still render as full cubes: the meshers (`SnapshotMesher`, `ChunkMesher`) ignore the level nibble, so flowing water looks like full blocks. The metadata only marks the mesh dirty.
- Physics treats lava as a swimmable liquid (`BlockRegistry::isLiquid` includes lava). Lava causes no damage or slowdown.
- No smoke particles for fizz, because `ParticleSystem` only has textured block-break bursts. Only the sound is played: the loudest fizz of each tick, with a linear 16-block gain falloff (`loudestFizzSound`), through the acoustics path.
- Decoration (pass 2) writes do not wake the simulator. Flora or trees placed next to settled water stay static until a neighbour changes, as in Beta. Only the load scan (`scheduleUnsettledFluids`) and player edits schedule fluid updates.
- No Nether, so `lavaDecayStep = 1` is not used. The F3 debug overlay does not show `FluidTickStats`.
- Pass-1 chunks are lit twice: once on arrival, so frontier chunks render sensibly, and again after decoration. The integration tests measured chunk loads at about 3.3 ms versus about 2 ms before. A later optimisation could skip lighting and meshing for undecorated frontier chunks.
- Undecorated frontier chunks (the outermost loaded ring) render without trees or flora until their neighbours load, so decoration pops in at the view edge.
- Saves from before two-pass decoration have no decoration record. They load as decorated with an empty plan, which is correct because their old trees never crossed borders. Their legacy tube caves do not line up with the new caves in freshly generated neighbouring chunks.
- `World::setSeed` with chunks already loaded re-registers them as decorated with empty plans (`resetDecorationPipeline`). `Application` calls it before any chunk loads, so this does not happen in practice.
- `CaveCarver::AQUIFER_THRESHOLD` (spec literal 0.42) is untuned. It floods about 27% of columns at Y 11..32 on the calibrated Simplex field.
- Out of scope: P3.4 climate multi-splines and Jigsaw structures (P3.5).

### Dynamic Lighting & Acoustics (Sprint 2.4)
- No `LightSource::Entity` emitters (burning mobs, projectiles), because they do not exist yet. No flicker modulation either. It would fit as a seeded intensity tweak before `submit()`.
- Particles and the block-crack overlay (`DYNAMIC_FS`) are not lit by dynamic lights. Only chunks (shader) and the first-person hand (vertex light level) are.
- The dynamic light colour is folded into a scalar block-light level (max of r, g, b), combined by max with the baked light, and then tinted with the existing warm torch tint. A future emitter with a non-warm colour would not show its own tint.
- `SDL_WINDOW_ALLOW_HIGHDPI` is not set at window creation. The engine handles a drawable size that differs from the window size (`glViewport` and the light tiles use `SDL_GL_GetDrawableSize`; the HUD ortho stays in window points), but it does not opt into Retina-size framebuffers on macOS.
- No stereo panning or distance attenuation in the mixer: voices stay mono-centred. Occlusion and reverb are the only spatial cues.
- Sources do not move. Each sound is tracked at its start position, and nothing calls `OcclusionScheduler::setSourcePosition` yet. Future moving emitters (mobs, projectiles) should call it.
- The audio thread does not report when a voice ends. `AcousticsController` expires a source after its nominal duration plus 0.1 s of grace. The bank counts the remaining 10 Hz updates for a stolen voice as stale.
- Updates flagged `beyondAudibleRange` are not posted, so a voice the listener walks away from keeps its last target and is not muted. Sounds that start beyond 48 blocks are never played.
- `Application` never calls `AcousticsController::reset()`, because there is no world switch or teleport yet. `EnclosureMonitor` detects jumps above 16 blocks between samples.
- No muffling under water: liquids are acoustically transparent (`blocksSound`).
- The bank's internal 512-entry MPSC queue (about 20 KB) is unused, because `AudioEngine` relays acoustic commands through its own queue.

### Resolved by later integration steps
Earlier module notes listed these items as open. They are now done in the code:
- `World` has a fluid adapter (`World::FluidAccess`) and `FluidSimulator` ticks in `WorldSession::tick` (`World::tick()`). Unsettled generated fluid is scheduled on chunk load.
- `WorldGen::generateChunk` no longer uses the legacy tube caves. `CaveCarver::carveChunk` runs in pass 1, and `DecorationPipeline` is wired into `World`, so generated worlds contain cave lava.
- The player inventory uses 16-bit `gameplay::ItemId` (`physics::InventorySlot`), and `render::ItemAppearance` defines names and icons for items at 256 and above.
- `BlockWashedAway` events spawn item drops (`EntitySimulation::spawnFluidDrops`), and the fizz sound uses the acoustics path.
- `Application` reaches the `ecs::Registry` through `WorldSession::simulation()`, so dropped items emit dynamic light (`collectDynamicLights(..., &sim().entities(), ...)`).
- The stale "Default palette has no lava block yet" comment is no longer in `modules/worldgen/test/test_cave_chunk_carving.cpp`.
