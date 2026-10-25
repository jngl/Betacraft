// Adversarial checks on light propagation: the analytical Manhattan bound (N(15) = 4,991 voxels)
// on every placement and removal, torches toggled 1,000 times, overlapping sources removed one by
// one, light across chunk corners, partially loaded neighbourhoods and the world limits, skylight
// under a single block, and block light across a section boundary inside one Chunk.
#include "test_support/TempDir.hpp"
#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"
#include "world/DifferentialLightingEngine.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>

using namespace betacraft;
using namespace betacraft::world;
using namespace betacraft::voxel;

// ============================================================================
// SUITE 1: DifferentialLightingEngine Worst-Case & Stress
// ============================================================================

TEST(AdversarialDifferentialLighting, AnalyticalVolumeFormulasExhaustive) {
    for (size_t r = 0; r <= 20; ++r) {
        size_t shellSum = 1;
        for (size_t i = 1; i <= r; ++i) {
            shellSum += DifferentialLightingEngine::analyticalManhattanShell(i);
        }
        EXPECT_EQ(DifferentialLightingEngine::analyticalManhattanVolume(r), shellSum);
    }

    // Exact theoretical values
    EXPECT_EQ(DifferentialLightingEngine::analyticalManhattanVolume(14), 4089u);
    EXPECT_EQ(DifferentialLightingEngine::analyticalManhattanVolume(15), 4991u);

    // Discrete 3D lattice BFS verification
    for (int r = 0; r <= 15; ++r) {
        const size_t discrete = DifferentialLightingEngine::computeDiscreteManhattanVolumeBFS(r);
        const size_t analytical = DifferentialLightingEngine::analyticalManhattanVolume(r);
        EXPECT_EQ(discrete, analytical) << "Failed at radius " << r;
    }
}

TEST(AdversarialDifferentialLighting, OpenAirTorchPlacementVisitBudgetUnder4991) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(42ULL, tempDir.path());

    // Load 3x3 chunk area (from (1,1) to (3,3)) to provide wide open air
    for (int cz = 1; cz <= 3; ++cz) {
        for (int cx = 1; cx <= 3; ++cx) {
            world.ensureChunkLoaded({cx, cz});
        }
    }

    // Clear open sphere of radius 16 around center (32, 64, 32)
    const glm::ivec3 center(32, 64, 32);
    for (int dy = -16; dy <= 16; ++dy) {
        for (int dz = -16; dz <= 16; ++dz) {
            for (int dx = -16; dx <= 16; ++dx) {
                world.setBlock(center.x + dx, center.y + dy, center.z + dz, BlockId::Air);
                world.setBlockLight(center.x + dx, center.y + dy, center.z + dz, 0);
            }
        }
    }

    EXPECT_EQ(world.getBlockLight(center.x, center.y, center.z), 0u);

    // Place Torch (emission 14)
    world.setBlock(center.x, center.y, center.z, BlockId::Torch);

    // Verify visit count is bounded by analytical volume N(15) = 4991 (and <= 4089 for radius 14)
    const size_t placementVisits = world.getLightingEngine().getLastVoxelInspectionCount();
    EXPECT_LE(placementVisits, 4991u);
    EXPECT_LE(placementVisits, 4089u);
    EXPECT_GT(placementVisits, 0u);

    // Verify Manhattan light attenuation along all 6 cardinal axes
    EXPECT_EQ(world.getBlockLight(center.x, center.y, center.z), 14u);
    for (int d = 1; d <= 13; ++d) {
        const uint8_t expected = static_cast<uint8_t>(14 - d);
        EXPECT_EQ(world.getBlockLight(center.x + d, center.y, center.z), expected);
        EXPECT_EQ(world.getBlockLight(center.x - d, center.y, center.z), expected);
        EXPECT_EQ(world.getBlockLight(center.x, center.y + d, center.z), expected);
        EXPECT_EQ(world.getBlockLight(center.x, center.y - d, center.z), expected);
        EXPECT_EQ(world.getBlockLight(center.x, center.y, center.z + d), expected);
        EXPECT_EQ(world.getBlockLight(center.x, center.y, center.z - d), expected);
    }
    // At distance 14, light should be 0
    EXPECT_EQ(world.getBlockLight(center.x + 14, center.y, center.z), 0u);
    EXPECT_EQ(world.getBlockLight(center.x, center.y + 14, center.z), 0u);
    EXPECT_EQ(world.getBlockLight(center.x, center.y, center.z + 14), 0u);
}

TEST(AdversarialDifferentialLighting, OpenAirTorchRemovalVisitBudgetUnder4991) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(42ULL, tempDir.path());

    for (int cz = 1; cz <= 3; ++cz) {
        for (int cx = 1; cx <= 3; ++cx) {
            world.ensureChunkLoaded({cx, cz});
        }
    }

    const glm::ivec3 center(32, 64, 32);
    for (int dy = -16; dy <= 16; ++dy) {
        for (int dz = -16; dz <= 16; ++dz) {
            for (int dx = -16; dx <= 16; ++dx) {
                world.setBlock(center.x + dx, center.y + dy, center.z + dz, BlockId::Air);
                world.setBlockLight(center.x + dx, center.y + dy, center.z + dz, 0);
            }
        }
    }

    world.setBlock(center.x, center.y, center.z, BlockId::Torch);
    EXPECT_EQ(world.getBlockLight(center.x, center.y, center.z), 14u);

    // Remove Torch
    world.setBlock(center.x, center.y, center.z, BlockId::Air);

    // Verify removal visit count strictly bounded by 4991
    const size_t removalVisits = world.getLightingEngine().getLastVoxelInspectionCount();
    EXPECT_LE(removalVisits, 4991u);
    EXPECT_LE(removalVisits, 4089u);
    EXPECT_GT(removalVisits, 0u);

    // Verify complete extinction everywhere
    for (int d = 0; d <= 14; ++d) {
        EXPECT_EQ(world.getBlockLight(center.x + d, center.y, center.z), 0u);
        EXPECT_EQ(world.getBlockLight(center.x - d, center.y, center.z), 0u);
        EXPECT_EQ(world.getBlockLight(center.x, center.y + d, center.z), 0u);
        EXPECT_EQ(world.getBlockLight(center.x, center.y - d, center.z), 0u);
        EXPECT_EQ(world.getBlockLight(center.x, center.y, center.z + d), 0u);
        EXPECT_EQ(world.getBlockLight(center.x, center.y, center.z - d), 0u);
    }
}

TEST(AdversarialDifferentialLighting, OpenAirMaxEmission15WorstCaseExactBound) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(42ULL, tempDir.path());

    for (int cz = 1; cz <= 3; ++cz) {
        for (int cx = 1; cx <= 3; ++cx) {
            world.ensureChunkLoaded({cx, cz});
        }
    }

    const glm::ivec3 center(32, 64, 32);
    for (int dy = -16; dy <= 16; ++dy) {
        for (int dz = -16; dz <= 16; ++dz) {
            for (int dx = -16; dx <= 16; ++dx) {
                world.setBlock(center.x + dx, center.y + dy, center.z + dz, BlockId::Air);
                world.setBlockLight(center.x + dx, center.y + dy, center.z + dz, 0);
            }
        }
    }

    // Set emission 15 (theoretical max light level)
    world.getLightingEngine().updateLightSource(center.x, center.y, center.z, 15);
    const size_t addVisits = world.getLightingEngine().getLastVoxelInspectionCount();
    // In voxel engine, light level 1 propagates to distance 14 (light 0 is darkness, not propagated).
    // Radius 14 visits exactly N(14) = 4,089 voxels, which strictly satisfies <= 4,991.
    EXPECT_LE(addVisits, 4991u);
    EXPECT_EQ(addVisits, 4089u);

    // Reset emission 0
    world.getLightingEngine().updateLightSource(center.x, center.y, center.z, 0);
    const size_t remVisits = world.getLightingEngine().getLastVoxelInspectionCount();
    EXPECT_LE(remVisits, 4991u);
    EXPECT_EQ(remVisits, 4089u);

    EXPECT_EQ(world.getBlockLight(center.x, center.y, center.z), 0u);
}

TEST(AdversarialDifferentialLighting, DualTorchesIndependentReinjectionStress) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(42ULL, tempDir.path());

    for (int cz = 1; cz <= 3; ++cz) {
        for (int cx = 1; cx <= 3; ++cx) {
            world.ensureChunkLoaded({cx, cz});
        }
    }

    // Clear open space
    for (int dy = -16; dy <= 16; ++dy) {
        for (int dz = -16; dz <= 16; ++dz) {
            for (int dx = 0; dx <= 64; ++dx) {
                world.setBlock(dx, 64 + dy, 32 + dz, BlockId::Air);
                world.setBlockLight(dx, 64 + dy, 32 + dz, 0);
            }
        }
    }

    // Place Torch A at (24, 64, 32) and Torch B at (40, 64, 32) (dist = 16)
    world.setBlock(24, 64, 32, BlockId::Torch);
    world.setBlock(40, 64, 32, BlockId::Torch);

    EXPECT_EQ(world.getBlockLight(24, 64, 32), 14u);
    EXPECT_EQ(world.getBlockLight(40, 64, 32), 14u);

    // Midpoint (32, 64, 32) is dist 8 from both: 14 - 8 = 6
    EXPECT_EQ(world.getBlockLight(32, 64, 32), 6u);

    // Remove Torch A
    world.setBlock(24, 64, 32, BlockId::Air);

    // Torch B must remain unaffected at 14
    EXPECT_EQ(world.getBlockLight(40, 64, 32), 14u);

    // Midpoint still receives 6 from Torch B
    EXPECT_EQ(world.getBlockLight(32, 64, 32), 6u);

    // At x=27 (dist 13 from Torch B): 14 - 13 = 1
    EXPECT_EQ(world.getBlockLight(27, 64, 32), 1u);

    // At x=26 (dist 14 from Torch B): 0
    EXPECT_EQ(world.getBlockLight(26, 64, 32), 0u);

    // At x=24 (where Torch A was): 0
    EXPECT_EQ(world.getBlockLight(24, 64, 32), 0u);

    // Now remove Torch B: entire world returns to 0
    world.setBlock(40, 64, 32, BlockId::Air);
    EXPECT_EQ(world.getBlockLight(40, 64, 32), 0u);
    EXPECT_EQ(world.getBlockLight(32, 64, 32), 0u);
}

TEST(AdversarialDifferentialLighting, AdjacentTorchesReinjectionStress) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(42ULL, tempDir.path());

    for (int cz = 1; cz <= 3; ++cz) {
        for (int cx = 1; cx <= 3; ++cx) {
            world.ensureChunkLoaded({cx, cz});
        }
    }

    for (int dy = -16; dy <= 16; ++dy) {
        for (int dz = -16; dz <= 16; ++dz) {
            for (int dx = 0; dx <= 64; ++dx) {
                world.setBlock(dx, 64 + dy, 32 + dz, BlockId::Air);
                world.setBlockLight(dx, 64 + dy, 32 + dz, 0);
            }
        }
    }

    // Place torches adjacent at dist 4: (30, 64, 32) and (34, 64, 32)
    world.setBlock(30, 64, 32, BlockId::Torch);
    world.setBlock(34, 64, 32, BlockId::Torch);

    EXPECT_EQ(world.getBlockLight(30, 64, 32), 14u);
    EXPECT_EQ(world.getBlockLight(34, 64, 32), 14u);
    // Midpoint (32, 64, 32) dist 2 from both: 14 - 2 = 12
    EXPECT_EQ(world.getBlockLight(32, 64, 32), 12u);

    // Remove Torch A at (30, 64, 32)
    world.setBlock(30, 64, 32, BlockId::Air);

    // (30, 64, 32) is dist 4 from Torch B: 14 - 4 = 10
    EXPECT_EQ(world.getBlockLight(30, 64, 32), 10u);
    EXPECT_EQ(world.getBlockLight(34, 64, 32), 14u);

    // Remove Torch B
    world.setBlock(34, 64, 32, BlockId::Air);
    EXPECT_EQ(world.getBlockLight(30, 64, 32), 0u);
    EXPECT_EQ(world.getBlockLight(34, 64, 32), 0u);
}

TEST(AdversarialDifferentialLighting, RapidTorchTogglingStress1000Cycles) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(42ULL, tempDir.path());

    for (int cz = 1; cz <= 3; ++cz) {
        for (int cx = 1; cx <= 3; ++cx) {
            world.ensureChunkLoaded({cx, cz});
        }
    }

    const glm::ivec3 pos(32, 64, 32);
    for (int dy = -16; dy <= 16; ++dy) {
        for (int dz = -16; dz <= 16; ++dz) {
            for (int dx = -16; dx <= 16; ++dx) {
                world.setBlock(pos.x + dx, pos.y + dy, pos.z + dz, BlockId::Air);
                world.setBlockLight(pos.x + dx, pos.y + dy, pos.z + dz, 0);
            }
        }
    }

    // Toggle torch 1,000 times (500 on, 500 off)
    for (int cycle = 0; cycle < 500; ++cycle) {
        // Place torch
        world.setBlock(pos.x, pos.y, pos.z, BlockId::Torch);
        const size_t onVisits = world.getLightingEngine().getLastVoxelInspectionCount();
        ASSERT_LE(onVisits, 4991u);
        ASSERT_EQ(world.getBlockLight(pos.x, pos.y, pos.z), 14u);

        // Remove torch
        world.setBlock(pos.x, pos.y, pos.z, BlockId::Air);
        const size_t offVisits = world.getLightingEngine().getLastVoxelInspectionCount();
        ASSERT_LE(offVisits, 4991u);
        ASSERT_EQ(world.getBlockLight(pos.x, pos.y, pos.z), 0u);
    }

    // Verify completely clean state
    EXPECT_EQ(world.getBlockLight(pos.x, pos.y, pos.z), 0u);
    EXPECT_EQ(world.getBlockLight(pos.x + 1, pos.y, pos.z), 0u);
}

TEST(AdversarialDifferentialLighting, ChunkBoundaryCrossBorderLighting) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(42ULL, tempDir.path());

    // Load chunks (0,0), (1,0), (0,1), (1,1)
    world.ensureChunkLoaded({0, 0});
    world.ensureChunkLoaded({1, 0});
    world.ensureChunkLoaded({0, 1});
    world.ensureChunkLoaded({1, 1});

    // Clear boundary region around (15, 64, 15)
    for (int dy = -16; dy <= 16; ++dy) {
        for (int z = 0; z < 32; ++z) {
            for (int x = 0; x < 32; ++x) {
                world.setBlock(x, 64 + dy, z, BlockId::Air);
                world.setBlockLight(x, 64 + dy, z, 0);
            }
        }
    }

    // Place torch at chunk border: (15, 64, 15)
    world.setBlock(15, 64, 15, BlockId::Torch);
    EXPECT_LE(world.getLightingEngine().getLastVoxelInspectionCount(), 4991u);

    // Check light in chunk (0,0)
    EXPECT_EQ(world.getBlockLight(15, 64, 15), 14u);
    // Check light crossed into chunk (1,0) at x=16
    EXPECT_EQ(world.getBlockLight(16, 64, 15), 13u);
    // Check light crossed into chunk (0,1) at z=16
    EXPECT_EQ(world.getBlockLight(15, 64, 16), 13u);
    // Check light crossed into chunk (1,1) at (16, 64, 16)
    EXPECT_EQ(world.getBlockLight(16, 64, 16), 12u);

    // Remove torch: clean removal across borders
    world.setBlock(15, 64, 15, BlockId::Air);
    EXPECT_LE(world.getLightingEngine().getLastVoxelInspectionCount(), 4991u);
    EXPECT_EQ(world.getBlockLight(15, 64, 15), 0u);
    EXPECT_EQ(world.getBlockLight(16, 64, 15), 0u);
    EXPECT_EQ(world.getBlockLight(15, 64, 16), 0u);
    EXPECT_EQ(world.getBlockLight(16, 64, 16), 0u);
}

TEST(AdversarialDifferentialLighting, NegativeCoordinatesAndVerticalWorldLimits) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(173ULL, tempDir.path());
    world.ensureChunkLoaded({-1, -1});
    world.ensureChunkLoaded({0, 0});
    // Open air around each torch, so the neighbours checked below are not generated stone.
    const auto clearAround = [&world](int cx, int cy, int cz) {
        for (int y = std::max(cy - 2, 0); y <= std::min(cy + 2, 127); ++y) {
            for (int z = cz - 2; z <= cz + 2; ++z) {
                for (int x = cx - 2; x <= cx + 2; ++x) {
                    world.setBlock(x, y, z, BlockId::Air);
                }
            }
        }
    };
    clearAround(-5, 64, -5);
    clearAround(5, 0, 5);
    clearAround(5, 127, 5);

    // Negative chunk coordinates
    world.setBlock(-5, 64, -5, BlockId::Torch);
    EXPECT_EQ(world.getBlockLight(-5, 64, -5), 14u);
    EXPECT_EQ(world.getBlockLight(-4, 64, -5), 13u);
    world.setBlock(-5, 64, -5, BlockId::Air);
    EXPECT_EQ(world.getBlockLight(-5, 64, -5), 0u);

    // Bottom of the world: Y = 0
    world.setBlock(5, 0, 5, BlockId::Torch);
    EXPECT_EQ(world.getBlockLight(5, 0, 5), 14u);
    EXPECT_EQ(world.getBlockLight(5, 1, 5), 13u);
    world.setBlock(5, 0, 5, BlockId::Air);
    EXPECT_EQ(world.getBlockLight(5, 0, 5), 0u);

    // Top of the world: Y = 127
    world.setBlock(5, 127, 5, BlockId::Torch);
    EXPECT_EQ(world.getBlockLight(5, 127, 5), 14u);
    EXPECT_EQ(world.getBlockLight(5, 126, 5), 13u);
    world.setBlock(5, 127, 5, BlockId::Air);
    EXPECT_EQ(world.getBlockLight(5, 127, 5), 0u);
}

// ============================================================================
// SUITE 2: Complex Multi-Chunk Border Light Propagation & Analytical Bounds
// ============================================================================

class AdversarialMultiChunkLighting : public ::testing::Test {
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

TEST_F(AdversarialMultiChunkLighting, CornerFourChunkLightPropagationAndExtinction) {
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

TEST_F(AdversarialMultiChunkLighting, PartialChunkBoundaryIsolationNoCrash) {
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

TEST_F(AdversarialMultiChunkLighting, MultiSourceOverlappingChunkBorderReinjection) {
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

TEST_F(AdversarialMultiChunkLighting, SkylightVerticalObstructionAndBoundaryExtinction) {
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

TEST_F(AdversarialMultiChunkLighting, AnalyticalManhattanBoundStrictnessAcrossAllEmissions) {
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
// SUITE 3: Light Across Section Boundaries Inside One Chunk
// ============================================================================

TEST(AdversarialChunkLighting, SunlightPenetrationThroughSubChunks) {
    Chunk chunk(core::ChunkCoord{0, 0});

    // Place an opaque roof in Section 4 (Y = 64) covering entire (X, Z) plane
    for (int32_t z = 0; z < 16; ++z) {
        for (int32_t x = 0; x < 16; ++x) {
            chunk.setBlock(x, 64, z, BlockId::Stone);
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

TEST(AdversarialChunkLighting, BlockLightCrossesSectionBoundaryY15ToY16) {
    Chunk chunk(core::ChunkCoord{0, 0});

    // Clear all lighting to 0
    for (size_t sec = 0; sec < 8; ++sec) {
        chunk.getSection(sec).setUniformLighting(0, 0);
    }

    // Place torch at Y = 15 (top-most layer of Section 0)
    chunk.setBlock(8, 15, 8, BlockId::Torch);
    chunk.spreadBlockLight(8, 15, 8, 14);

    EXPECT_EQ(chunk.getBlockLight(8, 15, 8), 14);

    // Light must cross into Section 1 (Y = 16, local Y = 0) with attenuation -1
    EXPECT_EQ(chunk.getBlockLight(8, 16, 8), 13);
    EXPECT_EQ(chunk.getSection(1).getBlockLight(8, 0, 8), 13);

    // Light must propagate further into Section 1 (Y = 17, local Y = 1) with attenuation -2
    EXPECT_EQ(chunk.getBlockLight(8, 17, 8), 12);
    EXPECT_EQ(chunk.getSection(1).getBlockLight(8, 1, 8), 12);
}
