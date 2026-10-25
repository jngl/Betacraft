// Streaming: mesh staleness is tracked per 16-high section so
// the AsyncChunkPipeline re-meshes only the sections whose 18^3 neighbourhood an edit
// touched (its own, plus the ones across every section or column seam it lies on).
#include "test_support/GLTestContext.hpp"

#include "tests/integration/support/WorldgenSupport.hpp"
#include "voxel/Chunk.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <memory>

using namespace betacraft;
using voxel::BlockId;
using voxel::Chunk;

namespace {

[[nodiscard]] uint8_t bits(std::initializer_list<int> sections) {
    uint8_t m = 0;
    for (const int s : sections) m = static_cast<uint8_t>(m | (1U << s));
    return m;
}

class StreamingDirty : public ::testing::Test {
protected:
    void SetUp() override {
        voxel::BlockRegistry::initialize();
        m_world = std::make_unique<world::World>(test::worldgen_support::kSeed, m_dir.path());
        test::worldgen_support::loadAll(*m_world, test::worldgen_support::squareRegion({0, 0}, 1));
        cleanAll();
    }

    void cleanAll() {
        for (const auto& [coord, chunk] : m_world->getChunks()) chunk->setMeshDirty(false);
    }

    [[nodiscard]] uint8_t mask(core::ChunkCoord c) const { return m_world->getChunk(c)->dirtySectionMask(); }

    // An edit whose whole footprint is the voxel itself: stone, then (after cleaning) another
    // opaque block. Neither the block light nor the sky light changes, so nothing relights.
    void swapOpaqueBlock(int32_t x, int32_t y, int32_t z) {
        (void)m_world->setBlock(x, y, z, BlockId::Stone);
        cleanAll();
        ASSERT_TRUE(m_world->setBlock(x, y, z, BlockId::Cobblestone));
    }

    test::TempDir m_dir{"betacraft_streaming_dirty_"};
    std::unique_ptr<world::World> m_world;
};

} // namespace

TEST(StreamingChunkDirty, BlockWriteMarksItsSectionAndTheOneAcrossAVerticalSeam) {
    voxel::BlockRegistry::initialize();
    Chunk c({0, 0});
    EXPECT_EQ(c.dirtySectionMask(), Chunk::ALL_SECTIONS_MASK) << "a new chunk has never been meshed";

    c.setMeshDirty(false);
    c.setBlock(3, 20, 3, BlockId::Stone);
    EXPECT_EQ(c.dirtySectionMask(), bits({1}));

    c.setMeshDirty(false);
    c.setBlock(3, 16, 3, BlockId::Stone);
    EXPECT_EQ(c.dirtySectionMask(), bits({0, 1}));

    c.setMeshDirty(false);
    c.setBlock(3, 127, 3, BlockId::Stone);
    EXPECT_EQ(c.dirtySectionMask(), bits({7}));

    c.setMeshDirty(false);
    c.setBlockLight(3, 47, 3, 9);
    EXPECT_EQ(c.dirtySectionMask(), bits({2, 3})) << "baked light is mesh input too";

    c.setMeshDirty(false);
    c.setBlock(3, 20, 3, BlockId::Stone); // unchanged
    EXPECT_EQ(c.dirtySectionMask(), 0);
}

TEST_F(StreamingDirty, ColumnSeamEditMarksOnlyTheSectionsOfTheColumnsThatSeeIt) {
    // x = 0 is the seam with column (-1, 0); z = 5 is no seam.
    swapOpaqueBlock(0, 64, 5);
    EXPECT_EQ(mask({0, 0}), bits({3, 4}));
    EXPECT_EQ(mask({-1, 0}), bits({3, 4}));
    EXPECT_EQ(mask({0, -1}), 0);
    EXPECT_EQ(mask({-1, -1}), 0);
    EXPECT_EQ(mask({1, 0}), 0);
}

TEST_F(StreamingDirty, InteriorEditLeavesNeighbourColumnsClean) {
    swapOpaqueBlock(7, 40, 7);
    EXPECT_EQ(mask({0, 0}), bits({2}));
    for (const auto& [coord, chunk] : m_world->getChunks()) {
        if (coord != core::ChunkCoord{0, 0}) {
            EXPECT_EQ(chunk->dirtySectionMask(), 0) << coord.x << "," << coord.z;
        }
    }
}

TEST_F(StreamingDirty, LoadingAChunkMarksAllEightNeighbours) {
    // The diagonal ones too: their corner voxels' AO and smooth light read the new column.
    test::worldgen_support::loadAll(*m_world, {{2, -1}, {2, 0}, {2, 1}});
    cleanAll();
    (void)m_world->ensureChunkLoaded({2, 2});
    for (const core::ChunkCoord n : {core::ChunkCoord{1, 1}, core::ChunkCoord{2, 1}}) {
        EXPECT_NE(mask(n), 0) << n.x << "," << n.z;
    }
}
