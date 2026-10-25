// (UI): the inventory screen draws the 2x2 / 3x3 crafting grid, its
// result slot, the creative palette and the 36 inventory slots from one GL-free layout, and
// mouse clicks are mapped back to those slots with the same layout.
#include "ui/HudRenderer.hpp"

#include "gameplay/ItemIds.hpp"
#include "physics/Player.hpp"
#include "voxel/Block.hpp"

#include <gtest/gtest.h>

#include <set>
#include <string>
#include <utility>
#include <vector>

using namespace betacraft;
using ui::HudRenderer;
using ui::InventoryHit;
using ui::InventoryRegion;

namespace {

constexpr int32_t kW = 1280;
constexpr int32_t kH = 720;

InventoryHit hitCentre(InventoryRegion region, size_t index, int gridDim) {
    const ui::SlotRect r = HudRenderer::inventorySlotRect(region, index, kW, kH, gridDim);
    return HudRenderer::hitTestInventory(static_cast<int32_t>(r.x + r.size * 0.5f),
                                         static_cast<int32_t>(r.y + r.size * 0.5f), kW, kH, gridDim);
}

} // namespace

TEST(HudInventoryLayout, EverySlotCentreMapsBackToItsSlot) {
    for (const int dim : {2, 3}) {
        for (size_t i = 0; i < static_cast<size_t>(dim * dim); ++i) {
            const InventoryHit hit = hitCentre(InventoryRegion::CraftingGrid, i, dim);
            EXPECT_EQ(hit.region, InventoryRegion::CraftingGrid) << dim << " " << i;
            EXPECT_EQ(hit.index, i) << dim;
        }
        EXPECT_EQ(hitCentre(InventoryRegion::CraftingResult, 0, dim).region, InventoryRegion::CraftingResult);
        for (size_t i = 0; i < physics::Player::TOTAL_INVENTORY_SIZE; ++i) {
            const InventoryHit hit = hitCentre(InventoryRegion::Inventory, i, dim);
            EXPECT_EQ(hit.region, InventoryRegion::Inventory) << i;
            EXPECT_EQ(hit.index, i);
        }
    }
    EXPECT_EQ(hitCentre(InventoryRegion::Palette, 35, 2).index, 35U);
}

TEST(HudInventoryLayout, SlotsDoNotOverlapAndOutsideClicksHitNothing) {
    std::set<std::pair<int, int>> corners;
    size_t count = 0;
    const auto add = [&](InventoryRegion region, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            const ui::SlotRect r = HudRenderer::inventorySlotRect(region, i, kW, kH, 3);
            corners.insert({static_cast<int>(r.x), static_cast<int>(r.y)});
            ++count;
        }
    };
    add(InventoryRegion::CraftingGrid, 9);
    add(InventoryRegion::CraftingResult, 1);
    add(InventoryRegion::Palette, 36);
    add(InventoryRegion::Inventory, physics::Player::TOTAL_INVENTORY_SIZE);
    EXPECT_EQ(corners.size(), count);
    EXPECT_EQ(HudRenderer::hitTestInventory(2, 2, kW, kH, 2).region, InventoryRegion::None);
}

TEST(HudInventoryLayout, PaletteClicksStillEquipAndOtherSlotsAreLeftToTheGame) {
    voxel::BlockRegistry::initialize();
    physics::Player player;
    HudRenderer hud;
    player.setSelectedSlot(0);
    const physics::InventorySlot before = player.getInventorySlot(9);
    const ui::SlotRect inv = HudRenderer::inventorySlotRect(InventoryRegion::Inventory, 9, kW, kH, 2);
    EXPECT_FALSE(hud.handleInventoryClick(static_cast<int32_t>(inv.x + 5.0f), static_cast<int32_t>(inv.y + 5.0f),
                                          kW, kH, player));
    EXPECT_EQ(player.getInventorySlot(9), before);

    const ui::SlotRect pal = HudRenderer::inventorySlotRect(InventoryRegion::Palette, 1, kW, kH, 2);
    EXPECT_TRUE(hud.handleInventoryClick(static_cast<int32_t>(pal.x + 5.0f), static_cast<int32_t>(pal.y + 5.0f),
                                         kW, kH, player));
    // Slot 1 was grass in the fixed 36-entry palette; the categorised palette (issue #64)
    // opens on Building, whose second entry is cobblestone (grass moved to Nature).
    EXPECT_EQ(player.getHeldItem().id, voxel::BlockId::Cobblestone);
}

// Survival draws no palette, so its slots are not clickable either; the rest of the
// screen maps exactly as in Creative.
TEST(HudInventoryLayout, PaletteIsNotHitWithoutCreative) {
    for (const int dim : {2, 3}) {
        for (size_t i = 0; i < 36; ++i) {
            const ui::SlotRect r = HudRenderer::inventorySlotRect(InventoryRegion::Palette, i, kW, kH, dim);
            const auto mx = static_cast<int32_t>(r.x + r.size * 0.5f);
            const auto my = static_cast<int32_t>(r.y + r.size * 0.5f);
            EXPECT_EQ(HudRenderer::hitTestInventory(mx, my, kW, kH, dim, true).region, InventoryRegion::Palette);
            EXPECT_EQ(HudRenderer::hitTestInventory(mx, my, kW, kH, dim, false).region, InventoryRegion::None) << i;
        }
        for (size_t i = 0; i < physics::Player::TOTAL_INVENTORY_SIZE; ++i) {
            const ui::SlotRect r = HudRenderer::inventorySlotRect(InventoryRegion::Inventory, i, kW, kH, dim);
            const InventoryHit hit = HudRenderer::hitTestInventory(static_cast<int32_t>(r.x + r.size * 0.5f),
                                                                   static_cast<int32_t>(r.y + r.size * 0.5f), kW,
                                                                   kH, dim, false);
            EXPECT_EQ(hit.region, InventoryRegion::Inventory);
            EXPECT_EQ(hit.index, i);
        }
    }
}

// Furnace and chest slots sit in the palette area (no grid, no result, no palette on
// those screens), map back to their index and never overlap the inventory.
TEST(HudInventoryLayout, FurnaceAndChestSlotsMapBackAndDoNotOverlap) {
    struct Case {
        ui::ScreenKind kind;
        size_t slots;
    };
    for (const Case c : {Case{ui::ScreenKind::Furnace, 3}, Case{ui::ScreenKind::Chest, 27}}) {
        for (const bool palette : {true, false}) {
            const ui::InventoryLayoutKey layout{2, c.kind, palette};
            std::vector<ui::SlotRect> rects;
            const auto centreHit = [&](InventoryRegion region, size_t i) {
                const ui::SlotRect r = HudRenderer::inventorySlotRect(region, i, kW, kH, layout);
                rects.push_back(r);
                return HudRenderer::hitTestInventory(static_cast<int32_t>(r.x + r.size * 0.5f),
                                                     static_cast<int32_t>(r.y + r.size * 0.5f), kW, kH, layout);
            };
            for (size_t i = 0; i < c.slots; ++i) {
                const InventoryHit hit = centreHit(InventoryRegion::Container, i);
                EXPECT_EQ(hit.region, InventoryRegion::Container) << i;
                EXPECT_EQ(hit.index, i);
            }
            for (size_t i = 0; i < physics::Player::TOTAL_INVENTORY_SIZE; ++i) {
                const InventoryHit hit = centreHit(InventoryRegion::Inventory, i);
                EXPECT_EQ(hit.region, InventoryRegion::Inventory) << i;
                EXPECT_EQ(hit.index, i);
            }
            for (size_t a = 0; a < rects.size(); ++a) {
                EXPECT_GT(rects[a].size, 0.0f);
                for (size_t b = a + 1; b < rects.size(); ++b) {
                    const bool overlap = rects[a].x < rects[b].x + rects[b].size && rects[b].x < rects[a].x + rects[a].size &&
                                         rects[a].y < rects[b].y + rects[b].size && rects[b].y < rects[a].y + rects[a].size;
                    EXPECT_FALSE(overlap) << a << " " << b;
                }
            }
            // Neither the grid, the result nor the palette is clickable on a container screen.
            for (const InventoryRegion region :
                 {InventoryRegion::CraftingGrid, InventoryRegion::CraftingResult, InventoryRegion::Palette}) {
                const ui::SlotRect r = HudRenderer::inventorySlotRect(region, 0, kW, kH, 2);
                const InventoryHit hit = HudRenderer::hitTestInventory(static_cast<int32_t>(r.x + r.size * 0.5f),
                                                                       static_cast<int32_t>(r.y + r.size * 0.5f), kW,
                                                                       kH, layout);
                EXPECT_NE(hit.region, region);
            }
        }
    }
    // The old signature still describes the grid screens.
    const ui::InventoryLayoutKey grid{3, ui::ScreenKind::Workbench3x3, true};
    const ui::SlotRect a = HudRenderer::inventorySlotRect(InventoryRegion::CraftingGrid, 4, kW, kH, 3);
    const ui::SlotRect b = HudRenderer::inventorySlotRect(InventoryRegion::CraftingGrid, 4, kW, kH, grid);
    EXPECT_EQ(a.x, b.x);
    EXPECT_EQ(a.y, b.y);
}

// Off the dialog panel the screen hit test reports Outside (Beta drops the cursor
// stack there); gaps inside the panel stay None, and slots keep mapping as before.
TEST(HudInventoryLayout, OffPanelClicksReportOutside) {
    const ui::InventoryLayoutKey layout{2, ui::ScreenKind::Inventory2x2, false};
    EXPECT_EQ(HudRenderer::hitTestInventoryScreen(2, 2, kW, kH, layout).region, InventoryRegion::Outside);
    EXPECT_EQ(HudRenderer::hitTestInventoryScreen(kW - 2, kH - 2, kW, kH, layout).region, InventoryRegion::Outside);
    const ui::SlotRect panel = HudRenderer::inventorySlotRect(InventoryRegion::None, 0, kW, kH, layout);
    const auto px = static_cast<int32_t>(panel.x);
    const auto py = static_cast<int32_t>(panel.y);
    EXPECT_EQ(HudRenderer::hitTestInventoryScreen(px + 3, py + 3, kW, kH, layout).region, InventoryRegion::None);
    EXPECT_EQ(HudRenderer::hitTestInventoryScreen(px - 3, py + 3, kW, kH, layout).region, InventoryRegion::Outside);
    const ui::SlotRect slot = HudRenderer::inventorySlotRect(InventoryRegion::Inventory, 5, kW, kH, layout);
    const InventoryHit hit = HudRenderer::hitTestInventoryScreen(static_cast<int32_t>(slot.x + slot.size * 0.5f),
                                                                 static_cast<int32_t>(slot.y + slot.size * 0.5f),
                                                                 kW, kH, layout);
    EXPECT_EQ(hit.region, InventoryRegion::Inventory);
    EXPECT_EQ(hit.index, 5U);
    // The plain hit test keeps reporting None off the panel.
    EXPECT_EQ(HudRenderer::hitTestInventory(2, 2, kW, kH, layout).region, InventoryRegion::None);
}

// GUI Scale lays the HUD out in a (window / scale) canvas, scale = guiScale / 3. Auto at
// 1280x720 is guiScale 3, so the canvas is the window and the layout is unchanged.
TEST(HudInventoryLayout, AutoScaleAt720pKeepsLayout) {
    const ui::HudCanvas canvas = HudRenderer::hudCanvas(kW, kH, render::GuiScalePreference::Auto);
    EXPECT_FLOAT_EQ(canvas.scale, 1.0f);
    EXPECT_FLOAT_EQ(HudRenderer::hudScale(kW, kH, render::GuiScalePreference::Auto), 1.0f);
    EXPECT_EQ(canvas.width, kW);
    EXPECT_EQ(canvas.height, kH);
    for (const int32_t v : {0, 1, 333, 719}) {
        EXPECT_EQ(canvas.toCanvasX(v), v);
        EXPECT_EQ(canvas.toCanvasY(v), v);
    }
    const ui::InventoryLayoutKey layout{2, ui::ScreenKind::Inventory2x2, false};
    const ui::SlotRect a = HudRenderer::inventorySlotRect(InventoryRegion::Inventory, 7, kW, kH, layout);
    const ui::SlotRect b =
        HudRenderer::inventorySlotRect(InventoryRegion::Inventory, 7, canvas.width, canvas.height, layout);
    EXPECT_EQ(a.x, b.x);
    EXPECT_EQ(a.y, b.y);
    EXPECT_EQ(a.size, b.size);

    // 1920x1080: Auto is guiScale 4, the HUD grows by a third (Beta behaviour).
    const ui::HudCanvas big = HudRenderer::hudCanvas(1920, 1080, render::GuiScalePreference::Auto);
    EXPECT_NEAR(big.scale, 4.0f / 3.0f, 1e-6f);
    EXPECT_EQ(big.width, 1440);
    EXPECT_EQ(big.height, 810);
    // A fixed preference wins over the window size.
    EXPECT_NEAR(HudRenderer::hudScale(kW, kH, render::GuiScalePreference::Scale1x), 1.0f / 3.0f, 1e-6f);
    EXPECT_NEAR(HudRenderer::hudScale(kW, kH, render::GuiScalePreference::Scale4x), 4.0f / 3.0f, 1e-6f);
}

// Window clicks map back through the canvas onto the slot drawn there, at every scale.
TEST(HudInventoryLayout, SlotsMapBackAtEveryGuiScale) {
    struct Window {
        int32_t w;
        int32_t h;
    };
    struct Slots {
        InventoryRegion region;
        size_t count;
    };
    const ui::InventoryLayoutKey layout{3, ui::ScreenKind::Workbench3x3, true};
    for (const Window win : {Window{1280, 720}, Window{1920, 1080}, Window{854, 480}, Window{2560, 1440}}) {
        for (const render::GuiScalePreference pref :
             {render::GuiScalePreference::Auto, render::GuiScalePreference::Scale1x, render::GuiScalePreference::Scale2x,
              render::GuiScalePreference::Scale3x, render::GuiScalePreference::Scale4x}) {
            const ui::HudCanvas c = HudRenderer::hudCanvas(win.w, win.h, pref);
            ASSERT_GT(c.width, 0);
            for (const Slots s : {Slots{InventoryRegion::CraftingGrid, 9}, Slots{InventoryRegion::CraftingResult, 1},
                                  Slots{InventoryRegion::Palette, 36},
                                  Slots{InventoryRegion::Inventory, physics::Player::TOTAL_INVENTORY_SIZE}}) {
                for (size_t i = 0; i < s.count; ++i) {
                    const ui::SlotRect r = HudRenderer::inventorySlotRect(s.region, i, c.width, c.height, layout);
                    // Where the ortho matrix draws the slot centre in the window.
                    const auto wx = static_cast<int32_t>((r.x + r.size * 0.5f) * static_cast<float>(win.w) /
                                                         static_cast<float>(c.width));
                    const auto wy = static_cast<int32_t>((r.y + r.size * 0.5f) * static_cast<float>(win.h) /
                                                         static_cast<float>(c.height));
                    const InventoryHit hit = HudRenderer::hitTestInventoryScreen(c.toCanvasX(wx), c.toCanvasY(wy),
                                                                                 c.width, c.height, layout);
                    EXPECT_EQ(hit.region, s.region) << win.w << "x" << win.h << " pref " << static_cast<int>(pref);
                    EXPECT_EQ(hit.index, i);
                }
            }
            // The window corner is off the panel whenever the panel fits the canvas.
            const ui::SlotRect panel = HudRenderer::inventorySlotRect(InventoryRegion::None, 0, c.width, c.height, layout);
            if (panel.x < 2.0f || panel.y < 2.0f) continue;
            EXPECT_EQ(HudRenderer::hitTestInventoryScreen(c.toCanvasX(1), c.toCanvasY(1), c.width, c.height, layout)
                          .region,
                      InventoryRegion::Outside);
        }
    }
}

// The tooltip names the hovered item; a tool adds its remaining durability.
TEST(HudInventoryLayout, TooltipShowsNameAndToolDurability) {
    EXPECT_TRUE(HudRenderer::inventoryTooltip({}).empty());
    const std::vector<std::string> dirt = HudRenderer::inventoryTooltip({voxel::BlockId::Dirt, 12});
    ASSERT_EQ(dirt.size(), 1U);
    EXPECT_FALSE(dirt[0].empty());
    const std::vector<std::string> pick =
        HudRenderer::inventoryTooltip({gameplay::items::StonePickaxe, 1, 31}); // Stone: 131 uses
    ASSERT_EQ(pick.size(), 2U);
    EXPECT_EQ(pick[1], "100 / 131");
}

// The 4 armor slots sit left of the inventory rows, inside the panel, on the player's
// inventory screen only, and overlap nothing else.
TEST(HudInventoryLayout, ArmorSlotsOnlyOnThePlayerInventory) {
    const auto overlaps = [](const ui::SlotRect& a, const ui::SlotRect& b) {
        return a.x < b.x + b.size && b.x < a.x + a.size && a.y < b.y + b.size && b.y < a.y + a.size;
    };
    const ui::SlotRect panel = HudRenderer::inventorySlotRect(InventoryRegion::None, 0, kW, kH, 2);
    for (size_t i = 0; i < physics::Player::ARMOR_SLOT_COUNT; ++i) {
        const InventoryHit hit = hitCentre(InventoryRegion::Armor, i, 2);
        EXPECT_EQ(hit.region, InventoryRegion::Armor) << i;
        EXPECT_EQ(hit.index, i);
        const ui::SlotRect r = HudRenderer::inventorySlotRect(InventoryRegion::Armor, i, kW, kH, 2);
        EXPECT_GT(r.size, 0.0f);
        EXPECT_GE(r.x, panel.x);
        EXPECT_LT(r.x + r.size, HudRenderer::inventorySlotRect(InventoryRegion::Inventory, 9, kW, kH, 2).x);
        for (size_t j = 0; j < physics::Player::TOTAL_INVENTORY_SIZE; ++j) {
            EXPECT_FALSE(overlaps(r, HudRenderer::inventorySlotRect(InventoryRegion::Inventory, j, kW, kH, 2)));
        }
        for (size_t j = 0; j < 4; ++j) {
            EXPECT_FALSE(overlaps(r, HudRenderer::inventorySlotRect(InventoryRegion::CraftingGrid, j, kW, kH, 2)));
        }
        EXPECT_FALSE(overlaps(r, HudRenderer::inventorySlotRect(InventoryRegion::CraftingResult, 0, kW, kH, 2)));
        // The workbench and the containers have no armor slots.
        EXPECT_EQ(HudRenderer::inventorySlotRect(InventoryRegion::Armor, i, kW, kH, 3).size, 0.0f);
        EXPECT_NE(hitCentre(InventoryRegion::Armor, i, 3).region, InventoryRegion::Armor);
    }
    EXPECT_EQ(HudRenderer::inventorySlotRect(InventoryRegion::Armor, 4, kW, kH, 2).size, 0.0f);
}

TEST(HudInventoryLayout, ArmorTooltipShowsDurability) {
    const auto lines = HudRenderer::inventoryTooltip({gameplay::ItemId{306}, 1, 32});
    ASSERT_EQ(lines.size(), 2U);
    EXPECT_EQ(lines[0], "Iron Helmet");
    EXPECT_EQ(lines[1], "100 / 132");
}
