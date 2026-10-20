// S5.7 wheat rules (Beta 1.7.3 BlockCrops): the growth rate from the farmland and the
// crowding around a crop, and the harvest of wheat and seeds by stage.
#include "gameplay/Crops.hpp"
#include "gameplay/Harvesting.hpp"

#include <gtest/gtest.h>

#include <array>

using namespace betacraft::gameplay;
using betacraft::voxel::BlockId;

namespace {

// A crop on farmland of the given moisture over the whole 3x3, nothing else around.
CropNeighbourhood field(uint8_t moisture) {
    CropNeighbourhood cells{};
    for (auto& row : cells.level) row.fill(BlockId::Air);
    for (auto& row : cells.below) row.fill(BlockId::Farmland);
    for (auto& row : cells.belowMeta) row.fill(moisture);
    cells.level[1][1] = BlockId::Crops;
    return cells;
}

void put(CropNeighbourhood& cells, int dx, int dz, BlockId id) {
    cells.level[static_cast<size_t>(dx + 1)][static_cast<size_t>(dz + 1)] = id;
}

} // namespace

TEST(CropRules, GrowthRateIsHighestOnAWetThreeByThree) {
    // 1 + 3 under the crop + 8 x 3/4 around it.
    EXPECT_FLOAT_EQ(cropGrowthRate(field(7)), 10.0f);
    EXPECT_FLOAT_EQ(cropGrowthRate(field(1)), 10.0f) << "any moisture above 0 counts as wet";
    // Dry: 1 + 1 + 8 x 1/4.
    EXPECT_FLOAT_EQ(cropGrowthRate(field(0)), 4.0f);
    // No farmland at all (a crop that will pop off): only the base 1.
    CropNeighbourhood bare = field(7);
    for (auto& row : bare.below) row.fill(BlockId::Dirt);
    EXPECT_FLOAT_EQ(cropGrowthRate(bare), 1.0f);
    // Wet only under the crop itself.
    CropNeighbourhood single = bare;
    single.below[1][1] = BlockId::Farmland;
    EXPECT_FLOAT_EQ(cropGrowthRate(single), 4.0f);
}

TEST(CropRules, NeighbouringCropsHalveTheRate) {
    // A row along one axis is fine (Beta rows of wheat)...
    CropNeighbourhood row = field(7);
    put(row, -1, 0, BlockId::Crops);
    put(row, 1, 0, BlockId::Crops);
    EXPECT_FLOAT_EQ(cropGrowthRate(row), 10.0f);
    // ...crops on both axes, or on a diagonal, halve it.
    CropNeighbourhood cross = row;
    put(cross, 0, 1, BlockId::Crops);
    EXPECT_FLOAT_EQ(cropGrowthRate(cross), 5.0f);
    CropNeighbourhood diagonal = field(7);
    put(diagonal, 1, 1, BlockId::Crops);
    EXPECT_FLOAT_EQ(cropGrowthRate(diagonal), 5.0f);
    // Only once, however crowded.
    CropNeighbourhood crowded = field(7);
    for (int dx = -1; dx <= 1; ++dx)
        for (int dz = -1; dz <= 1; ++dz) put(crowded, dx, dz, BlockId::Crops);
    EXPECT_FLOAT_EQ(cropGrowthRate(crowded), 5.0f);
}

TEST(CropRules, GrowthBoundFollowsBeta) {
    EXPECT_EQ(cropGrowthBound(10.0f), 10); // (int)(100 / 10)
    EXPECT_EQ(cropGrowthBound(4.0f), 25);
    EXPECT_EQ(cropGrowthBound(3.0f), 33);
    EXPECT_EQ(cropGrowthBound(1.0f), 100);
    EXPECT_EQ(cropGrowthBound(0.0f), 1) << "never a zero bound";
}

TEST(CropRules, RipeHarvestAlwaysGivesOneWheatAndUpToThreeSeeds) {
    std::array<int, 4> histogram{};
    for (uint64_t seed = 0; seed < 3000; ++seed) {
        ItemStack hand{};
        const BlockBreakOutcome r = breakBlockWith(BlockId::Crops, hand, seed, kCropsRipeStage);
        ASSERT_TRUE(r.broken);
        ASSERT_TRUE(r.drop.has_value()) << seed;
        EXPECT_EQ(r.drop->id, items::Wheat);
        EXPECT_EQ(r.drop->count, 1);
        const int seeds = r.bonusDrop ? r.bonusDrop->count : 0;
        if (r.bonusDrop) {
            EXPECT_EQ(r.bonusDrop->id, items::Seeds);
        }
        ASSERT_GE(seeds, 0);
        ASSERT_LE(seeds, 3);
        ++histogram[static_cast<size_t>(seeds)];
    }
    // Each try succeeds 8 times in 15: every count from 0 to 3 shows up.
    for (int c = 0; c <= 3; ++c) {
        EXPECT_GT(histogram[static_cast<size_t>(c)], 100) << "count " << c;
    }
}

TEST(CropRules, UnripeCropsGiveSeedsOnly) {
    int seedsAtZero = 0;
    for (uint64_t seed = 0; seed < 3000; ++seed) {
        ItemStack hand{};
        const BlockBreakOutcome r = breakBlockWith(BlockId::Crops, hand, seed, 0);
        EXPECT_FALSE(r.drop.has_value()) << "no wheat before stage 7";
        seedsAtZero += r.bonusDrop ? r.bonusDrop->count : 0;
    }
    // Stage 0: each try succeeds 1 time in 15, so about 3000 * 3 / 15 = 600 seeds.
    EXPECT_GT(seedsAtZero, 450);
    EXPECT_LT(seedsAtZero, 750);

    SplitMix64 rng(42);
    for (int i = 0; i < 200; ++i) {
        const CropHarvest h = rollCropHarvest(3, rng);
        EXPECT_EQ(h.wheat, 0);
        EXPECT_LE(h.seeds, kCropSeedTries);
    }
}
