#include "physics/Player.hpp"
#include "tests/test_helpers.hpp"
#include "voxel/Block.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace betacraft;
using namespace testing;

TEST(PlayerWorldIntegrationTest, GravityLandingAndGroundState) {
    test::TempDir tempDir;
    world::World world(173ULL, tempDir.path());
    world.ensureChunkLoaded({0, 0});

    // Solid floor in sky at y = 100 to avoid pre-generated ground
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            world.setBlock(x, 100, z, voxel::BlockId::Stone);
        }
    }

    physics::Player player;
    player.setPosition({8.5f, 106.0f, 8.5f});
    EXPECT_FALSE(player.isOnGround());

    // Update for 1 second of simulation
    const physics::PlayerInput emptyInput{};
    for (int i = 0; i < 60; ++i) {
        (void)player.update(1.0f / 60.0f, emptyInput, world);
    }

    EXPECT_TRUE(player.isOnGround());
    EXPECT_NEAR(player.getPosition().y, 101.0f, 0.05f);
}

TEST(PlayerWorldIntegrationTest, SneakLedgeProtection) {
    test::TempDir tempDir;
    world::World world(173ULL, tempDir.path());
    world.ensureChunkLoaded({0, 0});

    // Platform at y = 60 from x=4 to x=8, and air at x > 8
    for (int z = 4; z <= 12; ++z) {
        for (int x = 4; x <= 8; ++x) {
            world.setBlock(x, 60, z, voxel::BlockId::Cobblestone);
        }
    }

    physics::Player player;
    player.setPosition({7.5f, 61.0f, 8.0f});
    player.setRotation(0.0f, 0.0f); // Facing +X towards edge at x = 8.0 (platform top is y=61.0)

    // First settle on ground
    physics::PlayerInput input{};
    for (int i = 0; i < 5; ++i) {
        (void)player.update(0.02f, input, world);
    }
    EXPECT_TRUE(player.isOnGround());

    // Walk forward with sneak enabled toward the drop at x > 8
    input.forward = true;
    input.sneak = true;
    for (int i = 0; i < 60; ++i) {
        (void)player.update(0.02f, input, world);
    }

    // Player should not have fallen off the edge
    EXPECT_LE(player.getPosition().x, 8.5f);
    EXPECT_NEAR(player.getPosition().y, 61.0f, 0.05f);
    EXPECT_TRUE(player.isOnGround());
}

TEST(PlayerWorldIntegrationTest, WaterExitJumpOntoShore) {
    test::TempDir tempDir;
    world::World world(173ULL, tempDir.path());
    world.ensureChunkLoaded({0, 0});

    // Water pool at x=5, shore at x=6 (y=65, top at 66.0)
    for (int z = 3; z <= 8; ++z) {
        for (int x = 3; x <= 8; ++x) {
            for (int y = 60; y <= 70; ++y) {
                world.setBlock(x, y, z, voxel::BlockId::Air);
            }
            world.setBlock(x, 62, z, voxel::BlockId::Stone);
        }
    }
    world.setBlock(5, 63, 5, voxel::BlockId::Water);
    world.setBlock(5, 64, 5, voxel::BlockId::Water);
    for (int sx = 6; sx <= 10; ++sx) {
        world.setBlock(sx, 63, 5, voxel::BlockId::Stone);
        world.setBlock(sx, 64, 5, voxel::BlockId::Stone);
        world.setBlock(sx, 65, 5, voxel::BlockId::Grass);
    }

    physics::Player player;
    player.setPosition({5.5f, 63.2f, 5.5f});
    player.setRotation(0.0f, 0.0f); // Facing +X toward shore

    physics::PlayerInput input{};
    input.forward = true;
    input.jump = true;

    for (int i = 0; i < 90; ++i) {
        (void)player.update(1.0f / 60.0f, input, world);
    }

    EXPECT_GT(player.getPosition().x, 6.0f);
    EXPECT_GE(player.getPosition().y, 66.0f);
}
