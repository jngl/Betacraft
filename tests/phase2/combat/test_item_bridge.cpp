// Integration hook P2.5 x P2.6: field-by-field conversion between the inventory slot, the
// wearable gameplay::ItemStack and the crafting::SlotStack (game/ItemBridge.hpp), plus the
// inventory widened to the shared item id scheme.
#include "game/ItemBridge.hpp"

#include "crafting/ItemIds.hpp"
#include "gameplay/ItemIds.hpp"
#include "physics/Player.hpp"

#include <gtest/gtest.h>

using namespace betacraft;
using physics::InventorySlot;
using physics::Player;
using voxel::BlockId;
namespace items = gameplay::items;

// Cross-module agreement both libraries promised once linked together.
static_assert(crafting::maxDurability(gameplay::toolItemId(gameplay::ToolKind::Pickaxe, gameplay::ToolMaterial::Wood)) ==
              gameplay::materialStats(gameplay::ToolMaterial::Wood).maxUses);
static_assert(crafting::maxDurability(gameplay::toolItemId(gameplay::ToolKind::Sword, gameplay::ToolMaterial::Diamond)) ==
              gameplay::materialStats(gameplay::ToolMaterial::Diamond).maxUses);

TEST(ItemBridge, ToolsCarryTheirWearIntoTheCombatStack) {
    const gameplay::ItemStack s = game::toItemStack(InventorySlot{items::StoneSword, 1, 17});
    EXPECT_EQ(s.id, items::StoneSword);
    EXPECT_EQ(s.count, 1);
    EXPECT_EQ(s.damage, 17);
    EXPECT_EQ(s.maxDamage, 131);
    EXPECT_EQ(game::toInventorySlot(s), InventorySlot(items::StoneSword, 1, 17));
}

TEST(ItemBridge, BlocksHaveNoWearAndEmptyStaysEmpty) {
    const gameplay::ItemStack s = game::toItemStack(InventorySlot{BlockId::Dirt, 12});
    EXPECT_EQ(s.id, gameplay::toItemId(BlockId::Dirt));
    EXPECT_EQ(s.maxDamage, 0);
    EXPECT_TRUE(game::toItemStack(InventorySlot{}).empty());
    EXPECT_TRUE(game::toItemStack(InventorySlot{BlockId::Dirt, 0}).empty());
    EXPECT_TRUE(game::toInventorySlot(gameplay::ItemStack{}).isEmpty());
    EXPECT_TRUE(game::toInventorySlot(crafting::SlotStack{}).isEmpty());
}

TEST(ItemBridge, VanillaAliasesFoldOntoEngineIds) {
    EXPECT_EQ(game::toInventorySlot(crafting::SlotStack{items::beta::Stick, 4, 0}), InventorySlot(items::Stick, 4));
    EXPECT_EQ(game::toItemStack(InventorySlot{items::beta::WoodPickaxe, 1, 3}).id, items::WoodPickaxe);
    EXPECT_EQ(game::toInventorySlot(crafting::RecipeResult{items::beta::Coal, 2}), InventorySlot(items::Coal, 2));
}

TEST(ItemBridge, SlotStacksRoundTrip) {
    const InventorySlot pick{items::IronPickaxe, 1, 200};
    EXPECT_EQ(game::toInventorySlot(game::toSlotStack(pick)), pick);
    const InventorySlot glow{items::GlowstoneDust, 33};
    EXPECT_EQ(game::toInventorySlot(game::toSlotStack(glow)), glow);
}

TEST(WidenedInventory, ItemsAboveTheBlockRangeStackByTheirBetaLimit) {
    Player p;
    for (size_t i = 0; i < Player::TOTAL_INVENTORY_SIZE; ++i) p.setInventorySlot(i, {});
    EXPECT_TRUE(p.addItem(items::Snowball, 20)); // Beta: 16 per stack
    EXPECT_EQ(p.getInventorySlot(0), InventorySlot(items::Snowball, 16));
    EXPECT_EQ(p.getInventorySlot(1), InventorySlot(items::Snowball, 4));
    EXPECT_TRUE(p.addItem(items::GlowstoneDust, 3));
    EXPECT_EQ(p.countItem(items::GlowstoneDust), 3);
    EXPECT_TRUE(p.addItem(BlockId::Stone, 1));
    EXPECT_EQ(p.countItem(BlockId::Stone), 1);
    EXPECT_EQ(p.getInventorySlot(3).id, BlockId::Stone);
}

TEST(WidenedInventory, InsertStackMergesThenFillsAndReportsTheRest) {
    Player p;
    for (size_t i = 0; i < Player::TOTAL_INVENTORY_SIZE; ++i) p.setInventorySlot(i, {BlockId::Glass, 64});
    p.setInventorySlot(5, {BlockId::Sand, 60});
    p.setInventorySlot(9, {});
    EXPECT_EQ(p.insertStack({BlockId::Sand, 70}), 2); // 4 merged, 64 in the free slot
    EXPECT_EQ(p.getInventorySlot(5), InventorySlot(BlockId::Sand, 64));
    EXPECT_EQ(p.getInventorySlot(9), InventorySlot(BlockId::Sand, 64));
    EXPECT_EQ(p.insertStack({}), 0);
}

TEST(WidenedInventory, WornToolsNeverMerge) {
    Player p;
    for (size_t i = 0; i < Player::TOTAL_INVENTORY_SIZE; ++i) p.setInventorySlot(i, {});
    EXPECT_EQ(p.insertStack({items::WoodAxe, 1, 5}), 0);
    EXPECT_EQ(p.insertStack({items::WoodAxe, 1, 5}), 0);
    EXPECT_EQ(p.getInventorySlot(0), InventorySlot(items::WoodAxe, 1, 5));
    EXPECT_EQ(p.getInventorySlot(1), InventorySlot(items::WoodAxe, 1, 5));
    // Oversized tool stacks are split into single tools.
    EXPECT_EQ(p.insertStack({items::WoodAxe, 3}), 0);
    EXPECT_EQ(p.countItem(items::WoodAxe), 5);
    EXPECT_EQ(p.getInventorySlot(4), InventorySlot(items::WoodAxe, 1));
}

TEST(WidenedInventory, ItemIdsCompareWithBlockIds) {
    EXPECT_TRUE(gameplay::toItemId(BlockId::Torch) == BlockId::Torch);
    EXPECT_TRUE(BlockId::Torch == gameplay::toItemId(BlockId::Torch));
    EXPECT_TRUE(items::WoodSword != BlockId::Torch);
    EXPECT_TRUE(gameplay::kNoItem == BlockId::Air);
}
