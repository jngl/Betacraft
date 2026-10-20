#pragma once

#include "acoustics/AcousticOcclusion.hpp"
#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"

#include <cstdint>
#include <utility>

namespace betacraft::world {
class World;
}

namespace betacraft::acoustics {

// Acoustic solidity of a block: anything the registry marks solid, except liquids (water transmits
// sound well; muffling under water is a separate effect). Thin decorations (torches, flowers, tall
// grass) are non-solid in the registry and therefore transparent. Requires BlockRegistry::initialize().
[[nodiscard]] bool blocksSound(world::BlockId id) noexcept;

// THREADING: world::World keeps its chunks in an unsynchronised unordered_map that the game thread
// mutates while streaming (inserts may rehash). Every World-backed query below is therefore GAME-THREAD
// ONLY: run the OcclusionScheduler / EnclosureMonitor there, and let other producers (physics, mob AI,
// fluids) hand their sound positions to it instead of ray-marching the World themselves. Off-thread
// producers that must ray-march need an immutable snapshot (e.g. a chunk-pipeline snapshot) wrapped
// in their own VoxelSolidityQuery.

// Adapts any block source exposing `BlockId getBlock(core::BlockPos) const` (world::World, a chunk
// snapshot, a test fake) to the VoxelSolidityQuery used by the DDA. Holds a non-owning pointer.
// Unloaded chunks read as Air in World, so sound simply passes through not-yet-generated terrain.
// Simple but costs one chunk-map lookup per voxel; prefer ChunkCachedSolidity on the hot path.
template <typename BlockSource>
class BlockSoliditySource final : public VoxelSolidityQuery {
public:
    explicit BlockSoliditySource(const BlockSource& source) noexcept : m_source(&source) {}

    [[nodiscard]] bool isSolid(const core::BlockPos& pos) const noexcept override {
        return blocksSound(m_source->getBlock(pos));
    }

private:
    const BlockSource* m_source;
};

// Instantiated where world/World.hpp is included (e.g. the integration code in Application).
using WorldSolidityQuery = BlockSoliditySource<world::World>;

// Same answers as BlockSoliditySource, but remembers the last chunk: a 48-block ray crosses at most a
// handful of 16x16 columns, so it costs ~3 hash lookups instead of up to 144 (needed for the spec's
// < 0.5 us per ray). ChunkSource exposes `const ChunkT* getChunk(core::ChunkCoord) const` and ChunkT
// `BlockId getBlock(int32_t lx, int32_t y, int32_t lz) const` (bounds-checked, Air outside).
// The cached pointer is only valid while no chunk is loaded or unloaded: construct one per scheduler
// update on the game thread (it is a few bytes) or call invalidate() after the World changed.
template <typename ChunkSource, int32_t ChunkWidth = world::Chunk::WIDTH, int32_t ChunkDepth = world::Chunk::DEPTH>
class ChunkCachedSolidity final : public VoxelSolidityQuery {
public:
    explicit ChunkCachedSolidity(const ChunkSource& source) noexcept : m_source(&source) {}

    [[nodiscard]] bool isSolid(const core::BlockPos& pos) const noexcept override {
        const core::ChunkCoord coord{core::floorDiv(pos.x, ChunkWidth), core::floorDiv(pos.z, ChunkDepth)};
        if (!m_cached || !(coord == m_coord)) {
            m_chunk = m_source->getChunk(coord);
            m_coord = coord;
            m_cached = true;
        }
        if (m_chunk == nullptr) {
            return false; // Unloaded: open air, like World::getBlock
        }
        return blocksSound(m_chunk->getBlock(core::floorMod(pos.x, ChunkWidth), pos.y, core::floorMod(pos.z, ChunkDepth)));
    }

    void invalidate() noexcept { m_cached = false; }

private:
    using ChunkPtr = decltype(std::declval<const ChunkSource&>().getChunk(core::ChunkCoord{}));

    const ChunkSource* m_source;
    // Memoisation only (isSolid is logically const).
    mutable ChunkPtr m_chunk{nullptr};
    mutable core::ChunkCoord m_coord{};
    mutable bool m_cached{false};
};

using WorldChunkSolidityQuery = ChunkCachedSolidity<world::World>;

// computeVoxelOcclusion() against the live World (game thread only, see WorldSolidityQuery).
[[nodiscard]] float computeVoxelOcclusion(const world::World& world, const glm::vec3& listener,
                                          const glm::vec3& soundPos) noexcept;

} // namespace betacraft::acoustics
