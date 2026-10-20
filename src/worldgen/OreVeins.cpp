#include "worldgen/OreVeins.hpp"

#include "voxel/Chunk.hpp"

namespace betacraft::worldgen {

void placeOreVeins(voxel::Chunk& chunk, uint64_t seed) {
    const core::ChunkCoord coord = chunk.getCoord();
    const int32_t baseX = coord.x * voxel::Chunk::WIDTH;
    const int32_t baseZ = coord.z * voxel::Chunk::DEPTH;
    const OreClip clip{baseX, baseX + voxel::Chunk::WIDTH - 1, baseZ, baseZ + voxel::Chunk::DEPTH - 1};
    // Increasing (z, x): any two chunks see their common sources in the same order.
    for (int32_t dz = -1; dz <= 1; ++dz) {
        for (int32_t dx = -1; dx <= 1; ++dx) {
            forEachOreVeinCell(seed, {coord.x + dx, coord.z + dz},
                               [&](voxel::BlockId ore, int32_t wx, int32_t wy, int32_t wz) {
                                   const int32_t lx = wx - baseX;
                                   const int32_t lz = wz - baseZ;
                                   if (lx < 0 || lx >= voxel::Chunk::WIDTH || lz < 0 || lz >= voxel::Chunk::DEPTH ||
                                       wy < 0 || wy >= voxel::Chunk::HEIGHT) {
                                       return;
                                   }
                                   if (chunk.getBlock(lx, wy, lz) == voxel::BlockId::Stone) {
                                       chunk.setBlock(lx, wy, lz, ore, false);
                                   }
                               },
                               clip);
        }
    }
}

} // namespace betacraft::worldgen
