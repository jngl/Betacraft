#include "gameplay/Harvesting.hpp"

#include <gtest/gtest.h>

using namespace betacraft::gameplay;
using betacraft::world::BlockId;

namespace {
ItemStack tool(ToolType type, ToolMaterial mat) {
    return makeStack(toolItemId(type, mat), 1);
}
} // namespace

TEST(BlockBreak, HarvestedBlockYieldsRegistryDrop) {
    ItemStack pick = tool(ToolType::Pickaxe, ToolMaterial::Stone);
    const BlockBreakOutcome r = breakBlockWith(BlockId::Stone, pick);
    EXPECT_TRUE(r.broken);
    ASSERT_TRUE(r.drop.has_value());
    EXPECT_EQ(r.drop->id, toItemId(betacraft::world::BlockRegistry::get(BlockId::Stone).dropId));
    EXPECT_EQ(r.drop->count, 1);
    EXPECT_EQ(r.toolWear, WearResult::Worn);
    EXPECT_EQ(pick.damage, 1);
}

TEST(BlockBreak, UnharvestedBlockYieldsNothingButStillWears) {
    ItemStack pick = tool(ToolType::Pickaxe, ToolMaterial::Wood);
    const BlockBreakOutcome r = breakBlockWith(BlockId::IronOre, pick);
    EXPECT_TRUE(r.broken);
    EXPECT_FALSE(r.drop.has_value());
    EXPECT_EQ(pick.damage, 1);
}

TEST(BlockBreak, HandMiningDirt) {
    ItemStack hand{};
    const BlockBreakOutcome r = breakBlockWith(BlockId::Grass, hand);
    ASSERT_TRUE(r.drop.has_value());
    EXPECT_EQ(r.drop->id, toItemId(BlockId::Dirt)) << "grass drops dirt (BlockDef::dropId)";
    EXPECT_EQ(r.toolWear, WearResult::Unchanged);
}

TEST(BlockBreak, UnbreakableBlockIsUntouched) {
    ItemStack pick = tool(ToolType::Pickaxe, ToolMaterial::Diamond);
    const BlockBreakOutcome r = breakBlockWith(BlockId::Bedrock, pick);
    EXPECT_FALSE(r.broken);
    EXPECT_FALSE(r.drop.has_value());
    EXPECT_EQ(r.toolWear, WearResult::Unchanged);
    EXPECT_EQ(pick.damage, 0);
}

TEST(BlockBreak, SwordWearsTwoPerBlock) {
    ItemStack sword = tool(ToolType::Sword, ToolMaterial::Iron);
    (void)breakBlockWith(BlockId::Leaves, sword);
    EXPECT_EQ(sword.damage, 2);
}

TEST(BlockBreak, WoodPickaxeMinesSixtyStoneThenBreaks) {
    // End-to-end: 60 uses (maxDamage 59, Beta breaks on damage > max), 23 ticks each.
    ItemStack pick = tool(ToolType::Pickaxe, ToolMaterial::Wood);
    int mined = 0;
    int64_t totalTicks = 0;
    int drops = 0;
    while (!pick.empty() && mined < 1000) {
        totalTicks += computeDig(BlockId::Stone, pick).ticks;
        const BlockBreakOutcome r = breakBlockWith(BlockId::Stone, pick);
        ++mined;
        drops += r.drop.has_value() ? 1 : 0;
        if (pick.empty()) {
            EXPECT_EQ(r.toolWear, WearResult::Broken);
        }
    }
    EXPECT_EQ(mined, 60);
    EXPECT_EQ(drops, 60) << "the breaking use still harvests the block";
    EXPECT_EQ(totalTicks, 60 * 23);
}

TEST(BlockBreak, DiamondOreProgression) {
    // Diagnostic: the iron pickaxe is the gate for diamonds.
    for (ToolMaterial m : {ToolMaterial::Wood, ToolMaterial::Stone, ToolMaterial::Gold}) {
        ItemStack pick = tool(ToolType::Pickaxe, m);
        EXPECT_FALSE(breakBlockWith(BlockId::DiamondOre, pick).drop.has_value());
    }
    for (ToolMaterial m : {ToolMaterial::Iron, ToolMaterial::Diamond}) {
        ItemStack pick = tool(ToolType::Pickaxe, m);
        EXPECT_TRUE(breakBlockWith(BlockId::DiamondOre, pick).drop.has_value());
    }
}
