// The palette category tabs of the Creative inventory screen: one per category above the
// palette grid, hit-tested like the slots, and the grid showing the navigation state.
#include "ui/CreativePalette.hpp"
#include "ui/HudRenderer.hpp"

#include "gameplay/ItemIds.hpp"
#include "physics/Player.hpp"
#include "voxel/Block.hpp"

#include <gtest/gtest.h>

#include <set>
#include <span>
#include <utility>

using namespace betacraft;
using namespace betacraft::ui;

namespace {

constexpr int32_t kW = 1280;
constexpr int32_t kH = 720;

InventoryHit hitCentre(InventoryRegion region, size_t index, InventoryLayoutKey layout) {
    const SlotRect r = HudRenderer::inventorySlotRect(region, index, kW, kH, layout);
    return HudRenderer::hitTestInventory(static_cast<int32_t>(r.x + r.size * 0.5f),
                                         static_cast<int32_t>(r.y + r.size * 0.5f), kW, kH, layout);
}

} // namespace

TEST(PaletteTabs, ClickOnTabKHitsPaletteTabK) {
    for (const auto [dim, kind] : {std::pair{2, ScreenKind::Inventory2x2}, std::pair{3, ScreenKind::Workbench3x3}}) {
        const InventoryLayoutKey layout{dim, kind, true};
        for (size_t k = 0; k < kPaletteCategories.size(); ++k) {
            const InventoryHit hit = hitCentre(InventoryRegion::PaletteTab, k, layout);
            EXPECT_EQ(hit.region, InventoryRegion::PaletteTab) << dim << " " << k;
            EXPECT_EQ(hit.index, k);
        }
    }
}

// Survival draws no palette and a container screen has none: no tab is ever hit there.
TEST(PaletteTabs, NoTabIsHitWithoutThePalette) {
    const InventoryLayoutKey creative{2, ScreenKind::Inventory2x2, true};
    for (const InventoryLayoutKey layout :
         {InventoryLayoutKey{2, ScreenKind::Inventory2x2, false}, InventoryLayoutKey{3, ScreenKind::Workbench3x3, false},
          InventoryLayoutKey{2, ScreenKind::Furnace, true}, InventoryLayoutKey{2, ScreenKind::Chest, true}}) {
        for (size_t k = 0; k < kPaletteCategories.size(); ++k) {
            // Where the tab would be on the Creative inventory screen.
            const SlotRect r = HudRenderer::inventorySlotRect(InventoryRegion::PaletteTab, k, kW, kH, creative);
            for (const float dy : {0.1f, 0.5f, 0.9f}) {
                const InventoryHit hit = HudRenderer::hitTestInventory(
                    static_cast<int32_t>(r.x + r.size * 0.5f), static_cast<int32_t>(r.y + r.size * dy), kW, kH, layout);
                EXPECT_NE(hit.region, InventoryRegion::PaletteTab) << static_cast<int>(layout.kind) << " " << k;
            }
        }
        // Nor anywhere else on the screen.
        for (int32_t y = 0; y < kH; y += 4) {
            for (int32_t x = 0; x < kW; x += 4) {
                ASSERT_NE(HudRenderer::hitTestInventory(x, y, kW, kH, layout).region, InventoryRegion::PaletteTab)
                    << x << "," << y;
            }
        }
    }
}

namespace {

std::pair<int32_t, int32_t> paletteSlotCentre(size_t i) {
    const SlotRect r = HudRenderer::inventorySlotRect(InventoryRegion::Palette, i, kW, kH, 2);
    return {static_cast<int32_t>(r.x + r.size * 0.5f), static_cast<int32_t>(r.y + r.size * 0.5f)};
}

// 50 Materials-like entries (6 rows), so the grid scrolls two rows; the others are real.
constexpr gameplay::ItemId kScrolling[50] = {
    gameplay::items::Coal, gameplay::items::IronIngot, gameplay::items::GoldIngot, gameplay::items::Diamond,
    gameplay::items::Stick, gameplay::items::Bowl, gameplay::items::String, gameplay::items::Feather,
    gameplay::items::Gunpowder, gameplay::items::Flint, gameplay::items::Leather, gameplay::items::Seeds,
    gameplay::items::Wheat, gameplay::items::Paper, gameplay::items::Book,
};
std::span<const gameplay::ItemId> scrollingCatalogue(PaletteCategory c) noexcept {
    return c == PaletteCategory::Materials ? std::span<const gameplay::ItemId>(kScrolling) : paletteEntries(c);
}

} // namespace

// A palette slot hands out what the navigation state shows there, as a full stack.
TEST(PaletteTabs, PaletteSlotEquipsTheItemOfTheSelectedCategory) {
    voxel::BlockRegistry::initialize();
    physics::Player player;
    HudRenderer hud;
    player.setSelectedSlot(3);

    PaletteNavigation nav;
    nav.selectCategory(PaletteCategory::Materials);
    const auto [x0, y0] = paletteSlotCentre(1);
    ASSERT_TRUE(hud.handleInventoryClick(x0, y0, kW, kH, player, nav));
    EXPECT_EQ(player.getHeldItem().id, gameplay::items::IronIngot);
    EXPECT_EQ(player.getHeldItem().count, 64);

    nav.selectCategory(PaletteCategory::Tools);
    const auto [x1, y1] = paletteSlotCentre(5); // stone pickaxe: a tool is a stack of 1
    ASSERT_TRUE(hud.handleInventoryClick(x1, y1, kW, kH, player, nav));
    EXPECT_EQ(player.getHeldItem().id, gameplay::items::StonePickaxe);
    EXPECT_EQ(player.getHeldItem().count, 1);
    EXPECT_EQ(player.getSelectedSlot(), 3);

    // Scrolled one row down, slot 1 shows the 11th entry.
    PaletteNavigation scrolled(scrollingCatalogue);
    scrolled.selectCategory(PaletteCategory::Materials);
    scrolled.scroll(-1);
    ASSERT_TRUE(hud.handleInventoryClick(x0, y0, kW, kH, player, scrolled));
    EXPECT_EQ(player.getHeldItem().id, gameplay::items::Leather);
    EXPECT_EQ(player.getHeldItem().count, 64);
}

// The tabs sit in one row above the grid, inside the panel, and overlap no slot.
TEST(PaletteTabs, TabsSitAboveTheGridAndOverlapNoSlot) {
    const auto overlap = [](const SlotRect& a, const SlotRect& b) {
        return a.x < b.x + b.size && b.x < a.x + a.size && a.y < b.y + b.size && b.y < a.y + a.size;
    };
    for (const auto [dim, kind] : {std::pair{2, ScreenKind::Inventory2x2}, std::pair{3, ScreenKind::Workbench3x3}}) {
        const InventoryLayoutKey layout{dim, kind, true};
        const SlotRect panel = HudRenderer::inventorySlotRect(InventoryRegion::None, 0, kW, kH, layout);
        const SlotRect firstSlot = HudRenderer::inventorySlotRect(InventoryRegion::Palette, 0, kW, kH, layout);
        for (size_t k = 0; k < kPaletteCategories.size(); ++k) {
            const SlotRect tab = HudRenderer::inventorySlotRect(InventoryRegion::PaletteTab, k, kW, kH, layout);
            const SlotRect column = HudRenderer::inventorySlotRect(InventoryRegion::Palette, k, kW, kH, layout);
            EXPECT_GT(tab.size, 0.0f);
            EXPECT_GE(tab.y, panel.y);
            EXPECT_LE(tab.y + tab.size, firstSlot.y) << "above the grid";
            EXPECT_GE(tab.x, column.x) << "over its own column";
            EXPECT_LE(tab.x + tab.size, column.x + column.size);
            for (const auto [region, count] :
                 {std::pair{InventoryRegion::Palette, kPaletteVisibleSlots},
                  std::pair{InventoryRegion::CraftingGrid, static_cast<size_t>(dim * dim)},
                  std::pair{InventoryRegion::CraftingResult, size_t{1}},
                  std::pair{InventoryRegion::Inventory, physics::Player::TOTAL_INVENTORY_SIZE},
                  std::pair{InventoryRegion::Armor, physics::Player::ARMOR_SLOT_COUNT}}) {
                for (size_t i = 0; i < count; ++i) {
                    const SlotRect r = HudRenderer::inventorySlotRect(region, i, kW, kH, layout);
                    if (r.size > 0.0f) {
                        EXPECT_FALSE(overlap(tab, r)) << k << " " << static_cast<int>(region) << " " << i;
                    }
                }
            }
        }
    }
}

// Clicking each tab in turn (what game::Application does with a PaletteTab hit) and
// scrolling its grid reaches every entry of the catalogue through a palette slot.
TEST(PaletteTabs, EveryCatalogueEntryIsReachableThroughTheTabs) {
    voxel::BlockRegistry::initialize();
    physics::Player player;
    HudRenderer hud;
    const InventoryLayoutKey layout{2, ScreenKind::Inventory2x2, true};
    std::set<gameplay::ItemId> reached;
    PaletteNavigation nav;
    for (size_t k = 0; k < kPaletteCategories.size(); ++k) {
        const InventoryHit tab = hitCentre(InventoryRegion::PaletteTab, k, layout);
        ASSERT_EQ(tab.region, InventoryRegion::PaletteTab);
        nav.selectCategory(kPaletteCategories[tab.index]);
        for (size_t row = 0; row <= nav.maxScrollRow(); ++row) {
            for (size_t i = 0; i < kPaletteVisibleSlots; ++i) {
                const auto [x, y] = paletteSlotCentre(i);
                if (hud.handleInventoryClick(x, y, kW, kH, player, nav)) reached.insert(player.getHeldItem().id);
            }
            nav.scroll(-1);
        }
    }
    const auto all = allPaletteEntries();
    EXPECT_EQ(reached, std::set<gameplay::ItemId>(all.begin(), all.end()));
}
