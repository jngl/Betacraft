#pragma once
// Shared helpers for the worldgen integration tests (P3.3 caves + P3.4 two-pass
// decoration through the real World / WorldGen API).

#include "core/Config.hpp"
#include "core/Types.hpp"
#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"
#include "world/World.hpp"
#include "worldgen/DecorationFeatures.hpp"

#include <cstdint>
#include <vector>

namespace betacraft::test::worldgen_support {

using voxel::BlockId;
using voxel::Chunk;

// Seed whose spawn area mixes forest (border trees), plains, ocean and caves.
constexpr uint64_t kSeed = 173173173ULL;

[[nodiscard]] inline std::vector<core::ChunkCoord> squareRegion(core::ChunkCoord center, int32_t radius) {
    std::vector<core::ChunkCoord> out;
    for (int32_t dz = -radius; dz <= radius; ++dz) {
        for (int32_t dx = -radius; dx <= radius; ++dx) {
            out.push_back({center.x + dx, center.z + dz});
        }
    }
    return out;
}

inline void loadAll(world::World& w, const std::vector<core::ChunkCoord>& coords) {
    for (const core::ChunkCoord c : coords) (void)w.ensureChunkLoaded(c);
}

// Logs, leaves, flora, cactus and sugar cane: everything pass 2 writes except soil under trunks.
[[nodiscard]] inline bool isDecorationBlock(BlockId b) noexcept {
    return worldgen::decorationPriority(b) > 0;
}

[[nodiscard]] inline bool isLogBlock(BlockId b) noexcept {
    return b == BlockId::OakLog || b == BlockId::BirchLog || b == BlockId::PineLog;
}

[[nodiscard]] inline int32_t countDecoration(const Chunk& c) noexcept {
    int32_t n = 0;
    for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x)
                if (isDecorationBlock(c.getBlock(x, y, z))) ++n;
    return n;
}

// First voxel where two chunks differ, or -1 when they hold identical blocks.
[[nodiscard]] inline int64_t firstBlockDifference(const Chunk& a, const Chunk& b) noexcept {
    for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x)
                if (a.getBlock(x, y, z) != b.getBlock(x, y, z)) {
                    return static_cast<int64_t>(Chunk::blockIndex(x, y, z));
                }
    return -1;
}

} // namespace betacraft::test::worldgen_support
