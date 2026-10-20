#include <gtest/gtest.h>
#include "voxel/Chunk.hpp"
#include "voxel/PalettedSection.hpp"

namespace betacraft::world {

TEST(PalettedSectionTest, SizeAndAlignmentInvariants) {
    EXPECT_LE(sizeof(PalettedSection), 64U);
    EXPECT_EQ(sizeof(PalettedSection), 64U);
    EXPECT_EQ(alignof(PalettedSection), 64U);
    static_assert(sizeof(PalettedSection) <= 64, "PalettedSection must fit in a 64-byte L1 cache line");
    static_assert(alignof(PalettedSection) == 64, "PalettedSection must be aligned to 64 bytes");
}

TEST(PalettedSectionTest, HomogeneousZeroAllocation) {
    PalettedSection airSection;
    EXPECT_TRUE(airSection.isHomogeneous());
    EXPECT_FALSE(airSection.hasLightData());
    EXPECT_EQ(airSection.getBitsPerVoxel(), 0);
    EXPECT_EQ(airSection.getBlock(0, 0, 0), BlockId::Air);
    EXPECT_EQ(airSection.getBlock(15, 15, 15), BlockId::Air);
    EXPECT_EQ(airSection.getSkyLight(0, 0, 0), 15);
    EXPECT_EQ(airSection.getBlockLight(0, 0, 0), 0);
    EXPECT_EQ(airSection.getMemoryFootprintBytes(), sizeof(PalettedSection));

    PalettedSection stoneSection(BlockId::Stone, 0, 0);
    EXPECT_TRUE(stoneSection.isHomogeneous());
    EXPECT_FALSE(stoneSection.hasLightData());
    EXPECT_EQ(stoneSection.getBitsPerVoxel(), 0);
    EXPECT_EQ(stoneSection.getBlock(0, 0, 0), BlockId::Stone);
    EXPECT_EQ(stoneSection.getBlock(7, 7, 7), BlockId::Stone);
    EXPECT_EQ(stoneSection.getMemoryFootprintBytes(), sizeof(PalettedSection));
}

TEST(PalettedSectionTest, DynamicPaletteTransitions) {
    PalettedSection section;
    EXPECT_EQ(section.getBitsPerVoxel(), 0);
    EXPECT_TRUE(section.isHomogeneous());

    // 1. Transition 0-bit -> 4-bit (adding 2nd unique block)
    section.setBlock(0, 0, 0, BlockId::Stone);
    EXPECT_EQ(section.getBitsPerVoxel(), 4);
    EXPECT_FALSE(section.isHomogeneous());
    EXPECT_EQ(section.getBlock(0, 0, 0), BlockId::Stone);
    EXPECT_EQ(section.getBlock(1, 0, 0), BlockId::Air);
    EXPECT_EQ(section.getBlock(15, 15, 15), BlockId::Air);

    // 2. Add blocks up to 16 unique blocks (still 4-bit)
    // Palette currently contains Air and Stone (2 blocks). Add 14 more unique blocks:
    const BlockId sampleBlocks[] = {
        BlockId::Dirt, BlockId::Grass, BlockId::Cobblestone, BlockId::OakPlanks,
        BlockId::Bedrock, BlockId::Sand, BlockId::Gravel, BlockId::OakLog,
        BlockId::Leaves, BlockId::Glass, BlockId::LapisOre, BlockId::Sandstone,
        BlockId::Wool, BlockId::GoldOre
    };
    for (size_t i = 0; i < 14; ++i) {
        section.setBlock(static_cast<int32_t>(i + 1), 0, 0, sampleBlocks[i]);
    }
    EXPECT_EQ(section.getBitsPerVoxel(), 4);
    EXPECT_EQ(section.getPaletteSize(), 16U);

    // Verify all 16 blocks are preserved accurately in 4-bit mode
    EXPECT_EQ(section.getBlock(0, 0, 0), BlockId::Stone);
    for (size_t i = 0; i < 14; ++i) {
        EXPECT_EQ(section.getBlock(static_cast<int32_t>(i + 1), 0, 0), sampleBlocks[i]);
    }
    EXPECT_EQ(section.getBlock(15, 0, 0), BlockId::Air);

    // 3. Transition 4-bit -> 8-bit upon inserting 17th unique block
    section.setBlock(15, 0, 0, BlockId::IronOre);
    EXPECT_EQ(section.getBitsPerVoxel(), 8);
    EXPECT_GE(section.getPaletteSize(), 17U);

    // Verify all previous blocks and the new 17th block are preserved
    EXPECT_EQ(section.getBlock(0, 0, 0), BlockId::Stone);
    for (size_t i = 0; i < 14; ++i) {
        EXPECT_EQ(section.getBlock(static_cast<int32_t>(i + 1), 0, 0), sampleBlocks[i]);
    }
    EXPECT_EQ(section.getBlock(15, 0, 0), BlockId::IronOre);
    EXPECT_EQ(section.getBlock(0, 1, 0), BlockId::Air);

    // 4. Fill with distinct blocks up to 256 unique blocks
    // Palette currently has 17 unique blocks. Add 239 more distinct states (1000..1238):
    for (uint16_t i = 0; i < 239; ++i) {
        const int32_t x = i % 16;
        const int32_t y = 1 + (i / 16);
        const int32_t z = 1 + (i / 256);
        section.setBlock(x, y, z, static_cast<BlockStateId>(1000 + i));
    }
    EXPECT_EQ(section.getBitsPerVoxel(), 8);
    EXPECT_EQ(section.getPaletteSize(), 256U);

    // 5. Transition 8-bit -> 16-bit direct mode upon inserting 257th unique block
    section.setBlock(15, 15, 15, static_cast<BlockStateId>(2000));
    EXPECT_EQ(section.getBitsPerVoxel(), 16);
    // In 16-bit direct mode, palette is freed to conserve RAM
    EXPECT_EQ(section.getPaletteSize(), 0U);
    EXPECT_EQ(section.getBlockState(15, 15, 15), static_cast<BlockStateId>(2000));
    EXPECT_EQ(section.getBlock(0, 0, 0), BlockId::Stone);
}

TEST(PalettedSectionTest, NibblePackingIndependence) {
    PalettedSection section;
    // lx=0, ly=0, lz=0 -> index 0 (even nibble)
    // lx=1, ly=0, lz=0 -> index 1 (odd nibble)
    section.setBlock(0, 0, 0, BlockId::Dirt);
    section.setBlock(1, 0, 0, BlockId::Sand);

    EXPECT_EQ(section.getBlock(0, 0, 0), BlockId::Dirt);
    EXPECT_EQ(section.getBlock(1, 0, 0), BlockId::Sand);

    // Modifying even nibble does not alter odd nibble
    section.setBlock(0, 0, 0, BlockId::Gravel);
    EXPECT_EQ(section.getBlock(0, 0, 0), BlockId::Gravel);
    EXPECT_EQ(section.getBlock(1, 0, 0), BlockId::Sand);

    // Modifying odd nibble does not alter even nibble
    section.setBlock(1, 0, 0, BlockId::Cobblestone);
    EXPECT_EQ(section.getBlock(0, 0, 0), BlockId::Gravel);
    EXPECT_EQ(section.getBlock(1, 0, 0), BlockId::Cobblestone);
}

TEST(PalettedSectionTest, LazyLightAllocationAndReset) {
    PalettedSection section(BlockId::Air, 15, 0);
    EXPECT_FALSE(section.hasLightData());

    // Reading uniform values
    EXPECT_EQ(section.getSkyLight(3, 4, 5), 15);
    EXPECT_EQ(section.getBlockLight(3, 4, 5), 0);
    EXPECT_FALSE(section.hasLightData());

    // Writing identical values must NOT allocate
    section.setSkyLight(3, 4, 5, 15);
    section.setBlockLight(3, 4, 5, 0);
    EXPECT_FALSE(section.hasLightData());

    // Writing a non-uniform light value must allocate DenseLightArray
    section.setBlockLight(3, 4, 5, 14);
    EXPECT_TRUE(section.hasLightData());
    EXPECT_EQ(section.getBlockLight(3, 4, 5), 14);
    EXPECT_EQ(section.getBlockLight(3, 4, 6), 0); // other voxels retain prior uniform value
    EXPECT_EQ(section.getSkyLight(3, 4, 5), 15);  // sky light retained

    // Resetting uniform lighting deallocates DenseLightArray
    section.setUniformLighting(15, 0);
    EXPECT_FALSE(section.hasLightData());
    EXPECT_EQ(section.getBlockLight(3, 4, 5), 0);
    EXPECT_EQ(section.getSkyLight(3, 4, 5), 15);
}

TEST(PalettedSectionTest, DeepCopyAndAssignment) {
    PalettedSection original(BlockId::Air, 15, 0);
    original.setBlock(2, 3, 4, BlockId::Stone);
    original.setBlockLight(2, 3, 4, 12);
    EXPECT_TRUE(original.hasLightData());

    // Copy construction
    PalettedSection copy(original);
    EXPECT_EQ(copy.getBlock(2, 3, 4), BlockId::Stone);
    EXPECT_EQ(copy.getBlockLight(2, 3, 4), 12);
    EXPECT_TRUE(copy.hasLightData());

    // Mutating copy must not affect original
    copy.setBlock(2, 3, 4, BlockId::DiamondOre);
    copy.setBlockLight(2, 3, 4, 7);
    EXPECT_EQ(original.getBlock(2, 3, 4), BlockId::Stone);
    EXPECT_EQ(original.getBlockLight(2, 3, 4), 12);
    EXPECT_EQ(copy.getBlock(2, 3, 4), BlockId::DiamondOre);
    EXPECT_EQ(copy.getBlockLight(2, 3, 4), 7);
}

TEST(PalettedSectionTest, ChunkSubChunkIntegration) {
    Chunk chunk(core::ChunkCoord{0, 0});

    // Chunk height 128 = 8 sections of 16 voxels each
    // Section 0: Y = 0..15
    // Section 4: Y = 64..79
    // Section 7: Y = 112..127
    chunk.setBlock(5, 5, 5, BlockId::Bedrock);
    chunk.setBlock(5, 65, 5, BlockId::Dirt);
    chunk.setBlock(5, 120, 5, BlockId::Glass);

    EXPECT_EQ(chunk.getBlock(5, 5, 5), BlockId::Bedrock);
    EXPECT_EQ(chunk.getBlock(5, 65, 5), BlockId::Dirt);
    EXPECT_EQ(chunk.getBlock(5, 120, 5), BlockId::Glass);

    // Verify sections access
    const auto& sec0 = chunk.getSection(0);
    const auto& sec4 = chunk.getSection(4);
    const auto& sec7 = chunk.getSection(7);

    EXPECT_EQ(sec0.getBlock(5, 5, 5), BlockId::Bedrock);
    EXPECT_EQ(sec4.getBlock(5, 1, 5), BlockId::Dirt);   // 65 & 15 = 1
    EXPECT_EQ(sec7.getBlock(5, 8, 5), BlockId::Glass);  // 120 & 15 = 8

    // Sections 1, 2, 3, 5, 6 must remain homogeneous air
    EXPECT_TRUE(chunk.getSection(1).isHomogeneous());
    EXPECT_FALSE(chunk.getSection(1).hasLightData());
    EXPECT_TRUE(chunk.getSection(2).isHomogeneous());
    EXPECT_FALSE(chunk.getSection(2).hasLightData());
}

} // namespace betacraft::world
