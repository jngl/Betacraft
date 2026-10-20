#include "physics/Player.hpp"
#include "render/PackedChunkVertex.hpp"
#include "tests/test_helpers.hpp"
#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"
#include "voxel/PalettedSection.hpp"
#include "world/DifferentialLightingEngine.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>
#include <random>
#include <vector>
#include <array>
#include <cmath>
#include <algorithm>

using namespace betacraft;
using namespace betacraft::physics;
using namespace betacraft::world;
using namespace betacraft::render;

// ============================================================================
// SUITE 1: Complex Multi-Chunk Border Light Propagation & Analytical Bounds
// ============================================================================

class MultiChunkLightingFixture : public ::testing::Test {
protected:
    test::TempDir tempDir;
    std::unique_ptr<World> world;

    void SetUp() override {
        BlockRegistry::initialize();
        world = std::make_unique<World>(173173ULL, tempDir.path());
    }

    void loadChunkGrid(int minCX, int maxCX, int minCZ, int maxCZ) {
        for (int cz = minCZ; cz <= maxCZ; ++cz) {
            for (int cx = minCX; cx <= maxCX; ++cx) {
                world->ensureChunkLoaded({cx, cz});
            }
        }
    }

    void clearAirVolume(int minX, int maxX, int minY, int maxY, int minZ, int maxZ) {
        for (int y = minY; y <= maxY; ++y) {
            for (int z = minZ; z <= maxZ; ++z) {
                for (int x = minX; x <= maxX; ++x) {
                    world->setBlock(x, y, z, BlockId::Air);
                    world->setBlockLight(x, y, z, 0);
                    world->setSkyLight(x, y, z, 0);
                }
            }
        }
    }
};

TEST_F(MultiChunkLightingFixture, CornerFourChunkLightPropagationAndExtinction) {
    // 4 chunks meeting at origin (0, 0): (0, 0), (-1, 0), (0, -1), (-1, -1)
    loadChunkGrid(-1, 0, -1, 0);
    clearAirVolume(-16, 15, 48, 80, -16, 15);

    // Place light source at (0, 64, 0) with emission 15 (Glowstone)
    // Touching the corner of all 4 chunks
    world->setBlock(0, 64, 0, BlockId::Glowstone);
    const size_t placeVisits = world->getLightingEngine().getLastVoxelInspectionCount();

    // Verify analytical bound: N(15) <= 4,991 voxels
    EXPECT_LE(placeVisits, 4991u) << "Glowstone placement must not exceed analytical bound N(15)=4991";
    EXPECT_EQ(world->getBlockLight(0, 64, 0), 15u);

    // Verify light propagated symmetrically into all 4 chunks
    // Chunk (0, 0): (+X, +Z)
    EXPECT_EQ(world->getBlockLight(1, 64, 0), 14u);
    EXPECT_EQ(world->getBlockLight(0, 64, 1), 14u);
    EXPECT_EQ(world->getBlockLight(1, 64, 1), 13u);

    // Chunk (-1, 0): (-X, +Z)
    EXPECT_EQ(world->getBlockLight(-1, 64, 0), 14u);
    EXPECT_EQ(world->getBlockLight(-1, 64, 1), 13u);

    // Chunk (0, -1): (+X, -Z)
    EXPECT_EQ(world->getBlockLight(0, 64, -1), 14u);
    EXPECT_EQ(world->getBlockLight(1, 64, -1), 13u);

    // Chunk (-1, -1): (-X, -Z)
    EXPECT_EQ(world->getBlockLight(-1, 64, -1), 13u);
    EXPECT_EQ(world->getBlockLight(-7, 64, -7), 1u); // Manhattan dist = 7 + 0 + 7 = 14 => 15 - 14 = 1
    EXPECT_EQ(world->getBlockLight(-8, 64, -7), 0u); // Manhattan dist = 15 => 0

    // Remove Glowstone: assert clean extinction across all 4 chunks
    world->setBlock(0, 64, 0, BlockId::Air);
    const size_t removeVisits = world->getLightingEngine().getLastVoxelInspectionCount();
    EXPECT_LE(removeVisits, 4991u) << "Glowstone removal must not exceed analytical bound N(15)=4991";

    EXPECT_EQ(world->getBlockLight(0, 64, 0), 0u);
    EXPECT_EQ(world->getBlockLight(1, 64, 0), 0u);
    EXPECT_EQ(world->getBlockLight(-1, 64, 0), 0u);
    EXPECT_EQ(world->getBlockLight(0, 64, 1), 0u);
    EXPECT_EQ(world->getBlockLight(0, 64, -1), 0u);
    EXPECT_EQ(world->getBlockLight(-1, 64, -1), 0u);
    EXPECT_EQ(world->getBlockLight(-7, 64, -7), 0u);
}

TEST_F(MultiChunkLightingFixture, PartialChunkBoundaryIsolationNoCrash) {
    // Only load Chunk (0, 0) and Chunk (1, 0).
    // Chunks (0, 1) and (1, 1) remain UNLOADED.
    world->ensureChunkLoaded({0, 0});
    world->ensureChunkLoaded({1, 0});
    clearAirVolume(0, 31, 48, 80, 0, 15);

    // Place Glowstone at (15, 64, 15) in Chunk (0, 0), right on the edge of unloaded (0, 1)
    world->setBlock(15, 64, 15, BlockId::Glowstone);
    const size_t placeVisits = world->getLightingEngine().getLastVoxelInspectionCount();

    // Must be bounded and must not crash or leak memory
    EXPECT_LE(placeVisits, 4991u);
    EXPECT_EQ(world->getBlockLight(15, 64, 15), 15u);
    EXPECT_EQ(world->getBlockLight(16, 64, 15), 14u); // in loaded Chunk (1, 0)

    // Querying unloaded chunk directly returns 0 safely
    EXPECT_EQ(world->getBlockLight(15, 64, 16), 0u); // in unloaded Chunk (0, 1)

    // Remove light source
    world->setBlock(15, 64, 15, BlockId::Air);
    EXPECT_EQ(world->getBlockLight(15, 64, 15), 0u);
    EXPECT_EQ(world->getBlockLight(16, 64, 15), 0u);
}

TEST_F(MultiChunkLightingFixture, MultiSourceOverlappingChunkBorderReinjection) {
    // Load 2x2 chunks
    loadChunkGrid(0, 1, 0, 1);
    clearAirVolume(0, 31, 48, 80, 0, 31);

    // 4 torches arranged in a 2x2 square right on the chunk boundaries:
    // (15, 64, 15) in Chunk (0, 0)
    // (16, 64, 15) in Chunk (1, 0)
    // (15, 64, 16) in Chunk (0, 1)
    // (16, 64, 16) in Chunk (1, 1)
    world->setBlock(15, 64, 15, BlockId::Torch);
    world->setBlock(16, 64, 15, BlockId::Torch);
    world->setBlock(15, 64, 16, BlockId::Torch);
    world->setBlock(16, 64, 16, BlockId::Torch);

    EXPECT_EQ(world->getBlockLight(15, 64, 15), 14u);
    EXPECT_EQ(world->getBlockLight(16, 64, 15), 14u);
    EXPECT_EQ(world->getBlockLight(15, 64, 16), 14u);
    EXPECT_EQ(world->getBlockLight(16, 64, 16), 14u);

    // Remove Torch at (15, 64, 15)
    world->setBlock(15, 64, 15, BlockId::Air);
    // Position (15, 64, 15) should be illuminated by adjacent torches at dist 1 -> light = 13!
    EXPECT_EQ(world->getBlockLight(15, 64, 15), 13u) << "Position must be reinjected by 3 remaining torches";

    // Remove Torch at (16, 64, 15)
    world->setBlock(16, 64, 15, BlockId::Air);
    // (15, 64, 15) is still dist 1 from remaining torch at (15, 64, 16) -> light = 13!
    EXPECT_EQ(world->getBlockLight(15, 64, 15), 13u);
    EXPECT_EQ(world->getBlockLight(16, 64, 15), 13u);

    // Remove Torch at (15, 64, 16)
    world->setBlock(15, 64, 16, BlockId::Air);
    EXPECT_EQ(world->getBlockLight(15, 64, 15), 12u); // from (16, 64, 16) at dist 2 => 14 - 2 = 12

    // Remove final Torch at (16, 64, 16)
    world->setBlock(16, 64, 16, BlockId::Air);
    EXPECT_EQ(world->getBlockLight(15, 64, 15), 0u);
    EXPECT_EQ(world->getBlockLight(16, 64, 15), 0u);
    EXPECT_EQ(world->getBlockLight(15, 64, 16), 0u);
    EXPECT_EQ(world->getBlockLight(16, 64, 16), 0u);
}

TEST_F(MultiChunkLightingFixture, SkylightVerticalObstructionAndBoundaryExtinction) {
    world->ensureChunkLoaded({0, 0});

    // Clear vertical column (8, Y, 8)
    for (int y = 0; y < Chunk::HEIGHT; ++y) {
        world->setBlock(8, y, 8, BlockId::Air);
        world->setSkyLight(8, y, 8, 15);
    }

    // 1. Obstruct at top of world Y = 127
    world->setBlock(8, 127, 8, BlockId::Cobblestone);
    const size_t obstructVisits = world->getLightingEngine().getLastVoxelInspectionCount();
    // 1D raycast visits voxels directly
    EXPECT_LE(obstructVisits, 4991u);

    // Single block obstruction attenuates sunlight from 15 to <= 14 (neighbor leakage)
    EXPECT_LE(world->getSkyLight(8, 126, 8), 14u);
    EXPECT_LE(world->getSkyLight(8, 64, 8), 14u);

    // 2. Re-expose by destroying block at Y = 127
    world->setBlock(8, 127, 8, BlockId::Air);
    // Vertical column should be restored to 15 down to 0
    EXPECT_EQ(world->getSkyLight(8, 127, 8), 15u);
    EXPECT_EQ(world->getSkyLight(8, 126, 8), 15u);
    EXPECT_EQ(world->getSkyLight(8, 64, 8), 15u);
    EXPECT_EQ(world->getSkyLight(8, 0, 8), 15u);

    // 3. Boundary edge case: Obstruction at bedrock level Y = 0
    world->setBlock(8, 0, 8, BlockId::Bedrock);
    // Above bedrock (Y=1) must still be 15
    EXPECT_EQ(world->getSkyLight(8, 1, 8), 15u);
    EXPECT_EQ(world->getSkyLight(8, 0, 8), 0u);
}

TEST_F(MultiChunkLightingFixture, AnalyticalManhattanBoundStrictnessAcrossAllEmissions) {
    world->ensureChunkLoaded({0, 0});
    clearAirVolume(0, 15, 48, 80, 0, 15);

    // For all emission levels 1..15, verify:
    // placement visits <= N(emission)
    // removal visits <= N(emission)
    for (uint8_t emission = 1; emission <= 15; ++emission) {
        const size_t bound = DifferentialLightingEngine::analyticalManhattanVolume(emission);
        world->getLightingEngine().updateLightSource(8, 64, 8, emission);
        const size_t placeVisits = world->getLightingEngine().getLastVoxelInspectionCount();
        EXPECT_LE(placeVisits, bound) << "Emission " << (int)emission << " placement exceeded N(" << (int)emission << ")=" << bound;

        world->getLightingEngine().updateLightSource(8, 64, 8, 0);
        const size_t removeVisits = world->getLightingEngine().getLastVoxelInspectionCount();
        EXPECT_LE(removeVisits, bound) << "Emission " << (int)emission << " removal exceeded N(" << (int)emission << ")=" << bound;
    }
}

// ============================================================================
// SUITE 2: PalettedSection Dynamic Transitions & Memory Stress
// ============================================================================

TEST(AdversarialTier5Storage, MemoryInvariantsAndHomogeneousStates) {
    EXPECT_EQ(sizeof(PalettedSection), 64u);
    EXPECT_EQ(alignof(PalettedSection), 64u);

    PalettedSection secAir(BlockId::Air, 15, 0);
    EXPECT_TRUE(secAir.isHomogeneous());
    EXPECT_FALSE(secAir.hasLightData());
    EXPECT_EQ(secAir.getBitsPerVoxel(), 0u);
    EXPECT_EQ(secAir.getMemoryFootprintBytes(), 64u);

    PalettedSection secBedrock(BlockId::Bedrock, 0, 0);
    EXPECT_TRUE(secBedrock.isHomogeneous());
    EXPECT_FALSE(secBedrock.hasLightData());
    EXPECT_EQ(secBedrock.getBitsPerVoxel(), 0u);
    EXPECT_EQ(secBedrock.getMemoryFootprintBytes(), 64u);
}

TEST(AdversarialTier5Storage, DynamicPalettePromotionChurnUnderMemoryStress) {
    // Cycle 500 sections through 0 -> 4 -> 8 -> 16 bits with random mutations
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> coordDist(0, 15);
    std::uniform_int_distribution<uint16_t> blockDist(0, 500);

    for (int cycle = 0; cycle < 50; ++cycle) {
        PalettedSection sec;
        std::array<BlockStateId, 4096> shadowOracle;
        shadowOracle.fill(static_cast<BlockStateId>(BlockId::Air));

        // 1. Promote to 4-bit
        sec.setBlock(0, 0, 0, BlockId::Stone);
        shadowOracle[0] = static_cast<BlockStateId>(BlockId::Stone);
        EXPECT_EQ(sec.getBitsPerVoxel(), 4u);

        // 2. Fill to 16 blocks
        for (uint16_t id = 2; id < 16; ++id) {
            sec.setBlock(id, 0, 0, static_cast<BlockStateId>(100 + id));
            shadowOracle[PalettedSection::voxelIndex(id, 0, 0)] = static_cast<BlockStateId>(100 + id);
        }
        EXPECT_EQ(sec.getBitsPerVoxel(), 4u);
        EXPECT_EQ(sec.getPaletteSize(), 16u);

        // 3. Promote to 8-bit
        sec.setBlock(0, 1, 0, static_cast<BlockStateId>(999));
        shadowOracle[PalettedSection::voxelIndex(0, 1, 0)] = 999;
        EXPECT_EQ(sec.getBitsPerVoxel(), 8u);

        // 4. Fill to 256 blocks
        for (uint16_t id = 17; id < 256; ++id) {
            const int lx = id % 16;
            const int ly = (id / 16) % 16;
            sec.setBlock(lx, ly, 1, static_cast<BlockStateId>(2000 + id));
            shadowOracle[PalettedSection::voxelIndex(lx, ly, 1)] = static_cast<BlockStateId>(2000 + id);
        }
        EXPECT_EQ(sec.getBitsPerVoxel(), 8u);
        EXPECT_EQ(sec.getPaletteSize(), 256u);

        // 5. Promote to 16-bit direct mode
        sec.setBlock(15, 15, 15, static_cast<BlockStateId>(12345));
        shadowOracle[PalettedSection::voxelIndex(15, 15, 15)] = 12345;
        EXPECT_EQ(sec.getBitsPerVoxel(), 16u);
        EXPECT_EQ(sec.getPaletteSize(), 0u); // palette freed in direct mode

        // Random mutations in 16-bit mode
        for (int m = 0; m < 500; ++m) {
            const int x = coordDist(rng);
            const int y = coordDist(rng);
            const int z = coordDist(rng);
            const BlockStateId b = blockDist(rng);
            sec.setBlock(x, y, z, b);
            shadowOracle[PalettedSection::voxelIndex(x, y, z)] = b;
        }

        // Verify oracle integrity
        for (int y = 0; y < 16; ++y) {
            for (int z = 0; z < 16; ++z) {
                for (int x = 0; x < 16; ++x) {
                    EXPECT_EQ(sec.getBlockState(x, y, z), shadowOracle[PalettedSection::voxelIndex(x, y, z)]);
                }
            }
        }
    }
}

TEST(AdversarialTier5Storage, DenseLightArrayAllocationAndDeallocationChurn) {
    PalettedSection sec(BlockId::Air, 15, 0);
    EXPECT_FALSE(sec.hasLightData());

    for (int i = 0; i < 5000; ++i) {
        // Allocate via non-uniform light write
        sec.setBlockLight(i % 16, (i / 16) % 16, (i / 256) % 16, static_cast<uint8_t>((i % 14) + 1));
        EXPECT_TRUE(sec.hasLightData());

        // Deallocate via uniform reset
        sec.setUniformLighting(15, 0);
        EXPECT_FALSE(sec.hasLightData());
        EXPECT_EQ(sec.getMemoryFootprintBytes(), 64u);
    }
}

TEST(AdversarialTier5Storage, ExtremeBlockStateIdsDirectMode) {
    PalettedSection sec;
    const BlockStateId extremeIds[] = { 0, 1, 255, 256, 1000, 32767, 65534, 65535 };
    for (size_t i = 0; i < std::size(extremeIds); ++i) {
        sec.setBlock(static_cast<int>(i), 0, 0, extremeIds[i]);
    }
    // Force 16-bit direct mode
    for (uint16_t i = 0; i < 260; ++i) {
        sec.setBlock(i % 16, (i / 16) % 16, 1 + (i / 256), static_cast<BlockStateId>(50000 + i));
    }
    EXPECT_EQ(sec.getBitsPerVoxel(), 16u);

    for (size_t i = 0; i < std::size(extremeIds); ++i) {
        EXPECT_EQ(sec.getBlockState(static_cast<int>(i), 0, 0), extremeIds[i]);
    }
}

// ============================================================================
// SUITE 3: Player Collision Edge Cases at World Boundaries
// ============================================================================

class PlayerWorldCollisionFixture : public ::testing::Test {
protected:
    test::TempDir tempDir;
    std::unique_ptr<World> world;

    void SetUp() override {
        BlockRegistry::initialize();
        world = std::make_unique<World>(173ULL, tempDir.path());
    }
};

TEST_F(PlayerWorldCollisionFixture, NegativeCoordinatesFloorAndBoundaryMovement) {
    // Load chunks covering [-20..20]
    for (int cz = -2; cz <= 1; ++cz) {
        for (int cx = -2; cx <= 1; ++cx) {
            world->ensureChunkLoaded({cx, cz});
        }
    }

    // Build solid floor at Y = 60 across negative to positive range [-20..20]
    for (int z = -20; z <= 20; ++z) {
        for (int x = -20; x <= 20; ++x) {
            world->setBlock(x, 60, z, BlockId::Stone);
            for (int y = 61; y <= 70; ++y) {
                world->setBlock(x, y, z, BlockId::Air);
            }
        }
    }

    Player player;
    // Spawn player at negative coordinates
    player.setPosition(glm::vec3(-10.5f, 61.0f, -10.5f));
    player.setRotation(45.0f, 0.0f); // Face toward positive origin (+X, +Z)
    player.resetInterpolationHistory();

    PlayerInput input{.forward = true};
    // Move across chunk boundary toward origin
    for (int t = 0; t < 120; ++t) {
        player.fixedUpdate(1.0f / 60.0f, input, *world);
    }

    // Player must remain on ground at Y = 61.0f without falling into the void
    const glm::vec3 pos = player.getPosition();
    EXPECT_NEAR(pos.y, 61.0f, 0.05f);
    EXPECT_TRUE(player.isOnGround());
}

TEST_F(PlayerWorldCollisionFixture, SneakLedgeGuardAtNegativeCoordinates) {
    world->ensureChunkLoaded({-1, -1});

    // Create a single solitary block at (-5, 60, -5), surrounded by void
    world->setBlock(-5, 60, -5, BlockId::Stone);

    Player player;
    // Stand on the solitary block (feet at Y=61.0f)
    player.setPosition(glm::vec3(-4.5f, 61.0f, -4.5f));
    player.resetInterpolationHistory();

    // Verify on ground initially
    player.fixedUpdate(1.0f / 60.0f, {}, *world);
    EXPECT_TRUE(player.isOnGround());

    // Try to walk forward off the ledge while sneaking
    PlayerInput sneakInput{.forward = true, .sneak = true};
    for (int t = 0; t < 60; ++t) {
        player.fixedUpdate(1.0f / 60.0f, sneakInput, *world);
    }

    // Sneak ledge guard must have prevented player from walking off the solitary block!
    const glm::vec3 pos = player.getPosition();
    EXPECT_NEAR(pos.y, 61.0f, 0.05f) << "Player must not fall off ledge while sneaking";
    EXPECT_TRUE(player.isOnGround());
    EXPECT_GE(pos.x, -5.2f);
    EXPECT_LE(pos.x, -3.8f);
    EXPECT_GE(pos.z, -5.2f);
    EXPECT_LE(pos.z, -3.8f);
}

TEST_F(PlayerWorldCollisionFixture, VoidFallRecoveryAndCeilingBoundary) {
    world->ensureChunkLoaded({0, 0});

    Player player;
    player.setGameMode(core::GameMode::Creative); // The void safety net is Creative-only (S1.5)
    // Drop player into deep void below Y = -15.0f
    player.setPosition(glm::vec3(8.0f, -16.0f, 8.0f));
    player.resetInterpolationHistory();

    player.fixedUpdate(1.0f / 60.0f, {}, *world);

    // Engine must detect Y < -15.0f and teleport back to Y = 95.0f safely
    EXPECT_GE(player.getPosition().y, 90.0f) << "Player below void must be teleported back to safety";

    // Ceiling boundary test: high elevation above Y = 128
    // Build solid ceiling at Y = 127
    for (int z = 6; z <= 10; ++z) {
        for (int x = 6; x <= 10; ++x) {
            world->setBlock(x, 127, z, BlockId::Stone);
        }
    }

    player.setPosition(glm::vec3(8.0f, 140.0f, 8.0f));
    player.resetInterpolationHistory();

    // Fall down from Y = 140 onto ceiling at Y = 127 (feet should land at Y = 128.0f)
    for (int t = 0; t < 120; ++t) {
        player.fixedUpdate(1.0f / 60.0f, {}, *world);
    }

    EXPECT_NEAR(player.getPosition().y, 128.0f, 0.05f);
    EXPECT_TRUE(player.isOnGround()) << "Player must land on world ceiling without tunneling";
}

TEST_F(PlayerWorldCollisionFixture, HighSpeedImpactNoTunneling) {
    world->ensureChunkLoaded({0, 0});

    // Create 1-block thick stone wall at X = 10
    for (int y = 60; y <= 65; ++y) {
        for (int z = 0; z < 16; ++z) {
            world->setBlock(10, y, z, BlockId::Stone);
        }
    }
    // Solid floor at Y = 60
    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            world->setBlock(x, 60, z, BlockId::Stone);
        }
    }

    Player player;
    player.setPosition(glm::vec3(8.0f, 61.0f, 8.0f));
    player.resetInterpolationHistory();

    // Sprint right into the wall
    PlayerInput sprintInput{.forward = true, .sprint = true};
    for (int t = 0; t < 120; ++t) {
        player.fixedUpdate(1.0f / 60.0f, sprintInput, *world);
    }

    // Player width = 0.6f => half width = 0.3f. Wall at X = 10.
    // Contact position should be <= 10.0f - 0.3f = 9.7f
    EXPECT_LE(player.getPosition().x, 9.72f) << "Player must not tunnel through wall at high speed";
}

// ============================================================================
// SUITE 4: PackedChunkVertex Bitfield & Planar UV Mapping Stress
// ============================================================================

TEST(AdversarialTier5Vertex, BitfieldIntegrityAndMaxValues) {
    // Word 0 max values: X=31, Y=31, Z=31, Norm=7, AO=3, UV=3, Mat=1023
    const uint32_t w0 = PackedChunkVertex::packWord0(31, 31, 31, 7, 3, 3, 1023);
    EXPECT_EQ(w0, 0xFFFFFFFFu);
    EXPECT_EQ(PackedChunkVertex::unpackX(w0), 31u);
    EXPECT_EQ(PackedChunkVertex::unpackY(w0), 31u);
    EXPECT_EQ(PackedChunkVertex::unpackZ(w0), 31u);
    EXPECT_EQ(PackedChunkVertex::unpackNormal(w0), 7u);
    EXPECT_EQ(PackedChunkVertex::unpackAO(w0), 3u);
    EXPECT_EQ(PackedChunkVertex::unpackUVCorner(w0), 3u);
    EXPECT_EQ(PackedChunkVertex::unpackMatLayer(w0), 1023u);

    // Word 1 max values: Sky=15, Block=15, Tint=255, Anim=31, Sway=1, Trans=1, Res=511
    const uint32_t w1 = PackedChunkVertex::packWord1(15, 15, 255, 31, true, true, 511);
    EXPECT_EQ(w1, 0xFFFFFFFFu);
    EXPECT_EQ(PackedChunkVertex::unpackSkyLight(w1), 15u);
    EXPECT_EQ(PackedChunkVertex::unpackBlockLight(w1), 15u);
    EXPECT_EQ(PackedChunkVertex::unpackBiomeTint(w1), 255u);
    EXPECT_EQ(PackedChunkVertex::unpackAnimFrame(w1), 31u);
    EXPECT_TRUE(PackedChunkVertex::unpackSway(w1));
    EXPECT_TRUE(PackedChunkVertex::unpackTranslucent(w1));
    EXPECT_EQ(PackedChunkVertex::unpackReserved(w1), 511u);
}

TEST(AdversarialTier5Vertex, PlanarUVProjectionNegativeCoordinatesAndBounds) {
    // Planar UV projection must always lie in [0.0, 1.0) regardless of negative coords
    const glm::vec3 testCoords[] = {
        {-15.75f, 64.25f, -3.5f},
        {-0.001f, -0.999f, -100.33f},
        {0.0f, 0.0f, 0.0f},
        {16.0f, 128.0f, 16.0f},
        {1234.56f, 789.12f, -567.89f}
    };

    for (const auto& pos : testCoords) {
        for (uint32_t norm = 0; norm < 6; ++norm) {
            const glm::vec2 uv = PackedChunkVertex::computePlanarUV(pos, norm);
            EXPECT_GE(uv.x, 0.0f);
            EXPECT_LT(uv.x, 1.0f);
            EXPECT_GE(uv.y, 0.0f);
            EXPECT_LT(uv.y, 1.0f);
        }
    }
}

TEST(AdversarialTier5Vertex, SharedEboGeneration65536QuadsValidation) {
    constexpr size_t quads = 65536;
    const auto ebo = generateSharedEboPattern(quads);
    EXPECT_EQ(ebo.size(), quads * 6);
    EXPECT_TRUE(validateEboPattern(ebo, quads));

    // Assert that vertex indices exceed 16-bit uint limit (65535)
    // 65535 * 4 + 3 = 262143
    EXPECT_EQ(ebo[quads * 6 - 1], 262143u);
    EXPECT_GT(ebo[quads * 6 - 1], 65535u);
}
