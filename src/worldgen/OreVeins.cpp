#include "worldgen/OreVeins.hpp"

#include "voxel/Chunk.hpp"

namespace betacraft::worldgen {

void placeOreVeins(world::Chunk& chunk, uint64_t seed) {
    const core::ChunkCoord coord = chunk.getCoord();
    const int32_t baseX = coord.x * world::Chunk::WIDTH;
    const int32_t baseZ = coord.z * world::Chunk::DEPTH;
    const OreClip clip{baseX, baseX + world::Chunk::WIDTH - 1, baseZ, baseZ + world::Chunk::DEPTH - 1};
    // Increasing (z, x): any two chunks see their common sources in the same order.
    for (int32_t dz = -1; dz <= 1; ++dz) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            forEachOreVeinCell(seed, {coord.x + dx, coord.z + dz},
                               [&](world::BlockId ore, int32_t wx, int32_t wy, int32_t wz) {
                                   const int32_t lx = wx - baseX;
                                   const int32_t lz = wz - baseZ;
                                   if (lx < 0 || lx >= world::Chunk::WIDTH || lz < 0 || lz >= world::Chunk::DEPTH ||
                                       wy < 0 || wy >= world::Chunk::HEIGHT) {
                                       return;
                                   }
                                   if (chunk.getBlock(lx, wy, lz) == world::BlockId::Stone) {
                                       chunk.setBlock(lx, wy, lz, ore, false);
                                   }
                               },
                               clip);
        }
    }
}

} // namespace betacraft::worldgen
