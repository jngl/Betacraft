// Phase 2 integration, P3.4 decoration state across sessions: a saved chunk remembers
// whether pass 2 ran and the feature plan it was registered with, so reloading neither
// re-decorates player edits nor re-plans edited terrain; saves predating the record load as
// decorated (their decoration is baked in).
#include "tests/integration/support/WorldgenSupport.hpp"
#include "test_support/GLTestContext.hpp"

#include "storage/WorldStorage.hpp"
#include "world/World.hpp"
#include "world/WorldGen.hpp"
#include "worldgen/DecorationFeatures.hpp"
#include "worldgen/DecorationPipeline.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <vector>

using namespace betacraft;
using namespace betacraft::test::p2worldgen;

namespace {

class P2WorldgenPersistence : public ::testing::Test {
protected:
    void SetUp() override { voxel::BlockRegistry::initialize(); }
};

struct EdgeTree {
    core::ChunkCoord owner{};     // chunk that planned the tree
    core::ChunkCoord neighbour{}; // west neighbour its crown overhangs into
    worldgen::Feature tree{};
};

// A tree rooted in the west-most column of its chunk, found with WorldGen + the planner alone.
[[nodiscard]] EdgeTree findWestEdgeTree() {
    world::WorldGen gen(kSeed);
    const worldgen::FeaturePlanner planner(kSeed);
    for (int32_t cz = -4; cz <= 4; ++cz) {
        for (int32_t cx = -4; cx <= 4; ++cx) {
            Chunk chunk({cx, cz});
            gen.generateChunk(chunk);
            for (const worldgen::Feature& f : planner.plan(chunk)) {
                if (worldgen::isTreeFeature(f.type) && core::floorMod(f.origin.x, Chunk::WIDTH) == 0) {
                    return EdgeTree{{cx, cz}, {cx - 1, cz}, f};
                }
            }
        }
    }
    ADD_FAILURE() << "no west-edge tree near spawn for this seed";
    return {};
}

} // namespace

TEST_F(P2WorldgenPersistence, DecoratedChunkIsNotRedecoratedAfterReload) {
    test::TempDir dir;
    const core::ChunkCoord center{0, 0};
    std::optional<core::BlockPos> chopped;
    {
        world::World w(kSeed, dir.path());
        loadAll(w, squareRegion(center, 1));
        ASSERT_TRUE(w.isChunkDecorated(center));
        const Chunk& c = *w.getChunk(center);
        for (int32_t y = 0; y < Chunk::HEIGHT && !chopped; ++y)
            for (int32_t lz = 0; lz < Chunk::DEPTH && !chopped; ++lz)
                for (int32_t lx = 0; lx < Chunk::WIDTH && !chopped; ++lx)
                    if (isDecorationBlock(c.getBlock(lx, y, lz))) chopped = core::BlockPos{lx, y, lz};
        ASSERT_TRUE(chopped.has_value());
        ASSERT_TRUE(w.setBlock(*chopped, BlockId::Air)); // the player removes it
        w.saveModifiedChunks(storage::WorldLevelData{});
    }

    world::World w(kSeed, dir.path());
    (void)w.ensureChunkLoaded(center);
    EXPECT_TRUE(w.isChunkDecorated(center)) << "the saved flag, not the 3x3, decides";
    loadAll(w, squareRegion(center, 1));
    EXPECT_EQ(w.getBlock(*chopped), BlockId::Air) << "pass 2 ran twice on a saved chunk";
}

TEST_F(P2WorldgenPersistence, EditedUndecoratedChunkKeepsItsSavedPlanAcrossSessions) {
    const EdgeTree edge = findWestEdgeTree();
    const core::BlockPos soil = edge.tree.origin.offset(0, -1, 0);
    test::TempDir dir;
    {
        world::World w(kSeed, dir.path());
        // The neighbour's 3x3 is resident: it is decorated with the overhanging crown while the
        // tree's own chunk, on the frontier, is not.
        loadAll(w, squareRegion(edge.neighbour, 1));
        ASSERT_TRUE(w.isChunkDecorated(edge.neighbour));
        ASSERT_FALSE(w.isChunkDecorated(edge.owner));
        int32_t overhangingLeaves = 0;
        worldgen::forEachFeatureBlock(edge.tree, [&](core::BlockPos p, BlockId b) {
            if (b == BlockId::Leaves && core::floorDiv(p.x, Chunk::WIDTH) == edge.neighbour.x &&
                w.getBlock(p) == BlockId::Leaves) {
                ++overhangingLeaves;
            }
        });
        ASSERT_GT(overhangingLeaves, 0);

        // The player digs out the soil the tree would grow on: re-planning that chunk now
        // drops the tree whose crown the neighbour already shows.
        ASSERT_TRUE(w.setBlock(soil, BlockId::Air));
        const worldgen::FeaturePlanner planner(kSeed);
        ASSERT_NE(planner.plan(*w.getChunk(edge.owner)), *w.getDecorationPipeline().findPlan(edge.owner));
        w.saveModifiedChunks(storage::WorldLevelData{});
    }

    world::World w(kSeed, dir.path());
    loadAll(w, squareRegion(edge.owner, 1));
    ASSERT_TRUE(w.isChunkDecorated(edge.owner));
    for (int32_t i = 0; i < edge.tree.height; ++i) {
        EXPECT_TRUE(isLogBlock(w.getBlock(edge.tree.origin.offset(0, i, 0)))) << "trunk cut at " << i;
    }
    EXPECT_EQ(w.getBlock(soil), BlockId::Air) << "terrain edits are kept";
}

TEST_F(P2WorldgenPersistence, SaveWithoutDecorationRecordLoadsAsDecoratedAndUntouched) {
    test::TempDir dir;
    const core::ChunkCoord legacy{0, 0};
    Chunk original(legacy);
    {
        // A chunk saved before two-pass decoration existed: blocks only, no record.
        for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz)
            for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx)
                for (int32_t y = 0; y < 70; ++y)
                    original.setBlock(lx, y, lz, y == 0 ? BlockId::Bedrock : (y == 69 ? BlockId::Grass : BlockId::Dirt));
        const storage::WorldStorage storage(dir.path());
        ASSERT_TRUE(storage.saveChunk(original));
    }

    world::World w(kSeed, dir.path());
    loadAll(w, squareRegion(legacy, 1));
    EXPECT_TRUE(w.isChunkDecorated(legacy));
    // Its own trees were baked in; its neighbours' crowns are not pasted onto it either.
    EXPECT_EQ(firstBlockDifference(*w.getChunk(legacy), original), -1);
}

TEST_F(P2WorldgenPersistence, ModifiedChunkEvictedByStreamingKeepsItsRecord) {
    test::TempDir dir;
    const core::ChunkCoord center{0, 0};
    world::World w(kSeed, dir.path());
    loadAll(w, squareRegion(center, 1));
    ASSERT_TRUE(w.isChunkDecorated(center));
    const core::BlockPos mark{8, 120, 8};
    ASSERT_TRUE(w.setBlock(mark, BlockId::Glass));

    w.updateChunksAround(glm::vec3(4000.0f, 70.0f, 4000.0f), 1, 64); // evicts and saves it
    ASSERT_EQ(w.getChunk(center), nullptr);

    (void)w.ensureChunkLoaded(center);
    EXPECT_EQ(w.getBlock(mark), BlockId::Glass);
    EXPECT_TRUE(w.isChunkDecorated(center));
}
