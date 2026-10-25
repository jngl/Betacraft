// Mouse-wheel scrolling of the creative palette: where the wheel scrolls the grid (the
// cursor over the palette grid, Creative only) and the indicative, non-clickable scrollbar
// drawn beside the grid for a category of more than 36 entries.
#include "ui/CreativePalette.hpp"
#include "ui/HudRenderer.hpp"

#include "gameplay/ItemIds.hpp"
#include "physics/Player.hpp"
#include "voxel/Block.hpp"

#include <gtest/gtest.h>

#include <span>
#include <utility>

using namespace betacraft;
using namespace betacraft::ui;

namespace {

constexpr int32_t kW = 1280;
constexpr int32_t kH = 720;
constexpr InventoryLayoutKey kCreative{2, ScreenKind::Inventory2x2, true};

// 60 entries (7 rows): the grid scrolls three rows. Slot k holds item 1000 + k.
constexpr auto kLong = [] {
    std::array<gameplay::ItemId, 60> a{};
    for (size_t k = 0; k < a.size(); ++k) a[k] = static_cast<gameplay::ItemId>(1000 + k);
    return a;
}();
std::span<const gameplay::ItemId> longNature(PaletteCategory c) noexcept {
    return c == PaletteCategory::Nature ? std::span<const gameplay::ItemId>(kLong) : paletteEntries(c);
}

std::pair<int32_t, int32_t> centre(InventoryRegion region, size_t i, InventoryLayoutKey layout = kCreative) {
    const SlotRect r = HudRenderer::inventorySlotRect(region, i, kW, kH, layout);
    return {static_cast<int32_t>(r.x + r.size * 0.5f), static_cast<int32_t>(r.y + r.size * 0.5f)};
}

} // namespace

TEST(PaletteScroll, CursorOverAnyPaletteSlotIsOverTheGrid) {
    for (const InventoryLayoutKey layout : {kCreative, InventoryLayoutKey{3, ScreenKind::Workbench3x3, true}}) {
        for (size_t i = 0; i < kPaletteVisibleSlots; ++i) {
            const auto [x, y] = centre(InventoryRegion::Palette, i, layout);
            EXPECT_TRUE(HudRenderer::isOverPaletteGrid(x, y, kW, kH, layout)) << i;
        }
        // The corners of the grid are inside too.
        const SlotRect first = HudRenderer::inventorySlotRect(InventoryRegion::Palette, 0, kW, kH, layout);
        const SlotRect last =
            HudRenderer::inventorySlotRect(InventoryRegion::Palette, kPaletteVisibleSlots - 1, kW, kH, layout);
        EXPECT_TRUE(HudRenderer::isOverPaletteGrid(static_cast<int32_t>(first.x), static_cast<int32_t>(first.y), kW,
                                                   kH, layout));
        EXPECT_TRUE(HudRenderer::isOverPaletteGrid(static_cast<int32_t>(last.x + last.size - 1.0f),
                                                   static_cast<int32_t>(last.y + last.size - 1.0f), kW, kH, layout));
    }
}

TEST(PaletteScroll, TabsInventoryAndCraftingAreNotOverTheGrid) {
    for (const auto [region, index] :
         {std::pair{InventoryRegion::PaletteTab, size_t{2}}, std::pair{InventoryRegion::Inventory, size_t{12}},
          std::pair{InventoryRegion::Inventory, size_t{0}}, std::pair{InventoryRegion::CraftingGrid, size_t{0}},
          std::pair{InventoryRegion::CraftingResult, size_t{0}}, std::pair{InventoryRegion::Armor, size_t{1}}}) {
        const auto [x, y] = centre(region, index);
        EXPECT_FALSE(HudRenderer::isOverPaletteGrid(x, y, kW, kH, kCreative)) << static_cast<int>(region);
    }
    EXPECT_FALSE(HudRenderer::isOverPaletteGrid(5, 5, kW, kH, kCreative)) << "off the panel";
}

// Survival draws no palette and a container screen has none: the wheel never scrolls there.
TEST(PaletteScroll, NothingIsOverTheGridWithoutThePalette) {
    for (const InventoryLayoutKey layout :
         {InventoryLayoutKey{2, ScreenKind::Inventory2x2, false}, InventoryLayoutKey{3, ScreenKind::Workbench3x3, false},
          InventoryLayoutKey{2, ScreenKind::Furnace, true}, InventoryLayoutKey{2, ScreenKind::Chest, true}}) {
        for (size_t i = 0; i < kPaletteVisibleSlots; ++i) {
            const auto [x, y] = centre(InventoryRegion::Palette, i); // where the grid is in Creative
            EXPECT_FALSE(HudRenderer::isOverPaletteGrid(x, y, kW, kH, layout)) << static_cast<int>(layout.kind) << " " << i;
        }
    }
}

// What game::Application does with a wheel step over the grid, then a click: the slot hands
// out the entry scrolled into it, as a full stack.
TEST(PaletteScroll, WheelOverTheGridThenClickEquipsTheScrolledEntry) {
    voxel::BlockRegistry::initialize();
    physics::Player player;
    HudRenderer hud;
    PaletteNavigation nav(longNature);
    nav.selectCategory(PaletteCategory::Nature);

    const auto [x, y] = centre(InventoryRegion::Palette, 4);
    ASSERT_TRUE(HudRenderer::isOverPaletteGrid(x, y, kW, kH, kCreative));
    nav.scroll(-2); // two wheel steps down
    ASSERT_TRUE(hud.handleInventoryClick(x, y, kW, kH, player, nav));
    EXPECT_EQ(player.getHeldItem().id, 1000 + 2 * 9 + 4);
    EXPECT_EQ(player.getHeldItem().count, 64) << "a full stack";

    // Clamped at the last row (rows 3..6 shown): slot 4 of the bottom visible row is empty.
    nav.scroll(-10);
    EXPECT_EQ(nav.scrollRow(), 3u);
    const auto [bx, by] = centre(InventoryRegion::Palette, 3 * 9 + 6);
    EXPECT_FALSE(hud.handleInventoryClick(bx, by, kW, kH, player, nav)) << "past the last entry";
    const auto [lx, ly] = centre(InventoryRegion::Palette, 3 * 9 + 5);
    ASSERT_TRUE(hud.handleInventoryClick(lx, ly, kW, kH, player, nav));
    EXPECT_EQ(player.getHeldItem().id, 1059);

    // And at the first row.
    nav.scroll(10);
    ASSERT_TRUE(hud.handleInventoryClick(x, y, kW, kH, player, nav));
    EXPECT_EQ(player.getHeldItem().id, 1004);
}

TEST(PaletteScroll, ScrollbarOnlyForCategoriesAboveThirtySixEntries) {
    PaletteNavigation nav(longNature);
    EXPECT_FALSE(HudRenderer::paletteScrollbar(kW, kH, kCreative, nav)) << "Building fits the grid";
    nav.selectCategory(PaletteCategory::Nature);
    EXPECT_TRUE(HudRenderer::paletteScrollbar(kW, kH, kCreative, nav));
    // Not without the palette.
    EXPECT_FALSE(HudRenderer::paletteScrollbar(kW, kH, InventoryLayoutKey{2, ScreenKind::Inventory2x2, false}, nav));
    EXPECT_FALSE(HudRenderer::paletteScrollbar(kW, kH, InventoryLayoutKey{2, ScreenKind::Chest, true}, nav));
    // No real category scrolls today.
    PaletteNavigation real;
    for (const PaletteCategory c : kPaletteCategories) {
        real.selectCategory(c);
        EXPECT_EQ(HudRenderer::paletteScrollbar(kW, kH, kCreative, real).has_value(), real.scrollbarVisible());
    }
}

// Beside the grid, as tall as it, inside the panel; the thumb follows the scroll position.
TEST(PaletteScroll, ScrollbarSitsBesideTheGridAndItsThumbFollowsTheScroll) {
    PaletteNavigation nav(longNature);
    nav.selectCategory(PaletteCategory::Nature);
    const SlotRect panel = HudRenderer::inventorySlotRect(InventoryRegion::None, 0, kW, kH, kCreative);
    const SlotRect first = HudRenderer::inventorySlotRect(InventoryRegion::Palette, 0, kW, kH, kCreative);
    const SlotRect last =
        HudRenderer::inventorySlotRect(InventoryRegion::Palette, kPaletteVisibleSlots - 1, kW, kH, kCreative);

    const auto top = HudRenderer::paletteScrollbar(kW, kH, kCreative, nav);
    ASSERT_TRUE(top);
    EXPECT_GE(top->x, last.x + last.size) << "right of the grid";
    EXPECT_LE(top->x + top->width, panel.x + panel.size) << "inside the panel";
    EXPECT_GT(top->width, 0.0f);
    EXPECT_FLOAT_EQ(top->trackTop, first.y);
    EXPECT_FLOAT_EQ(top->trackTop + top->trackHeight, last.y + last.size);
    EXPECT_FLOAT_EQ(top->thumbTop, top->trackTop);
    EXPECT_FLOAT_EQ(top->thumbHeight, top->trackHeight * 4.0f / 7.0f);

    nav.scroll(-100);
    const auto bottom = HudRenderer::paletteScrollbar(kW, kH, kCreative, nav);
    ASSERT_TRUE(bottom);
    EXPECT_FLOAT_EQ(bottom->thumbTop + bottom->thumbHeight, bottom->trackTop + bottom->trackHeight);
}

// The bar only shows the position: clicking it hits nothing.
TEST(PaletteScroll, ScrollbarIsNotClickable) {
    PaletteNavigation nav(longNature);
    nav.selectCategory(PaletteCategory::Nature);
    const auto bar = HudRenderer::paletteScrollbar(kW, kH, kCreative, nav);
    ASSERT_TRUE(bar);
    const auto x = static_cast<int32_t>(bar->x + bar->width * 0.5f);
    for (float f : {0.05f, 0.5f, 0.95f}) {
        const auto y = static_cast<int32_t>(bar->trackTop + bar->trackHeight * f);
        EXPECT_EQ(HudRenderer::hitTestInventory(x, y, kW, kH, kCreative).region, InventoryRegion::None);
    }
}
