// P3.3 - "Nappes de lave systematiques sous Y=10": the lava fill needs a Lava block, which the
// block registry does not have yet. The palette picks it up automatically once it exists, and
// until then the magma band is left as rock instead of becoming an empty void under aquifers.
#include "worldgen/CaveCarver.hpp"
#include "WorldgenTestSupport.hpp"

#include <gtest/gtest.h>

#include <cstdint>

using namespace betacraft::worldgen;
using betacraft::core::ChunkCoord;
using betacraft::world::BlockId;
using betacraft::world::Chunk;

namespace {

enum class RegistryWithLava : uint8_t { Air = 0, Stone = 1, Lava = 11 };
enum class RegistryWithoutLava : uint8_t { Air = 0, Stone = 1 };

constexpr uint64_t SEED = 173173173ULL;

} // namespace

TEST(CaveFillPalette, LavaBlockIsDetectedWhenTheRegistryHasOne) {
    EXPECT_EQ(detail::lavaBlockOr(RegistryWithLava::Air), RegistryWithLava::Lava);
    EXPECT_EQ(detail::lavaBlockOr(RegistryWithoutLava::Air), RegistryWithoutLava::Air);
    EXPECT_TRUE(detail::hasLavaBlock<RegistryWithLava>);
    EXPECT_FALSE(detail::hasLavaBlock<RegistryWithoutLava>);
}

TEST(CaveFillPalette, DefaultPaletteUsesTheRegistryLavaWhenAvailable) {
    constexpr CaveFillPalette def{};
    EXPECT_EQ(def.providesLava(), detail::hasLavaBlock<BlockId>);
    EXPECT_EQ(def.lava, detail::lavaBlockOr(BlockId::Air));
    constexpr CaveFillPalette custom{BlockId::Air, BlockId::Water, BlockId::Obsidian};
    EXPECT_TRUE(custom.providesLava());
    constexpr CaveFillPalette noLava{BlockId::Air, BlockId::Water, BlockId::Air};
    EXPECT_FALSE(noLava.providesLava());
}

TEST(CaveChunkCarving, WithoutALavaBlockTheMagmaBandStaysSolid) {
    test::SyntheticTerrain terrain;
    const CaveCarver carver(SEED);
    const CaveFillPalette noLava{BlockId::Air, BlockId::Water, BlockId::Air};
    const CaveFillPalette withLava{BlockId::Air, BlockId::Water, BlockId::Obsidian};
    uint32_t lavaCellsWithBlock = 0;
    for (int32_t cx = -2; cx <= 2; ++cx) {
        Chunk before(cx, 1);
        Chunk a(cx, 1);
        Chunk b(cx, 1);
        terrain.fillChunk(before);
        terrain.fillChunk(a);
        terrain.fillChunk(b);
        const CarveStats sa = carver.carveChunk(a, terrain, noLava);
        lavaCellsWithBlock += carver.carveChunk(b, terrain, withLava).lava;
        EXPECT_EQ(sa.lava, 0U);
        for (int32_t y = 0; y <= CaveCarver::LAVA_MAX_Y; ++y)
            for (int32_t z = 0; z < Chunk::DEPTH; ++z)
                for (int32_t x = 0; x < Chunk::WIDTH; ++x)
                    ASSERT_EQ(a.getBlock(x, y, z), before.getBlock(x, y, z)) << x << "," << y << "," << z;
    }
    EXPECT_GT(lavaCellsWithBlock, 0U) << "fixture must reach the magma band";
}
