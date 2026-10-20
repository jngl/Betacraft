// Integration hook P2.6 (UI): game::InventoryCrafting is the inventory screen logic over a
// CraftingGrid 2x2 (inventory) or 3x3 (workbench), the RecipeRegistry preview and the
// cursor stack, with Beta Container.slotClick rules for left and right clicks.
#include "game/InventoryCrafting.hpp"

#include "crafting/ItemIds.hpp"
#include "gameplay/ItemIds.hpp"
#include "gameplay/SplitMix64.hpp"
#include "physics/Player.hpp"
#include "tests/test_helpers.hpp"
#include "voxel/BlockEntityStore.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <variant>

using namespace betacraft;
using crafting::GridSize;
using game::InventoryCrafting;
using game::SlotButton;
using physics::InventorySlot;
using physics::Player;
using world::BlockId;
namespace items = gameplay::items;
using gameplay::toItemId;

namespace {

class InventoryCraftingTest : public ::testing::Test {
protected:
    void SetUp() override {
        for (size_t i = 0; i < Player::TOTAL_INVENTORY_SIZE; ++i) m_player.setInventorySlot(i, {});
    }

    Player m_player;
    InventoryCrafting m_ui;
};

} // namespace

TEST_F(InventoryCraftingTest, OpensAsTheInventoryGridAndSwitchesToTheWorkbench) {
    EXPECT_EQ(m_ui.gridSize(), GridSize::Inventory2x2);
    EXPECT_TRUE(m_ui.open(GridSize::Workbench3x3, m_player).empty());
    EXPECT_EQ(m_ui.gridSize(), GridSize::Workbench3x3);
    EXPECT_EQ(m_ui.grid().slotCount(), 9U);
}

TEST_F(InventoryCraftingTest, LeftClickPicksUpPutsDownMergesAndSwaps) {
    m_player.setInventorySlot(0, {BlockId::Dirt, 40});
    m_player.setInventorySlot(1, {BlockId::Dirt, 30});
    m_player.setInventorySlot(2, {BlockId::Sand, 5});

    EXPECT_TRUE(m_ui.clickInventorySlot(m_player, 0)); // pick up 40
    EXPECT_EQ(m_ui.carried(), InventorySlot(BlockId::Dirt, 40));
    EXPECT_TRUE(m_player.getInventorySlot(0).isEmpty());

    EXPECT_TRUE(m_ui.clickInventorySlot(m_player, 1)); // merge up to 64
    EXPECT_EQ(m_player.getInventorySlot(1), InventorySlot(BlockId::Dirt, 64));
    EXPECT_EQ(m_ui.carried(), InventorySlot(BlockId::Dirt, 6));

    EXPECT_TRUE(m_ui.clickInventorySlot(m_player, 2)); // different item: swap
    EXPECT_EQ(m_player.getInventorySlot(2), InventorySlot(BlockId::Dirt, 6));
    EXPECT_EQ(m_ui.carried(), InventorySlot(BlockId::Sand, 5));

    EXPECT_TRUE(m_ui.clickInventorySlot(m_player, 0)); // put down into the empty slot
    EXPECT_EQ(m_player.getInventorySlot(0), InventorySlot(BlockId::Sand, 5));
    EXPECT_TRUE(m_ui.carried().isEmpty());
    EXPECT_FALSE(m_ui.clickInventorySlot(m_player, 3)); // empty on empty
    EXPECT_FALSE(m_ui.clickInventorySlot(m_player, Player::TOTAL_INVENTORY_SIZE));
}

TEST_F(InventoryCraftingTest, RightClickPicksUpHalfAndPutsDownOne) {
    m_player.setInventorySlot(0, {BlockId::Gravel, 7});
    EXPECT_TRUE(m_ui.clickInventorySlot(m_player, 0, SlotButton::Right));
    EXPECT_EQ(m_ui.carried(), InventorySlot(BlockId::Gravel, 4)); // Beta: the larger half
    EXPECT_EQ(m_player.getInventorySlot(0), InventorySlot(BlockId::Gravel, 3));

    EXPECT_TRUE(m_ui.clickGridSlot(3, SlotButton::Right));
    EXPECT_TRUE(m_ui.clickGridSlot(3, SlotButton::Right));
    EXPECT_EQ(m_ui.gridSlot(3), InventorySlot(BlockId::Gravel, 2));
    EXPECT_EQ(m_ui.carried(), InventorySlot(BlockId::Gravel, 2));
    EXPECT_FALSE(m_ui.clickGridSlot(4, SlotButton::Right)); // outside the 2x2 grid
}

TEST_F(InventoryCraftingTest, ToolsAreClampedToOnePerSlot) {
    m_player.setInventorySlot(0, {items::WoodSword, 1});
    m_player.setInventorySlot(1, {items::WoodSword, 1, 9});
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, 0));
    EXPECT_TRUE(m_ui.clickInventorySlot(m_player, 1)); // no merge for unstackables: swap
    EXPECT_EQ(m_ui.carried(), InventorySlot(items::WoodSword, 1, 9));
    EXPECT_EQ(m_player.getInventorySlot(1), InventorySlot(items::WoodSword, 1));
}

TEST_F(InventoryCraftingTest, TheResultPreviewFollowsTheGrid) {
    m_player.setInventorySlot(0, {BlockId::OakPlanks, 4});
    EXPECT_TRUE(m_ui.resultPreview().isEmpty());
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, 0));
    for (size_t i = 0; i < 4; ++i) {
        ASSERT_TRUE(m_ui.clickGridSlot(i, SlotButton::Right));
    }
    EXPECT_EQ(m_ui.resultPreview(), InventorySlot(BlockId::CraftingTable, 1));
    ASSERT_TRUE(m_ui.clickResult(m_player));
    EXPECT_EQ(m_ui.carried(), InventorySlot(BlockId::CraftingTable, 1));
    EXPECT_TRUE(m_ui.grid().isEmpty());
}

TEST_F(InventoryCraftingTest, TheResultIsRefusedWhenTheCursorCannotTakeIt) {
    m_player.setInventorySlot(0, {BlockId::OakLog, 2});
    m_player.setInventorySlot(1, {BlockId::Dirt, 1});
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, 0));
    ASSERT_TRUE(m_ui.clickGridSlot(0));
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, 1)); // cursor: dirt
    EXPECT_FALSE(m_ui.clickResult(m_player));
    EXPECT_EQ(m_ui.gridSlot(0), InventorySlot(BlockId::OakLog, 2)); // nothing consumed
    EXPECT_EQ(m_ui.carried(), InventorySlot(BlockId::Dirt, 1));

    // 62 planks on the cursor: a 4-plank result does not fit the stack either.
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, 1));
    m_player.setInventorySlot(2, {BlockId::OakPlanks, 62});
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, 2));
    EXPECT_FALSE(m_ui.clickResult(m_player));
}

TEST_F(InventoryCraftingTest, CloseReturnsEverythingAndOpenEmptiesTheOldGrid) {
    m_player.setInventorySlot(0, {BlockId::Cobblestone, 3});
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, 0));
    ASSERT_TRUE(m_ui.clickGridSlot(0, SlotButton::Right));
    EXPECT_TRUE(m_ui.open(GridSize::Workbench3x3, m_player).empty());
    EXPECT_TRUE(m_ui.grid().isEmpty());
    EXPECT_TRUE(m_ui.carried().isEmpty());
    EXPECT_EQ(m_player.countItem(BlockId::Cobblestone), 3);
}

// --- S2.6: furnace and chest screens over World::blockEntities() ---

namespace {

constexpr core::BlockPos kContainerPos{4, 100, 4};

class ContainerScreenTest : public InventoryCraftingTest {
protected:
    void SetUp() override {
        InventoryCraftingTest::SetUp();
        world::BlockRegistry::initialize();
        m_world = std::make_unique<world::World>(1234ULL, m_dir.path());
        (void)m_world->ensureChunkLoaded({0, 0});
    }

    world::FurnaceData& placeFurnace() {
        EXPECT_TRUE(m_world->setBlock(kContainerPos, BlockId::Furnace));
        auto& f = std::get<world::FurnaceData>(m_world->blockEntities().getOrCreate(kContainerPos, BlockId::Furnace));
        EXPECT_TRUE(m_ui.openContainer(ui::ScreenKind::Furnace, kContainerPos, m_player).empty());
        return f;
    }

    world::ChestData& placeChest() {
        EXPECT_TRUE(m_world->setBlock(kContainerPos, BlockId::Chest));
        auto& c = std::get<world::ChestData>(m_world->blockEntities().getOrCreate(kContainerPos, BlockId::Chest));
        EXPECT_TRUE(m_ui.openContainer(ui::ScreenKind::Chest, kContainerPos, m_player).empty());
        return c;
    }

    test::TempDir m_dir{"betacraft_container_screen_"};
    std::unique_ptr<world::World> m_world;
};

} // namespace

TEST_F(ContainerScreenTest, FurnaceOutputIsTakeOnly) {
    world::FurnaceData& f = placeFurnace();
    EXPECT_EQ(m_ui.kind(), ui::ScreenKind::Furnace);
    ASSERT_TRUE(m_ui.containerPos().has_value());
    EXPECT_EQ(m_ui.containerSlotCount(), 3U);

    // Ore into the input, coal into the fuel slot: any item goes there (Beta Slot).
    m_player.setInventorySlot(0, {BlockId::IronOre, 8});
    m_player.setInventorySlot(1, {items::Coal, 1});
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, 0));
    EXPECT_TRUE(m_ui.clickContainerSlot(*m_world, ui::kFurnaceInputSlot));
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, 1));
    EXPECT_TRUE(m_ui.clickContainerSlot(*m_world, ui::kFurnaceFuelSlot));
    EXPECT_EQ(f.input, (world::StoredStack{toItemId(BlockId::IronOre), 8, 0}));
    EXPECT_EQ(f.fuel, (world::StoredStack{items::Coal, 1, 0}));
    EXPECT_EQ(m_ui.containerSlot(*m_world, ui::kFurnaceInputSlot), InventorySlot(BlockId::IronOre, 8));

    // Nothing can be put down in (or swapped into) the output.
    m_player.setInventorySlot(2, {items::IronIngot, 5});
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, 2));
    EXPECT_FALSE(m_ui.clickContainerSlot(*m_world, ui::kFurnaceOutputSlot));
    EXPECT_FALSE(m_ui.clickContainerSlot(*m_world, ui::kFurnaceOutputSlot, SlotButton::Right));
    EXPECT_TRUE(f.output.empty());
    f.output = {toItemId(BlockId::Stone), 3, 0};
    EXPECT_FALSE(m_ui.clickContainerSlot(*m_world, ui::kFurnaceOutputSlot)); // no swap
    EXPECT_EQ(m_ui.carried(), InventorySlot(items::IronIngot, 5));

    // A matching cursor stack takes the whole output; an empty cursor picks it up.
    f.output = {items::IronIngot, 3, 0};
    EXPECT_TRUE(m_ui.clickContainerSlot(*m_world, ui::kFurnaceOutputSlot));
    EXPECT_EQ(m_ui.carried(), InventorySlot(items::IronIngot, 8));
    EXPECT_TRUE(f.output.empty());
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, 2));
    f.output = {items::IronIngot, 6, 0};
    EXPECT_TRUE(m_ui.clickContainerSlot(*m_world, ui::kFurnaceOutputSlot, SlotButton::Right));
    EXPECT_EQ(m_ui.carried(), InventorySlot(items::IronIngot, 3));
    EXPECT_EQ(f.output, (world::StoredStack{items::IronIngot, 3, 0}));
    EXPECT_FALSE(m_ui.clickContainerSlot(*m_world, ui::kFurnaceSlotCount)); // out of range
}

TEST_F(ContainerScreenTest, ChestSlotsMergeAndSwapLikeInventory) {
    world::ChestData& c = placeChest();
    EXPECT_EQ(m_ui.containerSlotCount(), world::kChestSlots);
    c.slots[0] = {toItemId(BlockId::Dirt), 30, 0};
    c.slots[26] = {toItemId(BlockId::Sand), 5, 0};
    m_player.setInventorySlot(0, {BlockId::Dirt, 40});
    m_world->getChunkMutable({0, 0})->setModified(false);

    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, 0));                // pick up 40 dirt
    EXPECT_TRUE(m_ui.clickContainerSlot(*m_world, 0));                // merge up to 64
    EXPECT_EQ(c.slots[0], (world::StoredStack{toItemId(BlockId::Dirt), 64, 0}));
    EXPECT_EQ(m_ui.carried(), InventorySlot(BlockId::Dirt, 6));
    EXPECT_TRUE(m_ui.clickContainerSlot(*m_world, 26));               // different item: swap
    EXPECT_EQ(c.slots[26], (world::StoredStack{toItemId(BlockId::Dirt), 6, 0}));
    EXPECT_EQ(m_ui.carried(), InventorySlot(BlockId::Sand, 5));
    EXPECT_TRUE(m_ui.clickContainerSlot(*m_world, 13, SlotButton::Right)); // put down one
    EXPECT_EQ(c.slots[13], (world::StoredStack{toItemId(BlockId::Sand), 1, 0}));
    EXPECT_EQ(m_ui.carried(), InventorySlot(BlockId::Sand, 4));
    EXPECT_FALSE(m_ui.clickContainerSlot(*m_world, world::kChestSlots));

    // Every change marked the chunk for saving.
    EXPECT_TRUE(m_world->getChunk({0, 0})->isModified());
    // Closing returns the cursor to the player and leaves the chest as it is.
    EXPECT_TRUE(m_ui.close(m_player).empty());
    EXPECT_FALSE(m_ui.containerPos().has_value());
    EXPECT_EQ(m_player.countItem(BlockId::Sand), 4);
    EXPECT_EQ(c.slots[0].count, 64);
}

TEST_F(ContainerScreenTest, ContainerClickAfterBlockRemovedClosesScreen) {
    world::ChestData& c = placeChest();
    c.slots[3] = {toItemId(BlockId::Cobblestone), 10, 0};
    ASSERT_TRUE(m_world->setBlock(kContainerPos, BlockId::Air));
    EXPECT_EQ(m_world->blockEntities().find(kContainerPos), nullptr);

    EXPECT_FALSE(m_ui.clickContainerSlot(*m_world, 3));
    EXPECT_FALSE(m_ui.containerPos().has_value());
    EXPECT_EQ(m_ui.kind(), ui::ScreenKind::Inventory2x2);
    EXPECT_TRUE(m_ui.carried().isEmpty()); // nothing was duplicated out of the dropped chest
    EXPECT_TRUE(m_ui.containerSlot(*m_world, 3).isEmpty());
}

// S3.4: a click off the panel throws the cursor stack (left) or one item of it (right).
TEST_F(InventoryCraftingTest, TakeCarriedThrowsWholeStackOrOneItem) {
    EXPECT_TRUE(m_ui.takeCarried(SlotButton::Left).isEmpty()) << "nothing carried";
    m_player.setInventorySlot(0, {BlockId::Dirt, 12});
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, 0));
    EXPECT_EQ(m_ui.takeCarried(SlotButton::Right), InventorySlot(BlockId::Dirt, 1));
    EXPECT_EQ(m_ui.carried(), InventorySlot(BlockId::Dirt, 11));
    EXPECT_EQ(m_ui.takeCarried(SlotButton::Left), InventorySlot(BlockId::Dirt, 11));
    EXPECT_TRUE(m_ui.carried().isEmpty());

    m_player.setInventorySlot(1, {items::StonePickaxe, 1, 40});
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, 1));
    EXPECT_EQ(m_ui.takeCarried(SlotButton::Right), InventorySlot(items::StonePickaxe, 1, 40)) << "wear kept";
    EXPECT_TRUE(m_ui.carried().isEmpty());
    EXPECT_TRUE(m_ui.pendingStacks().empty());
}

// --- S3.5: shift-click (Beta transferStackInSlot) ---

TEST_F(InventoryCraftingTest, ShiftClickHotbarToMainAndBack) {
    m_player.setInventorySlot(2, {BlockId::Dirt, 20});
    m_player.setInventorySlot(15, {BlockId::Dirt, 60});
    EXPECT_TRUE(m_ui.shiftClickInventorySlot(m_player, nullptr, 2));
    // Merges into the main inventory's dirt first, then the first empty main slot.
    EXPECT_EQ(m_player.getInventorySlot(15), InventorySlot(BlockId::Dirt, 64));
    EXPECT_EQ(m_player.getInventorySlot(9), InventorySlot(BlockId::Dirt, 16));
    EXPECT_TRUE(m_player.getInventorySlot(2).isEmpty());

    EXPECT_TRUE(m_ui.shiftClickInventorySlot(m_player, nullptr, 9)); // main -> first hotbar slot
    EXPECT_EQ(m_player.getInventorySlot(0), InventorySlot(BlockId::Dirt, 16));
    EXPECT_TRUE(m_player.getInventorySlot(9).isEmpty());
    EXPECT_FALSE(m_ui.shiftClickInventorySlot(m_player, nullptr, 9)); // empty slot
    EXPECT_FALSE(m_ui.shiftClickInventorySlot(m_player, nullptr, Player::TOTAL_INVENTORY_SIZE));

    // Tools keep their wear and never merge.
    m_player.setInventorySlot(20, {items::StonePickaxe, 1, 17});
    EXPECT_TRUE(m_ui.shiftClickInventorySlot(m_player, nullptr, 20));
    EXPECT_EQ(m_player.getInventorySlot(1), InventorySlot(items::StonePickaxe, 1, 17));
    EXPECT_EQ(m_player.countItem(BlockId::Dirt), 80);
}

TEST_F(InventoryCraftingTest, ShiftClickResultCraftsMax) {
    m_player.setInventorySlot(0, {BlockId::OakLog, 2});
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, 0));
    ASSERT_TRUE(m_ui.clickGridSlot(0));
    EXPECT_TRUE(m_ui.shiftClickResult(m_player));
    EXPECT_EQ(m_player.countItem(BlockId::OakPlanks), 8);
    EXPECT_EQ(m_player.getInventorySlot(9), InventorySlot(BlockId::OakPlanks, 8)) << "main inventory first";
    EXPECT_TRUE(m_ui.grid().isEmpty());
    EXPECT_TRUE(m_ui.carried().isEmpty());
    EXPECT_FALSE(m_ui.shiftClickResult(m_player)); // nothing left to craft

    // 16 logs on the 3x3 grid: 64 planks.
    ASSERT_TRUE(m_ui.open(GridSize::Workbench3x3, m_player).empty());
    for (size_t i = 0; i < Player::TOTAL_INVENTORY_SIZE; ++i) m_player.setInventorySlot(i, {});
    m_player.setInventorySlot(0, {BlockId::OakLog, 16});
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, 0));
    ASSERT_TRUE(m_ui.clickGridSlot(4));
    EXPECT_TRUE(m_ui.shiftClickResult(m_player));
    EXPECT_EQ(m_player.countItem(BlockId::OakPlanks), 64);
}

TEST_F(ContainerScreenTest, ShiftClickChestRoundTrip) {
    world::ChestData& c = placeChest();
    c.slots[5] = {toItemId(BlockId::Cobblestone), 50, 0};
    m_player.setInventorySlot(3, {BlockId::Cobblestone, 30});
    m_world->getChunkMutable({0, 0})->setModified(false);

    // Inventory -> chest: merge into the 50, then the first empty chest slot.
    EXPECT_TRUE(m_ui.shiftClickInventorySlot(m_player, m_world.get(), 3));
    EXPECT_TRUE(m_player.getInventorySlot(3).isEmpty());
    EXPECT_EQ(c.slots[5], (world::StoredStack{toItemId(BlockId::Cobblestone), 64, 0}));
    EXPECT_EQ(c.slots[0], (world::StoredStack{toItemId(BlockId::Cobblestone), 16, 0}));
    EXPECT_TRUE(m_world->getChunk({0, 0})->isModified());

    // Chest -> inventory: main inventory first.
    EXPECT_TRUE(m_ui.shiftClickContainerSlot(m_player, *m_world, 5));
    EXPECT_TRUE(m_ui.shiftClickContainerSlot(m_player, *m_world, 0));
    EXPECT_TRUE(c.slots[5].empty());
    EXPECT_TRUE(c.slots[0].empty());
    EXPECT_EQ(m_player.getInventorySlot(9), InventorySlot(BlockId::Cobblestone, 64));
    EXPECT_EQ(m_player.getInventorySlot(10), InventorySlot(BlockId::Cobblestone, 16));
    EXPECT_EQ(m_player.countItem(BlockId::Cobblestone), 80);
    EXPECT_FALSE(m_ui.shiftClickContainerSlot(m_player, *m_world, 0)); // empty slot
}

TEST_F(ContainerScreenTest, ShiftClickWithFurnaceOpenOnlySwapsMainAndHotbar) {
    world::FurnaceData& f = placeFurnace();
    m_player.setInventorySlot(0, {BlockId::IronOre, 8});
    EXPECT_TRUE(m_ui.shiftClickInventorySlot(m_player, m_world.get(), 0));
    EXPECT_TRUE(f.input.empty());
    EXPECT_EQ(m_player.getInventorySlot(9), InventorySlot(BlockId::IronOre, 8));

    f.output = {items::IronIngot, 5, 0};
    EXPECT_TRUE(m_ui.shiftClickContainerSlot(m_player, *m_world, ui::kFurnaceOutputSlot));
    EXPECT_TRUE(f.output.empty());
    EXPECT_EQ(m_player.countItem(items::IronIngot), 5);
}

TEST_F(InventoryCraftingTest, ShiftClickFullInventoryLeavesStack) {
    for (size_t i = 0; i < Player::TOTAL_INVENTORY_SIZE; ++i) m_player.setInventorySlot(i, {BlockId::Stone, 64});
    m_player.setInventorySlot(12, {BlockId::Dirt, 10});
    EXPECT_FALSE(m_ui.shiftClickInventorySlot(m_player, nullptr, 12));
    EXPECT_EQ(m_player.getInventorySlot(12), InventorySlot(BlockId::Dirt, 10));

    // One slot of room for part of a grid stack: the rest stays in the grid.
    m_player.setInventorySlot(30, {BlockId::Sand, 60});
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, 12));
    ASSERT_TRUE(m_ui.clickGridSlot(1));
    m_player.setInventorySlot(12, {BlockId::Stone, 64});
    m_player.setInventorySlot(30, {BlockId::Dirt, 60});
    EXPECT_TRUE(m_ui.shiftClickGridSlot(m_player, 1));
    EXPECT_EQ(m_player.getInventorySlot(30), InventorySlot(BlockId::Dirt, 64));
    EXPECT_EQ(m_ui.gridSlot(1), InventorySlot(BlockId::Dirt, 6));

    // A result that does not fit is not crafted.
    for (size_t i = 0; i < Player::TOTAL_INVENTORY_SIZE; ++i) m_player.setInventorySlot(i, {BlockId::Stone, 64});
    (void)m_ui.close(m_player); // The 6 dirt do not fit and come back as overflow
    ASSERT_TRUE(m_ui.open(GridSize::Inventory2x2, m_player).empty());
    m_player.setInventorySlot(0, {BlockId::OakLog, 1});
    ASSERT_TRUE(m_ui.clickInventorySlot(m_player, 0));
    ASSERT_TRUE(m_ui.clickGridSlot(0));
    m_player.setInventorySlot(0, {BlockId::Stone, 64});
    EXPECT_FALSE(m_ui.shiftClickResult(m_player));
    EXPECT_EQ(m_ui.gridSlot(0), InventorySlot(BlockId::OakLog, 1));
}

namespace {

// Every item the player, the screen (grid, cursor) and the chest hold, by id.
std::map<gameplay::ItemId, int64_t> countAll(const Player& player, const InventoryCrafting& ui,
                                             const world::ChestData& chest) {
    std::map<gameplay::ItemId, int64_t> total;
    for (size_t i = 0; i < Player::TOTAL_INVENTORY_SIZE; ++i) {
        const InventorySlot s = player.getInventorySlot(i);
        if (!s.isEmpty()) total[s.id] += s.count;
    }
    for (const InventorySlot& s : ui.pendingStacks()) total[s.id] += s.count;
    for (const world::StoredStack& s : chest.slots) {
        if (!s.empty()) total[s.id] += s.count;
    }
    return total;
}

} // namespace

TEST_F(ContainerScreenTest, ShiftClickConservesItemCount) {
    world::ChestData& c = placeChest();
    gameplay::SplitMix64 rng{0xC0FFEEULL};
    const gameplay::ItemId pool[] = {toItemId(BlockId::Dirt), toItemId(BlockId::Cobblestone),
                                     toItemId(BlockId::Sand), items::StonePickaxe, items::Stick};
    for (size_t i = 0; i < Player::TOTAL_INVENTORY_SIZE; ++i) {
        if (rng.nextInt(3) == 0) continue;
        const gameplay::ItemId id = pool[rng.nextInt(5)];
        const int32_t n = std::max(1, gameplay::maxStackSize(id) == 1 ? 1 : 1 + rng.nextInt(64));
        m_player.setInventorySlot(i, {id, n, id == items::StonePickaxe ? rng.nextInt(100) : 0});
    }
    for (auto& slot : c.slots) {
        if (rng.nextInt(2) == 0) continue;
        slot = {toItemId(BlockId::Dirt), static_cast<uint8_t>(1 + rng.nextInt(64)), 0};
    }
    const auto before = countAll(m_player, m_ui, c);

    for (int click = 0; click < 1000; ++click) {
        const SlotButton button = rng.nextInt(2) == 0 ? SlotButton::Left : SlotButton::Right;
        switch (rng.nextInt(6)) {
            case 0: (void)m_ui.shiftClickInventorySlot(m_player, m_world.get(), rng.nextInt(36)); break;
            case 1: (void)m_ui.shiftClickContainerSlot(m_player, *m_world, rng.nextInt(27)); break;
            case 2: (void)m_ui.shiftClickGridSlot(m_player, rng.nextInt(4)); break;
            case 3: (void)m_ui.clickInventorySlot(m_player, rng.nextInt(36), button); break;
            case 4: (void)m_ui.clickContainerSlot(*m_world, rng.nextInt(27), button); break;
            default: (void)m_ui.clickGridSlot(rng.nextInt(4), button); break;
        }
        ASSERT_EQ(countAll(m_player, m_ui, c), before) << "click " << click;
    }
}
