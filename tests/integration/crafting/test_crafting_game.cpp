// Crafting grids in the game: the inventory screen logic
// (game::InventoryCrafting) replaces the legacy ui::CraftingSystem recipe list with
// crafting::RecipeRegistry over a 2x2 grid (player inventory) and a 3x3 grid (workbench),
// driven by Beta's slot clicks, on an inventory widened to item ids so tools can be held.
#include "crafting/ItemIds.hpp"
#include "game/InventoryCrafting.hpp"
#include "gameplay/ItemIds.hpp"
#include "physics/Player.hpp"
#include "voxel/Block.hpp"

#include <gtest/gtest.h>

using namespace betacraft;
using crafting::GridSize;
using game::InventoryCrafting;
using game::SlotButton;
using physics::InventorySlot;
using physics::Player;
using gameplay::toItemId;
using voxel::BlockId;
namespace items = gameplay::items;

namespace {

void clearInventory(Player& p) {
    for (size_t i = 0; i < Player::TOTAL_INVENTORY_SIZE; ++i) {
        p.setInventorySlot(i, {});
    }
}

// Inventory slot holding `id`, or TOTAL_INVENTORY_SIZE.
size_t findSlot(const Player& p, gameplay::ItemId id) {
    for (size_t i = 0; i < Player::TOTAL_INVENTORY_SIZE; ++i) {
        if (p.getInventorySlot(i).id == id && !p.getInventorySlot(i).isEmpty()) return i;
    }
    return Player::TOTAL_INVENTORY_SIZE;
}

size_t firstEmpty(const Player& p) {
    for (size_t i = 0; i < Player::TOTAL_INVENTORY_SIZE; ++i) {
        if (p.getInventorySlot(i).isEmpty()) return i;
    }
    return Player::TOTAL_INVENTORY_SIZE;
}

// Puts the cursor stack back into the first empty inventory slot.
void stash(InventoryCrafting& ui, Player& p) {
    ASSERT_FALSE(ui.carried().isEmpty());
    const size_t slot = firstEmpty(p);
    ASSERT_LT(slot, Player::TOTAL_INVENTORY_SIZE);
    ASSERT_TRUE(ui.clickInventorySlot(p, slot));
    ASSERT_TRUE(ui.carried().isEmpty());
}

// Drops one item of inventory stack `id` into grid cell `index` (pick up, right-click, put back).
void placeOne(InventoryCrafting& ui, Player& p, gameplay::ItemId id, size_t index) {
    const size_t from = findSlot(p, id);
    ASSERT_LT(from, Player::TOTAL_INVENTORY_SIZE) << "no " << id << " left";
    ASSERT_TRUE(ui.clickInventorySlot(p, from));
    ASSERT_TRUE(ui.clickGridSlot(index, SlotButton::Right));
    if (!ui.carried().isEmpty()) {
        ASSERT_TRUE(ui.clickInventorySlot(p, from));
    }
    ASSERT_TRUE(ui.carried().isEmpty());
}

class GameplayCrafting : public ::testing::Test {
protected:
    void SetUp() override {
        voxel::BlockRegistry::initialize();
        clearInventory(m_player);
    }

    Player m_player;
    InventoryCrafting m_ui;
};

} // namespace

TEST_F(GameplayCrafting, WoodenPickaxeFromLogsThroughTheInventoryAndWorkbenchGrids) {
    ASSERT_TRUE(m_player.addItem(BlockId::OakLog, 3));
    (void)m_ui.open(GridSize::Inventory2x2, m_player);
    ASSERT_EQ(m_ui.gridSize(), GridSize::Inventory2x2);

    // 3 logs in one 2x2 cell -> 4 planks per take, three takes stack on the cursor.
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, findSlot(m_player, toItemId(BlockId::OakLog))));
    ASSERT_TRUE(m_ui.clickGridSlot(0));
    EXPECT_EQ(m_ui.resultPreview(), InventorySlot(BlockId::OakPlanks, 4));
    for (int i = 0; i < 3; ++i) {
        ASSERT_TRUE(m_ui.clickResult(m_player)) << "take " << i;
    }
    EXPECT_EQ(m_ui.carried(), InventorySlot(BlockId::OakPlanks, 12));
    EXPECT_TRUE(m_ui.gridSlot(0).isEmpty());
    EXPECT_TRUE(m_ui.resultPreview().isEmpty());
    EXPECT_FALSE(m_ui.clickResult(m_player));
    stash(m_ui, m_player);

    // 2 planks stacked vertically -> 4 sticks.
    placeOne(m_ui, m_player, toItemId(BlockId::OakPlanks), 0);
    placeOne(m_ui, m_player, toItemId(BlockId::OakPlanks), 2);
    EXPECT_EQ(m_ui.resultPreview(), InventorySlot(items::Stick, 4));
    ASSERT_TRUE(m_ui.clickResult(m_player));
    stash(m_ui, m_player);
    EXPECT_EQ(m_player.countItem(items::Stick), 4);
    EXPECT_EQ(m_player.countItem(BlockId::OakPlanks), 10);

    // The pickaxe pattern does not fit a 2x2 grid: it needs the workbench.
    (void)m_ui.open(GridSize::Workbench3x3, m_player);
    ASSERT_EQ(m_ui.gridSize(), GridSize::Workbench3x3);
    for (size_t x = 0; x < 3; ++x) {
        placeOne(m_ui, m_player, toItemId(BlockId::OakPlanks), x);
    }
    placeOne(m_ui, m_player, items::Stick, 4);
    placeOne(m_ui, m_player, items::Stick, 7);
    const InventorySlot preview = m_ui.resultPreview();
    EXPECT_EQ(preview.id, items::WoodPickaxe);
    EXPECT_EQ(preview.count, 1);
    ASSERT_TRUE(m_ui.clickResult(m_player));
    EXPECT_EQ(m_ui.carried(), InventorySlot(items::WoodPickaxe, 1));
    for (size_t i = 0; i < 9; ++i) {
        EXPECT_TRUE(m_ui.gridSlot(i).isEmpty()) << i;
    }

    // Into the hotbar, held as a real tool.
    const size_t hand = firstEmpty(m_player);
    ASSERT_LT(hand, Player::HOTBAR_SIZE);
    m_player.setSelectedSlot(static_cast<int32_t>(hand));
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, hand));
    EXPECT_EQ(m_player.getHeldItem(), InventorySlot(items::WoodPickaxe, 1));
    EXPECT_EQ(m_player.countItem(items::Stick), 2);
    EXPECT_EQ(m_player.countItem(BlockId::OakPlanks), 7);
    EXPECT_TRUE(m_ui.close(m_player).empty());
}

TEST_F(GameplayCrafting, ToolsAboveTheBlockRangeCanBeCraftedAndHeld) {
    // A wooden sword (vanilla 268) has no BlockId: the widened inventory holds it.
    ASSERT_TRUE(m_player.addItem(BlockId::OakPlanks, 2));
    ASSERT_TRUE(m_player.addItem(items::Stick, 1));
    (void)m_ui.open(GridSize::Workbench3x3, m_player);
    placeOne(m_ui, m_player, toItemId(BlockId::OakPlanks), 1);
    placeOne(m_ui, m_player, toItemId(BlockId::OakPlanks), 4);
    placeOne(m_ui, m_player, items::Stick, 7);
    EXPECT_EQ(m_ui.resultPreview(), InventorySlot(items::WoodSword, 1));
    ASSERT_TRUE(m_ui.clickResult(m_player));
    stash(m_ui, m_player);
    EXPECT_EQ(m_player.countItem(items::WoodSword), 1);
    EXPECT_FALSE(m_player.addItem(items::WoodSword, 0));
    // Tools never stack, even in the widened inventory.
    ASSERT_TRUE(m_player.addItem(items::WoodSword, 1));
    const size_t first = findSlot(m_player, items::WoodSword);
    EXPECT_EQ(m_player.getInventorySlot(first).count, 1);
    EXPECT_EQ(m_player.countItem(items::WoodSword), 2);
}

TEST_F(GameplayCrafting, ClosingTheScreenReturnsTheGridAndCursorToTheInventory) {
    ASSERT_TRUE(m_player.addItem(BlockId::Cobblestone, 10));
    (void)m_ui.open(GridSize::Workbench3x3, m_player);
    placeOne(m_ui, m_player, toItemId(BlockId::Cobblestone), 4);
    placeOne(m_ui, m_player, toItemId(BlockId::Cobblestone), 8);
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, findSlot(m_player, toItemId(BlockId::Cobblestone))));
    EXPECT_EQ(m_ui.carried(), InventorySlot(BlockId::Cobblestone, 8));
    EXPECT_EQ(m_player.countItem(BlockId::Cobblestone), 0);

    EXPECT_TRUE(m_ui.close(m_player).empty());
    EXPECT_EQ(m_player.countItem(BlockId::Cobblestone), 10);
    EXPECT_TRUE(m_ui.carried().isEmpty());
    EXPECT_TRUE(m_ui.grid().isEmpty());
}

TEST_F(GameplayCrafting, ClosingWithAFullInventoryHandsBackTheOverflow) {
    for (size_t i = 0; i < Player::TOTAL_INVENTORY_SIZE; ++i) {
        m_player.setInventorySlot(i, {BlockId::Dirt, 64});
    }
    m_player.setInventorySlot(0, {BlockId::Sand, 64});
    (void)m_ui.open(GridSize::Inventory2x2, m_player);
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, 0));
    ASSERT_TRUE(m_ui.clickGridSlot(3));
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, 1)); // cursor now holds 64 dirt
    m_player.setInventorySlot(0, {BlockId::Gravel, 64});
    m_player.setInventorySlot(1, {BlockId::Gravel, 64});

    const std::vector<InventorySlot> overflow = m_ui.close(m_player);
    int32_t sand = 0;
    int32_t dirt = 0;
    for (const InventorySlot& s : overflow) {
        if (s.id == BlockId::Sand) sand += s.count;
        if (s.id == BlockId::Dirt) dirt += s.count;
    }
    EXPECT_EQ(sand, 64);
    EXPECT_EQ(dirt, 64);
}
