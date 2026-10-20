#include "render/ChunkMesher.hpp"
#include "tests/test_helpers.hpp"
#include "voxel/Block.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace betacraft;
using namespace testing;

TEST(LightingMesherIntegrationTest, DarkEnclosureToLitEnclosure) {
    world::BlockRegistry::initialize();
    test::TempDir tempDir;
    world::World world(173ULL, tempDir.path());
    world::Chunk& chunk = world.ensureChunkLoaded({0, 0});

    // Build a 5x5x5 hollow cobblestone bunker inside chunk
    // walls from x=4..8, y=70..74, z=4..8
    for (int y = 70; y <= 74; ++y) {
        for (int z = 4; z <= 8; ++z) {
            for (int x = 4; x <= 8; ++x) {
                const bool isShell = (x == 4 || x == 8 || y == 70 || y == 74 || z == 4 || z == 8);
                chunk.setBlock(x, y, z, isShell ? world::BlockId::Cobblestone : world::BlockId::Air);
            }
        }
    }

    chunk.recalculateLighting();

    // Check interior light level: should be 0 sky light, 0 block light
    EXPECT_EQ(chunk.getSkyLight(6, 72, 6), 0);
    EXPECT_EQ(chunk.getBlockLight(6, 72, 6), 0);

    // Build mesh for dark interior
    const auto darkMesh = render::ChunkMesher::buildMesh(world, chunk, false);
    EXPECT_FALSE(darkMesh.opaqueVertices.empty());

    // Place torch in center of interior
    chunk.setBlock(6, 71, 6, world::BlockId::Torch);
    chunk.recalculateLighting();

    EXPECT_EQ(chunk.getBlockLight(6, 71, 6), 14);
    EXPECT_EQ(chunk.getBlockLight(6, 72, 6), 13);

    // Build mesh for lit interior
    const auto litMesh = render::ChunkMesher::buildMesh(world, chunk, false);
    EXPECT_FALSE(litMesh.opaqueVertices.empty());

    // Check that vertices near torch have higher block light (light.y) than in dark room
    float maxDarkBlockLight = 0.0f;
    for (const auto& v : darkMesh.opaqueVertices) {
        const glm::vec3 pos = v.getLocalPosition();
        if (pos.x >= 4.0f && pos.x <= 8.0f &&
            pos.y >= 70.0f && pos.y <= 74.0f &&
            pos.z >= 4.0f && pos.z <= 8.0f) {
            maxDarkBlockLight = std::max(maxDarkBlockLight, v.getLight().y);
        }
    }

    float maxLitBlockLight = 0.0f;
    for (const auto& v : litMesh.opaqueVertices) {
        const glm::vec3 pos = v.getLocalPosition();
        if (pos.x >= 4.0f && pos.x <= 8.0f &&
            pos.y >= 70.0f && pos.y <= 74.0f &&
            pos.z >= 4.0f && pos.z <= 8.0f) {
            maxLitBlockLight = std::max(maxLitBlockLight, v.getLight().y);
        }
    }

    EXPECT_FLOAT_EQ(maxDarkBlockLight, 0.0f);
    EXPECT_GT(maxLitBlockLight, 0.5f);
}

TEST(LightingMesherIntegrationTest, CrossChunkLightingPropagation) {
    world::BlockRegistry::initialize();
    test::TempDir tempDir;
    world::World world(173ULL, tempDir.path());

    // Load chunk (0, 0) and neighbor chunk (1, 0)
    world.ensureChunkLoaded({0, 0});
    world.ensureChunkLoaded({1, 0});

    // Clear sky area across the boundary at Y=105
    for (int z = 0; z < 16; ++z) {
        for (int x = 10; x <= 20; ++x) {
            for (int y = 100; y <= 110; ++y) {
                world.setBlock(x, y, z, world::BlockId::Air);
            }
        }
    }

    // Initially, block light in chunk 1 (x=16) is 0
    EXPECT_EQ(world.getBlockLight(16, 105, 5), 0);

    // Place torch in chunk 0 right at the border (x=15)
    world.setBlock(15, 105, 5, world::BlockId::Torch);

    // Verify torch emits 14 in chunk 0
    EXPECT_EQ(world.getBlockLight(15, 105, 5), 14);

    // Verify light propagated across chunk border into chunk 1!
    EXPECT_EQ(world.getBlockLight(16, 105, 5), 13);
    EXPECT_EQ(world.getBlockLight(17, 105, 5), 12);
    EXPECT_EQ(world.getBlockLight(18, 105, 5), 11);

    // Now break the torch in chunk 0
    world.setBlock(15, 105, 5, world::BlockId::Air);

    // Verify light is completely purged on both sides of the border
    EXPECT_EQ(world.getBlockLight(15, 105, 5), 0);
    EXPECT_EQ(world.getBlockLight(16, 105, 5), 0);
    EXPECT_EQ(world.getBlockLight(17, 105, 5), 0);
}

TEST(LightingMesherIntegrationTest, UnsupportedTorchBreaking) {
    world::BlockRegistry::initialize();
    test::TempDir tempDir;
    world::World world(173ULL, tempDir.path());
    world.ensureChunkLoaded({0, 0});

    // Place stone with torch on top
    world.setBlock(5, 80, 5, world::BlockId::Stone);
    world.setBlock(5, 81, 5, world::BlockId::Torch);
    EXPECT_EQ(world.getBlock(5, 81, 5), world::BlockId::Torch);

    // Remove supporting stone block
    world.setBlock(5, 80, 5, world::BlockId::Air);

    // Torch above should be broken to Air rather than floating
    EXPECT_EQ(world.getBlock(5, 81, 5), world::BlockId::Air);
}
