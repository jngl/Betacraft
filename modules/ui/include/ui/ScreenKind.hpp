#pragma once

#include <cstddef>
#include <cstdint>

namespace betacraft::ui {

// Which screen the inventory UI shows (game::InventoryCrafting, HudRenderer): the
// player's 2x2 grid, an opened workbench, or a block entity's container.
enum class ScreenKind : uint8_t { Inventory2x2, Workbench3x3, Furnace, Chest };

[[nodiscard]] constexpr bool isContainerScreen(ScreenKind kind) noexcept {
    return kind == ScreenKind::Furnace || kind == ScreenKind::Chest;
}

// Beta TileEntityFurnace slots as InventoryCrafting::clickContainerSlot indexes them.
inline constexpr size_t kFurnaceInputSlot = 0;
inline constexpr size_t kFurnaceFuelSlot = 1;
inline constexpr size_t kFurnaceOutputSlot = 2;
inline constexpr size_t kFurnaceSlotCount = 3;

} // namespace betacraft::ui
