#include "world/World.hpp"
#include "world/BlockShapes.hpp"
#include "world/BlockTicks.hpp"
#include "world/DifferentialLightingEngine.hpp"
#include "core/Logger.hpp"
#include "gameplay/ItemIds.hpp"
#include "storage/ChunkIoWorker.hpp"
#include "streaming/ChunkGenPipeline.hpp"
#include "streaming/SectionCoord.hpp"
#include "worldgen/DecorationPipeline.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <vector>

namespace betacraft::world {

namespace {

void foldLegacyItemIds(StoredStack& s) noexcept {
    s.id = gameplay::canonicalize(s.id);
}

void foldLegacyItemIds(FurnaceData& f) noexcept {
    foldLegacyItemIds(f.input);
    foldLegacyItemIds(f.fuel);
    foldLegacyItemIds(f.output);
}

void foldLegacyItemIds(ChestData& c) noexcept {
    for (StoredStack& s : c.slots) foldLegacyItemIds(s);
}

[[nodiscard]] constexpr core::ChunkCoord chunkOf(int32_t wx, int32_t wz) noexcept {
    return {core::floorDiv(wx, Chunk::WIDTH), core::floorDiv(wz, Chunk::DEPTH)};
}

[[nodiscard]] constexpr fluids::FluidType fluidTypeOf(BlockId id) noexcept {
    if (isWaterBlock(id)) return fluids::FluidType::Water;
    if (isLavaBlock(id)) return fluids::FluidType::Lava;
    return fluids::FluidType::None;
}

} // namespace

// IFluidVoxelAccess over the World: block id + Beta metadata nibble (0 = source), written
// through the raw edit path so the simulator's own writes do not wake it a second time.
class World::FluidAccess final : public fluids::IFluidVoxelAccess {
public:
    explicit FluidAccess(World& world) noexcept : m_world(world) {}

    [[nodiscard]] bool isLoaded(const core::BlockPos& pos) const noexcept override {
        return pos.y >= 0 && pos.y < Chunk::HEIGHT && m_world.getChunk(chunkOf(pos.x, pos.z)) != nullptr;
    }

    [[nodiscard]] BlockId getBlock(const core::BlockPos& pos) const noexcept override {
        return m_world.getBlock(pos);
    }

    [[nodiscard]] fluids::FluidState getFluid(const core::BlockPos& pos) const noexcept override {
        return m_world.getFluid(pos);
    }

    void setBlock(const core::BlockPos& pos, BlockId id) override {
        (void)m_world.applyBlockEdit(pos.x, pos.y, pos.z, id, EditOrigin::Simulation);
        (void)m_world.applyMetadataEdit(pos.x, pos.y, pos.z, 0, EditOrigin::Simulation);
    }

    void setFluid(const core::BlockPos& pos, fluids::FluidState state) override {
        if (state.isEmpty()) {
            setBlock(pos, BlockId::Air);
            return;
        }
        // Keep a still/flowing variant already there: only the level changes, no relight.
        const BlockId current = m_world.getBlock(pos);
        const BlockId canonical = state.type == fluids::FluidType::Lava ? BlockId::Lava : BlockId::Water;
        (void)m_world.applyBlockEdit(pos.x, pos.y, pos.z, isSameLiquid(current, canonical) ? current : canonical,
                                     EditOrigin::Simulation);
        (void)m_world.applyMetadataEdit(pos.x, pos.y, pos.z, static_cast<uint8_t>(state.betaDecay()),
                                        EditOrigin::Simulation);
    }

private:
    World& m_world;
};

// IRedstoneVoxelAccess over the World (S8.7): ordinary player-origin edits, so the
// circuits a player built are saved like the rest of their work.
class World::RedstoneAccess final : public redstone::IRedstoneVoxelAccess {
public:
    explicit RedstoneAccess(World& world) noexcept : m_world(world) {}

    [[nodiscard]] bool isLoaded(const core::BlockPos& pos) const noexcept override {
        return pos.y >= 0 && pos.y < Chunk::HEIGHT && m_world.getChunk(chunkOf(pos.x, pos.z)) != nullptr;
    }
    [[nodiscard]] BlockId getBlock(const core::BlockPos& pos) const noexcept override { return m_world.getBlock(pos); }
    [[nodiscard]] uint8_t getMetadata(const core::BlockPos& pos) const noexcept override {
        return m_world.getMetadata(pos);
    }
    void setBlock(const core::BlockPos& pos, BlockId id, uint8_t meta) override {
        (void)m_world.setBlockWithMetadata(pos.x, pos.y, pos.z, id, meta);
    }
    void setMetadata(const core::BlockPos& pos, uint8_t meta) override {
        (void)m_world.setMetadata(pos.x, pos.y, pos.z, meta);
    }
    bool pushPiston(const core::BlockPos& pos, core::FaceDir dir) override {
        return m_world.triggerPistonPush(pos, dir);
    }

private:
    World& m_world;
};

// The I/O worker's view of the save: whole encoded bundles through the WorldStorage facade
// (regions, legacy fallback, quarantine, read-only), called from the worker thread.
class World::StorageIo final : public storage::IChunkStore {
public:
    explicit StorageIo(WorldStorage& storage) noexcept : m_storage(storage) {}

    [[nodiscard]] std::optional<std::vector<uint8_t>> read(core::ChunkCoord coord) override {
        return m_storage.loadChunkBundleBytes(coord);
    }
    [[nodiscard]] bool contains(core::ChunkCoord coord) override {
        return m_storage.loadChunkBundle(coord).has_value();
    }
    bool write(core::ChunkCoord coord, std::span<const uint8_t> bundleBytes) override {
        return m_storage.saveChunkBundleBytes(coord, bundleBytes);
    }
    bool flush() override { return m_storage.flush(); }

private:
    WorldStorage& m_storage;
};

World::World(uint64_t seed, std::filesystem::path saveDir)
    : m_worldGen(seed),
      m_storage(std::move(saveDir)),
      m_diffLighting(std::make_unique<DifferentialLightingEngine>(*this)),
      m_fluidAccess(std::make_unique<FluidAccess>(*this)),
      m_genWorkers(streaming::defaultGenWorkerCount()),
      m_ioStore(std::make_unique<StorageIo>(m_storage)),
      m_io(std::make_unique<storage::ChunkIoWorker>(*m_ioStore)) {
    BlockRegistry::initialize();
    auto find = [this](core::ChunkCoord c) noexcept { return getChunkMutable(c); };
    m_chunkAccess = std::make_unique<worldgen::FunctionChunkAccess<decltype(find)>>(find);
    resetDecorationPipeline(seed);
    resetFluidSimulator(seed);
    m_redstoneAccess = std::make_unique<RedstoneAccess>(*this);
    m_redstone = std::make_unique<redstone::RedstoneEngine>(*m_redstoneAccess, &m_scheduledTicks);
}

World::~World() noexcept = default;

void World::resetDecorationPipeline(uint64_t seed) {
    m_decoration = std::make_unique<worldgen::DecorationPipeline>(seed, *m_chunkAccess);
    // Chunks generated under the previous seed keep their blocks and contribute no features.
    for (const auto& [coord, chunk] : m_chunks) {
        (void)m_decoration->onDecoratedChunkLoaded(coord, worldgen::FeaturePlan{});
    }
}

bool World::isChunkDecorated(core::ChunkCoord coord) const noexcept {
    return m_decoration->isDecorated(coord);
}

DifferentialLightingEngine& World::getLightingEngine() noexcept {
    return *m_diffLighting;
}

const DifferentialLightingEngine& World::getLightingEngine() const noexcept {
    return *m_diffLighting;
}

void World::setSeed(uint64_t seed) noexcept {
    m_worldGen.setSeed(seed);
    if (m_genPipeline) {
        // Chunks generated or being generated from the old seed are thrown away.
        m_sharedGen = std::make_shared<const WorldGen>(seed);
        m_genPipeline->setWorldGen(m_sharedGen);
        std::erase_if(m_streamRequests,
                      [](const auto& entry) { return entry.second.stage == StreamRequest::Stage::Generating; });
    }
    resetDecorationPipeline(seed);
    resetFluidSimulator(seed);
}

void World::resetFluidSimulator(uint64_t seed) {
    fluids::FluidConfig config{};
    config.randomSeed = seed; // lava thinning stays reproducible per world
    // Fluid writes relight on the main thread (a hardening lava cell is a light-15 source
    // removal, ~7 us); 256 keeps a tick near 2 ms when a fresh region settles hundreds of
    // generated lava/aquifer contacts, and is far above what one player-placed source needs.
    config.maxUpdatesPerTick = 256;
    m_fluids = std::make_unique<fluids::FluidSimulator>(*m_fluidAccess, config);
    for (const auto& [coord, chunk] : m_chunks) {
        scheduleUnsettledFluids(coord);
    }
}

fluids::FluidTickStats World::tick() {
    ++m_tickCount;
    m_redstone->setTime(m_tickCount);
    m_dueTicks.clear();
    (void)m_scheduledTicks.drainDue(m_tickCount, m_dueTicks, kMaxScheduledTicksPerTick);
    for (const ScheduledTickQueue::Entry& entry : m_dueTicks) {
        runScheduledTick(entry);
    }
    m_redstoneStats = m_redstoneEnabled ? m_redstone->tick(m_tickCount) : redstone::RedstoneTickStats{};
    return m_fluids->tick();
}

bool World::toggleLever(core::BlockPos pos) {
    return m_redstoneEnabled && m_redstone->toggleLever(pos);
}

bool World::pressButton(core::BlockPos pos) {
    return m_redstoneEnabled && m_redstone->pressButton(pos);
}

bool World::reportPlateContact(core::BlockPos pos) {
    return m_redstoneEnabled && m_redstone->reportPlateContact(pos);
}

std::vector<redstone::RedstoneEvent> World::takeRedstoneEvents() noexcept {
    return m_redstone->takeEvents();
}

void World::notifyRedstone(int32_t wx, int32_t wy, int32_t wz, BlockId oldId, BlockId newId) {
    if (m_redstoneEnabled && (redstone::isRedstoneComponent(oldId) || redstone::isRedstoneComponent(newId) ||
                              isNormalCube(oldId) != isNormalCube(newId))) {
        m_redstone->onBlockChanged({wx, wy, wz});
    }
}

void World::rescheduleRedstone(core::ChunkCoord coord) {
    // Scheduled updates are not saved: a torch whose input changed just before the save, a
    // button or a plate still down, would otherwise stay as they are.
    const Chunk* chunk = getChunk(coord);
    if (chunk == nullptr || !m_redstoneEnabled) {
        return;
    }
    const auto wanted = [](BlockId id) { return redstone::isRedstoneComponent(id) && id != BlockId::RedstoneWire; };
    for (size_t s = 0; s < Chunk::SECTIONS_PER_CHUNK; ++s) {
        if (!chunk->getSection(s).mayContain(wanted)) {
            continue;
        }
        const int32_t yBase = static_cast<int32_t>(s) * PalettedSection::SIZE;
        for (int32_t y = yBase; y < yBase + PalettedSection::SIZE; ++y) {
            for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz) {
                for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
                    if (wanted(chunk->getBlock(lx, y, lz))) {
                        m_redstone->onBlockLoaded({coord.x * Chunk::WIDTH + lx, y, coord.z * Chunk::DEPTH + lz});
                    }
                }
            }
        }
    }
}

bool World::scheduleBlockTick(core::BlockPos pos, int32_t delayTicks) {
    if (pos.y < 0 || pos.y >= Chunk::HEIGHT || getChunk(chunkOf(pos.x, pos.z)) == nullptr) {
        return false;
    }
    const auto delay = static_cast<uint64_t>(std::max(delayTicks, 1));
    return m_scheduledTicks.schedule(pos, getBlock(pos), m_tickCount + delay);
}

void World::scheduleFallingChecks(int32_t wx, int32_t wy, int32_t wz) {
    for (int32_t dy = 0; dy <= 1; ++dy) {
        const BlockId id = getBlock(wx, wy + dy, wz);
        if (id == BlockId::Sand || id == BlockId::Gravel) {
            (void)scheduleBlockTick({wx, wy + dy, wz}, kFallingBlockDelayTicks);
        }
    }
}

void World::runScheduledTick(const ScheduledTickQueue::Entry& entry) {
    const core::BlockPos pos = entry.pos;
    if (getBlock(pos) != entry.block) {
        return; // Replaced since it was scheduled
    }
    if (redstone::hasRedstoneScheduledTick(entry.block)) {
        if (m_redstoneEnabled) {
            m_redstone->runScheduledTick(pos);
        }
        return;
    }
    if (entry.block == BlockId::Sand || entry.block == BlockId::Gravel) {
        // Beta BlockSand.tryToFall: over air or liquid the block leaves the grid as an entity.
        const BlockId below = getBlock(pos.x, pos.y - 1, pos.z);
        if (pos.y <= 0 || (below != BlockId::Air && !BlockRegistry::isLiquid(below))) {
            return;
        }
        const int32_t changed = applyBlockEdit(pos.x, pos.y, pos.z, BlockId::Air, EditOrigin::Player);
        if (changed == 0) {
            return;
        }
        (void)applyMetadataEdit(pos.x, pos.y, pos.z, 0, EditOrigin::Player);
        notifyFluidColumn(pos.x, pos.y, pos.z, changed);
        m_fallingBlockSpawns.push_back({pos, entry.block});
        scheduleFallingChecks(pos.x, pos.y, pos.z); // The column above follows
    }
}

std::vector<World::FallingBlockSpawn> World::takeFallingBlockSpawns() noexcept {
    std::vector<FallingBlockSpawn> out;
    out.swap(m_fallingBlockSpawns);
    return out;
}

void World::tickRandomBlocks(const glm::vec3& centre, gameplay::SplitMix64& rng, int32_t skylightSubtracted) {
    m_skylightSubtracted = std::clamp(skylightSubtracted, 0, 15);
    const core::ChunkCoord here = chunkOf(static_cast<int32_t>(std::floor(centre.x)),
                                          static_cast<int32_t>(std::floor(centre.z)));
    std::vector<core::ChunkCoord> coords;
    for (const auto& [coord, chunk] : m_chunks) {
        if (std::abs(coord.x - here.x) <= kRandomTickRadiusChunks &&
            std::abs(coord.z - here.z) <= kRandomTickRadiusChunks && m_decoration->isDecorated(coord)) {
            coords.push_back(coord);
        }
    }
    // Hash-map order depends on load history; a fixed order keeps a seeded run reproducible.
    std::sort(coords.begin(), coords.end(), [](const core::ChunkCoord& a, const core::ChunkCoord& b) {
        return a.x != b.x ? a.x < b.x : a.z < b.z;
    });

    constexpr int32_t kPerSection = kRandomTicksPerChunk / static_cast<int32_t>(Chunk::SECTIONS_PER_CHUNK);
    for (const core::ChunkCoord coord : coords) {
        Chunk* chunk = getChunkMutable(coord);
        if (chunk == nullptr) continue;
        for (size_t s = 0; s < Chunk::SECTIONS_PER_CHUNK; ++s) {
            const PalettedSection& section = chunk->getSection(s);
            if (!section.mayContain([](BlockId id) { return hasRandomTick(id); })) {
                continue;
            }
            for (int32_t i = 0; i < kPerSection; ++i) {
                const uint64_t r = rng.next();
                const auto lx = static_cast<int32_t>(r & 15U);
                const auto lz = static_cast<int32_t>((r >> 4U) & 15U);
                const auto ly = static_cast<int32_t>((r >> 8U) & 15U);
                const BlockId id = section.getBlock(lx, ly, lz);
                if (const RandomTickFn fn = kRandomTick[static_cast<size_t>(id)]) {
                    const int32_t wy = static_cast<int32_t>(s) * PalettedSection::SIZE + ly;
                    fn(*this, {coord.x * Chunk::WIDTH + lx, wy, coord.z * Chunk::DEPTH + lz},
                       chunk->getMetadata(lx, wy, lz), rng);
                }
            }
        }
    }
}

uint8_t World::getEffectiveLight(core::BlockPos pos) const noexcept {
    const int32_t sky = static_cast<int32_t>(getSkyLight(pos.x, pos.y, pos.z)) - m_skylightSubtracted;
    return static_cast<uint8_t>(std::max<int32_t>(sky, getBlockLight(pos.x, pos.y, pos.z)));
}

bool World::breakBlockNaturally(core::BlockPos pos) {
    const BlockId old = getBlock(pos);
    if (old == BlockId::Air) {
        return false;
    }
    const uint8_t oldMeta = getMetadata(pos);
    const int32_t changed = applyBlockEdit(pos.x, pos.y, pos.z, BlockId::Air, EditOrigin::Player);
    if (changed == 0) {
        return false;
    }
    (void)applyMetadataEdit(pos.x, pos.y, pos.z, 0, EditOrigin::Player);
    notifyFluidColumn(pos.x, pos.y, pos.z, changed);
    // Before the flora above it, which applyBlockEdit recorded already: order does not matter
    // for drops.
    m_unsupportedBreaks.push_back({pos, old, oldMeta});
    scheduleFallingChecks(pos.x, pos.y, pos.z);
    if (changed > 1) {
        scheduleFallingChecks(pos.x, pos.y + changed - 1, pos.z);
    }
    return true;
}

void World::flagLeavesForDecay(int32_t wx, int32_t wy, int32_t wz, int32_t radius, EditOrigin origin) {
    for (int32_t dy = -radius; dy <= radius; ++dy) {
        for (int32_t dz = -radius; dz <= radius; ++dz) {
            for (int32_t dx = -radius; dx <= radius; ++dx) {
                if (getBlock(wx + dx, wy + dy, wz + dz) != BlockId::Leaves) continue;
                const uint8_t meta = getMetadata(wx + dx, wy + dy, wz + dz);
                if ((meta & kLeavesCheckDecayBit) == 0) {
                    (void)applyMetadataEdit(wx + dx, wy + dy, wz + dz,
                                            static_cast<uint8_t>(meta | kLeavesCheckDecayBit), origin);
                }
            }
        }
    }
}

std::vector<fluids::FluidEvent> World::takeFluidEvents() noexcept {
    return m_fluids->takeEvents();
}

fluids::FluidState World::getFluid(core::BlockPos pos) const noexcept {
    // The simulator's hottest read: one chunk lookup for both the id and the nibble.
    if (pos.y < 0 || pos.y >= Chunk::HEIGHT) return {};
    const Chunk* chunk = getChunk(chunkOf(pos.x, pos.z));
    if (!chunk) return {};
    const int32_t lx = core::floorMod(pos.x, Chunk::WIDTH);
    const int32_t lz = core::floorMod(pos.z, Chunk::DEPTH);
    const fluids::FluidType type = fluidTypeOf(chunk->getBlock(lx, pos.y, lz));
    if (type == fluids::FluidType::None) return {};
    return fluids::FluidState::fromBetaDecay(type, chunk->getMetadata(lx, pos.y, lz));
}

Chunk& World::ensureChunkLoaded(core::ChunkCoord coord) {
    auto it = m_chunks.find(coord);
    if (it != m_chunks.end()) {
        return *it->second;
    }
    cancelStreamRequest(coord);

    // S7.3: blocks, decoration record and block entities come from one bundle; S7.6: a save
    // still queued on the I/O worker is newer than the disk.
    std::optional<storage::ChunkBundle> bundle;
    if (std::optional<std::vector<uint8_t>> pending = m_io->pendingSave(coord)) {
        bundle = storage::decodeBundle(*pending);
    } else {
        bundle = m_storage.loadChunkBundle(coord);
    }
    auto chunk = std::make_unique<Chunk>(coord);
    const bool loadedFromDisk = deserializeBundle(bundle, chunk);
    if (!loadedFromDisk) {
        m_worldGen.generateChunk(*chunk); // pass 1
    }
    return integrateChunk(std::move(chunk), loadedFromDisk, std::move(bundle), false);
}

bool World::deserializeBundle(std::optional<storage::ChunkBundle>& bundle, std::unique_ptr<Chunk>& chunk) {
    if (!bundle || bundle->blocks.empty()) {
        return false;
    }
    if (!chunk->deserializeRLE(bundle->blocks)) {
        const core::ChunkCoord coord = chunk->getCoord();
        m_storage.quarantineChunk(coord); // Regenerated by the caller; the bytes are kept aside
        chunk = std::make_unique<Chunk>(coord);
        return false;
    }
    m_worldGen.populateBiomeColumns(*chunk);
    return true;
}

Chunk& World::integrateChunk(std::unique_ptr<Chunk> chunk, bool loadedFromDisk,
                             std::optional<storage::ChunkBundle> bundle, bool lit) {
    const core::ChunkCoord coord = chunk->getCoord();
    Chunk& ref = *chunk;
    m_chunks.emplace(coord, std::move(chunk));
    if (loadedFromDisk) {
        std::vector<BlockEntityEntry> entities = m_storage.decodeBundleBlockEntities(coord, *bundle);
        std::erase_if(entities, [&ref](const BlockEntityEntry& e) {
            const BlockId block = ref.getBlock(core::floorMod(e.first.x, Chunk::WIDTH), e.first.y,
                                               core::floorMod(e.first.z, Chunk::DEPTH));
            if (blockEntityMatches(block, e.second)) {
                return false;
            }
            core::Logger::warn("World", "Discarding block entity at ", e.first.x, " ", e.first.y, " ", e.first.z,
                               ": no matching block");
            return true;
        });
        // S2.10: chest and furnace stacks saved with the legacy ids 100..105 load as Beta numbers.
        for (BlockEntityEntry& e : entities) {
            std::visit([](auto& data) { foldLegacyItemIds(data); }, e.second);
        }
        m_blockEntities.insertChunk(coord, std::move(entities));
    }

    std::optional<worldgen::DecorationRecord> record;
    if (loadedFromDisk) {
        record = std::move(bundle->decoration);
    }
    const std::vector<core::ChunkCoord> decorated = registerForDecoration(coord, loadedFromDisk, std::move(record));
    if (std::find(decorated.begin(), decorated.end(), coord) == decorated.end()) {
        // Still undecorated, but resident and possibly rendered at the edge of the view.
        // Chunks read from disk were lit by deserializeRLE; a generation worker lit its chunk,
        // which is still valid unless a neighbour's decoration just reached into it.
        if (!loadedFromDisk && (!lit || !decorated.empty())) {
            ref.recalculateLighting();
        }
        syncChunkLightingWithNeighbors(coord);
    }
    for (const core::ChunkCoord d : decorated) {
        relightDecoratedChunk(d);
    }
    scheduleUnsettledFluids(coord);
    if (loadedFromDisk) {
        rescheduleRedstone(coord);
    }

    // Every neighbour's 18^3 section neighbourhoods reach into this column: border faces on
    // the four sides, corner AO and smooth light on the diagonals.
    for (int32_t dz = -1; dz <= 1; ++dz) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            if (dx == 0 && dz == 0) continue;
            if (Chunk* nb = getChunkMutable({coord.x + dx, coord.z + dz})) {
                nb->setMeshDirty(true);
            }
        }
    }

    return ref;
}

std::vector<core::ChunkCoord> World::registerForDecoration(core::ChunkCoord coord, bool loadedFromDisk,
                                                         std::optional<worldgen::DecorationRecord> record) {
    if (!loadedFromDisk) {
        return m_decoration->onTerrainReady(coord);
    }
    if (record) {
        return record->decorated ? m_decoration->onDecoratedChunkLoaded(coord, std::move(record->plan))
                                 : m_decoration->onUndecoratedChunkLoaded(coord, std::move(record->plan));
    }
    // Saved before two-pass decoration: its trees are baked in and never crossed a border.
    return m_decoration->onDecoratedChunkLoaded(coord, worldgen::FeaturePlan{});
}

void World::relightDecoratedChunk(core::ChunkCoord coord) noexcept {
    Chunk* chunk = getChunkMutable(coord);
    if (!chunk) return;
    // Logs and leaves shade what pass 1 lit as open sky.
    chunk->recalculateLighting();
    syncChunkLightingWithNeighbors(coord);
    for (int32_t dz = -1; dz <= 1; ++dz) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            if (Chunk* c = getChunkMutable({coord.x + dx, coord.z + dz})) {
                c->setMeshDirty(true);
            }
        }
    }
}

bool World::persistChunk(const Chunk& chunk) {
    if (m_saveDisabled) {
        return false;
    }
    const core::ChunkCoord coord = chunk.getCoord();
    const worldgen::FeaturePlan* plan = m_decoration->findPlan(coord);
    storage::ChunkBundle bundle;
    bundle.blocks = chunk.serializeRLE();
    bundle.decoration = worldgen::DecorationRecord{plan == nullptr || m_decoration->isDecorated(coord),
                                                   plan ? *plan : worldgen::FeaturePlan{}};
    const std::vector<BlockEntityEntry> entities = m_blockEntities.chunkEntries(coord);
    if (!entities.empty()) {
        bundle.blockEntities = encodeBlockEntities(entities);
    }
    // S7.5 (bug 8 under the new storage): only player-touched chunks and their neighbours
    // ever reach the store; a write outside them is a regression the F3 counter shows.
    if (!persistSimulationEdits(coord)) {
        ++m_storageStats.unsolicitedBundleWrites;
        core::Logger::warn("World", "Chunk ", coord.x, ",", coord.z, " saved without any player edit around it");
    }
    // S7.6: serialised here, compressed and written by the I/O worker.
    m_io->enqueueSave({coord, m_storage.encodeChunkBundle(bundle)});
    ++m_storageStats.bundlesWritten;
    return true;
}

const Chunk* World::getChunk(core::ChunkCoord coord) const noexcept {
    const auto it = m_chunks.find(coord);
    return (it != m_chunks.end()) ? it->second.get() : nullptr;
}

Chunk* World::getChunkMutable(core::ChunkCoord coord) noexcept {
    const auto it = m_chunks.find(coord);
    return (it != m_chunks.end()) ? it->second.get() : nullptr;
}

void World::updateChunksAround(const glm::vec3& centerPos, int32_t radiusChunks, int32_t maxNewPerCall) {
    const int32_t centerCX = core::floorDiv(static_cast<int32_t>(std::floor(centerPos.x)), Chunk::WIDTH);
    const int32_t centerCZ = core::floorDiv(static_cast<int32_t>(std::floor(centerPos.z)), Chunk::DEPTH);

    // Load closest missing chunks in spiral / radial order
    std::vector<core::ChunkCoord> missing;
    for (int32_t dz = -radiusChunks; dz <= radiusChunks; ++dz) {
        for (int32_t dx = -radiusChunks; dx <= radiusChunks; ++dx) {
            if (dx * dx + dz * dz > radiusChunks * radiusChunks + 2) {
                continue;
            }
            const core::ChunkCoord coord{centerCX + dx, centerCZ + dz};
            if (m_chunks.find(coord) == m_chunks.end()) {
                missing.push_back(coord);
            }
        }
    }

    std::sort(missing.begin(), missing.end(), [centerCX, centerCZ](const core::ChunkCoord& a, const core::ChunkCoord& b) {
        const int32_t da = (a.x - centerCX) * (a.x - centerCX) + (a.z - centerCZ) * (a.z - centerCZ);
        const int32_t db = (b.x - centerCX) * (b.x - centerCX) + (b.z - centerCZ) * (b.z - centerCZ);
        return da < db;
    });

    const int32_t toLoad = std::min<int32_t>(maxNewPerCall, static_cast<int32_t>(missing.size()));
    for (int32_t i = 0; i < toLoad; ++i) {
        ensureChunkLoaded(missing[static_cast<size_t>(i)]);
    }

    unloadDistantChunks(centerCX, centerCZ, radiusChunks);
}

void World::unloadDistantChunks(int32_t centerCX, int32_t centerCZ, int32_t radiusChunks) {
    // Unload distant chunks beyond radius + 3
    const int32_t unloadDistSq = (radiusChunks + 3) * (radiusChunks + 3);
    for (auto it = m_chunks.begin(); it != m_chunks.end();) {
        const int32_t dx = it->first.x - centerCX;
        const int32_t dz = it->first.z - centerCZ;
        if (dx * dx + dz * dz > unloadDistSq) {
            if (it->second->isModified()) {
                (void)persistChunk(*it->second);
            }
            // Its plan comes back from the saved record, or from regenerating identical pass-1
            // terrain when it was never edited, so the pipeline need not keep a copy.
            m_decoration->onChunkUnloaded(it->first, worldgen::PlanRetention::CallerRestores);
            (void)m_blockEntities.extractChunk(it->first); // Saved above with the chunk
            it = m_chunks.erase(it);
        } else {
            ++it;
        }
    }
    // Requests that far out are not wanted any more.
    for (auto it = m_streamRequests.begin(); it != m_streamRequests.end();) {
        const int32_t dx = it->first.x - centerCX;
        const int32_t dz = it->first.z - centerCZ;
        if (dx * dx + dz * dz > unloadDistSq) {
            if (it->second.stage == StreamRequest::Stage::Generating && m_genPipeline) {
                (void)m_genPipeline->cancel(it->first);
            }
            it = m_streamRequests.erase(it);
        } else {
            ++it;
        }
    }
}

// --- S7.7 asynchronous streaming ----------------------------------------------------------

streaming::ChunkGenPipeline& World::genPipeline() {
    if (!m_genPipeline) {
        m_sharedGen = std::make_shared<const WorldGen>(m_worldGen.getSeed());
        m_genPipeline = std::make_unique<streaming::ChunkGenPipeline>(m_sharedGen,
                                                                      streaming::ChunkGenConfig{m_genWorkers});
    }
    return *m_genPipeline;
}

void World::cancelStreamRequest(core::ChunkCoord coord) {
    const auto it = m_streamRequests.find(coord);
    if (it == m_streamRequests.end()) return;
    if (it->second.stage == StreamRequest::Stage::Generating && m_genPipeline) {
        (void)m_genPipeline->cancel(coord);
    }
    m_streamRequests.erase(it); // A disk answer still to come is ignored
}

void World::requestChunksAround(const glm::vec3& centerPos, int32_t radiusChunks) {
    const int32_t centerCX = core::floorDiv(static_cast<int32_t>(std::floor(centerPos.x)), Chunk::WIDTH);
    const int32_t centerCZ = core::floorDiv(static_cast<int32_t>(std::floor(centerPos.z)), Chunk::DEPTH);
    genPipeline().setCenter({centerCX, centerCZ});
    unloadDistantChunks(centerCX, centerCZ, radiusChunks);

    std::vector<core::ChunkCoord> missing;
    for (int32_t dz = -radiusChunks; dz <= radiusChunks; ++dz) {
        for (int32_t dx = -radiusChunks; dx <= radiusChunks; ++dx) {
            if (dx * dx + dz * dz > radiusChunks * radiusChunks + 2) {
                continue;
            }
            const core::ChunkCoord coord{centerCX + dx, centerCZ + dz};
            if (!m_chunks.contains(coord) && !m_streamRequests.contains(coord)) {
                missing.push_back(coord);
            }
        }
    }
    std::sort(missing.begin(), missing.end(), [centerCX, centerCZ](const core::ChunkCoord& a, const core::ChunkCoord& b) {
        const int32_t da = (a.x - centerCX) * (a.x - centerCX) + (a.z - centerCZ) * (a.z - centerCZ);
        const int32_t db = (b.x - centerCX) * (b.x - centerCX) + (b.z - centerCZ) * (b.z - centerCZ);
        return da < db;
    });
    for (const core::ChunkCoord coord : missing) {
        const uint64_t ticket = m_nextLoadTicket++;
        m_streamRequests.emplace(coord, StreamRequest{StreamRequest::Stage::Loading, {}, ticket});
        m_io->loadAsync(coord, ticket);
    }
}

void World::pumpStreaming() {
    for (storage::LoadResult& result : m_io->takeCompletedLoads()) {
        const auto it = m_streamRequests.find(result.coord);
        if (it == m_streamRequests.end() || it->second.stage != StreamRequest::Stage::Loading ||
            it->second.loadTicket != result.ticket) {
            // Cancelled, loaded synchronously in the meantime, or the answer to an older request
            // (cancelled by ensureChunkLoaded, then edited, unloaded and requested again).
            continue;
        }
        if (result.bundleBytes) {
            it->second.stage = StreamRequest::Stage::Loaded;
            it->second.bundleBytes = std::move(*result.bundleBytes);
            m_loadedOrder.push_back(result.coord);
        } else {
            it->second.stage = StreamRequest::Stage::Generating;
            (void)genPipeline().request(result.coord);
        }
    }
    // The store kept the previous bundle: save these chunks again later.
    for (const storage::SaveJob& failed : m_io->takeFailedSaves()) {
        if (Chunk* chunk = getChunkMutable(failed.coord)) {
            chunk->setModified(true);
        } else {
            core::Logger::error("World", "Chunk ", failed.coord.x, ",", failed.coord.z,
                                " could not be saved and is no longer loaded");
        }
    }
}

World::IntegrationStats World::integrateReadyChunks(std::chrono::nanoseconds budget, size_t maxChunks) {
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();
    pumpStreaming();
    IntegrationStats stats{};
    // One ready chunk, disk reads first (they are what the player saved), or nothing.
    auto integrateOne = [this]() -> bool {
        while (!m_loadedOrder.empty()) {
            const core::ChunkCoord coord = m_loadedOrder.front();
            m_loadedOrder.pop_front();
            const auto it = m_streamRequests.find(coord);
            if (it == m_streamRequests.end() || it->second.stage != StreamRequest::Stage::Loaded) {
                continue;
            }
            std::optional<storage::ChunkBundle> bundle = storage::decodeBundle(it->second.bundleBytes);
            auto chunk = std::make_unique<Chunk>(coord);
            if (!deserializeBundle(bundle, chunk)) {
                // Nothing usable stored (or quarantined just now): generate it like a new chunk.
                it->second = StreamRequest{StreamRequest::Stage::Generating, {}, 0};
                (void)genPipeline().request(coord);
                continue;
            }
            m_streamRequests.erase(it);
            (void)integrateChunk(std::move(chunk), true, std::move(bundle), false);
            return true;
        }
        if (!m_genPipeline) return false;
        while (std::optional<streaming::GeneratedChunk> ready = m_genPipeline->popReady()) {
            const auto it = m_streamRequests.find(ready->coord);
            if (it == m_streamRequests.end() || it->second.stage != StreamRequest::Stage::Generating ||
                m_chunks.contains(ready->coord)) {
                continue;
            }
            m_streamRequests.erase(it);
            (void)integrateChunk(std::move(ready->chunk), false, std::nullopt, true);
            return true;
        }
        return false;
    };
    while (stats.integrated < maxChunks) {
        if (stats.integrated > 0 && Clock::now() - start >= budget) {
            stats.budgetExhausted = !m_loadedOrder.empty() || (m_genPipeline && m_genPipeline->readyResults() > 0);
            break;
        }
        const Clock::time_point chunkStart = Clock::now();
        if (!integrateOne()) break;
        stats.slowestChunk = std::max<std::chrono::nanoseconds>(stats.slowestChunk, Clock::now() - chunkStart);
        ++stats.integrated;
    }
    stats.elapsed = Clock::now() - start;
    m_lastIntegration = stats.elapsed;
    m_integrationWindow.push_back(stats.elapsed);
    if (m_integrationWindow.size() > kIntegrationWindow) {
        m_integrationWindow.pop_front();
    }
    return stats;
}

void World::waitStreamingIdle() {
    for (;;) {
        (void)m_io->waitIdle();
        pumpStreaming();
        if (m_genPipeline) {
            (void)m_genPipeline->waitIdle();
        }
        bool loading = false;
        for (const auto& [coord, request] : m_streamRequests) {
            if (request.stage == StreamRequest::Stage::Loading) {
                loading = true;
                break;
            }
        }
        if (!loading && m_io->pendingLoads() == 0 && m_io->completedLoads() == 0) {
            return;
        }
    }
}

void World::waitDiskIdle() {
    (void)m_io->waitIdle();
}

bool World::flushStorage() {
    const bool ok = m_io->flushAll();
    pumpStreaming(); // Failed saves mark their chunks modified again
    return ok;
}

World::StreamingStats World::getStreamingStats() const {
    StreamingStats stats{};
    for (const auto& [coord, request] : m_streamRequests) {
        switch (request.stage) {
            case StreamRequest::Stage::Loading: ++stats.loading; break;
            case StreamRequest::Stage::Loaded: ++stats.integrating; break;
            case StreamRequest::Stage::Generating: break;
        }
    }
    if (m_genPipeline) {
        stats.generating = m_genPipeline->pendingJobs() + m_genPipeline->runningJobs();
        stats.integrating += m_genPipeline->readyResults();
    }
    stats.writing = m_io->pendingSaves();
    stats.lastIntegration = m_lastIntegration;
    for (const std::chrono::nanoseconds t : m_integrationWindow) {
        stats.maxIntegration = std::max(stats.maxIntegration, t);
    }
    stats.openRegions = m_storage.openRegionCount();
    stats.openRegionBytes = m_storage.openRegionBytes();
    return stats;
}

void World::setSaveDisabled(bool disabled) {
    (void)m_io->waitIdle(); // The worker never sees the flag change under a running write
    m_saveDisabled = disabled;
    m_storage.setReadOnly(disabled);
}

void World::setRegionStorageEnabled(bool enabled) {
    (void)m_io->waitIdle();
    m_storage.setRegionStorageEnabled(enabled);
}

const WorldStorage& World::getStorage() const {
    (void)m_io->waitIdle();
    return m_storage;
}

BlockId World::getBlock(int32_t wx, int32_t wy, int32_t wz) const noexcept {
    if (wy < 0 || wy >= Chunk::HEIGHT) {
        return BlockId::Air;
    }
    const core::ChunkCoord coord{
        core::floorDiv(wx, Chunk::WIDTH),
        core::floorDiv(wz, Chunk::DEPTH)
    };
    const Chunk* chunk = getChunk(coord);
    if (!chunk) {
        return BlockId::Air;
    }
    const int32_t lx = core::floorMod(wx, Chunk::WIDTH);
    const int32_t lz = core::floorMod(wz, Chunk::DEPTH);
    return chunk->getBlock(lx, wy, lz);
}

void World::markNeighborChunksDirty(core::ChunkCoord coord, int32_t lx, int32_t wy, int32_t lz) noexcept {
    // The voxel's own chunk marked itself; only a voxel on a column seam is also inside the
    // 18^3 meshing neighbourhood of sections in the adjacent columns.
    if (lx != 0 && lx != Chunk::WIDTH - 1 && lz != 0 && lz != Chunk::DEPTH - 1) {
        return;
    }
    const streaming::SectionBox box =
        streaming::sectionsSeeingVoxel(coord.x * Chunk::WIDTH + lx, wy, coord.z * Chunk::DEPTH + lz);
    for (int32_t cz = box.min.z; cz <= box.max.z; ++cz) {
        for (int32_t cx = box.min.x; cx <= box.max.x; ++cx) {
            if (cx == coord.x && cz == coord.z) continue;
            if (Chunk* nb = getChunkMutable({cx, cz})) {
                for (int32_t sy = box.min.y; sy <= box.max.y; ++sy) {
                    nb->markSectionDirty(static_cast<size_t>(sy));
                }
            }
        }
    }
}

bool World::setBlock(int32_t wx, int32_t wy, int32_t wz, BlockId id) {
    const int32_t changed = applyBlockEdit(wx, wy, wz, id, EditOrigin::Player);
    notifyFluidColumn(wx, wy, wz, changed);
    if (changed > 0) {
        scheduleFallingChecks(wx, wy, wz);
        if (changed > 1) {
            scheduleFallingChecks(wx, wy + changed - 1, wz); // Above the flora/torches that broke with it
        }
    }
    return changed > 0;
}

bool World::persistSimulationEdits(core::ChunkCoord coord) const noexcept {
    for (int32_t dz = -1; dz <= 1; ++dz) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            const Chunk* c = getChunk({coord.x + dx, coord.z + dz});
            if (c != nullptr && c->isPlayerTouched()) {
                return true;
            }
        }
    }
    return false;
}

int32_t World::applyBlockEdit(int32_t wx, int32_t wy, int32_t wz, BlockId id, EditOrigin origin) {
    if (wy < 0 || wy >= Chunk::HEIGHT) {
        return 0;
    }
    const core::ChunkCoord coord{
        core::floorDiv(wx, Chunk::WIDTH),
        core::floorDiv(wz, Chunk::DEPTH)
    };
    Chunk* chunk = getChunkMutable(coord);
    if (!chunk) {
        return 0;
    }

    const int32_t lx = core::floorMod(wx, Chunk::WIDTH);
    const int32_t lz = core::floorMod(wz, Chunk::DEPTH);
    const BlockId oldId = chunk->getBlock(lx, wy, lz);
    if (oldId == id) {
        return 0;
    }
    const uint8_t oldMeta = chunk->getMetadata(lx, wy, lz);

    if (origin == EditOrigin::Player) {
        chunk->markPlayerTouched();
    }
    const bool markModified = origin == EditOrigin::Player || persistSimulationEdits(coord);
    chunk->setBlock(lx, wy, lz, id, markModified);
    int32_t changed = 1;

    // The single hook for block entities: mining, placing, fluids, pistons and explosions
    // all end up here.
    if (hasBlockEntity(oldId) && !keepsBlockEntity(oldId, id)) {
        if (auto entity = m_blockEntities.take({wx, wy, wz})) {
            m_removedBlockEntities.push_back({{wx, wy, wz}, oldId, std::move(*entity)});
            chunk->setModified(true); // The .be file must lose it too
        }
    }

    // If a supporting block was removed, break unsupported flora or torches directly above
    const size_t firstBreak = m_unsupportedBreaks.size();
    if (id == BlockId::Air && wy + 1 < Chunk::HEIGHT) {
        for (int32_t cy = wy + 1; cy < Chunk::HEIGHT; ++cy) {
            const BlockId above = chunk->getBlock(lx, cy, lz);
            const BlockDef& aboveDef = BlockRegistry::get(above);
            // S8.5: wire and plates lie on the block, a lever or a torch may stand on it.
            const bool standing = (aboveDef.shape == RenderShape::Torch || aboveDef.shape == RenderShape::Lever) &&
                                  attachmentOffset(chunk->getMetadata(lx, cy, lz)).y < 0;
            if (aboveDef.shape == RenderShape::Cross || above == BlockId::Cactus || standing ||
                aboveDef.shape == RenderShape::Crops || aboveDef.shape == RenderShape::Flat) {
                m_unsupportedBreaks.push_back({{wx, cy, wz}, above, chunk->getMetadata(lx, cy, lz)});
                chunk->setBlock(lx, cy, lz, BlockId::Air, markModified);
                ++changed;
            } else {
                break;
            }
        }
    } else if (oldId == BlockId::Farmland && wy + 1 < Chunk::HEIGHT && chunk->getBlock(lx, wy + 1, lz) == BlockId::Crops) {
        // S5.7: crops only stay on farmland (Beta BlockCrops.canThisPlantGrowOnThisBlockID).
        m_unsupportedBreaks.push_back({{wx, wy + 1, wz}, BlockId::Crops, chunk->getMetadata(lx, wy + 1, lz)});
        chunk->setBlock(lx, wy + 1, lz, BlockId::Air, markModified);
        ++changed;
    }

    // Top down, before the edited cell: a broken torch must take its light with it before
    // the opened cell pulls light in from its brightest neighbour.
    for (size_t i = m_unsupportedBreaks.size(); i > firstBreak; --i) {
        const UnsupportedBreak& broken = m_unsupportedBreaks[i - 1];
        m_diffLighting->setBlockAndUpdateLighting(broken.pos.x, broken.pos.y, broken.pos.z, BlockId::Air,
                                                  broken.block);
    }
    m_diffLighting->setBlockAndUpdateLighting(wx, wy, wz, id, oldId);
    for (int32_t i = 0; i < changed; ++i) {
        markNeighborChunksDirty(coord, lx, wy + i, lz);
    }
    if (id == BlockId::Air && (isLogBlock(oldId) || oldId == BlockId::Leaves)) {
        flagLeavesForDecay(wx, wy, wz, isLogBlock(oldId) ? kLeavesSupportRange : 1, origin);
    }
    if (isNormalCube(oldId) && !isNormalCube(id)) {
        breakDetachedWallBlocks(wx, wy, wz, origin);
    }
    if (isDoorBlock(oldId) || oldId == BlockId::Bed) {
        removeOtherHalf(wx, wy, wz, oldId, oldMeta, origin);
    }
    // Beta BlockFarmland.onNeighborBlockChange: a solid block on farmland turns it to dirt.
    if (BlockRegistry::isSolid(id) && wy > 0 && chunk->getBlock(lx, wy - 1, lz) == BlockId::Farmland) {
        if (applyBlockEdit(wx, wy - 1, wz, BlockId::Dirt, origin) > 0) {
            (void)applyMetadataEdit(wx, wy - 1, wz, 0, origin);
            m_fluids->notifyBlockChanged({wx, wy - 1, wz});
        }
    }
    notifyRedstone(wx, wy, wz, oldId, id);
    for (size_t i = firstBreak; i < m_unsupportedBreaks.size(); ++i) {
        const UnsupportedBreak& broken = m_unsupportedBreaks[i];
        notifyRedstone(broken.pos.x, broken.pos.y, broken.pos.z, broken.block, BlockId::Air);
    }
    return changed;
}

void World::removeOtherHalf(int32_t wx, int32_t wy, int32_t wz, BlockId id, uint8_t meta, EditOrigin origin) {
    // Beta BlockDoor/BlockBed.onNeighborBlockChange: a half whose partner went away goes too.
    // The half the caller removed carries the one drop, so this one is not recorded.
    const bool door = isDoorBlock(id);
    const glm::ivec3 d = door ? doorPartnerOffset(meta) : bedPartnerOffset(meta);
    const uint8_t halfBit = door ? kDoorUpperBit : kBedHeadBit;
    const int32_t x = wx + d.x;
    const int32_t y = wy + d.y;
    const int32_t z = wz + d.z;
    if (getBlock(x, y, z) != id || (getMetadata(x, y, z) & halfBit) == (meta & halfBit)) {
        return; // Not its other half
    }
    const int32_t changed = applyBlockEdit(x, y, z, BlockId::Air, origin);
    if (changed > 0) {
        (void)applyMetadataEdit(x, y, z, 0, origin);
        notifyFluidColumn(x, y, z, changed);
    }
}

void World::breakDetachedWallBlocks(int32_t wx, int32_t wy, int32_t wz, EditOrigin origin) {
    // S5.5: the support check of the column above, extended to the side neighbours for
    // blocks hanging on a wall (Beta BlockLadder.onNeighborBlockChange).
    static constexpr std::array<glm::ivec3, 4> kSides{{{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}}};
    for (const glm::ivec3& d : kSides) {
        const int32_t x = wx + d.x;
        const int32_t z = wz + d.z;
        const BlockId side = getBlock(x, wy, z);
        const uint8_t sideMeta = getMetadata(x, wy, z);
        // S8.5: levers, buttons and redstone torches hanging on that wall go too.
        const bool hangs = side == BlockId::Ladder ? ladderSupportOffset(sideMeta) == -d
                         : (side == BlockId::Lever || side == BlockId::StoneButton || side == BlockId::RedstoneTorchOn ||
                            side == BlockId::RedstoneTorchOff) && attachmentOffset(sideMeta) == -d;
        if (!hangs) {
            continue;
        }
        if (applyBlockEdit(x, wy, z, BlockId::Air, origin) > 0) {
            (void)applyMetadataEdit(x, wy, z, 0, origin);
            m_unsupportedBreaks.push_back({{x, wy, z}, side, sideMeta});
            m_fluids->notifyBlockChanged({x, wy, z});
        }
    }
    // S5.6: a door stands on a normal cube (Beta BlockDoor.onNeighborBlockChange); its lower
    // half breaks with the drop and takes the upper half along.
    if (const BlockId door = getBlock(wx, wy + 1, wz); wy + 1 < Chunk::HEIGHT && isDoorBlock(door)) {
        const uint8_t meta = getMetadata(wx, wy + 1, wz);
        if ((meta & kDoorUpperBit) == 0 && applyBlockEdit(wx, wy + 1, wz, BlockId::Air, origin) > 0) {
            (void)applyMetadataEdit(wx, wy + 1, wz, 0, origin);
            m_unsupportedBreaks.push_back({{wx, wy + 1, wz}, door, meta});
            m_fluids->notifyBlockChanged({wx, wy + 1, wz});
        }
    }
}

uint8_t World::getMetadata(int32_t wx, int32_t wy, int32_t wz) const noexcept {
    if (wy < 0 || wy >= Chunk::HEIGHT) return 0;
    const Chunk* chunk = getChunk({core::floorDiv(wx, Chunk::WIDTH), core::floorDiv(wz, Chunk::DEPTH)});
    if (!chunk) return 0;
    return chunk->getMetadata(core::floorMod(wx, Chunk::WIDTH), wy, core::floorMod(wz, Chunk::DEPTH));
}

bool World::setMetadata(int32_t wx, int32_t wy, int32_t wz, uint8_t meta) {
    const bool changed = applyMetadataEdit(wx, wy, wz, meta, EditOrigin::Player);
    notifyFluidColumn(wx, wy, wz, changed ? 1 : 0);
    return changed;
}

bool World::applyMetadataEdit(int32_t wx, int32_t wy, int32_t wz, uint8_t meta, EditOrigin origin) {
    if (wy < 0 || wy >= Chunk::HEIGHT) return false;
    const core::ChunkCoord coord{core::floorDiv(wx, Chunk::WIDTH), core::floorDiv(wz, Chunk::DEPTH)};
    Chunk* chunk = getChunkMutable(coord);
    if (!chunk) return false;
    const int32_t lx = core::floorMod(wx, Chunk::WIDTH);
    const int32_t lz = core::floorMod(wz, Chunk::DEPTH);
    const uint8_t nibble = meta & 0x0FU;
    if (chunk->getMetadata(lx, wy, lz) == nibble) return false;
    if (origin == EditOrigin::Player) {
        chunk->markPlayerTouched();
    }
    const bool markModified = origin == EditOrigin::Player || persistSimulationEdits(coord);
    chunk->setMetadata(lx, wy, lz, nibble, markModified);
    // S5.4: re-mesh only where the metadata shapes the block (door state, ladder side, crop
    // stage...). Fluid level changes keep re-meshing nothing.
    const BlockId id = chunk->getBlock(lx, wy, lz);
    if (shapeDependsOnMetadata(BlockRegistry::get(id).shape)) {
        chunk->markSectionsAround(wy);
        markNeighborChunksDirty(coord, lx, wy, lz);
    }
    if (m_redstoneEnabled && redstone::isRedstoneComponent(id)) {
        m_redstone->onBlockChanged({wx, wy, wz});
    }
    return true;
}

bool World::setBlockWithMetadata(int32_t wx, int32_t wy, int32_t wz, BlockId id, uint8_t meta) {
    const int32_t blockChanged = applyBlockEdit(wx, wy, wz, id, EditOrigin::Player);
    const bool metaChanged = applyMetadataEdit(wx, wy, wz, meta, EditOrigin::Player);
    notifyFluidColumn(wx, wy, wz, std::max(blockChanged, metaChanged ? 1 : 0));
    if (blockChanged > 0) {
        scheduleFallingChecks(wx, wy, wz);
        if (blockChanged > 1) {
            scheduleFallingChecks(wx, wy + blockChanged - 1, wz);
        }
    }
    return blockChanged > 0 || metaChanged;
}

void World::markBlockEntityChanged(core::BlockPos pos) noexcept {
    if (pos.y < 0 || pos.y >= Chunk::HEIGHT) return;
    if (Chunk* chunk = getChunkMutable(chunkOf(pos.x, pos.z))) {
        chunk->setModified(true);
        chunk->markPlayerTouched();
    }
}

std::vector<World::RemovedBlockEntity> World::takeRemovedBlockEntities() noexcept {
    std::vector<RemovedBlockEntity> out;
    out.swap(m_removedBlockEntities);
    return out;
}

std::vector<World::UnsupportedBreak> World::takeUnsupportedBreaks() noexcept {
    std::vector<UnsupportedBreak> out;
    out.swap(m_unsupportedBreaks);
    return out;
}

void World::notifyFluidColumn(int32_t wx, int32_t wy, int32_t wz, int32_t cells) {
    for (int32_t i = 0; i < cells; ++i) {
        m_fluids->notifyBlockChanged({wx, wy + i, wz});
    }
}

void World::scheduleUnsettledFluids(core::ChunkCoord coord) {
    // Worldgen and saves write fluid without the simulator, and fluid that stopped at this
    // chunk's border (unloaded cells are walls) may now move into it. Queue only the cells
    // whose update would change something: a whole ocean is settled and costs no ticks.
    const int32_t x0 = coord.x * Chunk::WIDTH;
    const int32_t z0 = coord.z * Chunk::DEPTH;
    const auto visit = [this](int32_t wx, int32_t wy, int32_t wz) {
        const core::BlockPos p{wx, wy, wz};
        if (!m_fluids->isSettled(p)) {
            m_fluids->scheduleUpdate(p);
        }
    };
    if (const Chunk* chunk = getChunk(coord)) {
        for (size_t s = 0; s < Chunk::SECTIONS_PER_CHUNK; ++s) {
            const PalettedSection& section = chunk->getSection(s);
            if (section.isHomogeneous() && !BlockRegistry::isLiquid(section.getBlock(0, 0, 0))) {
                continue;
            }
            const int32_t yBase = static_cast<int32_t>(s) * PalettedSection::SIZE;
            for (int32_t y = yBase; y < yBase + PalettedSection::SIZE; ++y) {
                for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz) {
                    for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
                        if (BlockRegistry::isLiquid(chunk->getBlock(lx, y, lz))) {
                            visit(x0 + lx, y, z0 + lz);
                        }
                    }
                }
            }
        }
    }
    // The ring just outside, in resident neighbours.
    const auto visitColumn = [&](int32_t wx, int32_t wz) {
        const Chunk* nb = getChunk(chunkOf(wx, wz));
        if (!nb) return;
        const int32_t lx = core::floorMod(wx, Chunk::WIDTH);
        const int32_t lz = core::floorMod(wz, Chunk::DEPTH);
        for (int32_t y = 0; y < Chunk::HEIGHT; ++y) {
            if (BlockRegistry::isLiquid(nb->getBlock(lx, y, lz))) {
                visit(wx, y, wz);
            }
        }
    };
    for (int32_t i = 0; i < Chunk::WIDTH; ++i) {
        visitColumn(x0 + i, z0 - 1);
        visitColumn(x0 + i, z0 + Chunk::DEPTH);
    }
    for (int32_t i = 0; i < Chunk::DEPTH; ++i) {
        visitColumn(x0 - 1, z0 + i);
        visitColumn(x0 + Chunk::WIDTH, z0 + i);
    }
}

bool World::triggerPistonPush(core::BlockPos pistonPos, core::FaceDir pushDir) {
    if (getBlock(pistonPos) != BlockId::Piston) {
        return false;
    }

    const glm::ivec3 step = core::faceNormal(pushDir);
    static constexpr int32_t MAX_PUSH_BLOCKS = 12; // Classic Beta 1.7 piston limit!

    int32_t chainLength = 0;
    std::optional<core::BlockPos> destroyed;
    for (int32_t i = 1; i <= MAX_PUSH_BLOCKS + 1; ++i) {
        const core::BlockPos checkPos{
            pistonPos.x + step.x * i,
            pistonPos.y + step.y * i,
            pistonPos.z + step.z * i
        };
        if (checkPos.y <= 0 || checkPos.y >= Chunk::HEIGHT) {
            return false;
        }
        const BlockId b = getBlock(checkPos);
        const BlockDef& def = BlockRegistry::get(b);
        if (def.replaceable) {
            chainLength = i - 1;
            break;
        }
        // Beta getMobilityFlag() == 1 (BlockDoor, BlockBed, Material.circuits/plants): the
        // block ends the line and breaks with its drop instead of moving, so a door or a bed
        // never ends up as an orphaned half and a crop never leaves its farmland.
        if (isDoorBlock(b) || b == BlockId::Bed || b == BlockId::Ladder || b == BlockId::Crops ||
            b == BlockId::RedstoneWire || b == BlockId::Lever || b == BlockId::StoneButton ||
            b == BlockId::RedstoneTorchOff || b == BlockId::RedstoneTorchOn ||
            b == BlockId::StonePressurePlate || b == BlockId::WoodPressurePlate) {
            chainLength = i - 1;
            destroyed = checkPos;
            break;
        }
        if (b == BlockId::Bedrock || b == BlockId::Obsidian || b == BlockId::MobSpawner || i == MAX_PUSH_BLOCKS + 1) {
            return false; // Immovable or exceeds 12-block limit
        }
        if (hasBlockEntity(b) || m_blockEntities.find(checkPos) != nullptr) {
            return false; // Beta BlockPistonBase.canPushBlock: tile entities never move
        }
    }

    if (chainLength == 0 && !destroyed) {
        return false;
    }
    if (destroyed) {
        // Its other half (door, bed) goes without a drop through applyBlockEdit.
        const BlockId id = getBlock(*destroyed);
        const uint8_t meta = getMetadata(*destroyed);
        if (setBlockWithMetadata(destroyed->x, destroyed->y, destroyed->z, BlockId::Air, 0)) {
            m_unsupportedBreaks.push_back({*destroyed, id, meta});
        }
    }
    if (chainLength == 0) {
        return true;
    }

    // Shift blocks from back to front with their metadata (stairs orientation, farmland moisture).
    for (int32_t i = chainLength; i >= 1; --i) {
        const core::BlockPos src{
            pistonPos.x + step.x * i,
            pistonPos.y + step.y * i,
            pistonPos.z + step.z * i
        };
        const core::BlockPos dst{
            pistonPos.x + step.x * (i + 1),
            pistonPos.y + step.y * (i + 1),
            pistonPos.z + step.z * (i + 1)
        };
        (void)setBlockWithMetadata(dst.x, dst.y, dst.z, getBlock(src), getMetadata(src));
    }

    const core::BlockPos frontPos{
        pistonPos.x + step.x,
        pistonPos.y + step.y,
        pistonPos.z + step.z
    };
    setBlock(frontPos, BlockId::Air);
    return true;
}

uint8_t World::getSkyLight(int32_t wx, int32_t wy, int32_t wz) const noexcept {
    if (wy >= Chunk::HEIGHT) return 15;
    if (wy < 0) return 0;
    const core::ChunkCoord coord{
        core::floorDiv(wx, Chunk::WIDTH),
        core::floorDiv(wz, Chunk::DEPTH)
    };
    const Chunk* chunk = getChunk(coord);
    if (!chunk) return 15;
    return chunk->getSkyLight(core::floorMod(wx, Chunk::WIDTH), wy, core::floorMod(wz, Chunk::DEPTH));
}

uint8_t World::getBlockLight(int32_t wx, int32_t wy, int32_t wz) const noexcept {
    if (wy < 0 || wy >= Chunk::HEIGHT) return 0;
    const core::ChunkCoord coord{
        core::floorDiv(wx, Chunk::WIDTH),
        core::floorDiv(wz, Chunk::DEPTH)
    };
    const Chunk* chunk = getChunk(coord);
    if (!chunk) return 0;
    return chunk->getBlockLight(core::floorMod(wx, Chunk::WIDTH), wy, core::floorMod(wz, Chunk::DEPTH));
}

void World::setSkyLight(int32_t wx, int32_t wy, int32_t wz, uint8_t val) noexcept {
    if (wy < 0 || wy >= Chunk::HEIGHT) return;
    const core::ChunkCoord coord{
        core::floorDiv(wx, Chunk::WIDTH),
        core::floorDiv(wz, Chunk::DEPTH)
    };
    if (Chunk* chunk = getChunkMutable(coord)) {
        const int32_t lx = core::floorMod(wx, Chunk::WIDTH);
        const int32_t lz = core::floorMod(wz, Chunk::DEPTH);
        chunk->setSkyLight(lx, wy, lz, val);
        markNeighborChunksDirty(coord, lx, wy, lz);
    }
}

void World::setBlockLight(int32_t wx, int32_t wy, int32_t wz, uint8_t val) noexcept {
    if (wy < 0 || wy >= Chunk::HEIGHT) return;
    const core::ChunkCoord coord{
        core::floorDiv(wx, Chunk::WIDTH),
        core::floorDiv(wz, Chunk::DEPTH)
    };
    if (Chunk* chunk = getChunkMutable(coord)) {
        const int32_t lx = core::floorMod(wx, Chunk::WIDTH);
        const int32_t lz = core::floorMod(wz, Chunk::DEPTH);
        chunk->setBlockLight(lx, wy, lz, val);
        markNeighborChunksDirty(coord, lx, wy, lz);
    }
}

namespace {

void syncLightPair(
    Chunk& a, int32_t ax, int32_t ay, int32_t az,
    Chunk& b, int32_t bx, int32_t by, int32_t bz
) noexcept {
    const BlockDef& defA = BlockRegistry::get(a.getBlock(ax, ay, az));
    const BlockDef& defB = BlockRegistry::get(b.getBlock(bx, by, bz));

    // 1. Block Light
    const uint8_t blkA = a.getBlockLight(ax, ay, az);
    const uint8_t blkB = b.getBlockLight(bx, by, bz);

    if (blkA > 1 && defB.lightOpacity < 15) {
        const int32_t costB = std::max<int32_t>(1, defB.lightOpacity);
        if (static_cast<int32_t>(blkA) - costB > static_cast<int32_t>(blkB)) {
            b.spreadBlockLight(bx, by, bz, static_cast<uint8_t>(blkA - costB));
        }
    }
    if (blkB > 1 && defA.lightOpacity < 15) {
        const int32_t costA = std::max<int32_t>(1, defA.lightOpacity);
        if (static_cast<int32_t>(blkB) - costA > static_cast<int32_t>(blkA)) {
            a.spreadBlockLight(ax, ay, az, static_cast<uint8_t>(blkB - costA));
        }
    }

    // 2. Sky Light
    const uint8_t skyA = a.getSkyLight(ax, ay, az);
    const uint8_t skyB = b.getSkyLight(bx, by, bz);

    if (skyA > 1 && defB.lightOpacity < 15) {
        const int32_t costB = std::max<int32_t>(1, defB.lightOpacity);
        if (static_cast<int32_t>(skyA) - costB > static_cast<int32_t>(skyB)) {
            b.spreadSkyLight(bx, by, bz, static_cast<uint8_t>(skyA - costB));
        }
    }
    if (skyB > 1 && defA.lightOpacity < 15) {
        const int32_t costA = std::max<int32_t>(1, defA.lightOpacity);
        if (static_cast<int32_t>(skyB) - costA > static_cast<int32_t>(skyA)) {
            a.spreadSkyLight(ax, ay, az, static_cast<uint8_t>(skyB - costA));
        }
    }
}

} // namespace

void World::syncBorderLighting(Chunk& chunkA, Chunk& chunkB) noexcept {
    const auto ca = chunkA.getCoord();
    const auto cb = chunkB.getCoord();

    if (ca.x + 1 == cb.x && ca.z == cb.z) {
        // chunkB is East (+X) of chunkA
        for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz) {
            for (int32_t y = 0; y < Chunk::HEIGHT; ++y) {
                syncLightPair(chunkA, Chunk::WIDTH - 1, y, lz, chunkB, 0, y, lz);
            }
        }
    } else if (ca.x - 1 == cb.x && ca.z == cb.z) {
        // chunkB is West (-X) of chunkA
        for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz) {
            for (int32_t y = 0; y < Chunk::HEIGHT; ++y) {
                syncLightPair(chunkA, 0, y, lz, chunkB, Chunk::WIDTH - 1, y, lz);
            }
        }
    } else if (ca.x == cb.x && ca.z + 1 == cb.z) {
        // chunkB is North (+Z) of chunkA
        for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
            for (int32_t y = 0; y < Chunk::HEIGHT; ++y) {
                syncLightPair(chunkA, lx, y, Chunk::DEPTH - 1, chunkB, lx, y, 0);
            }
        }
    } else if (ca.x == cb.x && ca.z - 1 == cb.z) {
        // chunkB is South (-Z) of chunkA
        for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
            for (int32_t y = 0; y < Chunk::HEIGHT; ++y) {
                syncLightPair(chunkA, lx, y, 0, chunkB, lx, y, Chunk::DEPTH - 1);
            }
        }
    }
}

void World::syncChunkLightingWithNeighbors(core::ChunkCoord coord) noexcept {
    Chunk* chunk = getChunkMutable(coord);
    if (!chunk) return;

    static constexpr int32_t ndx[4] = {1, -1, 0, 0};
    static constexpr int32_t ndz[4] = {0, 0, 1, -1};

    for (int i = 0; i < 4; ++i) {
        if (Chunk* nb = getChunkMutable({coord.x + ndx[i], coord.z + ndz[i]})) {
            syncBorderLighting(*chunk, *nb);
        }
    }
}

glm::vec3 World::getGrassTint(int32_t wx, int32_t wz) const noexcept {
    const core::ChunkCoord coord{
        core::floorDiv(wx, Chunk::WIDTH),
        core::floorDiv(wz, Chunk::DEPTH)
    };
    const Chunk* chunk = getChunk(coord);
    if (!chunk) {
        return m_worldGen.sampleBiome(wx, wz).grassTint;
    }
    return chunk->getGrassTint(core::floorMod(wx, Chunk::WIDTH), core::floorMod(wz, Chunk::DEPTH));
}

glm::vec3 World::getFoliageTint(int32_t wx, int32_t wz) const noexcept {
    const core::ChunkCoord coord{
        core::floorDiv(wx, Chunk::WIDTH),
        core::floorDiv(wz, Chunk::DEPTH)
    };
    const Chunk* chunk = getChunk(coord);
    if (!chunk) {
        return m_worldGen.sampleBiome(wx, wz).foliageTint;
    }
    return chunk->getFoliageTint(core::floorMod(wx, Chunk::WIDTH), core::floorMod(wz, Chunk::DEPTH));
}

BiomeId World::getBiomeAt(int32_t wx, int32_t wz) const noexcept {
    const core::ChunkCoord coord{
        core::floorDiv(wx, Chunk::WIDTH),
        core::floorDiv(wz, Chunk::DEPTH)
    };
    const Chunk* chunk = getChunk(coord);
    if (!chunk) {
        return m_worldGen.sampleBiome(wx, wz).biome;
    }
    return chunk->getColumnBiome(core::floorMod(wx, Chunk::WIDTH), core::floorMod(wz, Chunk::DEPTH));
}

glm::vec3 World::findSafeSpawnPosition() noexcept {
    // Load initial 3x3 chunks around origin
    for (int32_t cz = -1; cz <= 1; ++cz) {
        for (int32_t cx = -1; cx <= 1; ++cx) {
            ensureChunkLoaded({cx, cz});
        }
    }

    // Spiral search for dry land above sea level near (8, 8)
    for (int32_t radius = 0; radius <= 20; ++radius) {
        for (int32_t dz = -radius; dz <= radius; ++dz) {
            for (int32_t dx = -radius; dx <= radius; ++dx) {
                const int32_t wx = 8 + dx * 2;
                const int32_t wz = 8 + dz * 2;
                // The whole 3x3 so the candidate chunk is decorated: a tree planted after the
                // search could otherwise grow through the player.
                const core::ChunkCoord cc{core::floorDiv(wx, Chunk::WIDTH), core::floorDiv(wz, Chunk::DEPTH)};
                for (int32_t nz = -1; nz <= 1; ++nz) {
                    for (int32_t nx = -1; nx <= 1; ++nx) {
                        ensureChunkLoaded({cc.x + nx, cc.z + nz});
                    }
                }
                for (int32_t y = Chunk::HEIGHT - 3; y >= core::EngineConfig::SEA_LEVEL; --y) {
                    const BlockId ground = getBlock(wx, y, wz);
                    const BlockId feet = getBlock(wx, y + 1, wz);
                    const BlockId head = getBlock(wx, y + 2, wz);
                    if (BlockRegistry::isSolid(ground) && ground != BlockId::Leaves && ground != BlockId::Cactus &&
                        !BlockRegistry::isSolid(feet) && !BlockRegistry::isLiquid(feet) &&
                        !BlockRegistry::isSolid(head) && !BlockRegistry::isLiquid(head)) {
                        return glm::vec3(static_cast<float>(wx) + 0.5f, static_cast<float>(y + 1) + 0.01f, static_cast<float>(wz) + 0.5f);
                    }
                }
            }
        }
    }

    return glm::vec3(8.5f, 82.0f, 8.5f);
}

glm::vec3 World::findStandableAbove(glm::vec3 pos) const noexcept {
    const auto blocksPlayer = [](BlockId id) noexcept {
        return BlockRegistry::isSolid(id) || BlockRegistry::isLiquid(id);
    };
    const int32_t wx = static_cast<int32_t>(std::floor(pos.x));
    const int32_t wz = static_cast<int32_t>(std::floor(pos.z));
    const int32_t startY = std::max(0, static_cast<int32_t>(std::floor(pos.y)));
    for (int32_t y = startY; y + 1 < Chunk::HEIGHT; ++y) {
        if (!blocksPlayer(getBlock(wx, y, wz)) && !blocksPlayer(getBlock(wx, y + 1, wz))) {
            // Keep the original feet height when it is already free (e.g. 70.01 on a slab top).
            const float feetY = y == startY ? std::max(pos.y, static_cast<float>(y)) : static_cast<float>(y) + 0.01f;
            return glm::vec3(static_cast<float>(wx) + 0.5f, feetY, static_cast<float>(wz) + 0.5f);
        }
    }
    return pos;
}

storage::MigrationReport World::migrateLegacyStorage(const std::function<void(float)>& progress) {
    if (m_saveDisabled) {
        return {};
    }
    (void)m_io->flushAll(); // The migration reopens the region files itself
    return m_storage.migrateLegacyChunks(progress);
}

void World::saveModifiedChunks(const WorldLevelData& levelData) {
    queueModifiedChunks(levelData);
    // Waits for the queued writes, then region headers after their data.
    (void)flushStorage();
}

void World::queueModifiedChunks(const WorldLevelData& levelData) {
    if (m_saveDisabled) {
        return;
    }
    (void)m_storage.saveLevelData(levelData);
    size_t savedCount = 0;
    for (auto& [coord, chunk] : m_chunks) {
        if (chunk->isModified()) {
            if (persistChunk(*chunk)) {
                chunk->setModified(false);
                ++savedCount;
            }
        }
    }
    if (savedCount > 0) {
        core::Logger::info("World", "Queued ", savedCount, " modified chunks for saving.");
    }
}

} // namespace betacraft::world
