#include "world/DifferentialLightingEngine.hpp"
#include "world/World.hpp"
#include "tests/test_helpers.hpp"
#include <gtest/gtest.h>

using namespace betacraft;
using namespace betacraft::world;
using namespace betacraft::voxel;

TEST(DifferentialLightingTest, AnalyticalManhattanShellFormula) {
    // S(0) = 1, S(r) = 4r^2 + 2 for r >= 1
    EXPECT_EQ(DifferentialLightingEngine::analyticalManhattanShell(0), 1u);
    EXPECT_EQ(DifferentialLightingEngine::analyticalManhattanShell(1), 6u);
    EXPECT_EQ(DifferentialLightingEngine::analyticalManhattanShell(2), 18u);
    EXPECT_EQ(DifferentialLightingEngine::analyticalManhattanShell(3), 38u);
    EXPECT_EQ(DifferentialLightingEngine::analyticalManhattanShell(15), 4 * 15 * 15 + 2); // 902
}

TEST(DifferentialLightingTest, AnalyticalManhattanVolumeSumFormula) {
    // N(R) = (2R+1)(2R^2 + 2R + 3) / 3
    for (size_t r = 0; r <= 15; ++r) {
        size_t iterativeSum = 1;
        for (size_t i = 1; i <= r; ++i) {
            iterativeSum += DifferentialLightingEngine::analyticalManhattanShell(i);
        }
        EXPECT_EQ(DifferentialLightingEngine::analyticalManhattanVolume(r), iterativeSum);
    }
}

TEST(DifferentialLightingTest, ExactManhattanVolumeBound4991) {
    EXPECT_EQ(DifferentialLightingEngine::analyticalManhattanVolume(15), 4991u);
}

TEST(DifferentialLightingTest, DiscreteLatticeBFSVolumeProof) {
    EXPECT_EQ(DifferentialLightingEngine::computeDiscreteManhattanVolumeBFS(0), 1u);
    EXPECT_EQ(DifferentialLightingEngine::computeDiscreteManhattanVolumeBFS(1), 7u);
    EXPECT_EQ(DifferentialLightingEngine::computeDiscreteManhattanVolumeBFS(2), 25u);
    EXPECT_EQ(DifferentialLightingEngine::computeDiscreteManhattanVolumeBFS(3), 63u);
    EXPECT_EQ(DifferentialLightingEngine::computeDiscreteManhattanVolumeBFS(15), 4991u);
}

TEST(DifferentialLightingTest, TorchPlacementAndRemovalInEnclosedChamber) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(173ULL, tempDir.path());
    world.ensureChunkLoaded({0, 0});

    // Build a hollow 7x7x7 cobblestone room centered at (8, 64, 8)
    for (int y = 60; y <= 68; ++y) {
        for (int z = 4; z <= 12; ++z) {
            for (int x = 4; x <= 12; ++x) {
                const bool isWall = (x == 4 || x == 12 || y == 60 || y == 68 || z == 4 || z == 12);
                world.setBlock(x, y, z, isWall ? BlockId::Cobblestone : BlockId::Air);
            }
        }
    }

    // Interior center should be pitch dark (0 light)
    EXPECT_EQ(world.getBlockLight(8, 64, 8), 0u);
    EXPECT_EQ(world.getSkyLight(8, 64, 8), 0u);

    // 1. Place torch in room center
    DifferentialLightingEngine engine(world);
    world.setBlock(8, 64, 8, BlockId::Torch);

    // Verify torch emits 14 and attenuates with Manhattan distance
    EXPECT_EQ(world.getBlockLight(8, 64, 8), 14u);
    EXPECT_EQ(world.getBlockLight(9, 64, 8), 13u);
    EXPECT_EQ(world.getBlockLight(10, 64, 8), 12u);
    EXPECT_EQ(world.getBlockLight(11, 64, 8), 11u);
    EXPECT_EQ(world.getBlockLight(8, 65, 8), 13u);
    EXPECT_EQ(world.getBlockLight(8, 66, 8), 12u);

    // Check inspection count is bounded well below 4991 in enclosed space
    EXPECT_LE(engine.getLastVoxelInspectionCount(), 4991u);

    // 2. Remove torch
    world.setBlock(8, 64, 8, BlockId::Air);

    // Entire interior should return to 0 block light
    EXPECT_EQ(world.getBlockLight(8, 64, 8), 0u);
    EXPECT_EQ(world.getBlockLight(9, 64, 8), 0u);
    EXPECT_EQ(world.getBlockLight(10, 64, 8), 0u);
    EXPECT_EQ(world.getBlockLight(11, 64, 8), 0u);
    EXPECT_LE(engine.getLastVoxelInspectionCount(), 4991u);
}

TEST(DifferentialLightingTest, DualTorchesIndependentReinjection) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(173ULL, tempDir.path());
    world.ensureChunkLoaded({0, 0});

    // Dark tunnel along X at Y=64, Z=8
    for (int y = 62; y <= 66; ++y) {
        for (int z = 6; z <= 10; ++z) {
            for (int x = 0; x < 16; ++x) {
                const bool isWall = (y == 62 || y == 66 || z == 6 || z == 10);
                world.setBlock(x, y, z, isWall ? BlockId::Cobblestone : BlockId::Air);
            }
        }
    }

    // Place two torches: Torch A at x=4, Torch B at x=10
    world.setBlock(4, 64, 8, BlockId::Torch);
    world.setBlock(10, 64, 8, BlockId::Torch);

    EXPECT_EQ(world.getBlockLight(4, 64, 8), 14u);
    EXPECT_EQ(world.getBlockLight(10, 64, 8), 14u);
    // Midpoint x=7 gets light from both (dist 3 from each -> 14 - 3 = 11)
    EXPECT_GE(world.getBlockLight(7, 64, 8), 11u);

    // Remove Torch A at x=4
    world.setBlock(4, 64, 8, BlockId::Air);

    // Torch A position should now be 0 or small remnant from Torch B (distance 6: 14 - 6 = 8)
    EXPECT_EQ(world.getBlockLight(4, 64, 8), 8u);
    EXPECT_EQ(world.getBlockLight(10, 64, 8), 14u); // Torch B completely unaffected
    EXPECT_EQ(world.getBlockLight(9, 64, 8), 13u);
}

TEST(DifferentialLightingTest, Skylight1DVerticalRaycastExtinction) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(173ULL, tempDir.path());
    world.ensureChunkLoaded({0, 0});

    // Clear column at (5, 5) from y=0 to 127
    for (int y = 0; y < 128; ++y) {
        world.setBlock(5, y, 5, BlockId::Air);
    }

    // Column should have full sunlight
    EXPECT_EQ(world.getSkyLight(5, 120, 5), 15u);
    EXPECT_EQ(world.getSkyLight(5, 50, 5), 15u);

    // Place opaque block at Y=100
    world.setBlock(5, 100, 5, BlockId::Stone);

    // Above stone, sunlight is still 15
    EXPECT_EQ(world.getSkyLight(5, 101, 5), 15u);
    // Directly below stone, sunlight is shaded/extinguished
    EXPECT_LT(world.getSkyLight(5, 99, 5), 15u);
    EXPECT_LT(world.getSkyLight(5, 50, 5), 15u);
}

TEST(DifferentialLightingTest, TorchAtEastBorderCrossing) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(12345ULL, tempDir.path());

    Chunk& c00 = world.ensureChunkLoaded({0, 0});
    Chunk& c10 = world.ensureChunkLoaded({1, 0});

    c00.setMeshDirty(false);
    c10.setMeshDirty(false);

    for (int x = 10; x <= 22; ++x) {
        for (int y = 60; y <= 68; ++y) {
            for (int z = 4; z <= 12; ++z) {
                world.setBlock(x, y, z, BlockId::Air);
            }
        }
    }

    c00.setMeshDirty(false);
    c10.setMeshDirty(false);

    // Place torch at East border x=15
    world.setBlock(15, 64, 8, BlockId::Torch);

    EXPECT_EQ(world.getBlockLight(15, 64, 8), 14u);
    EXPECT_EQ(world.getBlockLight(16, 64, 8), 13u);
    EXPECT_EQ(world.getBlockLight(17, 64, 8), 12u);
    EXPECT_EQ(world.getBlockLight(18, 64, 8), 11u);
    EXPECT_EQ(world.getBlockLight(20, 64, 8), 9u);

    EXPECT_TRUE(c00.isMeshDirty());
    EXPECT_TRUE(c10.isMeshDirty());

    c00.setMeshDirty(false);
    c10.setMeshDirty(false);
    world.setBlock(15, 64, 8, BlockId::Air);

    EXPECT_EQ(world.getBlockLight(15, 64, 8), 0u);
    EXPECT_EQ(world.getBlockLight(16, 64, 8), 0u);
    EXPECT_EQ(world.getBlockLight(17, 64, 8), 0u);
    EXPECT_TRUE(c00.isMeshDirty());
    EXPECT_TRUE(c10.isMeshDirty());
}

TEST(DifferentialLightingTest, TorchAtWestBorderCrossing) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(12345ULL, tempDir.path());

    Chunk& c00 = world.ensureChunkLoaded({0, 0});
    Chunk& cm10 = world.ensureChunkLoaded({-1, 0});

    for (int x = -6; x <= 6; ++x) {
        for (int y = 60; y <= 68; ++y) {
            for (int z = 4; z <= 12; ++z) {
                world.setBlock(x, y, z, BlockId::Air);
            }
        }
    }

    c00.setMeshDirty(false);
    cm10.setMeshDirty(false);

    // Place torch at West border x=0
    world.setBlock(0, 64, 8, BlockId::Torch);

    EXPECT_EQ(world.getBlockLight(0, 64, 8), 14u);
    EXPECT_EQ(world.getBlockLight(-1, 64, 8), 13u);
    EXPECT_EQ(world.getBlockLight(-2, 64, 8), 12u);
    EXPECT_EQ(world.getBlockLight(-5, 64, 8), 9u);

    EXPECT_TRUE(c00.isMeshDirty());
    EXPECT_TRUE(cm10.isMeshDirty());

    c00.setMeshDirty(false);
    cm10.setMeshDirty(false);
    world.setBlock(0, 64, 8, BlockId::Air);

    EXPECT_EQ(world.getBlockLight(0, 64, 8), 0u);
    EXPECT_EQ(world.getBlockLight(-1, 64, 8), 0u);
    EXPECT_TRUE(c00.isMeshDirty());
    EXPECT_TRUE(cm10.isMeshDirty());
}

TEST(DifferentialLightingTest, TorchAtNorthAndSouthBorderCrossing) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(12345ULL, tempDir.path());

    Chunk& c00 = world.ensureChunkLoaded({0, 0});
    Chunk& c01 = world.ensureChunkLoaded({0, 1});
    Chunk& c0m1 = world.ensureChunkLoaded({0, -1});

    for (int x = 4; x <= 12; ++x) {
        for (int y = 60; y <= 68; ++y) {
            for (int z = -6; z <= 22; ++z) {
                world.setBlock(x, y, z, BlockId::Air);
            }
        }
    }

    c00.setMeshDirty(false);
    c01.setMeshDirty(false);

    world.setBlock(8, 64, 15, BlockId::Torch);
    EXPECT_EQ(world.getBlockLight(8, 64, 15), 14u);
    EXPECT_EQ(world.getBlockLight(8, 64, 16), 13u);
    EXPECT_EQ(world.getBlockLight(8, 64, 17), 12u);
    EXPECT_TRUE(c00.isMeshDirty());
    EXPECT_TRUE(c01.isMeshDirty());

    world.setBlock(8, 64, 15, BlockId::Air);
    EXPECT_EQ(world.getBlockLight(8, 64, 16), 0u);

    c00.setMeshDirty(false);
    c0m1.setMeshDirty(false);

    world.setBlock(8, 64, 0, BlockId::Torch);
    EXPECT_EQ(world.getBlockLight(8, 64, 0), 14u);
    EXPECT_EQ(world.getBlockLight(8, 64, -1), 13u);
    EXPECT_EQ(world.getBlockLight(8, 64, -2), 12u);
    EXPECT_TRUE(c00.isMeshDirty());
    EXPECT_TRUE(c0m1.isMeshDirty());

    world.setBlock(8, 64, 0, BlockId::Air);
    EXPECT_EQ(world.getBlockLight(8, 64, -1), 0u);
}

TEST(DifferentialLightingTest, TorchAtCornerCrossingFourChunks) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(12345ULL, tempDir.path());

    Chunk& c00   = world.ensureChunkLoaded({0, 0});
    Chunk& cm10  = world.ensureChunkLoaded({-1, 0});
    Chunk& c0m1  = world.ensureChunkLoaded({0, -1});
    Chunk& cm1m1 = world.ensureChunkLoaded({-1, -1});

    for (int x = -8; x <= 8; ++x) {
        for (int y = 60; y <= 68; ++y) {
            for (int z = -8; z <= 8; ++z) {
                world.setBlock(x, y, z, BlockId::Air);
            }
        }
    }

    c00.setMeshDirty(false);
    cm10.setMeshDirty(false);
    c0m1.setMeshDirty(false);
    cm1m1.setMeshDirty(false);

    world.setBlock(0, 64, 0, BlockId::Torch);

    EXPECT_EQ(world.getBlockLight(0, 64, 0), 14u);
    EXPECT_EQ(world.getBlockLight(-1, 64, 0), 13u);
    EXPECT_EQ(world.getBlockLight(0, 64, -1), 13u);
    EXPECT_EQ(world.getBlockLight(-1, 64, -1), 12u);
    EXPECT_EQ(world.getBlockLight(-2, 64, -1), 11u);

    EXPECT_TRUE(c00.isMeshDirty());
    EXPECT_TRUE(cm10.isMeshDirty());
    EXPECT_TRUE(c0m1.isMeshDirty());
    EXPECT_TRUE(cm1m1.isMeshDirty());

    world.setBlock(0, 64, 0, BlockId::Air);
    EXPECT_EQ(world.getBlockLight(0, 64, 0), 0u);
    EXPECT_EQ(world.getBlockLight(-1, 64, 0), 0u);
    EXPECT_EQ(world.getBlockLight(0, 64, -1), 0u);
    EXPECT_EQ(world.getBlockLight(-1, 64, -1), 0u);
}

TEST(DifferentialLightingTest, AsyncNeighborChunkSynchronization) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(12345ULL, tempDir.path());

    Chunk& c00 = world.ensureChunkLoaded({0, 0});
    for (int y = 60; y <= 68; ++y) {
        for (int z = 4; z <= 12; ++z) {
            world.setBlock(15, y, z, BlockId::Air);
            world.setBlock(14, y, z, BlockId::Air);
        }
    }

    world.setBlock(15, 64, 8, BlockId::Torch);
    EXPECT_EQ(world.getBlockLight(15, 64, 8), 14u);

    Chunk& c10 = world.ensureChunkLoaded({1, 0});
    world.setBlock(16, 64, 8, BlockId::Air);
    world.syncBorderLighting(c00, c10);

    EXPECT_GE(world.getBlockLight(16, 64, 8), 13u);
    EXPECT_TRUE(c10.isMeshDirty());
}

TEST(DifferentialLightingTest, DualTorchesAcrossBorderReinjection) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(12345ULL, tempDir.path());

    world.ensureChunkLoaded({0, 0});
    world.ensureChunkLoaded({1, 0});

    for (int x = 10; x <= 22; ++x) {
        for (int y = 62; y <= 66; ++y) {
            for (int z = 6; z <= 10; ++z) {
                const bool isWall = (y == 62 || y == 66 || z == 6 || z == 10);
                world.setBlock(x, y, z, isWall ? BlockId::Cobblestone : BlockId::Air);
            }
        }
    }

    world.setBlock(14, 64, 8, BlockId::Torch);
    world.setBlock(18, 64, 8, BlockId::Torch);

    EXPECT_EQ(world.getBlockLight(14, 64, 8), 14u);
    EXPECT_EQ(world.getBlockLight(18, 64, 8), 14u);
    EXPECT_EQ(world.getBlockLight(15, 64, 8), 13u);
    EXPECT_EQ(world.getBlockLight(16, 64, 8), 12u);

    world.setBlock(14, 64, 8, BlockId::Air);

    EXPECT_EQ(world.getBlockLight(18, 64, 8), 14u);
    EXPECT_EQ(world.getBlockLight(16, 64, 8), 12u);
    EXPECT_EQ(world.getBlockLight(15, 64, 8), 11u);
    EXPECT_EQ(world.getBlockLight(14, 64, 8), 10u);
}

TEST(DifferentialLightingTest, SolidRoofRaycastExtinctionStress) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(12345ULL, tempDir.path());

    world.ensureChunkLoaded({0, 0});

    for (int y = 0; y < 128; ++y) {
        world.setBlock(7, y, 7, BlockId::Air);
    }

    EXPECT_EQ(world.getSkyLight(7, 110, 7), 15u);
    EXPECT_EQ(world.getSkyLight(7, 40, 7), 15u);

    for (int x = 6; x <= 8; ++x) {
        for (int z = 6; z <= 8; ++z) {
            world.setBlock(x, 90, z, BlockId::Stone);
        }
    }

    EXPECT_EQ(world.getSkyLight(7, 91, 7), 15u);
    const uint8_t skyUnderRoof = world.getSkyLight(7, 89, 7);
    EXPECT_LT(skyUnderRoof, 15u);
    EXPECT_LE(skyUnderRoof, 14u);

    for (int x = 6; x <= 8; ++x) {
        for (int z = 6; z <= 8; ++z) {
            world.setBlock(x, 90, z, BlockId::Air);
        }
    }

    EXPECT_EQ(world.getSkyLight(7, 89, 7), 15u);
    EXPECT_EQ(world.getSkyLight(7, 40, 7), 15u);
}

TEST(DifferentialLightingTest, CaveUnderSolidGroundExtinction) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(12345ULL, tempDir.path());

    world.ensureChunkLoaded({0, 0});

    for (int x = 0; x < 16; ++x) {
        for (int z = 0; z < 16; ++z) {
            for (int y = 50; y <= 70; ++y) {
                world.setBlock(x, y, z, BlockId::Stone);
            }
            for (int y = 30; y < 50; ++y) {
                world.setBlock(x, y, z, BlockId::Air);
            }
            for (int y = 71; y < 128; ++y) {
                world.setBlock(x, y, z, BlockId::Air);
            }
        }
    }

    EXPECT_EQ(world.getSkyLight(8, 40, 8), 0u);

    for (int y = 50; y <= 70; ++y) {
        world.setBlock(8, y, 8, BlockId::Air);
    }

    EXPECT_EQ(world.getSkyLight(8, 70, 8), 15u);
    EXPECT_EQ(world.getSkyLight(8, 50, 8), 15u);
    EXPECT_EQ(world.getSkyLight(8, 49, 8), 15u);
    EXPECT_EQ(world.getSkyLight(9, 49, 8), 14u);
    EXPECT_EQ(world.getSkyLight(10, 49, 8), 13u);

    world.setBlock(8, 70, 8, BlockId::Stone);

    EXPECT_EQ(world.getSkyLight(8, 71, 8), 15u);
    EXPECT_EQ(world.getSkyLight(8, 69, 8), 0u);
    EXPECT_EQ(world.getSkyLight(8, 50, 8), 0u);
    EXPECT_EQ(world.getSkyLight(8, 49, 8), 0u);
    EXPECT_EQ(world.getSkyLight(9, 49, 8), 0u);
}

TEST(DifferentialLightingTest, TreeCanopyExtinctionAndRestoration) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(12345ULL, tempDir.path());

    world.ensureChunkLoaded({0, 0});

    for (int x = 5; x <= 11; ++x) {
        for (int z = 5; z <= 11; ++z) {
            for (int y = 60; y <= 80; ++y) {
                world.setBlock(x, y, z, BlockId::Air);
            }
        }
    }

    for (int y = 64; y <= 68; ++y) {
        world.setBlock(8, y, 8, BlockId::OakLog);
    }

    EXPECT_EQ(world.getSkyLight(8, 69, 8), 15u);
    EXPECT_LT(world.getSkyLight(8, 63, 8), 15u);

    for (int x = 6; x <= 10; ++x) {
        for (int z = 6; z <= 10; ++z) {
            if (x == 8 && z == 8) continue;
            world.setBlock(x, 68, z, BlockId::Leaves);
        }
    }

    for (int x = 6; x <= 10; ++x) {
        for (int z = 6; z <= 10; ++z) {
            world.setBlock(x, 68, z, BlockId::Air);
        }
    }
    for (int y = 64; y <= 68; ++y) {
        world.setBlock(8, y, 8, BlockId::Air);
    }

    EXPECT_EQ(world.getSkyLight(8, 63, 8), 15u);
    EXPECT_EQ(world.getSkyLight(8, 65, 8), 15u);
    EXPECT_EQ(world.getSkyLight(7, 68, 7), 15u);
}

TEST(DifferentialLightingTest, MultiChunkWideRoofPitchBlackShadow) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(12345ULL, tempDir.path());

    for (int cx = -1; cx <= 1; ++cx) {
        for (int cz = -1; cz <= 1; ++cz) {
            world.ensureChunkLoaded({cx, cz});
        }
    }

    for (int x = -18; x <= 18; ++x) {
        for (int z = -18; z <= 18; ++z) {
            for (int y = 60; y <= 80; ++y) {
                world.setBlock(x, y, z, BlockId::Air);
            }
        }
    }

    for (int x = -16; x <= 16; ++x) {
        for (int z = -16; z <= 16; ++z) {
            world.setBlock(x, 75, z, BlockId::Stone);
        }
    }

    EXPECT_EQ(world.getSkyLight(0, 76, 0), 15u);
    EXPECT_EQ(world.getSkyLight(10, 76, 10), 15u);

    EXPECT_EQ(world.getSkyLight(0, 74, 0), 0u);
    EXPECT_EQ(world.getSkyLight(0, 65, 0), 0u);

    const uint8_t skyNearEdge = world.getSkyLight(15, 74, 0);
    EXPECT_GT(skyNearEdge, 0u);
    EXPECT_LE(skyNearEdge, 14u);
}
