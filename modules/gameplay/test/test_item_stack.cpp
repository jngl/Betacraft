#include "gameplay/ItemStack.hpp"

#include <gtest/gtest.h>

#include <random>

using namespace betacraft::gameplay;
using betacraft::voxel::BlockId;

namespace {
ItemStack tool(ToolType type, ToolMaterial mat) {
    return makeStack(toolItemId(type, mat), 1);
}
} // namespace

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

TEST(ItemStack, DefaultIsEmpty) {
    const ItemStack s{};
    EXPECT_TRUE(s.empty());
    EXPECT_FALSE(s.isDamageable());
}

TEST(ItemStack, MakeToolCarriesMaxDamage) {
    const ItemStack s = tool(ToolType::Pickaxe, ToolMaterial::Iron);
    EXPECT_FALSE(s.empty());
    EXPECT_EQ(s.count, 1);
    EXPECT_EQ(s.damage, 0);
    EXPECT_EQ(s.maxDamage, 250);
    EXPECT_TRUE(s.isDamageable());
}

TEST(ItemStack, MakeBlockStackIsNotDamageable) {
    const ItemStack s = makeStack(toItemId(BlockId::Cobblestone), 32);
    EXPECT_EQ(s.count, 32);
    EXPECT_EQ(s.maxDamage, 0);
    EXPECT_FALSE(s.isDamageable());
}

TEST(ItemStack, StackSizeLimits) {
    EXPECT_EQ(maxStackSize(toItemId(BlockId::Dirt)), 64);
    EXPECT_EQ(maxStackSize(toolItemId(ToolType::Sword, ToolMaterial::Wood)), 1);
    EXPECT_EQ(maxStackSize(kNoItem), 0);
}

TEST(ItemStack, BetaUnstackableItems) {
    // Beta 1.7.3 Item.maxStackSize = 1: buckets, cake, food, signs, doors, vehicles, armour...
    for (const ItemId id : {ItemId{325}, ItemId{326}, ItemId{327}, ItemId{335}, ItemId{354},
                            ItemId{260}, ItemId{282}, ItemId{297}, ItemId{319}, ItemId{320},
                            ItemId{322}, ItemId{349}, ItemId{350}, ItemId{323}, ItemId{324},
                            ItemId{330}, ItemId{328}, ItemId{329}, ItemId{333}, ItemId{342},
                            ItemId{343}, ItemId{355}, ItemId{358}, ItemId{359}, ItemId{259},
                            ItemId{261}, ItemId{346}, ItemId{298}, ItemId{317}, ItemId{2256},
                            ItemId{2257}}) {
        EXPECT_EQ(maxStackSize(id), 1) << id;
    }
    EXPECT_EQ(makeStack(325, 5).count, 1) << "bucket";
}

TEST(ItemStack, BetaSixteenAndEightStacks) {
    EXPECT_EQ(maxStackSize(344), 16) << "egg";
    EXPECT_EQ(maxStackSize(332), 16) << "snowball";
    EXPECT_EQ(maxStackSize(357), 8) << "cookie";
    EXPECT_EQ(makeStack(344, 64).count, 16);
}

TEST(ItemStack, OrdinaryItemsStackToSixtyFour) {
    for (const ItemId id : {ItemId{262}, ItemId{264}, ItemId{265}, ItemId{280}, ItemId{296},
                            ItemId{331}, ItemId{339}, ItemId{348}, ItemId{353}, ItemId{321}}) {
        EXPECT_EQ(maxStackSize(id), 64) << id;
    }
}

TEST(ItemStack, MakeStackClampsCount) {
    EXPECT_EQ(makeStack(toItemId(BlockId::Dirt), 100).count, 64);
    EXPECT_EQ(makeStack(toolItemId(ToolType::Axe, ToolMaterial::Stone), 5).count, 1);
    EXPECT_TRUE(makeStack(toItemId(BlockId::Dirt), 0).empty());
    EXPECT_TRUE(makeStack(toItemId(BlockId::Dirt), -3).empty());
    EXPECT_TRUE(makeStack(kNoItem, 10).empty());
}

TEST(ItemStack, MakeStackCanonicalisesVanillaToolIds) {
    const ItemStack s = makeStack(270, 1); // vanilla wooden pickaxe id
    EXPECT_EQ(s.id, toolItemId(ToolType::Pickaxe, ToolMaterial::Wood));
    EXPECT_EQ(s.maxDamage, 59);
}

TEST(ItemStack, EmptyStackIsNormalised) {
    const ItemStack s = makeStack(toItemId(BlockId::Dirt), -1);
    EXPECT_EQ(s, ItemStack{});
}

// ---------------------------------------------------------------------------
// Wear
// ---------------------------------------------------------------------------

TEST(ItemStackWear, DamageAccumulates) {
    ItemStack s = tool(ToolType::Shovel, ToolMaterial::Stone);
    EXPECT_EQ(s.applyWear(1), WearResult::Worn);
    EXPECT_EQ(s.applyWear(3), WearResult::Worn);
    EXPECT_EQ(s.damage, 4);
    EXPECT_EQ(s.remainingUses(), 131 + 1 - 4);
}

TEST(ItemStackWear, NonDamageableIgnoresWear) {
    ItemStack s = makeStack(toItemId(BlockId::Stone), 10);
    EXPECT_EQ(s.applyWear(5), WearResult::Unchanged);
    EXPECT_EQ(s.damage, 0);
    EXPECT_EQ(s.count, 10);
}

TEST(ItemStackWear, ZeroOrNegativeWearIsNoop) {
    ItemStack s = tool(ToolType::Sword, ToolMaterial::Iron);
    s.damage = 10;
    EXPECT_EQ(s.applyWear(0), WearResult::Unchanged);
    EXPECT_EQ(s.applyWear(-5), WearResult::Unchanged);
    EXPECT_EQ(s.damage, 10);
}

TEST(ItemStackWear, EmptyStackIgnoresWear) {
    ItemStack s{};
    EXPECT_EQ(s.applyWear(1), WearResult::Unchanged);
}

TEST(ItemStackWear, BreaksOnlyWhenDamageExceedsMax) {
    // Beta 1.7.3 ItemStack.damageItem: the stack breaks when damage > maxDamage, so a
    // wooden tool (maxDamage 59) survives damage == 59 and breaks on the 60th point.
    ItemStack s = tool(ToolType::Pickaxe, ToolMaterial::Wood);
    EXPECT_EQ(s.applyWear(59), WearResult::Worn);
    EXPECT_FALSE(s.empty());
    EXPECT_EQ(s.remainingUses(), 1);
    EXPECT_EQ(s.applyWear(1), WearResult::Broken);
    EXPECT_TRUE(s.empty());
    EXPECT_EQ(s, ItemStack{}) << "a broken single tool leaves an empty slot";
}

TEST(ItemStackWear, OverkillWearStillBreaksOnce) {
    ItemStack s = tool(ToolType::Pickaxe, ToolMaterial::Gold);
    EXPECT_EQ(s.applyWear(1000), WearResult::Broken);
    EXPECT_TRUE(s.empty());
}

TEST(ItemStackWear, BrokenStackOfManyLosesOneAndResetsDamage) {
    // Stacked damageable items (only reachable via external data) lose one item per break.
    ItemStack s = tool(ToolType::Hoe, ToolMaterial::Iron);
    s.count = 3;
    EXPECT_EQ(s.applyWear(251), WearResult::Broken);
    EXPECT_EQ(s.count, 2);
    EXPECT_EQ(s.damage, 0);
    EXPECT_FALSE(s.empty());
}

TEST(ItemStackWear, DurabilityFraction) {
    ItemStack s = tool(ToolType::Pickaxe, ToolMaterial::Diamond);
    EXPECT_FLOAT_EQ(s.durabilityFraction(), 1.0f);
    s.damage = 1561;
    EXPECT_FLOAT_EQ(s.durabilityFraction(), 0.0f);
    s.damage = 780;
    EXPECT_NEAR(s.durabilityFraction(), 1.0f - 780.0f / 1561.0f, 1e-6f);
    const ItemStack block = makeStack(toItemId(BlockId::Dirt), 1);
    EXPECT_FLOAT_EQ(block.durabilityFraction(), 1.0f);
}

TEST(ItemStackWear, RemainingUsesForNonDamageableIsZero) {
    EXPECT_EQ(makeStack(toItemId(BlockId::Dirt), 4).remainingUses(), 0);
}

TEST(ItemStackWear, EveryMaterialLastsMaxUsesPlusOne) {
    for (ToolMaterial mat : {ToolMaterial::Wood, ToolMaterial::Stone, ToolMaterial::Iron,
                             ToolMaterial::Gold, ToolMaterial::Diamond}) {
        ItemStack s = tool(ToolType::Axe, mat);
        int32_t uses = 0;
        while (!s.empty()) {
            (void)s.applyWear(1);
            ++uses;
            ASSERT_LE(uses, 2000);
        }
        EXPECT_EQ(uses, materialStats(mat).maxUses + 1);
    }
}

TEST(ItemStackWear, PropertyDamageNeverExceedsMaxWhileAlive) {
    std::mt19937 rng(0xC0FFEEu);
    std::uniform_int_distribution<int32_t> amount(-3, 40);
    for (int trial = 0; trial < 200; ++trial) {
        ItemStack s = tool(ToolType::Sword, ToolMaterial::Stone);
        int32_t total = 0;
        int guard = 0;
        while (!s.empty() && ++guard < 10'000) {
            const int32_t a = amount(rng);
            const WearResult r = s.applyWear(a);
            if (a > 0) {
                total += a;
            }
            if (r == WearResult::Broken) {
                EXPECT_GT(total, materialStats(ToolMaterial::Stone).maxUses);
                break;
            }
            ASSERT_GE(s.damage, 0);
            ASSERT_LE(s.damage, s.maxDamage);
            ASSERT_EQ(s.damage, total);
        }
        EXPECT_TRUE(s.empty());
    }
}

// ---------------------------------------------------------------------------
// Beta wear rules per action
// ---------------------------------------------------------------------------

TEST(ToolWearRules, BlockBreakCost) {
    EXPECT_EQ(wearForBlockBreak(ToolType::Pickaxe), 1);
    EXPECT_EQ(wearForBlockBreak(ToolType::Axe), 1);
    EXPECT_EQ(wearForBlockBreak(ToolType::Shovel), 1);
    EXPECT_EQ(wearForBlockBreak(ToolType::Sword), 2);
    EXPECT_EQ(wearForBlockBreak(ToolType::Hoe), 0) << "Beta hoes only wear when tilling";
    EXPECT_EQ(wearForBlockBreak(ToolType::None), 0);
}

TEST(ToolWearRules, EntityHitCost) {
    EXPECT_EQ(wearForEntityHit(ToolType::Sword), 1);
    EXPECT_EQ(wearForEntityHit(ToolType::Pickaxe), 2);
    EXPECT_EQ(wearForEntityHit(ToolType::Axe), 2);
    EXPECT_EQ(wearForEntityHit(ToolType::Shovel), 2);
    EXPECT_EQ(wearForEntityHit(ToolType::Hoe), 0);
    EXPECT_EQ(wearForEntityHit(ToolType::None), 0);
}

TEST(ToolWearRules, ApplyBlockBreakWearToHeldStack) {
    ItemStack sword = tool(ToolType::Sword, ToolMaterial::Wood);
    EXPECT_EQ(onBlockBroken(sword), WearResult::Worn);
    EXPECT_EQ(sword.damage, 2);
    ItemStack pick = tool(ToolType::Pickaxe, ToolMaterial::Wood);
    EXPECT_EQ(onBlockBroken(pick), WearResult::Worn);
    EXPECT_EQ(pick.damage, 1);
    ItemStack dirt = makeStack(toItemId(BlockId::Dirt), 3);
    EXPECT_EQ(onBlockBroken(dirt), WearResult::Unchanged);
}

TEST(ToolWearRules, ApplyEntityHitWearToHeldStack) {
    ItemStack sword = tool(ToolType::Sword, ToolMaterial::Gold);
    EXPECT_EQ(onEntityHit(sword), WearResult::Worn);
    EXPECT_EQ(sword.damage, 1);
    ItemStack axe = tool(ToolType::Axe, ToolMaterial::Gold);
    EXPECT_EQ(onEntityHit(axe), WearResult::Worn);
    EXPECT_EQ(axe.damage, 2);
}

TEST(ToolWearRules, TillingWearsOnlyHoes) {
    ItemStack hoe = tool(ToolType::Hoe, ToolMaterial::Wood);
    EXPECT_EQ(onSoilTilled(hoe), WearResult::Worn);
    EXPECT_EQ(hoe.damage, 1);
    ItemStack shovel = tool(ToolType::Shovel, ToolMaterial::Wood);
    EXPECT_EQ(onSoilTilled(shovel), WearResult::Unchanged);
}

TEST(ToolWearRules, GoldSwordBreaksAfterSixteenBlocks) {
    // maxDamage 32, 2 per block -> the 17th block pushes damage to 34 > 32.
    ItemStack sword = tool(ToolType::Sword, ToolMaterial::Gold);
    int blocks = 0;
    while (!sword.empty()) {
        (void)onBlockBroken(sword);
        ++blocks;
        ASSERT_LE(blocks, 100);
    }
    EXPECT_EQ(blocks, 17);
}
