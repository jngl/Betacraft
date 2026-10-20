#include "tests/test_helpers.hpp"
#include "voxel/Block.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace betacraft;
using namespace betacraft::world;
using namespace betacraft::voxel;
using namespace testing;

TEST(WorldTest, CrossChunkBoundaryBlocks) {
    test::TempDir tempDir;
    World world(173ULL, tempDir.path());

    // Coordinate x=15 is in chunk 0, x=16 is in chunk 1
    world.ensureChunkLoaded({0, 0});
    world.ensureChunkLoaded({1, 0});

    EXPECT_TRUE(world.setBlock(15, 70, 5, BlockId::OakPlanks));
    EXPECT_TRUE(world.setBlock(16, 70, 5, BlockId::Stone));

    EXPECT_EQ(world.getBlock(15, 70, 5), BlockId::OakPlanks);
    EXPECT_EQ(world.getBlock(16, 70, 5), BlockId::Stone);

    // Negative coordinates
    world.ensureChunkLoaded({-1, -1});
    EXPECT_TRUE(world.setBlock(-1, 65, -1, BlockId::DiamondOre));
    EXPECT_EQ(world.getBlock(-1, 65, -1), BlockId::DiamondOre);
}

TEST(WorldTest, PistonPushLimitsAndImmovableBlocks) {
    test::TempDir tempDir;
    World world(173ULL, tempDir.path());
    world.ensureChunkLoaded({0, 0});

    // Clear runway at Y=90
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            world.setBlock(x, 90, z, BlockId::Air);
        }
    }

    // 1. Single block push
    world.setBlock(1, 90, 5, BlockId::Piston);
    world.setBlock(2, 90, 5, BlockId::Cobblestone);
    EXPECT_TRUE(world.triggerPistonPush({1, 90, 5}, core::FaceDir::PosX));
    EXPECT_EQ(world.getBlock(2, 90, 5), BlockId::Air);
    EXPECT_EQ(world.getBlock(3, 90, 5), BlockId::Cobblestone);

    // 2. Piston pushing Bedrock should fail
    world.setBlock(3, 90, 5, BlockId::Bedrock);
    EXPECT_FALSE(world.triggerPistonPush({1, 90, 5}, core::FaceDir::PosX));

    // 3. Piston pushing Obsidian should fail
    world.setBlock(3, 90, 5, BlockId::Obsidian);
    EXPECT_FALSE(world.triggerPistonPush({1, 90, 5}, core::FaceDir::PosX));

    // 4. Piston pushing 12 blocks succeeds, 13 blocks fails
    world.ensureChunkLoaded({1, 0});
    for (int x = 2; x <= 13; ++x) {
        world.setBlock(x, 90, 8, BlockId::Stone);
    }
    world.setBlock(1, 90, 8, BlockId::Piston);
    // 12 blocks in line (x=2..13)
    EXPECT_TRUE(world.triggerPistonPush({1, 90, 8}, core::FaceDir::PosX));
    EXPECT_EQ(world.getBlock(2, 90, 8), BlockId::Air);
    EXPECT_EQ(world.getBlock(14, 90, 8), BlockId::Stone);

    // Now put 13 blocks in front (x=2..14)
    world.setBlock(2, 90, 8, BlockId::Stone);
    EXPECT_FALSE(world.triggerPistonPush({1, 90, 8}, core::FaceDir::PosX));
}

TEST(WorldTest, UnsupportedFloraBreaking) {
    test::TempDir tempDir;
    World world(173ULL, tempDir.path());
    world.ensureChunkLoaded({0, 0});

    // Place dirt with tall grass on top
    world.setBlock(5, 80, 5, BlockId::Dirt);
    world.setBlock(5, 81, 5, BlockId::TallGrass);
    EXPECT_EQ(world.getBlock(5, 81, 5), BlockId::TallGrass);

    // Remove dirt supporting block -> tall grass above should break to air
    world.setBlock(5, 80, 5, BlockId::Air);
    EXPECT_EQ(world.getBlock(5, 81, 5), BlockId::Air);
}

TEST(WorldTest, SafeSpawnFinder) {
    test::TempDir tempDir;
    World world(173173ULL, tempDir.path());

    const glm::vec3 spawn = world.findSafeSpawnPosition();
    EXPECT_GT(spawn.y, static_cast<float>(core::EngineConfig::SEA_LEVEL));

    const int32_t sx = static_cast<int32_t>(std::floor(spawn.x));
    const int32_t sy = static_cast<int32_t>(std::floor(spawn.y));
    const int32_t sz = static_cast<int32_t>(std::floor(spawn.z));

    // Spawn feet and head must be in air/replaceable blocks
    EXPECT_FALSE(BlockRegistry::isSolid(world.getBlock(sx, sy, sz)));
    EXPECT_FALSE(BlockRegistry::isSolid(world.getBlock(sx, sy + 1, sz)));

    // Ground beneath must be solid
    EXPECT_TRUE(BlockRegistry::isSolid(world.getBlock(sx, sy - 1, sz)));
}
