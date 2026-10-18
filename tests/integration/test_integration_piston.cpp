#include "render/ChunkMesher.hpp"
#include "tests/test_helpers.hpp"
#include "world/Block.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace betacraft;
using namespace testing;

TEST(PistonIntegrationTest, PushBlocksAcrossChunkBoundariesAndRemesh) {
    world::BlockRegistry::initialize();
    test::TempDir tempDir;
    world::World world(173ULL, tempDir.path());

    // Load chunk (0, 0) and chunk (1, 0)
    world::Chunk& c0 = world.ensureChunkLoaded({0, 0});
    world::Chunk& c1 = world.ensureChunkLoaded({1, 0});

    // Clear strip at y = 90
    for (int x = 12; x <= 18; ++x) {
        world.setBlock(x, 90, 5, world::BlockId::Air);
    }

    // Place piston at x=13 in chunk 0 facing +X (towards chunk 1)
    world.setBlock(13, 90, 5, world::BlockId::Piston);
    // Place blocks at x=14, 15 (chunk 0) and x=16 (chunk 1)
    world.setBlock(14, 90, 5, world::BlockId::Cobblestone);
    world.setBlock(15, 90, 5, world::BlockId::OakPlanks);
    world.setBlock(16, 90, 5, world::BlockId::Sandstone);

    // Initial mesh count
    const auto mesh0Before = render::ChunkMesher::buildMesh(world, c0, false);
    const auto mesh1Before = render::ChunkMesher::buildMesh(world, c1, false);

    // Trigger piston push along +X
    EXPECT_TRUE(world.triggerPistonPush({13, 90, 5}, core::FaceDir::PosX));

    // Verify positions shifted
    EXPECT_EQ(world.getBlock(14, 90, 5), world::BlockId::Air);
    EXPECT_EQ(world.getBlock(15, 90, 5), world::BlockId::Cobblestone);
    EXPECT_EQ(world.getBlock(16, 90, 5), world::BlockId::OakPlanks);
    EXPECT_EQ(world.getBlock(17, 90, 5), world::BlockId::Sandstone);

    // Both chunks must be marked dirty
    EXPECT_TRUE(c0.isMeshDirty());
    EXPECT_TRUE(c1.isMeshDirty());

    // Re-mesh
    const auto mesh0After = render::ChunkMesher::buildMesh(world, c0, false);
    const auto mesh1After = render::ChunkMesher::buildMesh(world, c1, false);
    EXPECT_FALSE(mesh0After.opaqueVertices.empty());
    EXPECT_FALSE(mesh1After.opaqueVertices.empty());
}
