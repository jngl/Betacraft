// P3.3 - Randomized (fixed-seed) property tests and boundary inputs for the cave carver.
#include "worldgen/CaveCarver.hpp"
#include "worldgen/ColumnSampling.hpp"
#include "worldgen/SeededRandom.hpp"
#include "WorldgenTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

using namespace betacraft::worldgen;
using betacraft::core::ChunkCoord;
using betacraft::world::BlockId;
using betacraft::world::Chunk;

namespace {

std::vector<uint64_t> randomSeeds(size_t n) {
    SeededRandom rng(0xA11CE5EEDULL);
    std::vector<uint64_t> out(n);
    for (auto& s : out) s = rng.next();
    return out;
}

void fillStone(Chunk& c, int32_t topY) {
    for (int32_t z = 0; z < Chunk::DEPTH; ++z)
        for (int32_t x = 0; x < Chunk::WIDTH; ++x)
            for (int32_t y = 0; y <= topY && y < Chunk::HEIGHT; ++y)
                c.setBlock(x, y, z, y == 0 ? BlockId::Bedrock : BlockId::Stone, false);
}

} // namespace

// For random seeds and random chunk coordinates: Y <= 5 untouched, the 6-block vault under
// every water column and the 3-block cover on land survive, and only carvable blocks change.
TEST(CaveProperties, SanctuariesHoldForRandomSeedsAndChunks) {
    const test::SyntheticTerrain terrain;
    SeededRandom coordRng(99ULL);
    uint32_t carvedTotal = 0;
    for (uint64_t seed : randomSeeds(6)) {
        const CaveCarver carver(seed);
        for (int i = 0; i < 2; ++i) {
            const ChunkCoord cc{static_cast<int32_t>(coordRng.nextBounded(2000U)) - 1000,
                                static_cast<int32_t>(coordRng.nextBounded(2000U)) - 1000};
            Chunk before(cc);
            Chunk after(cc);
            terrain.fillChunk(before);
            terrain.fillChunk(after);
            const ChunkColumnCache cache(terrain, cc);
            carvedTotal += carver.carveChunk(after, cache).total();
            for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz)
                for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
                    const ColumnContext col = terrain.sampleColumn(cc.x * 16 + lx, cc.z * 16 + lz);
                    for (int32_t y = 0; y < Chunk::HEIGHT; ++y) {
                        const BlockId b0 = before.getBlock(lx, y, lz);
                        const BlockId b1 = after.getBlock(lx, y, lz);
                        if (b0 == b1) continue;
                        ASSERT_GT(y, 5) << "seed " << seed;
                        ASSERT_TRUE(CaveCarver::isCarvable(b0));
                        ASSERT_TRUE(CaveCarver::isFillBlock(b1));
                        if (col.isWaterBody) ASSERT_LT(y, col.waterFloorHeight - 6) << "seed " << seed;
                        else ASSERT_LT(y, col.surfaceHeight - 3) << "seed " << seed;
                    }
                }
        }
    }
    EXPECT_GT(carvedTotal, 0U);
}

// The same world voxel gets the same verdict whichever chunk asks: evaluate a world column
// through its own chunk, and through a carver/cache built for a neighbouring chunk.
TEST(CaveProperties, ColumnVerdictIsIndependentOfTheChunkThatEvaluatesIt) {
    const test::SyntheticTerrain terrain;
    for (uint64_t seed : randomSeeds(3)) {
        const CaveCarver carver(seed);
        Chunk own(ChunkCoord{0, 0});
        terrain.fillChunk(own);
        (void)carver.carveChunk(own, terrain);
        // Border column (15, z) of chunk (0,0) is in the face ring of chunk (1,0)'s cache.
        const ChunkColumnCache neighbourCache(terrain, ChunkCoord{1, 0});
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            // Between bedrock and the soil the fixture column is pure stone, so "carved" is
            // simply "no longer stone".
            for (int32_t y = 1; y < test::syntheticSurface(15, z) - 3; ++y) {
                const bool carvedHere = own.getBlock(15, y, z) != BlockId::Stone;
                const bool predicted = carver.excavatesAt(15, y, z, neighbourCache);
                ASSERT_EQ(carvedHere, predicted) << "seed " << seed << " y=" << y << " z=" << z;
            }
    }
}

TEST(CaveProperties, SurfaceAboveBuildHeightCarvesOnlyInsideTheChunk) {
    const CaveCarver carver(5ULL);
    const test::FlatSampler sampler(ColumnContext{1000, 1000, false});
    Chunk c(0, 0);
    fillStone(c, Chunk::HEIGHT - 1);
    const CarveStats s = carver.carveChunk(c, sampler);
    EXPECT_GT(s.total(), 0U);
    for (int32_t z = 0; z < Chunk::DEPTH; ++z)
        for (int32_t x = 0; x < Chunk::WIDTH; ++x)
            for (int32_t y = 0; y <= 5; ++y) ASSERT_NE(c.getBlock(x, y, z), BlockId::Air);
}

TEST(CaveProperties, NegativeOrTinySurfacesCarveNothing) {
    const CaveCarver carver(5ULL);
    for (int32_t surface : {-500, -1, 0, 5, 8, 9}) {
        const test::FlatSampler sampler(ColumnContext{surface, surface, false});
        Chunk c(0, 0);
        fillStone(c, 40);
        EXPECT_EQ(carver.carveChunk(c, sampler).total(), 0U) << surface;
    }
    // Water floor at Y <= 11: the 6-block vault reaches the bedrock guard.
    for (int32_t floorY : {-3, 0, 11}) {
        const test::FlatSampler sampler(ColumnContext{80, floorY, true});
        Chunk c(0, 0);
        fillStone(c, 40);
        EXPECT_EQ(carver.carveChunk(c, sampler).total(), 0U) << floorY;
    }
}

// The aquitard rule follows the water, not a fixed altitude: a mountain lake above sea level
// (floor 90) still keeps its 6-block vault, while land at the same height carves to 87.
TEST(CaveProperties, AquitardAppliesToWaterBodiesAtAnyAltitude) {
    const CaveCarver carver(173173173ULL);
    int landNearTop = 0;
    for (int32_t x = 0; x < 64; ++x)
        for (int32_t z = 0; z < 64; ++z)
            for (int32_t y = 84; y <= 89; ++y) {
                ASSERT_FALSE(carver.shouldCarve(x, y, z, ColumnContext{90, 90, true}));
                if (carver.shouldCarve(x, y, z, ColumnContext{93, 93, false})) ++landNearTop;
            }
    EXPECT_GT(landNearTop, 0);
}
