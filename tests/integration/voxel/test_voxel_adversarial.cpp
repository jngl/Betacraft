// Adversarial checks on voxel storage: PalettedSection promotion 0 -> 4 -> 8 -> 16 bits at the
// exact palette limits, nibble crosstalk, 50,000 random writes against a shadow oracle, extreme
// block state ids, out-of-range coordinates, lazily allocated light arrays, and Chunk sections
// across their Y boundaries and through RLE serialisation (round trips and malformed buffers).
#include "core/Types.hpp"
#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"
#include "voxel/PalettedSection.hpp"

#include <gtest/gtest.h>

#include <array>
#include <random>
#include <span>
#include <vector>

using namespace betacraft;
using namespace betacraft::voxel;

// ============================================================================
// SUITE 1: PalettedSection Palette Transitions & Limits
// ============================================================================

TEST(AdversarialPalettedSection, MemoryInvariants) {
    ASSERT_LE(sizeof(PalettedSection), 64) << "sizeof(PalettedSection) must be <= 64 bytes";
    ASSERT_EQ(sizeof(PalettedSection), 64) << "sizeof(PalettedSection) must be exactly 64 bytes";
    ASSERT_EQ(alignof(PalettedSection), 64) << "alignof(PalettedSection) must be 64 bytes";

    PalettedSection secAir(BlockId::Air, 15, 0);
    ASSERT_TRUE(secAir.isHomogeneous()) << "Air section must be homogeneous";
    ASSERT_FALSE(secAir.hasLightData()) << "Air section lightData must be nullptr";
    ASSERT_EQ(secAir.getBitsPerVoxel(), 0) << "Air section must have 0 bits per voxel";
    ASSERT_EQ(secAir.getMemoryFootprintBytes(), 64) << "Air section memory footprint must be 64 bytes";

    PalettedSection secStone(BlockId::Stone, 0, 0);
    ASSERT_TRUE(secStone.isHomogeneous()) << "Stone section must be homogeneous";
    ASSERT_FALSE(secStone.hasLightData()) << "Stone section lightData must be nullptr";
    ASSERT_EQ(secStone.getBitsPerVoxel(), 0) << "Stone section must have 0 bits per voxel";
    ASSERT_EQ(secStone.getMemoryFootprintBytes(), 64) << "Stone section memory footprint must be 64 bytes";
}

TEST(AdversarialPalettedSection, PaletteTransitionsAndLimits) {
    PalettedSection sec;
    ASSERT_EQ(sec.getBitsPerVoxel(), 0) << "Initial section must have 0 bits per voxel";
    ASSERT_TRUE(sec.isHomogeneous()) << "Initial section must be homogeneous";

    // Tier 1: 0 -> 4 bits upon inserting 2nd block
    sec.setBlock(0, 0, 0, BlockId::Stone);
    ASSERT_EQ(sec.getBitsPerVoxel(), 4) << "Must promote to 4 bits per voxel";
    ASSERT_FALSE(sec.isHomogeneous()) << "Must no longer be homogeneous";
    ASSERT_EQ(sec.getBlock(0, 0, 0), BlockId::Stone) << "Voxel (0,0,0) must be Stone";
    ASSERT_EQ(sec.getBlock(1, 0, 0), BlockId::Air) << "Voxel (1,0,0) must remain Air";
    ASSERT_EQ(sec.getPaletteSize(), 2) << "Palette size must be 2";

    // Fill up to exactly 16 unique blocks (indices 0..15 in palette)
    for (uint16_t id = 2; id < 16; ++id) {
        sec.setBlock(id, 0, 0, static_cast<BlockStateId>(100 + id));
    }
    ASSERT_EQ(sec.getBitsPerVoxel(), 4) << "Must still be 4 bits at 16 unique blocks";
    ASSERT_EQ(sec.getPaletteSize(), 16) << "Palette size must be exactly 16";

    // Verify all 16 blocks are stored correctly in 4-bit nibbles
    ASSERT_EQ(sec.getBlock(0, 0, 0), BlockId::Stone) << "Block 0 must be Stone";
    for (uint16_t id = 2; id < 16; ++id) {
        ASSERT_EQ(sec.getBlockState(id, 0, 0), static_cast<BlockStateId>(100 + id)) << "Block must match inserted state";
    }

    // Overwrite existing voxel with an ALREADY EXISTING block in palette:
    // Palette must NOT grow!
    sec.setBlock(0, 1, 0, BlockId::Stone);
    ASSERT_EQ(sec.getBitsPerVoxel(), 4) << "Must stay 4 bits when placing existing block";
    ASSERT_EQ(sec.getPaletteSize(), 16) << "Palette size must remain 16";

    // Tier 2: 4 -> 8 bits upon inserting 17th unique block
    sec.setBlock(0, 2, 0, static_cast<BlockStateId>(999));
    ASSERT_EQ(sec.getBitsPerVoxel(), 8) << "Must promote to 8 bits on 17th block";
    ASSERT_EQ(sec.getPaletteSize(), 17) << "Palette size must be 17";
    ASSERT_EQ(sec.getBlockState(0, 2, 0), 999) << "17th block must be 999";
    ASSERT_EQ(sec.getBlock(0, 0, 0), BlockId::Stone) << "Stone block must still be present";

    // Fill up to exactly 256 unique blocks
    // Currently 17 blocks. Add 239 more:
    for (uint16_t id = 17; id < 256; ++id) {
        int lx = id % 16;
        int ly = (id / 16);
        int lz = 1;
        sec.setBlock(lx, ly, lz, static_cast<BlockStateId>(2000 + id));
    }
    ASSERT_EQ(sec.getBitsPerVoxel(), 8) << "Must still be 8 bits at 256 unique blocks";
    ASSERT_EQ(sec.getPaletteSize(), 256) << "Palette size must be exactly 256";

    // Tier 3: 8 -> 16 bits direct upon inserting 257th unique block
    sec.setBlock(15, 15, 15, static_cast<BlockStateId>(5000));
    ASSERT_EQ(sec.getBitsPerVoxel(), 16) << "Must promote to 16 bits on 257th block";
    // In 16-bit direct mode, palette is freed
    ASSERT_EQ(sec.getPaletteSize(), 0) << "Palette must be freed in 16-bit direct mode";
    ASSERT_EQ(sec.getBlockState(15, 15, 15), 5000) << "257th block must be 5000";
    ASSERT_EQ(sec.getBlock(0, 0, 0), BlockId::Stone) << "Stone block must remain intact";

    // Verify all 256 previously placed blocks are intact in 16-bit direct mode
    for (uint16_t id = 17; id < 256; ++id) {
        int lx = id % 16;
        int ly = (id / 16);
        int lz = 1;
        ASSERT_EQ(sec.getBlockState(lx, ly, lz), static_cast<BlockStateId>(2000 + id)) << "Direct mode block corrupted";
    }
}

TEST(AdversarialPalettedSection, NibblePackingPairwiseStress) {
    // Test all 2048 pairs of nibbles in 4-bit mode for bit interference
    PalettedSection sec;
    // Insert 15 non-zero palette entries
    for (uint16_t i = 1; i <= 15; ++i) {
        sec.setBlock(i, 0, 0, static_cast<BlockStateId>(i));
    }
    ASSERT_EQ(sec.getBitsPerVoxel(), 4) << "Must be 4-bit mode";

    // Create a deterministic pattern across all 4096 voxels
    // Even voxels get pattern (i * 3) % 16, Odd voxels get pattern (i * 7 + 1) % 16
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                size_t idx = (y * 16 + z) * 16 + x;
                uint16_t val = (idx & 1) ? static_cast<uint16_t>((idx * 7 + 1) % 16)
                                         : static_cast<uint16_t>((idx * 3) % 16);
                sec.setBlock(x, y, z, val);
            }
        }
    }

    // Verify all 4096 voxels retain exact values
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                size_t idx = (y * 16 + z) * 16 + x;
                uint16_t expected = (idx & 1) ? static_cast<uint16_t>((idx * 7 + 1) % 16)
                                              : static_cast<uint16_t>((idx * 3) % 16);
                ASSERT_EQ(sec.getBlockState(x, y, z), expected) << "Nibble crosstalk detected";
            }
        }
    }

    // Now mutate only the odd nibbles and verify even nibbles are untouched
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                size_t idx = (y * 16 + z) * 16 + x;
                if (idx & 1) {
                    sec.setBlock(x, y, z, static_cast<BlockStateId>(15));
                }
            }
        }
    }

    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                size_t idx = (y * 16 + z) * 16 + x;
                if (idx & 1) {
                    ASSERT_EQ(sec.getBlockState(x, y, z), 15) << "Mutated odd nibble mismatch";
                } else {
                    uint16_t expectedEven = static_cast<uint16_t>((idx * 3) % 16);
                    ASSERT_EQ(sec.getBlockState(x, y, z), expectedEven) << "Even nibble corrupted by odd write";
                }
            }
        }
    }
}

TEST(AdversarialPalettedSection, MassiveRandomizedBlockMutations) {
    // 50,000 random writes across hundreds of unique BlockIds with shadow oracle
    PalettedSection sec;
    std::array<BlockStateId, 4096> shadowOracle;
    shadowOracle.fill(static_cast<BlockStateId>(BlockId::Air));

    std::mt19937 rng(1337);
    std::uniform_int_distribution<int> coordDist(0, 15);
    std::uniform_int_distribution<uint16_t> blockDist(0, 450); // 451 unique block states

    constexpr int NUM_MUTATIONS = 50000;
    for (int i = 0; i < NUM_MUTATIONS; ++i) {
        int x = coordDist(rng);
        int y = coordDist(rng);
        int z = coordDist(rng);
        BlockStateId block = blockDist(rng);

        sec.setBlock(x, y, z, block);
        shadowOracle[(y * 16 + z) * 16 + x] = block;

        // Verify sampled voxels periodically
        if (i % 10000 == 0) {
            for (int sample = 0; sample < 100; ++sample) {
                int sx = coordDist(rng);
                int sy = coordDist(rng);
                int sz = coordDist(rng);
                size_t idx = (sy * 16 + sz) * 16 + sx;
                ASSERT_EQ(sec.getBlockState(sx, sy, sz), shadowOracle[idx]) << "Shadow oracle mismatch during massive mutations";
            }
        }
    }

    // Full section verification
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                size_t idx = (y * 16 + z) * 16 + x;
                ASSERT_EQ(sec.getBlockState(x, y, z), shadowOracle[idx]) << "Final oracle verification failed";
            }
        }
    }
    ASSERT_EQ(sec.getBitsPerVoxel(), 16) << "Section with 450 states must be 16-bit direct mode";
}

TEST(AdversarialPalettedSection, PaletteTransitionAndBackBehavior) {
    // Investigate: what happens when section promoted to 16-bit is rewritten with all Air?
    PalettedSection sec;
    // Force to 16-bit
    for (uint16_t id = 1; id <= 260; ++id) {
        int x = id % 16;
        int y = (id / 16) % 16;
        int z = (id / 256);
        sec.setBlock(x, y, z, static_cast<BlockStateId>(id));
    }
    ASSERT_EQ(sec.getBitsPerVoxel(), 16) << "Must be 16-bit mode";

    // Overwrite ALL 4096 voxels with Air
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                sec.setBlock(x, y, z, BlockId::Air);
            }
        }
    }

    // Correctness check: all voxels MUST be Air
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                ASSERT_EQ(sec.getBlock(x, y, z), BlockId::Air) << "All voxels must be Air";
            }
        }
    }

    // Note architectural invariant: does PalettedSection demote back to 0-bit?
    // In Sprint 1.1 specification, demotion/compaction is NOT part of the runtime setBlock path
    // because full scan on every setBlock would destroy O(1) write performance.
    // BitsPerVoxel remains 16, but correctness is 100% preserved.
    ASSERT_EQ(sec.getBitsPerVoxel(), 16) << "setBlock maintains 16-bit for O(1) performance";
    ASSERT_FALSE(sec.isHomogeneous()) << "isHomogeneous() is false when bitsPerVoxel != 0";
}

TEST(AdversarialPalettedSection, OutOfBoundsCoordinates) {
    PalettedSection sec;
    const int badCoords[][3] = {
        {-1, 0, 0}, {16, 0, 0}, {0, -1, 0}, {0, 16, 0}, {0, 0, -1}, {0, 0, 16},
        {-100, 50, 20}, {1000, 1000, 1000}, {-999, -999, -999}
    };

    for (const auto& c : badCoords) {
        // Must not crash or throw
        sec.setBlock(c[0], c[1], c[2], BlockId::Stone);
        BlockId b = sec.getBlock(c[0], c[1], c[2]);
        ASSERT_EQ(b, BlockId::Air) << "Out of bounds getBlock must return Air";

        sec.setSkyLight(c[0], c[1], c[2], 10);
        uint8_t sky = sec.getSkyLight(c[0], c[1], c[2]);
        ASSERT_EQ(sky, 0) << "Out of bounds getSkyLight must return 0";

        sec.setBlockLight(c[0], c[1], c[2], 10);
        uint8_t blk = sec.getBlockLight(c[0], c[1], c[2]);
        ASSERT_EQ(blk, 0) << "Out of bounds getBlockLight must return 0";
    }
}

TEST(AdversarialPalettedSection, BlockStateIdExtremeValues) {
    PalettedSection sec;
    // Test large 16-bit state values: 0, 1, 255, 256, 1000, 32767, 65534, 65535
    const BlockStateId testStates[] = { 0, 1, 255, 256, 1000, 32767, 65534, 65535 };
    for (size_t i = 0; i < std::size(testStates); ++i) {
        sec.setBlock(static_cast<int>(i), 0, 0, testStates[i]);
    }

    for (size_t i = 0; i < std::size(testStates); ++i) {
        ASSERT_EQ(sec.getBlockState(static_cast<int>(i), 0, 0), testStates[i]) << "Extreme BlockStateId mismatch in 4-bit palette";
    }

    // Now promote directly to 16-bit mode by adding 260 unique high IDs on Z >= 1
    for (uint16_t i = 0; i < 260; ++i) {
        sec.setBlock(i % 16, (i / 16) % 16, 1 + (i / 256), static_cast<BlockStateId>(60000 + i));
    }
    ASSERT_EQ(sec.getBitsPerVoxel(), 16) << "Must be in 16-bit direct mode";

    // Verify extreme states in direct mode
    for (size_t i = 0; i < std::size(testStates); ++i) {
        ASSERT_EQ(sec.getBlockState(static_cast<int>(i), 0, 0), testStates[i]) << "Extreme BlockStateId mismatch in 16-bit direct mode";
    }
    ASSERT_EQ(sec.getBlockState(0, 0, 0), 0) << "State 0 mismatch";
    ASSERT_EQ(sec.getBlockState(7, 0, 0), 65535) << "State 65535 mismatch";
}

TEST(AdversarialPalettedSection, HighBlockIdPreservedIn4BitAnd8Bit) {
    PalettedSection sec;

    // Use high block IDs that would be corrupted if truncated to 4 or 8 bits
    constexpr BlockStateId HIGH_ID_1 = 0xABCD; // 43,981
    constexpr BlockStateId HIGH_ID_2 = 0x1234; // 4,660

    sec.setBlock(0, 0, 0, HIGH_ID_1);
    EXPECT_EQ(sec.getBitsPerVoxel(), 4);
    EXPECT_EQ(sec.getBlockState(0, 0, 0), HIGH_ID_1);

    sec.setBlock(1, 0, 0, HIGH_ID_2);
    EXPECT_EQ(sec.getBitsPerVoxel(), 4);
    EXPECT_EQ(sec.getBlockState(0, 0, 0), HIGH_ID_1);
    EXPECT_EQ(sec.getBlockState(1, 0, 0), HIGH_ID_2);
    EXPECT_EQ(sec.getBlockState(2, 0, 0), 0); // Air
}

// ============================================================================
// SUITE 2: Lazy DenseLightArray Allocation & Light Nibbles
// ============================================================================

TEST(AdversarialPalettedSection, LazyLightAllocationAndDeallocationCycles) {
    PalettedSection sec(BlockId::Air, 15, 0);

    ASSERT_FALSE(sec.hasLightData()) << "Initial light data must be nullptr";
    ASSERT_EQ(sec.getSkyLight(5, 5, 5), 15) << "Uniform sky light must be 15";
    ASSERT_EQ(sec.getBlockLight(5, 5, 5), 0) << "Uniform block light must be 0";
    ASSERT_FALSE(sec.hasLightData()) << "Reading light must not allocate";

    // Setting uniform value must NOT allocate
    sec.setSkyLight(5, 5, 5, 15);
    sec.setBlockLight(5, 5, 5, 0);
    ASSERT_FALSE(sec.hasLightData()) << "Setting identical uniform values must not allocate";

    // Setting non-uniform block light must allocate
    sec.setBlockLight(5, 5, 5, 12);
    ASSERT_TRUE(sec.hasLightData()) << "Non-uniform block light must allocate DenseLightArray";
    ASSERT_EQ(sec.getBlockLight(5, 5, 5), 12) << "Block light at (5,5,5) must be 12";
    ASSERT_EQ(sec.getBlockLight(5, 5, 6), 0) << "Other voxels must remain at uniform 0";
    ASSERT_EQ(sec.getSkyLight(5, 5, 5), 15) << "Sky light must remain at uniform 15";

    // Resetting uniform lighting must deallocate
    sec.setUniformLighting(15, 0);
    ASSERT_FALSE(sec.hasLightData()) << "setUniformLighting must reset lightData to nullptr";
    ASSERT_EQ(sec.getBlockLight(5, 5, 5), 0) << "Block light must reset to uniform 0";

    // Setting non-uniform sky light must allocate
    sec.setSkyLight(2, 3, 4, 8);
    ASSERT_TRUE(sec.hasLightData()) << "Non-uniform sky light must allocate DenseLightArray";
    ASSERT_EQ(sec.getSkyLight(2, 3, 4), 8) << "Sky light at (2,3,4) must be 8";
    ASSERT_EQ(sec.getSkyLight(2, 3, 5), 15) << "Other voxels must remain at uniform 15";

    sec.setUniformLighting(15, 0);
    ASSERT_FALSE(sec.hasLightData()) << "Reset must free lightData again";

    // 10,000 cycles of allocate & deallocate churn
    for (int i = 0; i < 10000; ++i) {
        sec.setBlockLight(i % 16, (i / 16) % 16, (i / 256) % 16, static_cast<uint8_t>((i % 15) + 1));
        ASSERT_TRUE(sec.hasLightData()) << "Must allocate in churn";
        sec.setUniformLighting(15, 0);
        ASSERT_FALSE(sec.hasLightData()) << "Must deallocate in churn";
    }
}

TEST(AdversarialPalettedSection, LightNibblePackingInterference) {
    PalettedSection sec(BlockId::Air, 15, 0);
    sec.setBlockLight(0, 0, 0, 1); // trigger allocation

    // Test across all 4096 voxels that sky light (upper 4 bits) and block light (lower 4 bits)
    // do NOT interfere with each other
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                size_t idx = (y * 16 + z) * 16 + x;
                uint8_t sky = static_cast<uint8_t>(idx % 16);
                uint8_t blk = static_cast<uint8_t>((idx * 3 + 5) % 16);
                sec.setSkyLight(x, y, z, sky);
                sec.setBlockLight(x, y, z, blk);
            }
        }
    }

    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                size_t idx = (y * 16 + z) * 16 + x;
                uint8_t expectedSky = static_cast<uint8_t>(idx % 16);
                uint8_t expectedBlk = static_cast<uint8_t>((idx * 3 + 5) % 16);
                ASSERT_EQ(sec.getSkyLight(x, y, z), expectedSky) << "Sky light channel corrupted";
                ASSERT_EQ(sec.getBlockLight(x, y, z), expectedBlk) << "Block light channel corrupted";
            }
        }
    }
}

TEST(AdversarialPalettedSection, DeepCopyAndMoveSemantics) {
    PalettedSection original(BlockId::Stone, 15, 0);
    original.setBlock(1, 2, 3, BlockId::GoldOre);
    original.setBlockLight(1, 2, 3, 14);
    ASSERT_TRUE(original.hasLightData()) << "Original must have light data";

    // Copy construction
    PalettedSection copySec(original);
    ASSERT_TRUE(copySec.hasLightData()) << "Copy must have light data";
    ASSERT_EQ(copySec.getBlock(1, 2, 3), BlockId::GoldOre) << "Copy block must match";
    ASSERT_EQ(copySec.getBlockLight(1, 2, 3), 14) << "Copy light must match";

    // Mutate copy
    copySec.setBlock(1, 2, 3, BlockId::DiamondOre);
    copySec.setBlockLight(1, 2, 3, 7);

    // Original must NOT change
    ASSERT_EQ(original.getBlock(1, 2, 3), BlockId::GoldOre) << "Original block must not change";
    ASSERT_EQ(original.getBlockLight(1, 2, 3), 14) << "Original light must not change";

    // Self-assignment
    copySec = copySec;
    ASSERT_EQ(copySec.getBlock(1, 2, 3), BlockId::DiamondOre) << "Self-assignment must be safe";
    ASSERT_EQ(copySec.getBlockLight(1, 2, 3), 7) << "Self-assignment must preserve light";
}

// ============================================================================
// SUITE 3: Chunk Sections Across Y Boundaries & RLE Persistence
// ============================================================================

TEST(AdversarialChunkSections, BoundaryBlockAccessY0_15_16_31_32_127) {
    Chunk chunk(core::ChunkCoord{0, 0});

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
    const BlockId testBlocks[] = {
        BlockId::Bedrock, BlockId::Stone,      // Sec 0: 0, 15
        BlockId::Dirt, BlockId::Grass,        // Sec 1: 16, 31
        BlockId::Cobblestone, BlockId::OakPlanks, // Sec 2: 32, 47
        BlockId::Sand, BlockId::Gravel,       // Sec 3: 48, 63
        BlockId::GoldOre, BlockId::IronOre,   // Sec 4: 64, 79
        BlockId::CoalOre, BlockId::OakLog,    // Sec 5: 80, 95
        BlockId::Leaves, BlockId::Glass,      // Sec 6: 96, 111
        BlockId::LapisOre, BlockId::DiamondOre // Sec 7: 112, 127
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
    EXPECT_EQ(chunk.getBlock(7, 1, 7), BlockId::Air);
    EXPECT_EQ(chunk.getBlock(7, 14, 7), BlockId::Air);
    EXPECT_EQ(chunk.getBlock(7, 17, 7), BlockId::Air);
    EXPECT_EQ(chunk.getBlock(7, 30, 7), BlockId::Air);
    EXPECT_EQ(chunk.getBlock(7, 126, 7), BlockId::Air);

    // Verify Out of Bounds access
    EXPECT_EQ(chunk.getBlock(7, -1, 7), BlockId::Air);
    EXPECT_EQ(chunk.getBlock(7, 128, 7), BlockId::Air);
    EXPECT_EQ(chunk.getBlock(7, 255, 7), BlockId::Air);
    EXPECT_EQ(chunk.getBlock(-1, 0, 7), BlockId::Air);
    EXPECT_EQ(chunk.getBlock(16, 0, 7), BlockId::Air);
    EXPECT_EQ(chunk.getBlock(7, 0, -1), BlockId::Air);
    EXPECT_EQ(chunk.getBlock(7, 0, 16), BlockId::Air);
}

TEST(AdversarialChunkSections, CrossSectionColumnAccessAndIsolation) {
    Chunk chunk(core::ChunkCoord{0, 0});

    // Fill an entire vertical column (x=4, z=9) with distinctive blocks for all 128 heights
    for (int32_t y = 0; y < 128; ++y) {
        const BlockId id = static_cast<BlockId>(1 + (y % 15)); // rotating solid block types
        chunk.setBlock(4, y, 9, id);
    }

    // Verify all 128 blocks are exactly as written
    for (int32_t y = 0; y < 128; ++y) {
        const BlockId expected = static_cast<BlockId>(1 + (y % 15));
        EXPECT_EQ(chunk.getBlock(4, y, 9), expected) << "Mismatch at Y=" << y;
    }

    // Neighboring columns in all 8 sections must remain completely pure Air
    for (int32_t y = 0; y < 128; ++y) {
        EXPECT_EQ(chunk.getBlock(3, y, 9), BlockId::Air);
        EXPECT_EQ(chunk.getBlock(5, y, 9), BlockId::Air);
        EXPECT_EQ(chunk.getBlock(4, y, 8), BlockId::Air);
        EXPECT_EQ(chunk.getBlock(4, y, 10), BlockId::Air);
    }

    // Check that sections have bitsPerVoxel == 4 (since multiple unique blocks were placed per section)
    for (size_t sec = 0; sec < 8; ++sec) {
        EXPECT_FALSE(chunk.getSection(sec).isHomogeneous());
        EXPECT_EQ(chunk.getSection(sec).getBitsPerVoxel(), 4);
    }
}

TEST(AdversarialChunkSections, RLERoundtripCrossSectionLongRuns) {
    Chunk original(core::ChunkCoord{1, 2});

    // Construct challenging patterns:
    // 1. Column (0, 0): Run of 128 Stone blocks traversing all 8 sub-chunks
    for (int32_t y = 0; y < 128; ++y) {
        original.setBlock(0, y, 0, BlockId::Stone);
    }

    // 2. Boundary straddler: 10 Sand blocks from Y=12 to Y=21 (straddles Sec 0 and Sec 1)
    for (int32_t y = 12; y <= 21; ++y) {
        original.setBlock(1, y, 0, BlockId::Sand);
    }

    // 3. High boundary straddler: 10 Dirt blocks from Y=120 to Y=127 (top of Sec 7)
    for (int32_t y = 120; y <= 127; ++y) {
        original.setBlock(2, y, 0, BlockId::Dirt);
    }

    // 4. Multi-section checkerboard in Column (5, 5)
    for (int32_t y = 0; y < 128; ++y) {
        original.setBlock(5, y, 5, (y % 2 == 0) ? BlockId::Glass : BlockId::Obsidian);
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
    Chunk restored(core::ChunkCoord{1, 2});
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

TEST(AdversarialChunkSections, RLEMalformedBufferAdversarialInputs) {
    Chunk chunk(core::ChunkCoord{0, 0});

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

TEST(AdversarialChunkSections, RealisticHeterogeneousSubchunkRLEIntegrity) {
    Chunk chunk(core::ChunkCoord{-5, 7});

    // Section 0 (Y=0..15): Bedrock floor, Obsidian, Stone
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            chunk.setBlock(x, 0, z, BlockId::Bedrock);
            chunk.setBlock(x, 1, z, BlockId::Obsidian);
            for (int y = 2; y <= 15; ++y) {
                chunk.setBlock(x, y, z, BlockId::Stone);
            }
        }
    }

    // Section 1 (Y=16..31): Stone with veins of CoalOre, IronOre, GoldOre
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            for (int y = 16; y <= 31; ++y) {
                BlockId b = BlockId::Stone;
                if ((x + y + z) % 7 == 0) b = BlockId::CoalOre;
                if ((x * 3 + y + z) % 11 == 0) b = BlockId::IronOre;
                if ((x + y * 2 + z) % 19 == 0) b = BlockId::GoldOre;
                chunk.setBlock(x, y, z, b);
            }
        }
    }

    // Section 2 (Y=32..47): Cavern with air pockets and water pools
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            for (int y = 32; y <= 47; ++y) {
                if (x >= 4 && x <= 12 && z >= 4 && z <= 12) {
                    chunk.setBlock(x, y, z, (y == 32) ? BlockId::Water : BlockId::Air);
                } else {
                    chunk.setBlock(x, y, z, BlockId::Stone);
                }
            }
        }
    }

    // Section 3 (Y=48..63): Dirt with top Grass at Y=63
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            for (int y = 48; y < 63; ++y) {
                chunk.setBlock(x, y, z, BlockId::Dirt);
            }
            chunk.setBlock(x, 63, z, BlockId::Grass);
        }
    }

    // Section 4 (Y=64..79): Surface with trees (OakLog trunk, Leaves canopy)
    // Tree at (8, 64..70, 8)
    for (int y = 64; y <= 68; ++y) {
        chunk.setBlock(8, y, 8, BlockId::OakLog);
    }
    for (int lx = 6; lx <= 10; ++lx) {
        for (int lz = 6; lz <= 10; ++lz) {
            for (int ly = 67; ly <= 70; ++ly) {
                if (chunk.getBlock(lx, ly, lz) == BlockId::Air) {
                    chunk.setBlock(lx, ly, lz, BlockId::Leaves);
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
            chunk.setBlock(x, 127, z, BlockId::Glass);
        }
    }

    // Verify sections 5 and 6 are homogeneous
    EXPECT_TRUE(chunk.getSection(5).isHomogeneous());
    EXPECT_TRUE(chunk.getSection(6).isHomogeneous());

    chunk.recalculateLighting();

    // Roundtrip RLE
    const std::vector<uint8_t> rle = chunk.serializeRLE();
    EXPECT_GE(rle.size(), 8U);

    Chunk loaded(core::ChunkCoord{-5, 7});
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
    EXPECT_EQ(loaded.getSection(5).getBlock(8, 8, 8), BlockId::Air);
    EXPECT_EQ(loaded.getSection(6).getBlock(8, 8, 8), BlockId::Air);
}

TEST(AdversarialChunkSections, ChunkSectionBoundariesAndRLEPersistence) {
    Chunk original(core::ChunkCoord{3, -5});

    original.getSection(0) = PalettedSection(BlockId::Bedrock, 15, 0);

    // Section 1 (Y=16..31): 4-bit palette (Stone, Dirt, Coal, Iron, Gold)
    const BlockId sec1Blocks[] = { BlockId::Stone, BlockId::Dirt, BlockId::CoalOre, BlockId::IronOre, BlockId::GoldOre };
    for (int y = 16; y < 32; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                original.setBlock(x, y, z, sec1Blocks[(x + y + z) % 5]);
            }
        }
    }
    // Section 2 (Y=32..47): 8-bit palette (100 distinct states)
    for (int y = 32; y < 48; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                original.setBlock(x, y, z, static_cast<BlockId>((x + y * 16 + z) % 100 + 1));
            }
        }
    }
    // Section 3 (Y=48..63): 16-bit direct mode (300 distinct states)
    for (uint16_t i = 0; i < 300; ++i) {
        original.getSection(3).setBlock(i % 16, (i / 16) % 16, i / 256, static_cast<BlockStateId>(1000 + i));
    }

    // Sections 4..7 remain default Air (0-bit)

    // Verify sub-chunk bit depths
    ASSERT_EQ(original.getSection(0).getBitsPerVoxel(), 0) << "Section 0 must be 0-bit";
    ASSERT_EQ(original.getSection(1).getBitsPerVoxel(), 4) << "Section 1 must be 4-bit";
    ASSERT_EQ(original.getSection(2).getBitsPerVoxel(), 8) << "Section 2 must be 8-bit";
    ASSERT_EQ(original.getSection(3).getBitsPerVoxel(), 16) << "Section 3 must be 16-bit";
    ASSERT_EQ(original.getSection(4).getBitsPerVoxel(), 0) << "Section 4 must be 0-bit";

    // Test cross-boundary boundary reads:
    // Y=15 (top of Sec 0) vs Y=16 (bottom of Sec 1)
    ASSERT_EQ(original.getBlock(5, 15, 5), BlockId::Bedrock) << "Y=15 boundary check";
    ASSERT_EQ(original.getBlock(5, 16, 5), sec1Blocks[(5 + 16 + 5) % 5]) << "Y=16 boundary check";

    // Test RLE serialization roundtrip
    const std::vector<uint8_t> rleData = original.serializeRLE();
    ASSERT_FALSE(rleData.empty()) << "Serialized RLE data must not be empty";

    Chunk restored(core::ChunkCoord{3, -5});
    bool desOk = restored.deserializeRLE(rleData);
    ASSERT_TRUE(desOk) << "Deserialization must succeed";

    // Verify all 32,768 voxels match exactly
    for (int y = 0; y < 128; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                BlockId origBlock = original.getBlock(x, y, z);
                BlockId restBlock = restored.getBlock(x, y, z);
                ASSERT_EQ(origBlock, restBlock) << "Voxel mismatch after RLE roundtrip";
            }
        }
    }
}
