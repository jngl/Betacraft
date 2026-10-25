// Integration hook - WorldGen::columnContext is the pass-1 column function the carver
// uses for ring columns of chunks that are not generated yet. It must give exactly what
// CaveCarver::scanColumn reads from the generated chunk, or carving would differ on either
// side of a chunk border depending on which chunk was generated first.
#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"
#include "world/WorldGen.hpp"
#include "worldgen/CaveCarver.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

using betacraft::core::ChunkCoord;
using betacraft::voxel::BlockRegistry;
using betacraft::voxel::Chunk;
using betacraft::world::WorldGen;
using betacraft::worldgen::CaveCarver;
using betacraft::worldgen::ColumnContext;

namespace {

void expectMatchesGeneratedChunk(uint64_t seed, ChunkCoord coord, int32_t& waterColumns) {
    const WorldGen gen(seed);
    Chunk chunk(coord);
    gen.generateChunk(chunk);
    int32_t mismatches = 0;
    std::string first;
    for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz) {
        for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
            const int32_t wx = coord.x * Chunk::WIDTH + lx;
            const int32_t wz = coord.z * Chunk::DEPTH + lz;
            const ColumnContext expected = CaveCarver::scanColumn(chunk, lx, lz);
            const ColumnContext actual = gen.columnContext(wx, wz);
            if (expected.isWaterBody) ++waterColumns;
            if (actual.surfaceHeight != expected.surfaceHeight || actual.waterFloorHeight != expected.waterFloorHeight ||
                actual.isWaterBody != expected.isWaterBody) {
                if (mismatches++ == 0) {
                    first = std::to_string(wx) + "," + std::to_string(wz) + ": got " +
                            std::to_string(actual.surfaceHeight) + "/" + std::to_string(actual.waterFloorHeight) + "/" +
                            std::to_string(actual.isWaterBody) + " want " + std::to_string(expected.surfaceHeight) +
                            "/" + std::to_string(expected.waterFloorHeight) + "/" + std::to_string(expected.isWaterBody);
                }
            }
        }
    }
    EXPECT_EQ(mismatches, 0) << "seed " << seed << " chunk " << coord.x << "," << coord.z << " first " << first;
}

} // namespace

TEST(WorldGenColumnContext, MatchesTheGeneratedChunkOnLandAndSea) {
    BlockRegistry::initialize();
    int32_t waterColumns = 0;
    // Spawn land, a coast and open sea for the default seed, plus another seed.
    for (const ChunkCoord c : {ChunkCoord{0, 0}, ChunkCoord{-2, 3}, ChunkCoord{-5, 8}, ChunkCoord{-7, 7}}) {
        expectMatchesGeneratedChunk(173173173ULL, c, waterColumns);
    }
    expectMatchesGeneratedChunk(42ULL, ChunkCoord{3, -4}, waterColumns);
    EXPECT_GT(waterColumns, 256) << "the sample must include sea columns";
}

TEST(WorldGenColumnContext, IsAPureFunctionOfTheColumn) {
    const WorldGen a(173173173ULL);
    const WorldGen b(173173173ULL);
    for (int32_t i = -40; i <= 40; i += 7) {
        const ColumnContext x = a.columnContext(i * 13, -i * 5);
        const ColumnContext y = b.columnContext(i * 13, -i * 5);
        const ColumnContext z = a.columnContext(i * 13, -i * 5);
        EXPECT_EQ(x.surfaceHeight, y.surfaceHeight);
        EXPECT_EQ(x.surfaceHeight, z.surfaceHeight);
        EXPECT_EQ(x.waterFloorHeight, y.waterFloorHeight);
        EXPECT_EQ(x.isWaterBody, y.isWaterBody);
    }
}

TEST(WorldGenColumnContext, ReseedingChangesTheCarver) {
    WorldGen gen(1ULL);
    const uint64_t before = gen.caveCarver().getSeed();
    gen.setSeed(2ULL);
    EXPECT_NE(gen.caveCarver().getSeed(), before);
}
