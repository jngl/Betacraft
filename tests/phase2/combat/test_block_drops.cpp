// Drop decisions of breakBlockWith where Beta 1.7.3 differs from world::BlockDef::dropId
// (BlockIce / BlockGlass / BlockGlowStone / BlockLeaves / BlockBookshelf quantityDropped,
// BlockOre / BlockRedstoneOre / BlockSnowBlock idDropped).
#include "gameplay/Harvesting.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <set>

using namespace betacraft::gameplay;
using betacraft::world::BlockId;
using betacraft::world::BlockRegistry;

namespace {
ItemStack tool(ToolType type, ToolMaterial mat) {
    return makeStack(toolItemId(type, mat), 1);
}

constexpr std::array<BlockId, 15> kOverridden{BlockId::Ice,       BlockId::Glass,      BlockId::Glowstone,
                                              BlockId::Leaves,    BlockId::Bookshelf,  BlockId::DiamondOre,
                                              BlockId::RedstoneOre, BlockId::SnowBlock,
                                              // S5.6-S5.8: seeds, the door and bed items, wheat
                                              BlockId::TallGrass, BlockId::WoodDoor,   BlockId::Bed,
                                              BlockId::Crops,
                                              // S6.6: flint from gravel
                                              BlockId::Gravel,
                                              // S8.5: redstone dust from wire, the iron door item
                                              BlockId::RedstoneWire, BlockId::IronDoor};

bool isOverridden(BlockId id) {
    for (const BlockId o : kOverridden) {
        if (o == id) {
            return true;
        }
    }
    return false;
}
} // namespace

TEST(BlockDrops, IceDropsNothingAndMeltsIntoWater) {
    for (uint64_t seed = 0; seed < 64; ++seed) {
        ItemStack hand{};
        const BlockBreakOutcome r = breakBlockWith(BlockId::Ice, hand, seed);
        EXPECT_TRUE(r.broken);
        EXPECT_FALSE(r.drop.has_value()) << seed;
        EXPECT_TRUE(r.meltsIntoWater);
    }
    ItemStack pick = tool(ToolType::Pickaxe, ToolMaterial::Diamond);
    const BlockBreakOutcome r = breakBlockWith(BlockId::Ice, pick);
    EXPECT_FALSE(r.drop.has_value());
    EXPECT_TRUE(r.meltsIntoWater);
    EXPECT_EQ(pick.damage, 1) << "the tool still wears";
}

TEST(BlockDrops, GlassAndBookshelfDropNothing) {
    for (const BlockId id : {BlockId::Glass, BlockId::Bookshelf}) {
        for (uint64_t seed = 0; seed < 64; ++seed) {
            ItemStack hand{};
            const BlockBreakOutcome r = breakBlockWith(id, hand, seed);
            EXPECT_TRUE(r.broken);
            EXPECT_FALSE(r.drop.has_value()) << BlockRegistry::get(id).name << " seed " << seed;
            EXPECT_FALSE(r.meltsIntoWater);
        }
    }
}

TEST(BlockDrops, GlowstoneDropsTwoToFourDust) {
    std::array<int, 5> histogram{};
    constexpr int kTrials = 3000;
    for (uint64_t seed = 0; seed < kTrials; ++seed) {
        ItemStack hand{};
        const BlockBreakOutcome r = breakBlockWith(BlockId::Glowstone, hand, seed);
        ASSERT_TRUE(r.drop.has_value()) << seed;
        EXPECT_EQ(r.drop->id, kGlowstoneDustItem);
        ASSERT_GE(r.drop->count, 2);
        ASSERT_LE(r.drop->count, 4);
        ++histogram[static_cast<size_t>(r.drop->count)];
    }
    for (int c = 2; c <= 4; ++c) {
        EXPECT_GT(histogram[static_cast<size_t>(c)], kTrials / 4) << "count " << c;
    }
}

TEST(BlockDrops, LeavesDropASaplingOneTimeInTwenty) {
    constexpr int kTrials = 20'000;
    int saplings = 0;
    for (uint64_t seed = 0; seed < kTrials; ++seed) {
        ItemStack hand{};
        const BlockBreakOutcome r = breakBlockWith(BlockId::Leaves, hand, seed);
        EXPECT_TRUE(r.broken);
        if (r.drop) {
            EXPECT_EQ(r.drop->id, toItemId(BlockId::Sapling));
            EXPECT_EQ(r.drop->count, 1);
            ++saplings;
        }
    }
    // Expected 1000; +-20% is > 6 sigma for a binomial(20000, 0.05).
    EXPECT_GT(saplings, 800);
    EXPECT_LT(saplings, 1200);
}

TEST(BlockDrops, SameSeedSameDrop) {
    for (uint64_t seed = 0; seed < 200; ++seed) {
        ItemStack a{};
        ItemStack b{};
        EXPECT_EQ(breakBlockWith(BlockId::Glowstone, a, seed).drop,
                  breakBlockWith(BlockId::Glowstone, b, seed).drop);
        EXPECT_EQ(breakBlockWith(BlockId::Leaves, a, seed).drop,
                  breakBlockWith(BlockId::Leaves, b, seed).drop);
    }
}

TEST(BlockDrops, PropertyOtherBlocksDropTheirRegistryItemOnce) {
    // Every other harvestable block keeps BlockDef::dropId x1, whatever the seed.
    const ItemStack diamondPick = tool(ToolType::Pickaxe, ToolMaterial::Diamond);
    const ItemStack diamondShovel = tool(ToolType::Shovel, ToolMaterial::Diamond);
    for (int i = 1; i < static_cast<int>(BlockId::Count); ++i) {
        const auto id = static_cast<BlockId>(i);
        if (isOverridden(id) || !harvestInfo(id).breakable()) {
            continue;
        }
        const BlockId expected = BlockRegistry::get(id).dropId;
        for (uint64_t seed : {0ull, 1ull, 0xDEADBEEFull}) {
            ItemStack held = harvestInfo(id).requiredTool == ToolType::Shovel ? diamondShovel : diamondPick;
            const BlockBreakOutcome r = breakBlockWith(id, held, seed);
            EXPECT_FALSE(r.meltsIntoWater) << BlockRegistry::get(id).name;
            if (expected == BlockId::Air) {
                EXPECT_FALSE(r.drop.has_value()) << BlockRegistry::get(id).name;
                continue;
            }
            ASSERT_TRUE(r.drop.has_value()) << BlockRegistry::get(id).name;
            // S2.10: legacy item BlockIds in dropId (coal, stick) drop as their Beta number.
            EXPECT_EQ(r.drop->id, canonicalize(toItemId(expected))) << BlockRegistry::get(id).name;
            EXPECT_EQ(r.drop->count, 1) << BlockRegistry::get(id).name;
        }
    }
}

TEST(BlockDrops, DropIdentityOfCommonBlocks) {
    struct Case {
        BlockId block;
        BlockId drop;
    };
    for (const Case c : {Case{BlockId::Stone, BlockId::Cobblestone}, Case{BlockId::Grass, BlockId::Dirt},
                         Case{BlockId::SnowGrass, BlockId::Dirt}, Case{BlockId::OakLog, BlockId::OakLog},
                         Case{BlockId::CoalOre, BlockId::CoalItem}, Case{BlockId::Sand, BlockId::Sand},
                         Case{BlockId::Cobblestone, BlockId::Cobblestone}}) {
        ItemStack pick = tool(ToolType::Pickaxe, ToolMaterial::Iron);
        const BlockBreakOutcome r = breakBlockWith(c.block, pick);
        ASSERT_TRUE(r.drop.has_value()) << BlockRegistry::get(c.block).name;
        EXPECT_EQ(r.drop->id, canonicalize(toItemId(c.drop))) << BlockRegistry::get(c.block).name;
    }
    ItemStack pick = tool(ToolType::Pickaxe, ToolMaterial::Iron);
    EXPECT_EQ(breakBlockWith(BlockId::CoalOre, pick).drop->id, items::Coal); // 263, not legacy 101
}

// S5.7 regression: tall grass no longer drops nothing, it gives seeds 1 time in 8 (Beta
// BlockTallGrass.idDropped: rand(8) == 0 ? seeds : nothing).
TEST(BlockDrops, TallGrassDropsSeedsOneTimeInEight) {
    constexpr int kTrials = 16'000;
    int seeds = 0;
    for (uint64_t seed = 0; seed < kTrials; ++seed) {
        ItemStack hand{};
        const BlockBreakOutcome r = breakBlockWith(BlockId::TallGrass, hand, seed);
        EXPECT_TRUE(r.broken);
        EXPECT_FALSE(r.bonusDrop.has_value());
        if (r.drop) {
            EXPECT_EQ(r.drop->id, items::Seeds);
            EXPECT_EQ(r.drop->count, 1);
            ++seeds;
        }
    }
    // Expected 2000; +-15% is > 5 sigma for a binomial(16000, 1/8).
    EXPECT_GT(seeds, 1700);
    EXPECT_LT(seeds, 2300);
}

// S5.6 / S5.8: each door or bed half drops the one item (the other half goes without one).
TEST(BlockDrops, DoorAndBedHalvesDropTheirItem) {
    for (const uint8_t meta : {uint8_t{0}, uint8_t{8}}) {
        ItemStack hand{};
        const BlockBreakOutcome door = breakBlockWith(BlockId::WoodDoor, hand, 3, meta);
        ASSERT_TRUE(door.drop.has_value());
        EXPECT_EQ(door.drop->id, items::WoodenDoor);
        EXPECT_EQ(door.drop->count, 1);
        const BlockBreakOutcome bed = breakBlockWith(BlockId::Bed, hand, 3, meta);
        ASSERT_TRUE(bed.drop.has_value());
        EXPECT_EQ(bed.drop->id, items::Bed);
        EXPECT_EQ(bed.drop->count, 1);
    }
}

TEST(BlockDrops, OverridesDoNotBypassHarvestLevels) {
    // Overrides only change *what* drops; the harvest-level gate still runs first.
    ItemStack woodPick = tool(ToolType::Pickaxe, ToolMaterial::Wood);
    EXPECT_FALSE(breakBlockWith(BlockId::DiamondOre, woodPick, 7).drop.has_value());
}

// S6.6 Beta BlockGravel.idDropped: flint 1 time in 10, the gravel itself otherwise.
TEST(BlockDrops, GravelDropsFlintOneTimeInTen) {
    constexpr int kTrials = 20'000;
    int flint = 0;
    for (uint64_t seed = 0; seed < kTrials; ++seed) {
        ItemStack hand{};
        const BlockBreakOutcome r = breakBlockWith(BlockId::Gravel, hand, seed);
        ASSERT_TRUE(r.drop.has_value());
        EXPECT_EQ(r.drop->count, 1);
        if (r.drop->id == items::Flint) {
            ++flint;
        } else {
            EXPECT_EQ(r.drop->id, toItemId(BlockId::Gravel));
        }
    }
    // Expected 2000; +-15% is > 6 sigma for a binomial(20000, 1/10).
    EXPECT_GT(flint, 1700);
    EXPECT_LT(flint, 2300);
    EXPECT_EQ(items::Flint, 318);
}

// S6.6 explosions drop what a harvest would, without the tool check.
TEST(BlockDrops, ExplosionDropsIgnoreTheTool) {
    const BlockBreakOutcome stone = explosionDrops(BlockId::Stone, 1);
    ASSERT_TRUE(stone.drop.has_value());
    EXPECT_EQ(stone.drop->id, toItemId(BlockId::Cobblestone));
    const BlockBreakOutcome ore = explosionDrops(BlockId::DiamondOre, 1);
    ASSERT_TRUE(ore.drop.has_value());
    EXPECT_EQ(ore.drop->id, items::Diamond);
    EXPECT_FALSE(explosionDrops(BlockId::Glass, 1).drop.has_value());
    EXPECT_FALSE(explosionDrops(BlockId::Water, 1).broken);
    EXPECT_FALSE(explosionDrops(BlockId::Air, 1).broken);
}
