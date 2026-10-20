#include "worldgen/ColumnSampling.hpp"

#include "voxel/Chunk.hpp"

namespace betacraft::worldgen {

namespace {

// 64-bit offsets: chunk coords near the int32 world limit must not overflow while comparing.
[[nodiscard]] constexpr bool inChunk(int64_t d) noexcept { return d >= 0 && d < voxel::Chunk::WIDTH; }
[[nodiscard]] constexpr bool inRing(int64_t d) noexcept {
    return d >= -ChunkColumnCache::MARGIN && d < voxel::Chunk::WIDTH + ChunkColumnCache::MARGIN;
}

} // namespace

static_assert(voxel::Chunk::WIDTH == voxel::Chunk::DEPTH, "cache assumes square chunks");

ChunkColumnCache::ChunkColumnCache(const ColumnSampler& source, core::ChunkCoord coord) noexcept
    : m_source(source),
      m_coord(coord),
      m_baseX(static_cast<int64_t>(coord.x) * voxel::Chunk::WIDTH),
      m_baseZ(static_cast<int64_t>(coord.z) * voxel::Chunk::DEPTH) {
    for (int64_t dz = -MARGIN; dz < voxel::Chunk::DEPTH + MARGIN; ++dz) {
        for (int64_t dx = -MARGIN; dx < voxel::Chunk::WIDTH + MARGIN; ++dx) {
            if (!inChunk(dx) && !inChunk(dz)) continue; // ring corner
            m_columns[slot(dx, dz)] =
                m_source.sampleColumn(static_cast<int32_t>(m_baseX + dx), static_cast<int32_t>(m_baseZ + dz));
        }
    }
}

bool ChunkColumnCache::contains(int32_t wx, int32_t wz) const noexcept {
    const int64_t dx = static_cast<int64_t>(wx) - m_baseX;
    const int64_t dz = static_cast<int64_t>(wz) - m_baseZ;
    return inRing(dx) && inRing(dz) && (inChunk(dx) || inChunk(dz));
}

ColumnContext ChunkColumnCache::sampleColumn(int32_t wx, int32_t wz) const noexcept {
    if (!contains(wx, wz)) return m_source.sampleColumn(wx, wz);
    return m_columns[slot(static_cast<int64_t>(wx) - m_baseX, static_cast<int64_t>(wz) - m_baseZ)];
}

} // namespace betacraft::worldgen
