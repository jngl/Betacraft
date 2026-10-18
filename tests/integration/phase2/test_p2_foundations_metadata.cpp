// Phase 2 integration, data foundations: the per-voxel 4-bit metadata nibble (fluid level
// 0..7 + falling flag) through PalettedSection -> Chunk -> World -> WorldStorage.
#include "tests/test_helpers.hpp"

#include "fluids/FluidState.hpp"
#include "world/Chunk.hpp"
#include "world/PalettedSection.hpp"
#include "world/World.hpp"
#include "world/WorldStorage.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <vector>

using namespace betacraft;
using world::BlockId;
using world::Chunk;
using world::PalettedSection;

namespace {

class P2FoundationsMetadata : public ::testing::Test {
protected:
    void SetUp() override { world::BlockRegistry::initialize(); }
};

constexpr int32_t kPocketY = 120;

} // namespace

// --- PalettedSection -------------------------------------------------------------

TEST_F(P2FoundationsMetadata, SectionStaysCacheLineSized) {
    static_assert(sizeof(PalettedSection) <= 64);
    EXPECT_LE(sizeof(PalettedSection), 64u);
}

TEST_F(P2FoundationsMetadata, SectionAllocatesMetadataLazily) {
    PalettedSection sec(BlockId::Water, 15, 0);
    EXPECT_FALSE(sec.hasMetadata());
    EXPECT_EQ(sec.getMetadata(3, 4, 5), 0);
    const size_t before = sec.getMemoryFootprintBytes();

    sec.setMetadata(3, 4, 5, 0); // writing the implicit value allocates nothing
    EXPECT_FALSE(sec.hasMetadata());
    EXPECT_TRUE(sec.isHomogeneous());

    sec.setMetadata(3, 4, 5, 0x0B);
    EXPECT_TRUE(sec.hasMetadata());
    EXPECT_FALSE(sec.isHomogeneous());
    EXPECT_EQ(sec.getMetadata(3, 4, 5), 0x0B);
    EXPECT_EQ(sec.getMetadata(4, 4, 5), 0);
    EXPECT_EQ(sec.getMemoryFootprintBytes(), before + PalettedSection::VOLUME / 2);
}

TEST_F(P2FoundationsMetadata, SectionMetadataIsANibbleIndependentOfLight) {
    PalettedSection sec(BlockId::Air, 15, 0);
    sec.setSkyLight(1, 1, 1, 7);
    sec.setBlockLight(1, 1, 1, 9);
    sec.setMetadata(1, 1, 1, 0xFF); // only the low nibble is kept
    sec.setMetadata(0, 1, 1, 0x3);  // neighbour sharing the same byte
    EXPECT_EQ(sec.getMetadata(1, 1, 1), 0x0F);
    EXPECT_EQ(sec.getMetadata(0, 1, 1), 0x03);
    EXPECT_EQ(sec.getSkyLight(1, 1, 1), 7);
    EXPECT_EQ(sec.getBlockLight(1, 1, 1), 9);
    sec.setUniformLighting(15, 0); // relighting must not wipe fluid levels
    EXPECT_EQ(sec.getMetadata(1, 1, 1), 0x0F);
    EXPECT_EQ(sec.getMetadata(-1, 0, 0), 0);
    sec.setMetadata(16, 0, 0, 5); // out of bounds is ignored
    EXPECT_EQ(sec.getMetadata(15, 0, 0), 0);
}

TEST_F(P2FoundationsMetadata, ChangingTheBlockResetsItsMetadata) {
    // Beta Chunk.setBlockID clears the metadata nibble of the cell it rewrites.
    PalettedSection sec(BlockId::Air, 15, 0);
    sec.setBlock(2, 2, 2, BlockId::Water);
    sec.setMetadata(2, 2, 2, 5);
    sec.setBlock(2, 2, 2, BlockId::Water); // same block: level kept
    EXPECT_EQ(sec.getMetadata(2, 2, 2), 5);
    sec.setBlock(2, 2, 2, BlockId::Lava);
    EXPECT_EQ(sec.getMetadata(2, 2, 2), 0);
}

TEST_F(P2FoundationsMetadata, SectionCopiesDeepCopyMetadata) {
    PalettedSection a(BlockId::Water, 15, 0);
    a.setMetadata(7, 7, 7, 9);
    PalettedSection b = a;
    b.setMetadata(7, 7, 7, 2);
    EXPECT_EQ(a.getMetadata(7, 7, 7), 9);
    EXPECT_EQ(b.getMetadata(7, 7, 7), 2);
    PalettedSection c;
    c = a;
    EXPECT_EQ(c.getMetadata(7, 7, 7), 9);
    PalettedSection d = std::move(c);
    EXPECT_EQ(d.getMetadata(7, 7, 7), 9);
}

TEST_F(P2FoundationsMetadata, SectionBlocksSurviveEveryPaletteWidthAfterTheLayoutChange) {
    // The packed block storage moved to an exact-size buffer to make room for the metadata
    // pointer; walk 4 -> 8 -> 16 bit promotions and copies to pin it.
    PalettedSection sec(BlockId::Air, 15, 0);
    std::vector<world::BlockStateId> oracle(PalettedSection::VOLUME, 0);
    for (int i = 0; i < 300; ++i) {
        const int lx = i % 16;
        const int ly = (i / 16) % 16;
        const int lz = i / 256;
        const auto state = static_cast<world::BlockStateId>(1000 + i);
        sec.setBlock(lx, ly, lz, state);
        oracle[PalettedSection::voxelIndex(lx, ly, lz)] = state;
        if (i == 10) {
            EXPECT_EQ(sec.getBitsPerVoxel(), 4u);
        }
        if (i == 100) {
            EXPECT_EQ(sec.getBitsPerVoxel(), 8u);
        }
    }
    EXPECT_EQ(sec.getBitsPerVoxel(), 16u);
    const PalettedSection copy = sec;
    for (int ly = 0; ly < 16; ++ly)
        for (int lz = 0; lz < 16; ++lz)
            for (int lx = 0; lx < 16; ++lx) {
                ASSERT_EQ(sec.getBlockState(lx, ly, lz), oracle[PalettedSection::voxelIndex(lx, ly, lz)]);
                ASSERT_EQ(copy.getBlockState(lx, ly, lz), oracle[PalettedSection::voxelIndex(lx, ly, lz)]);
            }
}

// --- Chunk / World -----------------------------------------------------------------

TEST_F(P2FoundationsMetadata, ChunkMetadataMarksTheChunkModifiedButNotDirty) {
    // No mesher reads metadata yet (liquids are full cubes): a level change is saved but
    // must not queue a re-mesh that could only produce identical geometry.
    Chunk c({0, 0});
    c.setBlock(3, 70, 3, BlockId::Water, false);
    c.setMeshDirty(false);
    c.setModified(false);
    c.setMetadata(3, 70, 3, 4);
    EXPECT_EQ(c.getMetadata(3, 70, 3), 4);
    EXPECT_FALSE(c.isMeshDirty());
    EXPECT_TRUE(c.isModified());
    EXPECT_TRUE(c.getSection(70 / 16).hasMetadata());
    EXPECT_FALSE(c.getSection(0).hasMetadata());

    c.setMeshDirty(false);
    c.setModified(false);
    c.setMetadata(3, 70, 3, 4); // unchanged: no dirtiness
    EXPECT_FALSE(c.isMeshDirty());
    EXPECT_FALSE(c.isModified());
    EXPECT_EQ(c.getMetadata(3, Chunk::HEIGHT, 3), 0);
}

TEST_F(P2FoundationsMetadata, WorldStoresFluidStateNibbles) {
    test::TempDir dir;
    world::World w(173173ULL, dir.path());
    w.ensureChunkLoaded({0, 0});
    w.ensureChunkLoaded({-1, 0});

    // World convention: Beta-native metadata (FluidState::betaDecay, 0 = source).
    const fluids::FluidState flowing = fluids::FluidState::flowing(fluids::FluidType::Lava, 3);
    ASSERT_TRUE(w.setBlockWithMetadata(-1, kPocketY, 5, BlockId::Lava, static_cast<uint8_t>(flowing.betaDecay())));
    EXPECT_EQ(w.getBlock(-1, kPocketY, 5), BlockId::Lava);
    EXPECT_EQ(fluids::FluidState::fromBetaDecay(fluids::FluidType::Lava, w.getMetadata(-1, kPocketY, 5)), flowing);
    EXPECT_EQ(w.getBlockLight(-1, kPocketY, 5), 15) << "setBlockWithMetadata must relight like setBlock";

    // Same block, new level: only the nibble changes.
    const fluids::FluidState falling = fluids::FluidState::falling(fluids::FluidType::Lava);
    const auto fallingMeta = static_cast<uint8_t>(falling.betaDecay());
    EXPECT_TRUE(w.setBlockWithMetadata(-1, kPocketY, 5, BlockId::Lava, fallingMeta));
    EXPECT_EQ(w.getMetadata(core::BlockPos{-1, kPocketY, 5}), fallingMeta);
    EXPECT_EQ(fluids::FluidState::fromBetaDecay(fluids::FluidType::Lava, fallingMeta), falling);
    EXPECT_FALSE(w.setBlockWithMetadata(-1, kPocketY, 5, BlockId::Lava, fallingMeta));

    EXPECT_TRUE(w.setMetadata(-1, kPocketY, 5, 2));
    EXPECT_EQ(w.getMetadata(-1, kPocketY, 5), 2);

    // Replacing the block clears the level; unloaded/out-of-range cells read 0, ignore writes.
    ASSERT_TRUE(w.setBlock(-1, kPocketY, 5, BlockId::Stone));
    EXPECT_EQ(w.getMetadata(-1, kPocketY, 5), 0);
    EXPECT_FALSE(w.setMetadata(500, 70, 500, 3));
    EXPECT_EQ(w.getMetadata(500, 70, 500), 0);
    EXPECT_EQ(w.getMetadata(0, -1, 0), 0);
    EXPECT_FALSE(w.setMetadata(0, Chunk::HEIGHT, 0, 3));
}

TEST_F(P2FoundationsMetadata, GeneratedAndLegacyWaterDecodesAsSourceUnderTheWorldConvention) {
    // Worldgen and pre-existing saves never wrote metadata, so every water cell reads 0. The
    // Beta-native encoding maps 0 to a source block; FluidState's own toNibble() layout would
    // turn every ocean into the thinnest flowing water, which is why World stores Beta decay.
    test::TempDir dir;
    world::World w(173173ULL, dir.path());
    const world::Chunk& c = w.ensureChunkLoaded({0, 0});
    int waterCells = 0;
    for (int z = 0; z < Chunk::DEPTH; ++z)
        for (int x = 0; x < Chunk::WIDTH; ++x)
            for (int y = 0; y < Chunk::HEIGHT; ++y) {
                if (!world::isWaterBlock(c.getBlock(x, y, z))) continue;
                ++waterCells;
                ASSERT_EQ(c.getMetadata(x, y, z), 0);
                ASSERT_TRUE(fluids::FluidState::fromBetaDecay(fluids::FluidType::Water, c.getMetadata(x, y, z)).isSource());
            }
    if (waterCells == 0) {
        GTEST_SKIP() << "no water in chunk (0,0) for this seed";
    }
    EXPECT_FALSE(fluids::FluidState::fromNibble(fluids::FluidType::Water, 0).isSource());
}

TEST_F(P2FoundationsMetadata, WorldMetadataOnABorderLeavesTheNeighbourMeshClean) {
    test::TempDir dir;
    world::World w(173173ULL, dir.path());
    world::Chunk& east = w.ensureChunkLoaded({0, 0});
    world::Chunk& west = w.ensureChunkLoaded({-1, 0});
    ASSERT_TRUE(w.setBlock(0, kPocketY, 5, BlockId::Water));
    west.setMeshDirty(false);
    east.setMeshDirty(false);
    ASSERT_TRUE(w.setMetadata(0, kPocketY, 5, 6));
    EXPECT_FALSE(west.isMeshDirty()) << "no mesher reads the level yet";
    EXPECT_FALSE(east.isMeshDirty());
    EXPECT_TRUE(east.isModified());
}

// --- Persistence ---------------------------------------------------------------------

TEST_F(P2FoundationsMetadata, ChunkWithoutMetadataStillSerializesAsVersion1) {
    Chunk c({2, 3});
    c.setBlock(1, 1, 1, BlockId::Water);
    const std::vector<uint8_t> bytes = c.serializeRLE();
    ASSERT_GE(bytes.size(), 5u);
    EXPECT_EQ(bytes[4], 1) << "saves without fluid levels keep the original format";
}

TEST_F(P2FoundationsMetadata, ChunkMetadataRoundTripsThroughRle) {
    Chunk c({2, 3});
    for (int x = 0; x < 16; ++x) {
        c.setBlock(x, 64, 0, BlockId::Water);
        c.setMetadata(x, 64, 0, static_cast<uint8_t>(x & 0x0F));
    }
    c.setBlock(15, 127, 15, BlockId::StillLava);
    c.setMetadata(15, 127, 15, 0x08);
    const std::vector<uint8_t> bytes = c.serializeRLE();
    ASSERT_GE(bytes.size(), 5u);
    EXPECT_EQ(bytes[4], 2);

    Chunk restored({2, 3});
    ASSERT_TRUE(restored.deserializeRLE(bytes));
    for (int x = 0; x < 16; ++x) {
        EXPECT_EQ(restored.getBlock(x, 64, 0), BlockId::Water);
        EXPECT_EQ(restored.getMetadata(x, 64, 0), x & 0x0F) << x;
    }
    EXPECT_EQ(restored.getMetadata(15, 127, 15), 0x08);
    EXPECT_EQ(restored.getMetadata(0, 0, 0), 0);
    EXPECT_FALSE(restored.isModified());
    EXPECT_FALSE(restored.getSection(0).hasMetadata()) << "zero metadata must not allocate on load";
}

TEST_F(P2FoundationsMetadata, LegacyVersion1SavesLoadWithZeroMetadata) {
    // A version-1 stream as written before the nibble existed: all air except a water run.
    std::vector<uint8_t> v1{'B', 'C', '1', '7', 1};
    const auto run = [&v1](BlockId id, uint16_t n) {
        v1.push_back(static_cast<uint8_t>(id));
        v1.push_back(static_cast<uint8_t>(n & 0xFFU));
        v1.push_back(static_cast<uint8_t>(n >> 8U));
    };
    run(BlockId::Stone, 256);
    run(BlockId::Water, 256);
    run(BlockId::Air, static_cast<uint16_t>(Chunk::VOLUME - 512));
    Chunk c({0, 0});
    ASSERT_TRUE(c.deserializeRLE(v1));
    EXPECT_EQ(c.getBlock(0, 1, 0), BlockId::Water);
    EXPECT_EQ(c.getMetadata(0, 1, 0), 0);
    for (const auto& sec : c.getSections()) {
        EXPECT_FALSE(sec.hasMetadata());
    }
}

TEST_F(P2FoundationsMetadata, MalformedMetadataStreamsAreRejected) {
    Chunk src({0, 0});
    src.setBlock(0, 0, 0, BlockId::Water);
    src.setMetadata(0, 0, 0, 3);
    const std::vector<uint8_t> good = src.serializeRLE();
    ASSERT_EQ(good[4], 2);

    Chunk c({0, 0});
    ASSERT_TRUE(c.deserializeRLE(good));

    std::vector<uint8_t> truncated(good.begin(), good.end() - 3);
    EXPECT_FALSE(c.deserializeRLE(truncated));

    std::vector<uint8_t> badNibble = good;
    badNibble[badNibble.size() - 3] = 0x10; // last metadata run value > 15
    EXPECT_FALSE(c.deserializeRLE(badNibble));

    std::vector<uint8_t> missing(good.begin(), good.end());
    missing.resize(missing.size() - 6); // drop the metadata runs entirely
    EXPECT_FALSE(c.deserializeRLE(missing));
}

TEST_F(P2FoundationsMetadata, FluidLevelsSurviveWorldSaveAndReload) {
    test::TempDir dir;
    const auto nibble = static_cast<uint8_t>(fluids::FluidState::flowing(fluids::FluidType::Water, 5).betaDecay());
    {
        world::World w(173173ULL, dir.path());
        w.ensureChunkLoaded({0, 0});
        ASSERT_TRUE(w.setBlockWithMetadata(3, kPocketY, 3, BlockId::Water, nibble));
        w.saveModifiedChunks(world::WorldLevelData{});
    }
    {
        world::World w(173173ULL, dir.path());
        w.ensureChunkLoaded({0, 0});
        EXPECT_EQ(w.getBlock(3, kPocketY, 3), BlockId::Water);
        EXPECT_EQ(w.getMetadata(3, kPocketY, 3), nibble);
    }
}

TEST_F(P2FoundationsMetadata, OnlyAMetadataEditStillGetsSaved) {
    test::TempDir dir;
    {
        world::World w(173173ULL, dir.path());
        w.ensureChunkLoaded({0, 0});
        ASSERT_TRUE(w.setBlock(3, kPocketY, 3, BlockId::Water));
        w.saveModifiedChunks(world::WorldLevelData{});
        ASSERT_TRUE(w.setMetadata(3, kPocketY, 3, 0x0A));
        w.saveModifiedChunks(world::WorldLevelData{});
    }
    world::World w(173173ULL, dir.path());
    w.ensureChunkLoaded({0, 0});
    EXPECT_EQ(w.getMetadata(3, kPocketY, 3), 0x0A);
}
