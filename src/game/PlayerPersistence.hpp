#pragma once

#include "physics/Player.hpp"
#include "world/WorldStorage.hpp"

#include <cstdint>
#include <span>
#include <vector>

namespace betacraft::game {

// Snapshot of the player for level.dat V2. `pending` holds the stacks an open inventory
// screen keeps outside the inventory (InventoryCrafting::pendingStacks): they are merged
// into a copy of the player first, so saving with the screen open loses nothing.
[[nodiscard]] world::PlayerSaveData capturePlayer(const physics::Player& player,
                                                  std::span<const physics::InventorySlot> pending = {});
// Same, and the pending stacks a full inventory cannot take are appended to `overflow`
// (the caller drops them in the world) instead of vanishing.
[[nodiscard]] world::PlayerSaveData capturePlayer(const physics::Player& player,
                                                  std::span<const physics::InventorySlot> pending,
                                                  std::vector<physics::InventorySlot>* overflow);

// Applies a saved snapshot, clamping every value: counts to [1, maxStackSize], health to
// [0, 20], air to [0, 300], fire (S3.1 `player.fire`) to [0, 600], the selected slot to the
// hotbar. Unknown item ids empty their slot with a warning. The legacy ids 100..105 of `itemsScheme` 1 files are folded onto
// their Beta numbers (S2.10); found in a scheme 2 file they are folded too, with a warning.
void restorePlayer(physics::Player& player, const world::PlayerSaveData& data,
                   uint32_t itemsScheme = world::kCurrentItemsScheme);

} // namespace betacraft::game
