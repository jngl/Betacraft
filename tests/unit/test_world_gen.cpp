#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"
#include "world/WorldGen.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace betacraft::world;
using namespace betacraft::voxel;
using namespace testing;

TEST(WorldGenTest, BiomeSampling) {
    WorldGen gen(173173ULL);

    for (int i = -100; i <= 100; i += 25) {
        const auto sample = gen.sampleBiome(i * 16, i * -16);
        EXPECT_GE(sample.temperature, 0.0f);
        EXPECT_LE(sample.temperature, 1.0f);
        EXPECT_GE(sample.humidity, 0.0f);
        EXPECT_LE(sample.humidity, 1.0f);

        EXPECT_GE(sample.grassTint.r, 0.0f);
        EXPECT_LE(sample.grassTint.r, 1.0f);
        EXPECT_GE(sample.foliageTint.g, 0.0f);
        EXPECT_LE(sample.foliageTint.g, 1.0f);
    }
}

TEST(WorldGenTest, GenerateChunkStructure) {
    BlockRegistry::initialize();
    WorldGen gen(173173ULL);

    Chunk chunk({0, 0});
    gen.populateBiomeColumns(chunk);
    gen.generateChunk(chunk);

    // Bedrock must always be at y = 0
    for (int z = 0; z < Chunk::DEPTH; ++z) {
        for (int x = 0; x < Chunk::WIDTH; ++x) {
            EXPECT_EQ(chunk.getBlock(x, 0, z), BlockId::Bedrock);
        }
    }

    // Top of sky (y = 127) should be air
    EXPECT_EQ(chunk.getBlock(8, 127, 8), BlockId::Air);

    // Deep underground should have stone or ores
    bool foundStone = false;
    for (int y = 5; y < 40; ++y) {
        if (chunk.getBlock(8, y, 8) == BlockId::Stone) {
            foundStone = true;
            break;
        }
    }
    EXPECT_TRUE(foundStone);
}

TEST(WorldGenTest, SeedDivergence) {
    BlockRegistry::initialize();
    WorldGen gen1(11111ULL);
    WorldGen gen2(99999ULL);

    Chunk c1({5, 5});
    Chunk c2({5, 5});

    gen1.generateChunk(c1);
    gen2.generateChunk(c2);

    bool differencesFound = false;
    for (int y = 50; y < 80; ++y) {
        if (c1.getBlock(8, y, 8) != c2.getBlock(8, y, 8)) {
            differencesFound = true;
            break;
        }
    }
    EXPECT_TRUE(differencesFound);
}
