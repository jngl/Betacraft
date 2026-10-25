// Point-wise cave carving decision: spaghetti + cheese Simplex fields modulated by A(y).
#include "worldgen/CaveCarver.hpp"
#include "WorldgenTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <vector>

using namespace betacraft::worldgen;
using betacraft::voxel::BlockId;

namespace {

constexpr ColumnContext land(int32_t surface) { return ColumnContext{surface, surface, false}; }
constexpr ColumnContext water(int32_t floorY) { return ColumnContext{floorY, floorY, true}; }

constexpr std::array<uint64_t, 4> SEEDS{1ULL, 173173173ULL, 0xDEADBEEFCAFEULL, 0xFFFFFFFFFFFFFFFFULL};

int countCarved(const CaveCarver& c, int32_t y0, int32_t y1, const ColumnContext& ctx, int span = 48) {
    int n = 0;
    for (int32_t x = 0; x < span; ++x)
        for (int32_t z = 0; z < span; ++z)
            for (int32_t y = y0; y <= y1; ++y)
                if (c.shouldCarve(x - 17, y, z + 9, ctx.surfaceHeight, ctx.waterFloorHeight, ctx.isWaterBody)) ++n;
    return n;
}

} // namespace

TEST(CaveCarver, ReportsSeed) {
    EXPECT_EQ(CaveCarver(99ULL).getSeed(), 99ULL);
}

TEST(CaveCarver, NeverCarvesAtOrBelowBedrockGuard) {
    for (uint64_t seed : SEEDS) {
        const CaveCarver c(seed);
        EXPECT_EQ(countCarved(c, -10, 5, land(100), 64), 0) << seed;
        EXPECT_EQ(countCarved(c, -10, 5, water(40), 64), 0) << seed;
    }
}

TEST(CaveCarver, NeverCarvesAtOrAboveLandCeiling) {
    for (uint64_t seed : SEEDS) {
        const CaveCarver c(seed);
        EXPECT_EQ(countCarved(c, 67, 127, land(70), 64), 0) << seed;
    }
}

TEST(CaveCarver, NeverCarvesInsideAquitardVaultUnderWater) {
    for (uint64_t seed : SEEDS) {
        const CaveCarver c(seed);
        for (int32_t floorY : {20, 45, 58, 64}) {
            EXPECT_EQ(countCarved(c, floorY - 6, 127, water(floorY), 40), 0) << seed << " floor=" << floorY;
        }
    }
}

// The "Y <= 64" rule protects the sea floor; it is NOT a blanket ban below sea level.
TEST(CaveCarver, CavesStillExistBelowSeaLevelUnderLand) {
    const CaveCarver c(173173173ULL);
    EXPECT_GT(countCarved(c, 20, 64, land(100)), 0);
    EXPECT_GT(countCarved(c, 52, 64, land(100)), 0); // above the cheese band: spaghetti only
    EXPECT_GT(countCarved(c, 6, 37, water(44)), 0);  // and below the vault of deep oceans
}

TEST(CaveCarver, ShouldCarveMatchesClassifyAndContextOverload) {
    const CaveCarver c(4242ULL);
    const ColumnContext ctx = land(90);
    for (int32_t x = 0; x < 24; ++x)
        for (int32_t z = 0; z < 24; ++z)
            for (int32_t y = 0; y < 100; y += 3) {
                const bool sc = c.shouldCarve(x, y, z, 90, 90, false);
                ASSERT_EQ(sc, c.classify(x, y, z, ctx) != CaveKind::None);
                ASSERT_EQ(sc, c.shouldCarve(x, y, z, ctx));
            }
}

TEST(CaveCarver, ProducesBothSpaghettiAndCheeseCaves) {
    const CaveCarver c(173173173ULL);
    int spaghetti = 0;
    int cheese = 0;
    for (int32_t x = -64; x < 64; x += 2)
        for (int32_t z = -64; z < 64; z += 2)
            for (int32_t y = 6; y < 100; ++y) {
                const CaveKind k = c.classify(x, y, z, land(110));
                if (k == CaveKind::Spaghetti) ++spaghetti;
                if (k == CaveKind::Cheese) {
                    ++cheese;
                    ASSERT_LT(y, CaveCarver::CHEESE_MAX_Y) << "cheese chambers are deep-only";
                }
            }
    EXPECT_GT(spaghetti, 0);
    EXPECT_GT(cheese, 0);
}

TEST(CaveCarver, CarvedFractionIsPlausible) {
    const CaveCarver c(77ULL);
    const int carved = countCarved(c, 10, 60, land(100));
    const int total = 48 * 48 * 51;
    const double fraction = static_cast<double>(carved) / total;
    EXPECT_GT(fraction, 0.01);
    EXPECT_LT(fraction, 0.45);
}

TEST(CaveCarver, EnvelopeAttenuatesCarvingNearBedrock) {
    const CaveCarver c(77ULL);
    const int nearBedrock = countCarved(c, 6, 7, land(100), 96);   // A <= 0.5
    const int midDepth = countCarved(c, 24, 25, land(100), 96);    // A = 1
    EXPECT_LT(nearBedrock, midDepth);
}

TEST(CaveCarver, IsDeterministicAcrossInstances) {
    const CaveCarver a(31337ULL);
    const CaveCarver b(31337ULL);
    for (int32_t x = -20; x < 20; ++x)
        for (int32_t z = -20; z < 20; ++z)
            for (int32_t y = 0; y < 80; y += 2)
                ASSERT_EQ(a.classify(x, y, z, land(90)), b.classify(x, y, z, land(90)));
}

TEST(CaveCarver, DifferentSeedsCarveDifferentNetworks) {
    const CaveCarver a(1ULL);
    const CaveCarver b(2ULL);
    int differ = 0;
    for (int32_t x = 0; x < 32; ++x)
        for (int32_t z = 0; z < 32; ++z)
            for (int32_t y = 10; y < 60; ++y)
                if (a.shouldCarve(x, y, z, land(100)) != b.shouldCarve(x, y, z, land(100))) ++differ;
    EXPECT_GT(differ, 100);
}

TEST(CaveCarver, FillIsLavaAtOrBelowTen) {
    const CaveCarver c(5ULL);
    for (int32_t x = -50; x < 50; x += 7)
        for (int32_t z = -50; z < 50; z += 7)
            for (int32_t y = 6; y <= 10; ++y)
                ASSERT_EQ(c.getExcavationFill(x, y, z), CaveFill::Lava);
}

TEST(CaveFill, AquifersAreConfinedBetweenElevenAndThirtyTwo) {
    const CaveCarver c(5ULL);
    int water = 0;
    int air = 0;
    for (int32_t x = -400; x < 400; x += 9) {
        for (int32_t z = -400; z < 400; z += 9) {
            for (int32_t y = 11; y <= 32; y += 7) {
                const CaveFill f = c.getExcavationFill(x, y, z);
                ASSERT_NE(f, CaveFill::Lava);
                (f == CaveFill::Water ? water : air)++;
            }
            for (int32_t y = 33; y < 128; y += 5) {
                ASSERT_EQ(c.getExcavationFill(x, y, z), CaveFill::Air);
            }
        }
    }
    EXPECT_GT(water, 0); // aquifers exist (aq > 0.42) ...
    EXPECT_GT(air, water); // ... but are regional, not everywhere
}

TEST(CaveFill, AquiferIsAColumnPropertyIndependentOfY) {
    // A 2D aquifer field: inside the band, the fill depends only on (x, z).
    const CaveCarver c(8ULL);
    for (int32_t x = -100; x < 100; x += 3)
        for (int32_t z = -100; z < 100; z += 3) {
            const CaveFill ref = c.getExcavationFill(x, 11, z);
            for (int32_t y = 12; y <= 32; ++y) ASSERT_EQ(c.getExcavationFill(x, y, z), ref);
        }
}

TEST(CaveFillPalette, MapsKindsToBlocks) {
    const CaveFillPalette def{};
    EXPECT_EQ(def.blockFor(CaveFill::Air), BlockId::Air);
    EXPECT_EQ(def.blockFor(CaveFill::Water), BlockId::Water);
    const CaveFillPalette custom{BlockId::Air, BlockId::Water, BlockId::Obsidian};
    EXPECT_EQ(custom.blockFor(CaveFill::Lava), BlockId::Obsidian);
}

// ---- Lateral aquitard guard: a cave beside an ocean column must not open into it ----

TEST(CaveColumnGuard, LandWithoutWaterNeighboursIsUnchanged) {
    const std::array<ColumnContext, 4> n{land(70), land(72), land(68), land(90)};
    const ColumnContext g = CaveCarver::guardColumn(land(75), n);
    EXPECT_EQ(g.surfaceHeight, 75);
    EXPECT_FALSE(g.isWaterBody);
}

TEST(CaveColumnGuard, LandNextToWaterInheritsTheWaterFloorCeiling) {
    const std::array<ColumnContext, 4> n{land(70), water(50), land(68), water(55)};
    const ColumnContext g = CaveCarver::guardColumn(land(75), n);
    EXPECT_TRUE(g.isWaterBody);
    EXPECT_EQ(g.waterFloorHeight, 50);
    EXPECT_EQ(g.surfaceHeight, 75);
}

TEST(CaveColumnGuard, WaterColumnTakesTheDeepestAdjacentFloor) {
    const std::array<ColumnContext, 4> n{water(58), water(41), land(66), water(47)};
    const ColumnContext g = CaveCarver::guardColumn(water(52), n);
    EXPECT_TRUE(g.isWaterBody);
    EXPECT_EQ(g.waterFloorHeight, 41);
}

TEST(CaveColumnGuard, IsIndependentOfNeighbourOrder) {
    std::array<ColumnContext, 4> n{water(58), land(70), water(41), land(66)};
    const ColumnContext ref = CaveCarver::guardColumn(land(72), n);
    std::array<int, 4> idx{0, 1, 2, 3};
    do {
        const std::array<ColumnContext, 4> p{n[static_cast<size_t>(idx[0])], n[static_cast<size_t>(idx[1])],
                                             n[static_cast<size_t>(idx[2])], n[static_cast<size_t>(idx[3])]};
        const ColumnContext g = CaveCarver::guardColumn(land(72), p);
        ASSERT_EQ(g.waterFloorHeight, ref.waterFloorHeight);
        ASSERT_EQ(g.isWaterBody, ref.isWaterBody);
        ASSERT_EQ(g.surfaceHeight, ref.surfaceHeight);
    } while (std::next_permutation(idx.begin(), idx.end()));
}

TEST(CaveColumnGuard, ResolveColumnUsesTheFourFaceNeighbours) {
    test::SyntheticTerrain terrain;
    for (int32_t x = -40; x < 40; ++x) {
        for (int32_t z = -40; z < 40; ++z) {
            const std::array<ColumnContext, 4> n{
                terrain.sampleColumn(x + 1, z), terrain.sampleColumn(x - 1, z),
                terrain.sampleColumn(x, z + 1), terrain.sampleColumn(x, z - 1)};
            const ColumnContext expected = CaveCarver::guardColumn(terrain.sampleColumn(x, z), n);
            const ColumnContext got = CaveCarver::resolveColumn(terrain, x, z);
            ASSERT_EQ(got.surfaceHeight, expected.surfaceHeight);
            ASSERT_EQ(got.waterFloorHeight, expected.waterFloorHeight);
            ASSERT_EQ(got.isWaterBody, expected.isWaterBody);
        }
    }
}
