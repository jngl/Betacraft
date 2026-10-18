// P2.6 — CraftingGrid behaviour (2x2 inventory grid, 3x3 workbench grid).
#include "crafting/CraftingGrid.hpp"

#include <gtest/gtest.h>

using namespace betacraft::crafting;

namespace {
constexpr ItemId kPlanks = toItemId(betacraft::world::BlockId::OakPlanks);
constexpr ItemId kCobble = toItemId(betacraft::world::BlockId::Cobblestone);
} // namespace

TEST(CraftingGrid, InventoryGridIs2x2) {
    const CraftingGrid grid{GridSize::Inventory2x2};
    EXPECT_EQ(grid.width(), 2);
    EXPECT_EQ(grid.height(), 2);
    EXPECT_EQ(grid.slotCount(), 4u);
    EXPECT_TRUE(grid.isEmpty());
}

TEST(CraftingGrid, WorkbenchGridIs3x3) {
    const CraftingGrid grid{GridSize::Workbench3x3};
    EXPECT_EQ(grid.width(), 3);
    EXPECT_EQ(grid.height(), 3);
    EXPECT_EQ(grid.slotCount(), 9u);
    EXPECT_TRUE(grid.isEmpty());
}

TEST(CraftingGrid, SetAndGetRoundTrip) {
    CraftingGrid grid{GridSize::Workbench3x3};
    ASSERT_TRUE(grid.set(2, 1, SlotStack{kPlanks, 5, 0}));
    const SlotStack s = grid.get(2, 1);
    EXPECT_EQ(s.id, kPlanks);
    EXPECT_EQ(s.count, 5);
    EXPECT_FALSE(grid.isEmpty());
    EXPECT_TRUE(grid.get(0, 0).empty());
}

TEST(CraftingGrid, OutOfBoundsAccessIsRejectedAndReadsEmpty) {
    CraftingGrid grid{GridSize::Inventory2x2};
    EXPECT_FALSE(grid.set(2, 0, SlotStack{kPlanks, 1, 0}));
    EXPECT_FALSE(grid.set(0, 2, SlotStack{kPlanks, 1, 0}));
    EXPECT_FALSE(grid.set(-1, 0, SlotStack{kPlanks, 1, 0}));
    EXPECT_FALSE(grid.set(0, -1, SlotStack{kPlanks, 1, 0}));
    EXPECT_TRUE(grid.get(5, 5).empty());
    EXPECT_TRUE(grid.get(-1, 0).empty());
    EXPECT_TRUE(grid.isEmpty());
}

TEST(CraftingGrid, ZeroCountOrAirNormalisesToEmpty) {
    CraftingGrid grid{GridSize::Inventory2x2};
    ASSERT_TRUE(grid.set(0, 0, SlotStack{kPlanks, 0, 0}));
    EXPECT_TRUE(grid.get(0, 0).empty());
    EXPECT_EQ(grid.get(0, 0).id, kAir);
    ASSERT_TRUE(grid.set(1, 1, SlotStack{kAir, 7, 3}));
    EXPECT_TRUE(grid.get(1, 1).empty());
    EXPECT_EQ(grid.get(1, 1).count, 0);
    EXPECT_TRUE(grid.isEmpty());
}

TEST(CraftingGrid, CountIsClampedToMaxStack) {
    CraftingGrid grid{GridSize::Inventory2x2};
    ASSERT_TRUE(grid.set(0, 0, SlotStack{kPlanks, 200, 0}));
    EXPECT_EQ(grid.get(0, 0).count, kMaxStackSize);
}

TEST(CraftingGrid, IndexAccessIsRowMajor) {
    CraftingGrid grid{GridSize::Workbench3x3};
    ASSERT_TRUE(grid.set(1, 2, SlotStack{kCobble, 1, 0}));
    EXPECT_EQ(grid.at(2 * 3 + 1).id, kCobble);
    EXPECT_TRUE(grid.at(99).empty());
}

TEST(CraftingGrid, ClearEmptiesAllSlots) {
    CraftingGrid grid{GridSize::Workbench3x3};
    for (int y = 0; y < 3; ++y) {
        for (int x = 0; x < 3; ++x) {
            ASSERT_TRUE(grid.set(x, y, SlotStack{kCobble, 1, 0}));
        }
    }
    EXPECT_EQ(grid.occupiedCount(), 9u);
    grid.clear();
    EXPECT_TRUE(grid.isEmpty());
    EXPECT_EQ(grid.occupiedCount(), 0u);
}

TEST(CraftingGrid, BoundingBoxOfEmptyGridIsNullopt) {
    const CraftingGrid grid{GridSize::Workbench3x3};
    EXPECT_FALSE(grid.bounds().has_value());
}

TEST(CraftingGrid, BoundingBoxTrimsEmptyRowsAndColumns) {
    CraftingGrid grid{GridSize::Workbench3x3};
    ASSERT_TRUE(grid.set(1, 1, SlotStack{kPlanks, 1, 0}));
    ASSERT_TRUE(grid.set(2, 2, SlotStack{kPlanks, 1, 0}));
    const auto box = grid.bounds();
    ASSERT_TRUE(box.has_value());
    EXPECT_EQ(box->minX, 1);
    EXPECT_EQ(box->minY, 1);
    EXPECT_EQ(box->width(), 2);
    EXPECT_EQ(box->height(), 2);
}

TEST(CraftingGrid, BoundingBoxOfSingleCorner) {
    CraftingGrid grid{GridSize::Inventory2x2};
    ASSERT_TRUE(grid.set(1, 0, SlotStack{kPlanks, 1, 0}));
    const auto box = grid.bounds();
    ASSERT_TRUE(box.has_value());
    EXPECT_EQ(box->minX, 1);
    EXPECT_EQ(box->minY, 0);
    EXPECT_EQ(box->width(), 1);
    EXPECT_EQ(box->height(), 1);
}

TEST(CraftingGrid, InventoryGridIgnoresBackingSlotsBeyondFour) {
    CraftingGrid grid{GridSize::Inventory2x2};
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            ASSERT_TRUE(grid.set(x, y, SlotStack{kPlanks, 1, 0}));
        }
    }
    for (size_t i = 4; i < 9; ++i) {
        EXPECT_TRUE(grid.at(i).empty()) << i;
    }
    EXPECT_EQ(grid.occupiedCount(), 4u);
    EXPECT_EQ(grid.size(), GridSize::Inventory2x2);
}

TEST(CraftingGrid, CopiesAreIndependent) {
    CraftingGrid a{GridSize::Workbench3x3};
    ASSERT_TRUE(a.set(0, 0, SlotStack{kPlanks, 3, 0}));
    CraftingGrid b = a;
    ASSERT_TRUE(b.set(0, 0, SlotStack{kCobble, 1, 0}));
    EXPECT_EQ(a.get(0, 0).id, kPlanks);
    EXPECT_EQ(b.get(0, 0).id, kCobble);
}

// GridSize is a uint8_t-backed enum, so integration code casting a container
// size into it can produce values other than 2/3. The grid must stay inside its
// 9-slot backing store whatever it is given.
TEST(CraftingGrid, OversizedGridSizeValueIsClampedTo3x3) {
    CraftingGrid grid{static_cast<GridSize>(4)};
    EXPECT_EQ(grid.width(), 3);
    EXPECT_EQ(grid.height(), 3);
    EXPECT_EQ(grid.slotCount(), 9u);
    EXPECT_EQ(grid.size(), GridSize::Workbench3x3);
    EXPECT_FALSE(grid.set(3, 3, SlotStack{kPlanks, 1, 0}));
    EXPECT_FALSE(grid.set(0, 3, SlotStack{kPlanks, 1, 0}));
    EXPECT_TRUE(grid.at(15).empty());
    EXPECT_TRUE(grid.isEmpty());
}

TEST(CraftingGrid, UndersizedGridSizeValueIsClampedTo2x2) {
    for (const uint8_t raw : {uint8_t{0}, uint8_t{1}}) {
        CraftingGrid grid{static_cast<GridSize>(raw)};
        EXPECT_EQ(grid.width(), 2) << int{raw};
        EXPECT_EQ(grid.slotCount(), 4u) << int{raw};
        EXPECT_EQ(grid.size(), GridSize::Inventory2x2) << int{raw};
        EXPECT_TRUE(grid.set(1, 1, SlotStack{kPlanks, 1, 0}));
        EXPECT_FALSE(grid.set(2, 0, SlotStack{kPlanks, 1, 0}));
    }
}

TEST(CraftingGrid, AnyRawGridSizeStaysWithinBackingStore) {
    for (int raw = 0; raw < 256; ++raw) {
        CraftingGrid grid{static_cast<GridSize>(raw)};
        EXPECT_TRUE(grid.size() == GridSize::Inventory2x2 || grid.size() == GridSize::Workbench3x3) << raw;
        EXPECT_LE(grid.slotCount(), 9u) << raw;
        EXPECT_EQ(grid.slotCount(), static_cast<size_t>(grid.width() * grid.height())) << raw;
    }
}

// Beta caps a slot at the item's own stack size: tools, buckets and cake are 1.
TEST(CraftingGrid, CountIsClampedToItemMaxStackSize) {
    CraftingGrid grid{GridSize::Workbench3x3};
    ASSERT_TRUE(grid.set(0, 0, SlotStack{items::IronAxe, 64, 5}));
    EXPECT_EQ(grid.get(0, 0).count, 1);
    EXPECT_EQ(grid.get(0, 0).damage, 5);
    ASSERT_TRUE(grid.set(1, 0, SlotStack{items::MilkBucket, 5, 0}));
    EXPECT_EQ(grid.get(1, 0).count, 1);
    ASSERT_TRUE(grid.set(2, 0, SlotStack{items::beta::WoodPickaxe, 3, 0}));
    EXPECT_EQ(grid.get(2, 0).count, 1);
    ASSERT_TRUE(grid.set(0, 1, SlotStack{items::Cake, 2, 0}));
    EXPECT_EQ(grid.get(0, 1).count, 1);
    ASSERT_TRUE(grid.set(1, 1, SlotStack{items::Stick, 64, 0}));
    EXPECT_EQ(grid.get(1, 1).count, 64);
}
