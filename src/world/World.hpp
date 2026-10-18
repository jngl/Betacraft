#pragma once

#include "core/Types.hpp"
#include "fluids/FluidSimulator.hpp"
#include "gameplay/SplitMix64.hpp"
#include "redstone/RedstoneEngine.hpp"
#include "world/Block.hpp"
#include "world/BlockEntityStore.hpp"
#include "world/Chunk.hpp"
#include "world/ScheduledTicks.hpp"
#include "world/WorldGen.hpp"
#include "world/WorldStorage.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace betacraft::storage {
class ChunkIoWorker;
}

namespace betacraft::streaming {
class ChunkGenPipeline;
}

namespace betacraft::worldgen {
class ChunkAccess;
class DecorationPipeline;
}

namespace betacraft::world {

class DifferentialLightingEngine;

// Who made an edit: the player (and the game rules acting for it: pistons, mining, placing)
// always persists; the fluid simulation persists only next to player-touched chunks.
enum class EditOrigin : uint8_t { Player, Simulation };

class World {
public:
    World(uint64_t seed, std::filesystem::path saveDir);
    ~World() noexcept;

    void setSeed(uint64_t seed) noexcept;
    [[nodiscard]] uint64_t getSeed() const noexcept { return m_worldGen.getSeed(); }

    [[nodiscard]] DifferentialLightingEngine& getLightingEngine() noexcept;
    [[nodiscard]] const DifferentialLightingEngine& getLightingEngine() const noexcept;

    // Synchronous streaming: loads or generates up to `maxNewPerCall` of the nearest missing
    // chunks on the calling thread, then unloads those beyond radius + 3 (their saves go to the
    // I/O worker). Startup, respawn and --no-async-gen use it.
    void updateChunksAround(const glm::vec3& centerPos, int32_t radiusChunks, int32_t maxNewPerCall = 3);
    // Loads (save queue first, then disk) or generates the chunk right away. Stays synchronous
    // for findSafeSpawnPosition, respawn, startup and tests; cancels any async request of it.
    Chunk& ensureChunkLoaded(core::ChunkCoord coord);

    // S7.7 asynchronous streaming. requestChunksAround asks the I/O worker for every missing
    // chunk within the radius (nearest first); those absent from disk go to the pass-1
    // generation pool. It also unloads chunks beyond radius + 3 and cancels their requests.
    // integrateReadyChunks then adds finished chunks on the calling (main) thread through the
    // same path as ensureChunkLoaded, until `budget` is spent (at least one chunk per call, so
    // one call takes at most budget + one integration) or `maxChunks` were added.
    static constexpr std::chrono::nanoseconds kIntegrationBudget = std::chrono::milliseconds(2);
    void requestChunksAround(const glm::vec3& centerPos, int32_t radiusChunks);
    struct IntegrationStats {
        size_t integrated{0};
        std::chrono::nanoseconds elapsed{0};
        std::chrono::nanoseconds slowestChunk{0};
        bool budgetExhausted{false}; // Stopped with chunks still ready
    };
    IntegrationStats integrateReadyChunks(std::chrono::nanoseconds budget = kIntegrationBudget,
                                          size_t maxChunks = SIZE_MAX);
    // Generation workers of the pool, created on the first requestChunksAround (0 = no
    // thread: jobs run in waitStreamingIdle). Call before that first request.
    void setGenerationWorkers(size_t workers) noexcept { m_genWorkers = workers; }
    // Blocks until every requested chunk was read or generated and waits for integration
    // (nothing loading or generating), and every queued save is written. Tests use it.
    void waitStreamingIdle();
    // Writes every queued save and flushes the region headers (ChunkIoWorker::flushAll).
    bool flushStorage();
    // Blocks until the I/O worker has no queued or running job, without integrating anything:
    // finished reads wait for the next integrateReadyChunks. Tests use it to order a disk
    // answer against later main-thread calls.
    void waitDiskIdle();
    // S7.10 F3 numbers.
    struct StreamingStats {
        size_t loading{0};     // Disk reads requested, not answered yet
        size_t generating{0};  // Pass-1 jobs pending or running
        size_t integrating{0}; // Read or generated, waiting for integrateReadyChunks
        size_t writing{0};     // Saves queued or being written
        std::chrono::nanoseconds lastIntegration{0};
        std::chrono::nanoseconds maxIntegration{0}; // Slowest integrateReadyChunks call of the last kIntegrationWindow
        size_t openRegions{0};
        uint64_t openRegionBytes{0};
    };
    static constexpr size_t kIntegrationWindow = 120; // Calls (about two seconds of frames)
    [[nodiscard]] StreamingStats getStreamingStats() const;
    [[nodiscard]] const Chunk* getChunk(core::ChunkCoord coord) const noexcept;
    [[nodiscard]] Chunk* getChunkMutable(core::ChunkCoord coord) noexcept;

    // P3.4 pass 2 (trees, flora) runs on a chunk once its 3x3 neighbourhood is resident;
    // until then the chunk holds pass-1 terrain + caves only.
    [[nodiscard]] bool isChunkDecorated(core::ChunkCoord coord) const noexcept;
    [[nodiscard]] const worldgen::DecorationPipeline& getDecorationPipeline() const noexcept { return *m_decoration; }

    // One 50 ms game tick (core::EngineConfig::GAME_TICKS_PER_SECOND): counts the tick, runs
    // at most kMaxScheduledTicksPerTick due scheduled block updates (S5.2), then advances the
    // P2.3 fluid simulation. Fluid writes go through setBlock/setMetadata internals: block
    // changes relight and mark the touched chunks (and border neighbours) for re-meshing;
    // level-only changes are saved but re-mesh nothing (meshers read metadata only for the
    // S5.4 shapes, never for liquids).
    fluids::FluidTickStats tick();
    static constexpr size_t kMaxScheduledTicksPerTick = 512;
    // Game ticks run so far (tick() calls); scheduled updates are due relative to it.
    [[nodiscard]] uint64_t getTickCount() const noexcept { return m_tickCount; }

    // S8.7 redstone (redstone::RedstoneEngine over this World, sharing its scheduled tick
    // queue): every block or metadata edit that can matter to a circuit (a redstone part, a
    // door, a piston, a normal cube appearing or going) reaches the engine, tick() runs it
    // right before the fluids, and chunks read from disk get their torches checked and their
    // pressed buttons and plates scheduled again. FeatureFlags::redstone (--no-redstone):
    // when disabled nothing propagates and the interactions below do nothing.
    void setRedstoneEnabled(bool enabled) noexcept { m_redstoneEnabled = enabled; }
    [[nodiscard]] bool isRedstoneEnabled() const noexcept { return m_redstoneEnabled; }
    [[nodiscard]] redstone::RedstoneEngine& redstone() noexcept { return *m_redstone; }
    [[nodiscard]] const redstone::RedstoneEngine& redstone() const noexcept { return *m_redstone; }
    // Right click on a lever (switches it) or a button (presses it for 20 ticks); someone on a
    // pressure plate this tick. Each returns whether anything changed.
    bool toggleLever(core::BlockPos pos);
    bool pressButton(core::BlockPos pos);
    bool reportPlateContact(core::BlockPos pos);
    // Clicks, doors, pistons and burnt-out torches since the last call.
    [[nodiscard]] std::vector<redstone::RedstoneEvent> takeRedstoneEvents() noexcept;
    [[nodiscard]] const redstone::RedstoneTickStats& getRedstoneStats() const noexcept { return m_redstoneStats; }

    // S5.2: runs the scheduled update of the block now at `pos` after `delayTicks` game ticks
    // (Beta World.scheduleBlockUpdate). Ignored (false) when `pos` already has one pending;
    // the update does nothing if the block changed in the meantime. Not persisted.
    bool scheduleBlockTick(core::BlockPos pos, int32_t delayTicks);
    [[nodiscard]] const ScheduledTickQueue& getScheduledTicks() const noexcept { return m_scheduledTicks; }

    // S5.3: sand and gravel the scheduled update found over air or liquid: the cell is already
    // air, whoever runs the game tick spawns the falling entity (EntitySimulation::tickFallingBlocks).
    struct FallingBlockSpawn {
        core::BlockPos pos{};
        BlockId id{BlockId::Air};
    };
    [[nodiscard]] std::vector<FallingBlockSpawn> takeFallingBlockSpawns() noexcept;
    // Beta BlockSand.tickRate: sand and gravel fall 3 ticks after a change around them.
    static constexpr int32_t kFallingBlockDelayTicks = 3;

    // S5.1 random ticks (BlockTicks.hpp): kRandomTicksPerChunk random cells in each decorated
    // chunk within kRandomTickRadiusChunks of `centre`, sections holding no ticking block
    // skipped. `skylightSubtracted` (gameplay::skylightSubtracted) dims the sky light the
    // handlers see through getEffectiveLight, so nothing grows by night.
    void tickRandomBlocks(const glm::vec3& centre, gameplay::SplitMix64& rng, int32_t skylightSubtracted = 0);
    // Beta World.getBlockLightValue: the brighter of block light and sky light dimmed by the
    // last tickRandomBlocks' skylightSubtracted.
    [[nodiscard]] uint8_t getEffectiveLight(core::BlockPos pos) const noexcept;
    // Turns the block at `pos` into air as an edit and records it as an UnsupportedBreak, so
    // the game tick drops it (leaf decay). Returns whether anything changed.
    bool breakBlockNaturally(core::BlockPos pos);
    // Side effects of the fluid ticks since the last call (washed-away blocks, lava fizz).
    [[nodiscard]] std::vector<fluids::FluidEvent> takeFluidEvents() noexcept;
    [[nodiscard]] const fluids::FluidSimulator& getFluidSimulator() const noexcept { return *m_fluids; }
    // Fluid type from the block id, level from the Beta metadata; dry for anything else.
    [[nodiscard]] fluids::FluidState getFluid(core::BlockPos pos) const noexcept;

    [[nodiscard]] const std::unordered_map<core::ChunkCoord, std::unique_ptr<Chunk>>& getChunks() const noexcept {
        return m_chunks;
    }

    [[nodiscard]] BlockId getBlock(int32_t wx, int32_t wy, int32_t wz) const noexcept;
    [[nodiscard]] BlockId getBlock(core::BlockPos pos) const noexcept {
        return getBlock(pos.x, pos.y, pos.z);
    }

    bool setBlock(int32_t wx, int32_t wy, int32_t wz, BlockId id);
    bool setBlock(core::BlockPos pos, BlockId id) {
        return setBlock(pos.x, pos.y, pos.z, id);
    }

    // Per-voxel 4-bit metadata. For fluid blocks it holds Beta's native value
    // (fluids::FluidState::betaDecay / fromBetaDecay: 0 = source, 1..7 = flowing, 8 = falling),
    // so water from worldgen and from saves predating the nibble (all 0) stays a source.
    // Unloaded or out-of-range cells read 0 and ignore writes; setters return whether anything
    // changed.
    [[nodiscard]] uint8_t getMetadata(int32_t wx, int32_t wy, int32_t wz) const noexcept;
    [[nodiscard]] uint8_t getMetadata(core::BlockPos pos) const noexcept {
        return getMetadata(pos.x, pos.y, pos.z);
    }
    bool setMetadata(int32_t wx, int32_t wy, int32_t wz, uint8_t meta);
    // setBlock (relighting, support checks) followed by setMetadata, as one edit.
    bool setBlockWithMetadata(int32_t wx, int32_t wy, int32_t wz, BlockId id, uint8_t meta);

    // Flora, torches, cactus, crops and doors that an edit turned to air because it removed the
    // block they stand on (Beta canBlockStay), ladders whose wall went, and blocks broken by
    // the game rules (breakBlockNaturally: decayed leaves, crops in the dark), oldest first,
    // since the last call. Whoever runs the game
    // tick spawns their drops (Beta dropBlockAsItem).
    struct UnsupportedBreak {
        core::BlockPos pos{};
        BlockId block{BlockId::Air};
        uint8_t meta{0}; // Its metadata when it broke (crop stage: S5.7 drops)
    };
    [[nodiscard]] std::vector<UnsupportedBreak> takeUnsupportedBreaks() noexcept;

    // S2.4 block entities (furnaces, chests) of the resident chunks. They follow their chunk
    // to and from disk (.be file) and leave the store when an edit replaces their block with
    // an incompatible one (furnace <-> lit furnace keeps it), whatever caused the edit.
    [[nodiscard]] BlockEntityStore& blockEntities() noexcept { return m_blockEntities; }
    [[nodiscard]] const BlockEntityStore& blockEntities() const noexcept { return m_blockEntities; }
    // Call after changing an entity's contents: the chunk is saved (and player-touched).
    void markBlockEntityChanged(core::BlockPos pos) noexcept;
    struct RemovedBlockEntity {
        core::BlockPos pos{};
        BlockId block{BlockId::Air}; // The block that carried it
        BlockEntity entity{};
    };
    // Entities whose block was removed or replaced since the last call, oldest first. Whoever
    // runs the game tick spawns their contents as drops (Beta BlockChest/BlockFurnace.onBlockRemoval).
    [[nodiscard]] std::vector<RemovedBlockEntity> takeRemovedBlockEntities() noexcept;

    // Beta 1.7 Piston push mechanic (pushes up to 12 blocks in pushDir); a line holding a
    // block entity (chest, furnace) does not move.
    bool triggerPistonPush(core::BlockPos pistonPos, core::FaceDir pushDir);

    [[nodiscard]] uint8_t getSkyLight(int32_t wx, int32_t wy, int32_t wz) const noexcept;
    [[nodiscard]] uint8_t getBlockLight(int32_t wx, int32_t wy, int32_t wz) const noexcept;
    void setSkyLight(int32_t wx, int32_t wy, int32_t wz, uint8_t val) noexcept;
    void setBlockLight(int32_t wx, int32_t wy, int32_t wz, uint8_t val) noexcept;

    void syncBorderLighting(Chunk& chunkA, Chunk& chunkB) noexcept;
    void syncChunkLightingWithNeighbors(core::ChunkCoord coord) noexcept;

    [[nodiscard]] glm::vec3 getGrassTint(int32_t wx, int32_t wz) const noexcept;
    [[nodiscard]] glm::vec3 getFoliageTint(int32_t wx, int32_t wz) const noexcept;
    [[nodiscard]] BiomeId getBiomeAt(int32_t wx, int32_t wz) const noexcept;

    [[nodiscard]] glm::vec3 findSafeSpawnPosition() noexcept;
    // Feet position at or above `pos` (same column) where feet and head cells are free of
    // solid blocks and liquids, centred on the column; `pos` itself when no such cell exists
    // below the build limit. Reads loaded chunks only.
    [[nodiscard]] glm::vec3 findStandableAbove(glm::vec3 pos) const noexcept;

    void saveModifiedChunks(const WorldLevelData& levelData);
    // Read-only session (level.dat too new or unreadable, conventions §1.2): nothing, neither
    // level.dat nor chunks, is written to the save directory.
    void setSaveDisabled(bool disabled);
    [[nodiscard]] bool isSaveDisabled() const noexcept { return m_saveDisabled; }
    // FeatureFlags::regionStorage (S7.3); see WorldStorage::setRegionStorageEnabled.
    void setRegionStorageEnabled(bool enabled);
    // Waits for the I/O worker first, so the store holds every save queued so far.
    [[nodiscard]] const WorldStorage& getStorage() const;
    // S7.4: whether this save still has legacy chunks to convert, given level.dat's
    // storage_format, and the conversion itself (call before any chunk is loaded).
    [[nodiscard]] bool needsStorageMigration(uint32_t levelStorageFormat) const {
        return m_storage.needsMigration(levelStorageFormat);
    }
    storage::MigrationReport migrateLegacyStorage(const std::function<void(float)>& progress);
    // What level.dat should record as storage_format.
    [[nodiscard]] uint32_t storageFormat() const { return m_storage.storageFormat(); }

    // S7.5 counters for F3: bundles handed to the store (queued on the S7.6 I/O worker), and those for a chunk none of whose
    // 3x3 neighbourhood the player touched (fluid simulation alone: must stay 0, bug 8).
    struct StorageStats {
        uint64_t bundlesWritten{0};
        uint64_t unsolicitedBundleWrites{0};
    };
    [[nodiscard]] const StorageStats& getStorageStats() const noexcept { return m_storageStats; }
    [[nodiscard]] LevelLoadStatus loadLevelDataStatus(WorldLevelData& outData) const {
        return m_storage.loadLevelDataStatus(outData);
    }
    // True for Ok and LegacyV1.
    [[nodiscard]] bool loadLevelData(WorldLevelData& outData) const {
        return m_storage.loadLevelData(outData);
    }

private:
    class FluidAccess;
    class RedstoneAccess;

    // Raw edits without waking the fluid simulator (used by it, through FluidAccess).
    // applyBlockEdit returns how many cells of the column from wy upwards changed: the
    // block itself plus any flora or torches it no longer supports.
    int32_t applyBlockEdit(int32_t wx, int32_t wy, int32_t wz, BlockId id, EditOrigin origin);
    bool applyMetadataEdit(int32_t wx, int32_t wy, int32_t wz, uint8_t meta, EditOrigin origin);
    // Whether a simulation edit in `coord` is saved: the chunk or one of its 8 neighbours is
    // player-touched. Generated fluid elsewhere is re-simulated on reload instead
    // (scheduleUnsettledFluids).
    [[nodiscard]] bool persistSimulationEdits(core::ChunkCoord coord) const noexcept;
    void notifyFluidColumn(int32_t wx, int32_t wy, int32_t wz, int32_t cells);
    void resetFluidSimulator(uint64_t seed);
    void scheduleUnsettledFluids(core::ChunkCoord coord);
    // Hands an edit at (wx, wy, wz) from `oldId` to `newId` to the redstone engine when it can
    // matter to a circuit.
    void notifyRedstone(int32_t wx, int32_t wy, int32_t wz, BlockId oldId, BlockId newId);
    // S8.7: the redstone blocks of a chunk read from disk (RedstoneEngine::onBlockLoaded).
    void rescheduleRedstone(core::ChunkCoord coord);
    // Marks the sections of adjacent columns whose meshes see voxel (lx, wy, lz) of `coord`.
    void markNeighborChunksDirty(core::ChunkCoord coord, int32_t lx, int32_t wy, int32_t lz) noexcept;
    void resetDecorationPipeline(uint64_t seed);
    [[nodiscard]] std::vector<core::ChunkCoord> registerForDecoration(core::ChunkCoord coord, bool loadedFromDisk,
                                                                    std::optional<worldgen::DecorationRecord> record);
    void relightDecoratedChunk(core::ChunkCoord coord) noexcept;
    // Encodes the chunk's bundle (main thread) and queues it on the I/O worker.
    bool persistChunk(const Chunk& chunk);
    // Deserializes `bundle`'s blocks into `chunk`; an unreadable chunk is quarantined and
    // `chunk` reset to an empty one. True when the chunk came from disk.
    bool deserializeBundle(std::optional<storage::ChunkBundle>& bundle, std::unique_ptr<Chunk>& chunk);
    // The body shared by every load path (main thread): makes the chunk resident, restores its
    // block entities, registers it for decoration, lights it and dirties its neighbours.
    // `lit`: pass-1 lighting already computed (generation worker).
    Chunk& integrateChunk(std::unique_ptr<Chunk> chunk, bool loadedFromDisk, std::optional<storage::ChunkBundle> bundle,
                          bool lit);
    // Saves edited chunks beyond radius + 3 of the centre chunk and drops them.
    void unloadDistantChunks(int32_t centerCX, int32_t centerCZ, int32_t radiusChunks);
    // Moves answered disk reads to the integration queue (absent chunks to the generation
    // pool) and re-marks resident chunks whose save failed.
    void pumpStreaming();
    void cancelStreamRequest(core::ChunkCoord coord);
    streaming::ChunkGenPipeline& genPipeline();

    // Sand and gravel at `pos` and just above it get a falling check (S5.3).
    void scheduleFallingChecks(int32_t wx, int32_t wy, int32_t wz);
    void runScheduledTick(const ScheduledTickQueue::Entry& entry);
    // Beta BlockLog/BlockLeaves.onBlockRemoval: leaves within `radius` start checking for decay.
    void flagLeavesForDecay(int32_t wx, int32_t wy, int32_t wz, int32_t radius, EditOrigin origin);
    // S5.5: ladders hanging on (wx, wy, wz), which stopped being a normal cube, and a door
    // standing on it (S5.6) break and are recorded as UnsupportedBreak.
    void breakDetachedWallBlocks(int32_t wx, int32_t wy, int32_t wz, EditOrigin origin);
    // S5.6/S5.8: the other half of the door or bed (`id`, metadata `meta`) that was at
    // (wx, wy, wz) goes too, without a drop of its own.
    void removeOtherHalf(int32_t wx, int32_t wy, int32_t wz, BlockId id, uint8_t meta, EditOrigin origin);

    WorldGen m_worldGen;
    WorldStorage m_storage;
    bool m_saveDisabled{false};
    std::unordered_map<core::ChunkCoord, std::unique_ptr<Chunk>> m_chunks;
    std::unique_ptr<DifferentialLightingEngine> m_diffLighting;
    std::unique_ptr<worldgen::ChunkAccess> m_chunkAccess;
    std::unique_ptr<worldgen::DecorationPipeline> m_decoration;
    std::unique_ptr<FluidAccess> m_fluidAccess;
    std::unique_ptr<fluids::FluidSimulator> m_fluids;
    std::unique_ptr<RedstoneAccess> m_redstoneAccess;
    std::unique_ptr<redstone::RedstoneEngine> m_redstone;
    bool m_redstoneEnabled{true};
    redstone::RedstoneTickStats m_redstoneStats{};
    std::vector<UnsupportedBreak> m_unsupportedBreaks;
    BlockEntityStore m_blockEntities;
    std::vector<RemovedBlockEntity> m_removedBlockEntities;
    ScheduledTickQueue m_scheduledTicks;
    std::vector<ScheduledTickQueue::Entry> m_dueTicks; // Reused by tick()
    uint64_t m_tickCount{0};
    std::vector<FallingBlockSpawn> m_fallingBlockSpawns;
    int32_t m_skylightSubtracted{0};
    StorageStats m_storageStats;

    // S7.7 async streaming state (main thread only).
    struct StreamRequest {
        enum class Stage : uint8_t { Loading, Loaded, Generating };
        Stage stage{Stage::Loading};
        std::vector<uint8_t> bundleBytes; // Loaded
        // Ticket of this request's disk read: a late answer to an earlier, cancelled request of
        // the same chunk carries an older one and is dropped (it may predate later edits).
        uint64_t loadTicket{0};
    };
    std::unordered_map<core::ChunkCoord, StreamRequest> m_streamRequests;
    std::deque<core::ChunkCoord> m_loadedOrder; // Loaded requests, answer order
    uint64_t m_nextLoadTicket{1};
    std::shared_ptr<const WorldGen> m_sharedGen; // What the generation workers read
    size_t m_genWorkers;
    std::unique_ptr<streaming::ChunkGenPipeline> m_genPipeline;
    std::chrono::nanoseconds m_lastIntegration{0};
    std::deque<std::chrono::nanoseconds> m_integrationWindow;

    // S7.6 last: the worker drains its saves into m_storage before either is destroyed.
    class StorageIo;
    std::unique_ptr<StorageIo> m_ioStore;
    std::unique_ptr<storage::ChunkIoWorker> m_io;
};

} // namespace betacraft::world
