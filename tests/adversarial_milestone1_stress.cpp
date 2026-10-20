#include <gtest/gtest.h>

#include "core/Config.hpp"
#include "core/Types.hpp"
#include "math/AABB.hpp"
#include "world/Block.hpp"
#include "world/PalettedSection.hpp"
#include "world/Chunk.hpp"
#include "world/World.hpp"
#include "physics/Player.hpp"

#include <vector>
#include <random>
#include <cmath>

namespace betacraft::adversarial {

// =========================================================================
// Suite 1: Chunk Sub-Chunk Integration & Boundary Edge Cases
// =========================================================================

TEST(AdversarialChunkSubChunk, BoundaryBlockAccessY0_15_16_31_32_127) {
    world::Chunk chunk(core::ChunkCoord{0, 0});

    // Sub-chunk boundaries:
    // Sec 0: Y = 0 .. 15
    // Sec 1: Y = 16 .. 31
    // Sec 2: Y = 32 .. 47
    // Sec 3: Y = 48 .. 63
    // Sec 4: Y = 64 .. 79
    // Sec 5: Y = 80 .. 95
    // Sec 6: Y = 96 .. 111
    // Sec 7: Y = 112 .. 127

    const int32_t boundaryYs[] = {0, 15, 16, 31, 32, 47, 48, 63, 64, 79, 80, 95, 96, 111, 112, 127};
    const world::BlockId testBlocks[] = {
        world::BlockId::Bedrock, world::BlockId::Stone,      // Sec 0: 0, 15
        world::BlockId::Dirt, world::BlockId::Grass,        // Sec 1: 16, 31
        world::BlockId::Cobblestone, world::BlockId::OakPlanks, // Sec 2: 32, 47
        world::BlockId::Sand, world::BlockId::Gravel,       // Sec 3: 48, 63
        world::BlockId::GoldOre, world::BlockId::IronOre,   // Sec 4: 64, 79
        world::BlockId::CoalOre, world::BlockId::OakLog,    // Sec 5: 80, 95
        world::BlockId::Leaves, world::BlockId::Glass,      // Sec 6: 96, 111
        world::BlockId::LapisOre, world::BlockId::DiamondOre // Sec 7: 112, 127
    };

    // Set boundary blocks
    for (size_t i = 0; i < 16; ++i) {
        chunk.setBlock(7, boundaryYs[i], 7, testBlocks[i]);
    }

    // Verify through Chunk::getBlock
    for (size_t i = 0; i < 16; ++i) {
        EXPECT_EQ(chunk.getBlock(7, boundaryYs[i], 7), testBlocks[i])
            << "Mismatch at boundary Y=" << boundaryYs[i];
    }

    // Verify through Section direct access
    for (size_t i = 0; i < 16; ++i) {
        const size_t secIdx = static_cast<size_t>(boundaryYs[i] >> 4);
        const int32_t localY = boundaryYs[i] & 15;
        EXPECT_EQ(chunk.getSection(secIdx).getBlock(7, localY, 7), testBlocks[i])
            << "Section direct mismatch at sec=" << secIdx << " localY=" << localY;
    }

    // Verify adjacent non-boundary voxels remain Air
    EXPECT_EQ(chunk.getBlock(7, 1, 7), world::BlockId::Air);
    EXPECT_EQ(chunk.getBlock(7, 14, 7), world::BlockId::Air);
    EXPECT_EQ(chunk.getBlock(7, 17, 7), world::BlockId::Air);
    EXPECT_EQ(chunk.getBlock(7, 30, 7), world::BlockId::Air);
    EXPECT_EQ(chunk.getBlock(7, 126, 7), world::BlockId::Air);

    // Verify Out of Bounds access
    EXPECT_EQ(chunk.getBlock(7, -1, 7), world::BlockId::Air);
    EXPECT_EQ(chunk.getBlock(7, 128, 7), world::BlockId::Air);
    EXPECT_EQ(chunk.getBlock(7, 255, 7), world::BlockId::Air);
    EXPECT_EQ(chunk.getBlock(-1, 0, 7), world::BlockId::Air);
    EXPECT_EQ(chunk.getBlock(16, 0, 7), world::BlockId::Air);
    EXPECT_EQ(chunk.getBlock(7, 0, -1), world::BlockId::Air);
    EXPECT_EQ(chunk.getBlock(7, 0, 16), world::BlockId::Air);
}

TEST(AdversarialChunkSubChunk, CrossSectionColumnAccessAndIsolation) {
    world::Chunk chunk(core::ChunkCoord{0, 0});

    // Fill an entire vertical column (x=4, z=9) with distinctive blocks for all 128 heights
    for (int32_t y = 0; y < 128; ++y) {
        const world::BlockId id = static_cast<world::BlockId>(1 + (y % 15)); // rotating solid block types
        chunk.setBlock(4, y, 9, id);
    }

    // Verify all 128 blocks are exactly as written
    for (int32_t y = 0; y < 128; ++y) {
        const world::BlockId expected = static_cast<world::BlockId>(1 + (y % 15));
        EXPECT_EQ(chunk.getBlock(4, y, 9), expected) << "Mismatch at Y=" << y;
    }

    // Neighboring columns in all 8 sections must remain completely pure Air
    for (int32_t y = 0; y < 128; ++y) {
        EXPECT_EQ(chunk.getBlock(3, y, 9), world::BlockId::Air);
        EXPECT_EQ(chunk.getBlock(5, y, 9), world::BlockId::Air);
        EXPECT_EQ(chunk.getBlock(4, y, 8), world::BlockId::Air);
        EXPECT_EQ(chunk.getBlock(4, y, 10), world::BlockId::Air);
    }

    // Check that sections have bitsPerVoxel == 4 (since multiple unique blocks were placed per section)
    for (size_t sec = 0; sec < 8; ++sec) {
        EXPECT_FALSE(chunk.getSection(sec).isHomogeneous());
        EXPECT_EQ(chunk.getSection(sec).getBitsPerVoxel(), 4);
    }
}

TEST(AdversarialChunkSubChunk, RLERoundtripCrossSectionLongRuns) {
    world::Chunk original(core::ChunkCoord{1, 2});

    // Construct challenging patterns:
    // 1. Column (0, 0): Run of 128 Stone blocks traversing all 8 sub-chunks
    for (int32_t y = 0; y < 128; ++y) {
        original.setBlock(0, y, 0, world::BlockId::Stone);
    }

    // 2. Boundary straddler: 10 Sand blocks from Y=12 to Y=21 (straddles Sec 0 and Sec 1)
    for (int32_t y = 12; y <= 21; ++y) {
        original.setBlock(1, y, 0, world::BlockId::Sand);
    }

    // 3. High boundary straddler: 10 Dirt blocks from Y=120 to Y=127 (top of Sec 7)
    for (int32_t y = 120; y <= 127; ++y) {
        original.setBlock(2, y, 0, world::BlockId::Dirt);
    }

    // 4. Multi-section checkerboard in Column (5, 5)
    for (int32_t y = 0; y < 128; ++y) {
        original.setBlock(5, y, 5, (y % 2 == 0) ? world::BlockId::Glass : world::BlockId::Obsidian);
    }

    // Explicitly recalculate lighting on original chunk so its heightmap matches its contents
    original.recalculateLighting();

    // Serialize
    const std::vector<uint8_t> rle = original.serializeRLE();
    EXPECT_GE(rle.size(), 8U);
    EXPECT_EQ(rle[0], 'B');
    EXPECT_EQ(rle[1], 'C');
    EXPECT_EQ(rle[2], '1');
    EXPECT_EQ(rle[3], '7');
    EXPECT_EQ(rle[4], 1);

    // Deserialize into fresh chunk
    world::Chunk restored(core::ChunkCoord{1, 2});
    const bool success = restored.deserializeRLE(rle);
    EXPECT_TRUE(success);

    // Full 32,768 voxel equality check
    for (int32_t y = 0; y < 128; ++y) {
        for (int32_t z = 0; z < 16; ++z) {
            for (int32_t x = 0; x < 16; ++x) {
                ASSERT_EQ(restored.getBlock(x, y, z), original.getBlock(x, y, z))
                    << "Voxel divergence at (" << x << ", " << y << ", " << z << ")";
            }
        }
    }

    // Heightmap verification
    for (int32_t z = 0; z < 16; ++z) {
        for (int32_t x = 0; x < 16; ++x) {
            EXPECT_EQ(restored.getHeightAt(x, z), original.getHeightAt(x, z))
                << "Heightmap divergence at (" << x << ", " << z << ")";
        }
    }
}

TEST(AdversarialChunkSubChunk, RLEMalformedBufferAdversarialInputs) {
    world::Chunk chunk(core::ChunkCoord{0, 0});

    // 1. Empty buffer
    EXPECT_FALSE(chunk.deserializeRLE(std::span<const uint8_t>()));

    // 2. Header too short
    const uint8_t shortHeader[] = {'B', 'C', '1'};
    EXPECT_FALSE(chunk.deserializeRLE(shortHeader));

    // 3. Bad magic header
    const uint8_t badMagic[] = {'X', 'Y', 'Z', 'W', 1, 0, 1, 0};
    EXPECT_FALSE(chunk.deserializeRLE(badMagic));

    // 4. Bad version
    const uint8_t badVersion[] = {'B', 'C', '1', '7', 99, 0, 1, 0};
    EXPECT_FALSE(chunk.deserializeRLE(badVersion));

    // 5. Zero run length
    const uint8_t zeroRun[] = {'B', 'C', '1', '7', 1, 1, 0, 0};
    EXPECT_FALSE(chunk.deserializeRLE(zeroRun));

    // 6. Run length overflow beyond 32,768 voxels
    const uint8_t overflowRun[] = {'B', 'C', '1', '7', 1, 1, 0x01, 0x80}; // 32,769 run
    EXPECT_FALSE(chunk.deserializeRLE(overflowRun));

    // 7. Incomplete stream (< 32,768 total voxels)
    const uint8_t underflowRun[] = {'B', 'C', '1', '7', 1, 1, 10, 0}; // only 10 voxels
    EXPECT_FALSE(chunk.deserializeRLE(underflowRun));
}

// =========================================================================
// Suite 2: PalettedSection Dynamic Promotion & Lazy Lighting
// =========================================================================

TEST(AdversarialPalettedSection, HighBlockIdPreservedIn4BitAnd8Bit) {
    world::PalettedSection sec;

    // Use high block IDs that would be corrupted if truncated to 4 or 8 bits
    constexpr world::BlockStateId HIGH_ID_1 = 0xABCD; // 43,981
    constexpr world::BlockStateId HIGH_ID_2 = 0x1234; // 4,660

    sec.setBlock(0, 0, 0, HIGH_ID_1);
    EXPECT_EQ(sec.getBitsPerVoxel(), 4);
    EXPECT_EQ(sec.getBlockState(0, 0, 0), HIGH_ID_1);

    sec.setBlock(1, 0, 0, HIGH_ID_2);
    EXPECT_EQ(sec.getBitsPerVoxel(), 4);
    EXPECT_EQ(sec.getBlockState(0, 0, 0), HIGH_ID_1);
    EXPECT_EQ(sec.getBlockState(1, 0, 0), HIGH_ID_2);
    EXPECT_EQ(sec.getBlockState(2, 0, 0), 0); // Air
}

TEST(AdversarialPalettedSection, DenseLightReclaimMemoryOnReset) {
    world::PalettedSection sec(world::BlockId::Air, 15, 0);
    EXPECT_FALSE(sec.hasLightData());
    EXPECT_EQ(sec.getMemoryFootprintBytes(), sizeof(world::PalettedSection));

    // Non-uniform modification causes allocation
    sec.setSkyLight(5, 5, 5, 8);
    EXPECT_TRUE(sec.hasLightData());
    EXPECT_EQ(sec.getSkyLight(5, 5, 5), 8);
    EXPECT_EQ(sec.getSkyLight(5, 5, 6), 15);
    EXPECT_GT(sec.getMemoryFootprintBytes(), sizeof(world::PalettedSection));

    // Resetting uniform lighting frees the heap allocation
    sec.setUniformLighting(15, 0);
    EXPECT_FALSE(sec.hasLightData());
    EXPECT_EQ(sec.getSkyLight(5, 5, 5), 15);
    EXPECT_EQ(sec.getMemoryFootprintBytes(), sizeof(world::PalettedSection));
}

// =========================================================================
// Suite 3: Lighting Across Sub-Chunk Boundaries
// =========================================================================

TEST(AdversarialLighting, SunlightPenetrationThroughSubChunks) {
    world::Chunk chunk(core::ChunkCoord{0, 0});

    // Place an opaque roof in Section 4 (Y = 64) covering entire (X, Z) plane
    for (int32_t z = 0; z < 16; ++z) {
        for (int32_t x = 0; x < 16; ++x) {
            chunk.setBlock(x, 64, z, world::BlockId::Stone);
        }
    }

    chunk.recalculateLighting();

    // Sections 5, 6, 7 (Y = 80..127): Full sunlight (15)
    for (int32_t y = 65; y < 128; ++y) {
        EXPECT_EQ(chunk.getSkyLight(8, y, 8), 15) << "Expected full sunlight at Y=" << y;
    }

    // Sections 0, 1, 2, 3 (Y = 0..63): Darkness below roof (0)
    for (int32_t y = 0; y < 64; ++y) {
        EXPECT_EQ(chunk.getSkyLight(8, y, 8), 0) << "Expected dark shadow at Y=" << y;
    }
}

TEST(AdversarialLighting, BlockLightCrossesSectionBoundaryY15ToY16) {
    world::Chunk chunk(core::ChunkCoord{0, 0});

    // Clear all lighting to 0
    for (size_t sec = 0; sec < 8; ++sec) {
        chunk.getSection(sec).setUniformLighting(0, 0);
    }

    // Place torch at Y = 15 (top-most layer of Section 0)
    chunk.setBlock(8, 15, 8, world::BlockId::Torch);
    chunk.spreadBlockLight(8, 15, 8, 14);

    EXPECT_EQ(chunk.getBlockLight(8, 15, 8), 14);

    // Light must cross into Section 1 (Y = 16, local Y = 0) with attenuation -1
    EXPECT_EQ(chunk.getBlockLight(8, 16, 8), 13);
    EXPECT_EQ(chunk.getSection(1).getBlockLight(8, 0, 8), 13);

    // Light must propagate further into Section 1 (Y = 17, local Y = 1) with attenuation -2
    EXPECT_EQ(chunk.getBlockLight(8, 17, 8), 12);
    EXPECT_EQ(chunk.getSection(1).getBlockLight(8, 1, 8), 12);
}

// =========================================================================
// Suite 4: Player Collision Stepping & Alpha Interpolation Determinism
// =========================================================================

TEST(AdversarialPhysics, AlphaInterpolationMonotonicityAndBounds) {
    world::World world(123, "");
    world.ensureChunkLoaded({0, 0});
    // Platform at Y = 100
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            world.setBlock(x, 100, z, world::BlockId::Stone);
        }
    }

    physics::Player player;
    player.setPosition(glm::vec3(8.0f, 101.0f, 8.0f));
    player.resetInterpolationHistory();

    // Advance 1 tick forward
    physics::PlayerInput input{.forward = true};
    constexpr float FIXED_DT = 1.0f / 60.0f;
    player.fixedUpdate(FIXED_DT, input, world);

    // m_prevPosition is start (8.0, 101.0, 8.0), m_position has moved
    const glm::vec3 p0 = player.getInterpolatedPosition(0.0f);
    const glm::vec3 p1 = player.getInterpolatedPosition(1.0f);
    EXPECT_NE(p0, p1);

    // Clamping checks
    EXPECT_EQ(player.getInterpolatedPosition(-0.5f), p0);
    EXPECT_EQ(player.getInterpolatedPosition(0.0f), p0);
    EXPECT_EQ(player.getInterpolatedPosition(1.0f), p1);
    EXPECT_EQ(player.getInterpolatedPosition(1.5f), p1);

    // Strict monotonicity along movement vector
    float prevDist = -1.0f;
    for (int step = 0; step <= 20; ++step) {
        const float alpha = step / 20.0f;
        const glm::vec3 p = player.getInterpolatedPosition(alpha);
        const float distFromP0 = glm::distance(p, p0);
        EXPECT_GE(distFromP0, prevDist - 1e-6f);
        prevDist = distFromP0;
    }
}

TEST(AdversarialPhysics, LandingFlushOnBlockBoundary) {
    world::World world(999, "");
    world.ensureChunkLoaded({0, 0});

    // Create solid stone floor high at Y = 100 to avoid procedural terrain
    for (int32_t x = 0; x < 16; ++x) {
        for (int32_t z = 0; z < 16; ++z) {
            world.setBlock(x, 100, z, world::BlockId::Stone);
        }
    }

    physics::Player player;
    // Drop player from Y = 105.25f (in air above sky platform)
    player.setPosition(glm::vec3(8.0f, 105.25f, 8.0f));
    player.resetInterpolationHistory();

    physics::PlayerInput noInput{};
    constexpr float FIXED_DT = 1.0f / 60.0f;

    // Simulate 60 ticks (1.0 second) to let player fall and land
    for (int t = 0; t < 60; ++t) {
        player.fixedUpdate(FIXED_DT, noInput, world);
    }

    // Player bounding box height is 1.8f, feet should land flush on stone top (Y = 101.0f)
    EXPECT_TRUE(player.isOnGround());
    EXPECT_NEAR(player.getPosition().y, 101.0f, 1e-3f);
    EXPECT_FLOAT_EQ(player.getPosition().x, 8.0f);
    EXPECT_FLOAT_EQ(player.getPosition().z, 8.0f);
}

TEST(AdversarialPhysics, HorizontalWallCollisionPreventsPenetration) {
    world::World world(999, "");
    world.ensureChunkLoaded({0, 0});

    // Stone floor at Y = 100
    for (int32_t x = 0; x < 16; ++x) {
        for (int32_t z = 0; z < 16; ++z) {
            world.setBlock(x, 100, z, world::BlockId::Stone);
        }
    }

    // Solid wall at X = 10 from Y = 101 to Y = 103
    for (int32_t z = 0; z < 16; ++z) {
        for (int32_t y = 101; y <= 103; ++y) {
            world.setBlock(10, y, z, world::BlockId::Stone);
        }
    }

    physics::Player player;
    // Place player at X = 8.5 (facing +X, yaw = 0)
    player.setPosition(glm::vec3(8.5f, 101.0f, 8.0f));
    player.setRotation(0.0f, 0.0f); // 0 deg = +X in Betacraft
    player.resetInterpolationHistory();

    physics::PlayerInput forwardInput{.forward = true};
    constexpr float FIXED_DT = 1.0f / 60.0f;

    // Settle first tick then walk forward for 60 ticks into the wall
    for (int t = 0; t < 60; ++t) {
        player.fixedUpdate(FIXED_DT, forwardInput, world);
    }

    // Player half-width is 0.3f. Wall is at X=10.0f (min.x = 10.0f).
    // Player max.x = player.x + 0.3f <= 10.0f => player.x <= 9.7f.
    EXPECT_LE(player.getPosition().x, 9.701f);
    EXPECT_GE(player.getPosition().x, 9.60f); // Pressed closely against wall
}

TEST(AdversarialPhysics, ChaoticFramerateJitterDeterminism) {
    world::World world(12345, "");
    world.ensureChunkLoaded({0, 0});
    world.ensureChunkLoaded({1, 0});
    for (int32_t x = 0; x < 32; ++x) {
        for (int32_t z = 0; z < 32; ++z) {
            world.setBlock(x, 100, z, world::BlockId::Stone);
        }
    }

    const glm::vec3 startPos{5.0f, 101.0f, 5.0f};
    physics::PlayerInput input{.forward = true, .jump = true};
    constexpr float FIXED_DT = 1.0f / 60.0f;
    constexpr float TOTAL_DURATION = 3.0f; // 180 physics ticks exactly

    // Baseline: Constant 60 FPS
    physics::Player p60;
    p60.setPosition(startPos);
    p60.resetInterpolationHistory();
    float acc60 = 0.0f;
    int ticks60 = 0;
    while (ticks60 < 180) {
        acc60 += (1.0f / 60.0f);
        while (acc60 >= FIXED_DT - 1e-5f && ticks60 < 180) {
            p60.fixedUpdate(FIXED_DT, input, world);
            acc60 -= FIXED_DT;
            ++ticks60;
        }
    }

    // Chaotic Jitter simulation
    physics::Player pJitter;
    pJitter.setPosition(startPos);
    pJitter.resetInterpolationHistory();
    float accJitter = 0.0f;
    int ticksJitter = 0;

    std::mt19937 rng(42);
    std::uniform_real_distribution<float> jitterDist(0.003f, 0.045f); // 3ms to 45ms frame times

    float simulatedTime = 0.0f;
    while (ticksJitter < 180) {
        const float frameDt = jitterDist(rng);
        simulatedTime += frameDt;
        accJitter = std::min(accJitter + frameDt, 0.25f);
        while (accJitter >= FIXED_DT - 1e-5f && ticksJitter < 180) {
            pJitter.fixedUpdate(FIXED_DT, input, world);
            accJitter -= FIXED_DT;
            ++ticksJitter;
        }
    }

    EXPECT_EQ(ticks60, 180);
    EXPECT_EQ(ticksJitter, 180);

    // Positions must be identical down to floating point precision
    EXPECT_NEAR(p60.getPosition().x, pJitter.getPosition().x, 1e-4f);
    EXPECT_NEAR(p60.getPosition().y, pJitter.getPosition().y, 1e-4f);
    EXPECT_NEAR(p60.getPosition().z, pJitter.getPosition().z, 1e-4f);
}

TEST(AdversarialChunkSubChunk, RealisticHeterogeneousSubchunkRLEIntegrity) {
    world::Chunk chunk(core::ChunkCoord{-5, 7});

    // Section 0 (Y=0..15): Bedrock floor, Obsidian, Stone
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            chunk.setBlock(x, 0, z, world::BlockId::Bedrock);
            chunk.setBlock(x, 1, z, world::BlockId::Obsidian);
            for (int y = 2; y <= 15; ++y) {
                chunk.setBlock(x, y, z, world::BlockId::Stone);
            }
        }
    }

    // Section 1 (Y=16..31): Stone with veins of CoalOre, IronOre, GoldOre
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            for (int y = 16; y <= 31; ++y) {
                world::BlockId b = world::BlockId::Stone;
                if ((x + y + z) % 7 == 0) b = world::BlockId::CoalOre;
                if ((x * 3 + y + z) % 11 == 0) b = world::BlockId::IronOre;
                if ((x + y * 2 + z) % 19 == 0) b = world::BlockId::GoldOre;
                chunk.setBlock(x, y, z, b);
            }
        }
    }

    // Section 2 (Y=32..47): Cavern with air pockets and water pools
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            for (int y = 32; y <= 47; ++y) {
                if (x >= 4 && x <= 12 && z >= 4 && z <= 12) {
                    chunk.setBlock(x, y, z, (y == 32) ? world::BlockId::Water : world::BlockId::Air);
                } else {
                    chunk.setBlock(x, y, z, world::BlockId::Stone);
                }
            }
        }
    }

    // Section 3 (Y=48..63): Dirt with top Grass at Y=63
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            for (int y = 48; y < 63; ++y) {
                chunk.setBlock(x, y, z, world::BlockId::Dirt);
            }
            chunk.setBlock(x, 63, z, world::BlockId::Grass);
        }
    }

    // Section 4 (Y=64..79): Surface with trees (OakLog trunk, Leaves canopy)
    // Tree at (8, 64..70, 8)
    for (int y = 64; y <= 68; ++y) {
        chunk.setBlock(8, y, 8, world::BlockId::OakLog);
    }
    for (int lx = 6; lx <= 10; ++lx) {
        for (int lz = 6; lz <= 10; ++lz) {
            for (int ly = 67; ly <= 70; ++ly) {
                if (chunk.getBlock(lx, ly, lz) == world::BlockId::Air) {
                    chunk.setBlock(lx, ly, lz, world::BlockId::Leaves);
                }
            }
        }
    }

    // Section 5 (Y=80..95): Pure Air (homogeneous)
    // Left untouched

    // Section 6 (Y=96..111): Pure Air (homogeneous)
    // Left untouched

    // Section 7 (Y=112..127): Glass ceiling at Y=127
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            chunk.setBlock(x, 127, z, world::BlockId::Glass);
        }
    }

    // Verify sections 5 and 6 are homogeneous
    EXPECT_TRUE(chunk.getSection(5).isHomogeneous());
    EXPECT_TRUE(chunk.getSection(6).isHomogeneous());

    chunk.recalculateLighting();

    // Roundtrip RLE
    const std::vector<uint8_t> rle = chunk.serializeRLE();
    EXPECT_GE(rle.size(), 8U);

    world::Chunk loaded(core::ChunkCoord{-5, 7});
    ASSERT_TRUE(loaded.deserializeRLE(rle));

    // Full 3D verification
    for (int y = 0; y < 128; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                ASSERT_EQ(loaded.getBlock(x, y, z), chunk.getBlock(x, y, z))
                    << "Discrepancy at (" << x << ", " << y << ", " << z << ")";
            }
        }
    }

    // Verify sections 5 and 6 in loaded chunk remain pure air voxels (0 bits per voxel)
    EXPECT_EQ(loaded.getSection(5).getBitsPerVoxel(), 0);
    EXPECT_EQ(loaded.getSection(6).getBitsPerVoxel(), 0);
    EXPECT_EQ(loaded.getSection(5).getBlock(8, 8, 8), world::BlockId::Air);
    EXPECT_EQ(loaded.getSection(6).getBlock(8, 8, 8), world::BlockId::Air);
}

TEST(AdversarialPhysics, SneakLedgeProtectionDeterminism) {
    world::World world(777, "");
    world.ensureChunkLoaded({0, 0});

    // High platform in sky at Y = 100, extending from X=0..8, Z=0..16
    // Top surface of platform is at Y = 101.0f, platform edge is at X = 9.0f
    for (int32_t z = 0; z < 16; ++z) {
        for (int32_t x = 0; x <= 8; ++x) {
            world.setBlock(x, 100, z, world::BlockId::Cobblestone);
        }
    }

    physics::Player player;
    // Place player at (7.5f, 101.0f, 8.0f) facing +X (yaw = 0)
    player.setPosition(glm::vec3(7.5f, 101.0f, 8.0f));
    player.setRotation(0.0f, 0.0f);
    player.resetInterpolationHistory();

    // Settle player on ground
    physics::PlayerInput settleInput{};
    for (int i = 0; i < 10; ++i) {
        player.fixedUpdate(1.0f / 60.0f, settleInput, world);
    }
    ASSERT_TRUE(player.isOnGround());

    // Walk towards edge at X=9.0 with sneak held for 120 ticks (2.0s)
    physics::PlayerInput sneakForward{.forward = true, .sneak = true};
    for (int i = 0; i < 120; ++i) {
        player.fixedUpdate(1.0f / 60.0f, sneakForward, world);
    }

    // Player must remain on ground and NOT fall off ledge (Y must remain 101.0f)
    EXPECT_TRUE(player.isOnGround());
    EXPECT_NEAR(player.getPosition().y, 101.0f, 1e-3f);
    // Player position X must be bounded before stepping off platform edge (X=9.0)
    EXPECT_LE(player.getPosition().x, 9.35f);
    EXPECT_GE(player.getPosition().x, 8.5f);
}

TEST(AdversarialPhysics, InnerCornerCollisionResolution) {
    world::World world(888, "");
    world.ensureChunkLoaded({0, 0});

    // Floor at Y = 100
    for (int32_t z = 0; z < 16; ++z) {
        for (int32_t x = 0; x < 16; ++x) {
            world.setBlock(x, 100, z, world::BlockId::Stone);
        }
    }

    // Wall 1 at X = 10, Wall 2 at Z = 10 (forming an inside corner)
    for (int32_t y = 101; y <= 103; ++y) {
        for (int32_t z = 0; z < 16; ++z) {
            world.setBlock(10, y, z, world::BlockId::Stone);
        }
        for (int32_t x = 0; x < 16; ++x) {
            world.setBlock(x, y, 10, world::BlockId::Stone);
        }
    }

    physics::Player player;
    // Place player at (8.5f, 101.0f, 8.5f) facing diagonal +X +Z (yaw = 45)
    player.setPosition(glm::vec3(8.5f, 101.0f, 8.5f));
    player.setRotation(45.0f, 0.0f);
    player.resetInterpolationHistory();

    physics::PlayerInput forwardInput{.forward = true};
    constexpr float FIXED_DT = 1.0f / 60.0f;

    for (int t = 0; t < 60; ++t) {
        player.fixedUpdate(FIXED_DT, forwardInput, world);
    }

    // Player must be safely stopped by both walls without tunneling through the corner
    EXPECT_LE(player.getPosition().x, 9.701f);
    EXPECT_LE(player.getPosition().z, 9.701f);
    EXPECT_GE(player.getPosition().x, 9.50f);
    EXPECT_GE(player.getPosition().z, 9.50f);
    EXPECT_NEAR(player.getPosition().y, 101.0f, 1e-3f);
}

TEST(AdversarialPalettedSection, Full4096VoxelsRandomLightingArray) {
    world::PalettedSection sec;
    EXPECT_FALSE(sec.hasLightData());

    std::mt19937 rng(1337);
    std::uniform_int_distribution<int> lightDist(0, 15);

    uint8_t expectedSky[16][16][16];
    uint8_t expectedBlock[16][16][16];

    for (int ly = 0; ly < 16; ++ly) {
        for (int lz = 0; lz < 16; ++lz) {
            for (int lx = 0; lx < 16; ++lx) {
                const uint8_t s = static_cast<uint8_t>(lightDist(rng));
                const uint8_t b = static_cast<uint8_t>(lightDist(rng));
                expectedSky[ly][lz][lx] = s;
                expectedBlock[ly][lz][lx] = b;
                sec.setSkyLight(lx, ly, lz, s);
                sec.setBlockLight(lx, ly, lz, b);
            }
        }
    }

    EXPECT_TRUE(sec.hasLightData());

    // Verify all 4,096 voxels match exactly
    for (int ly = 0; ly < 16; ++ly) {
        for (int lz = 0; lz < 16; ++lz) {
            for (int lx = 0; lx < 16; ++lx) {
                ASSERT_EQ(sec.getSkyLight(lx, ly, lz), expectedSky[ly][lz][lx]);
                ASSERT_EQ(sec.getBlockLight(lx, ly, lz), expectedBlock[ly][lz][lx]);
            }
        }
    }
}

} // namespace betacraft::adversarial

