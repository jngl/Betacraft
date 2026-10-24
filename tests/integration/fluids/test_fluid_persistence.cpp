// Sprint 1 integration, bug 8: fluid simulation edits (EditOrigin::Simulation) only mark a
// chunk modified when it or one of its 8 neighbours was touched by the player, so generated
// water settling in unexplored terrain never writes a .chk (it is simulated again after a
// reload), while player water flowing over a chunk seam is still saved.
#include "test_support/GLTestContext.hpp"

#include "tests/integration/support/WorldgenSupport.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <filesystem>
#include <memory>

using namespace betacraft;
using voxel::BlockId;
using voxel::Chunk;

namespace {

[[nodiscard]] size_t chunkFileCount(const std::filesystem::path& dir) {
    size_t n = 0;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(dir / "chunks", ec)) {
        if (entry.path().extension() == ".chk") ++n;
    }
    return n;
}

[[nodiscard]] int32_t countWater(const Chunk& c, int32_t minY) {
    int32_t n = 0;
    for (int32_t y = minY; y < Chunk::HEIGHT; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x)
                if (voxel::isWaterBlock(c.getBlock(x, y, z))) ++n;
    return n;
}

} // namespace

TEST(S1FluidPersistence, GeneratedFluidSettlingMarksNothingModified) {
    voxel::BlockRegistry::initialize();
    test::TempDir dir{"betacraft_s1_fluids_"};
    world::World w(test::p2worldgen::kSeed, dir.path());
    test::p2worldgen::loadAll(w, test::p2worldgen::squareRegion({0, 0}, 2));
    for (const auto& [coord, chunk] : w.getChunks()) {
        ASSERT_FALSE(chunk->isModified()) << coord.x << "," << coord.z;
        ASSERT_FALSE(chunk->isPlayerTouched()) << coord.x << "," << coord.z;
    }

    uint32_t updates = 0;
    for (int i = 0; i < 200; ++i) {
        updates += w.tick().processed;
    }
    ASSERT_GT(updates, 0U) << "the seed's generated fluid must actually move for this test to mean anything";

    for (const auto& [coord, chunk] : w.getChunks()) {
        EXPECT_FALSE(chunk->isModified()) << "chunk " << coord.x << "," << coord.z;
    }
    w.saveModifiedChunks(storage::WorldLevelData{});
    EXPECT_EQ(chunkFileCount(dir.path()), 0U) << "exploring without editing writes no .chk";
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "region")) << "nor any region bundle (S7.3)";
}

TEST(S1FluidPersistence, PlayerWaterFlowingAcrossASeamIsPersisted) {
    voxel::BlockRegistry::initialize();
    test::TempDir dir{"betacraft_s1_fluids_"};
    constexpr int32_t kFloorY = 119;
    constexpr int32_t kY = 120;
    {
        world::World w(test::p2worldgen::kSeed, dir.path());
        test::p2worldgen::loadAll(w, test::p2worldgen::squareRegion({0, 0}, 1));
        // The floor stays inside chunk (0, 0): only that chunk is player-touched.
        for (int32_t z = 0; z < Chunk::DEPTH; ++z) {
            for (int32_t x = 0; x < Chunk::WIDTH; ++x) {
                ASSERT_TRUE(w.setBlock(x, kFloorY, z, BlockId::Glass));
            }
        }
        ASSERT_TRUE(w.setBlock(15, kY, 8, BlockId::Water));
        ASSERT_TRUE(w.getChunk({0, 0})->isPlayerTouched());
        const Chunk* east = w.getChunk({1, 0});
        ASSERT_NE(east, nullptr);
        ASSERT_FALSE(east->isPlayerTouched());
        ASSERT_FALSE(east->isModified());

        for (int i = 0; i < 200; ++i) {
            (void)w.tick();
        }
        ASSERT_GT(countWater(*east, 100), 0) << "the water flowed over the seam and fell into chunk (1, 0)";
        EXPECT_TRUE(east->isModified()) << "a neighbour of a player-touched chunk keeps simulation edits";
        EXPECT_FALSE(east->isPlayerTouched()) << "simulation edits do not spread the touched flag";

        w.saveModifiedChunks(storage::WorldLevelData{});
        // Reload the column into a second world on the same save and compare.
        world::World reloaded(test::p2worldgen::kSeed, dir.path());
        const Chunk& again = reloaded.ensureChunkLoaded({1, 0});
        EXPECT_EQ(test::p2worldgen::firstBlockDifference(*east, again), -1);
        EXPECT_EQ(countWater(again, 100), countWater(*east, 100));
        EXPECT_TRUE(again.isPlayerTouched()) << "a chunk read from disk was edited";
    }
}
