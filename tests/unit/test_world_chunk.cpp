#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace betacraft::voxel;
using namespace testing;

TEST(ChunkTest, InBoundsAndIndexing) {
    EXPECT_TRUE(Chunk::inBounds(0, 0, 0));
    EXPECT_TRUE(Chunk::inBounds(15, 127, 15));
    EXPECT_FALSE(Chunk::inBounds(-1, 0, 0));
    EXPECT_FALSE(Chunk::inBounds(16, 0, 0));
    EXPECT_FALSE(Chunk::inBounds(0, -1, 0));
    EXPECT_FALSE(Chunk::inBounds(0, 128, 0));
    EXPECT_FALSE(Chunk::inBounds(0, 0, -1));
    EXPECT_FALSE(Chunk::inBounds(0, 0, 16));

    const size_t idx0 = Chunk::blockIndex(0, 0, 0);
    const size_t idxLast = Chunk::blockIndex(15, 127, 15);
    EXPECT_EQ(idx0, 0U);
    EXPECT_EQ(idxLast, static_cast<size_t>(Chunk::VOLUME - 1));
}

TEST(ChunkTest, BlockGetSetAndModificationFlags) {
    Chunk c({0, 0});
    EXPECT_TRUE(c.isMeshDirty());
    EXPECT_FALSE(c.isModified());

    c.setMeshDirty(false);
    c.setBlock(2, 50, 3, BlockId::OakLog, true);
    EXPECT_EQ(c.getBlock(2, 50, 3), BlockId::OakLog);
    EXPECT_TRUE(c.isMeshDirty());
    EXPECT_TRUE(c.isModified());

    // Out of bounds set/get are safe
    c.setBlock(100, 200, 300, BlockId::Stone);
    EXPECT_EQ(c.getBlock(100, 200, 300), BlockId::Air);
}

TEST(ChunkTest, BiomesAndTints) {
    Chunk c({1, -1});
    c.setColumnBiome(5, 7, BiomeId::Desert, {0.9f, 0.8f, 0.5f}, {0.8f, 0.7f, 0.4f});
    EXPECT_EQ(c.getColumnBiome(5, 7), BiomeId::Desert);
    EXPECT_FLOAT_EQ(c.getGrassTint(5, 7).x, 0.9f);
    EXPECT_FLOAT_EQ(c.getFoliageTint(5, 7).y, 0.7f);
}

TEST(ChunkTest, SunlightAndTorchLighting) {
    BlockRegistry::initialize();
    Chunk c({0, 0});

    // Solid stone floor at y = 60
    for (int z = 0; z < Chunk::DEPTH; ++z) {
        for (int x = 0; x < Chunk::WIDTH; ++x) {
            c.setBlock(x, 60, z, BlockId::Stone);
        }
    }

    // Torch at (5, 61, 5)
    c.setBlock(5, 61, 5, BlockId::Torch);
    c.recalculateLighting();

    // Sky light directly above floor (y=61) is full daylight (15)
    EXPECT_EQ(c.getSkyLight(8, 70, 8), 15);

    // Below opaque stone floor (y=59), sky light is shaded to 0
    EXPECT_EQ(c.getSkyLight(8, 59, 8), 0);

    // Torch at (5, 61, 5) emits 14 block light
    EXPECT_EQ(c.getBlockLight(5, 61, 5), 14);
    // Adjacent neighbor gets 13
    EXPECT_EQ(c.getBlockLight(6, 61, 5), 13);
    // Two blocks away gets 12
    EXPECT_EQ(c.getBlockLight(7, 61, 5), 12);
}

TEST(ChunkTest, RLESerializationRoundtrip) {
    Chunk c1({-3, 4});
    c1.setBlock(0, 0, 0, BlockId::Bedrock);
    c1.setBlock(7, 64, 7, BlockId::DiamondOre);
    c1.setBlock(15, 120, 15, BlockId::Glowstone);

    const auto bytes = c1.serializeRLE();
    EXPECT_FALSE(bytes.empty());
    // Minimum 5 header bytes
    EXPECT_GE(bytes.size(), 5U);

    Chunk c2({-3, 4});
    EXPECT_TRUE(c2.deserializeRLE(bytes));
    EXPECT_EQ(c2.getBlock(0, 0, 0), BlockId::Bedrock);
    EXPECT_EQ(c2.getBlock(7, 64, 7), BlockId::DiamondOre);
    EXPECT_EQ(c2.getBlock(15, 120, 15), BlockId::Glowstone);
    EXPECT_EQ(c2.getBlock(1, 1, 1), BlockId::Air);
}

TEST(ChunkTest, CorruptedRLEHandling) {
    Chunk c({0, 0});
    const std::vector<uint8_t> badMagic = {'F', 'A', 'I', 'L', 1, 0, 1, 0};
    EXPECT_FALSE(c.deserializeRLE(badMagic));

    const std::vector<uint8_t> tooShort = {'B', 'C', '1'};
    EXPECT_FALSE(c.deserializeRLE(tooShort));
}
