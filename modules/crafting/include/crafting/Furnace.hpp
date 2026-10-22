#pragma once

#include "crafting/SmeltingRecipes.hpp"
#include "voxel/BlockEntity.hpp"

namespace betacraft::crafting {

struct FurnaceTickResult {
    bool changed{false};    // Any slot or timer changed: the block entity must be saved
    bool litChanged{false}; // The furnace started or stopped burning (Furnace <-> LitFurnace)
    bool smelted{false};    // One item went from the input to the output
};

// Whether the input has a known result that fits the output (Beta TileEntityFurnace.canSmelt):
// the output is empty, or holds the same item with room for one more result.
[[nodiscard]] bool canSmelt(const voxel::FurnaceData& furnace, const SmeltingBook& book) noexcept;

// One game tick of Beta 1.7.3 TileEntityFurnace.updateEntity:
//  * a burning furnace burns one tick of its fuel;
//  * at 0 it lights the next fuel item only if canSmelt holds;
//  * while it burns and canSmelt holds, cookTicks grows and the item smelts at kSmeltTicks;
//    otherwise cookTicks drops back to 0.
FurnaceTickResult tickFurnace(voxel::FurnaceData& furnace, const SmeltingBook& book) noexcept;

// HUD arrow (cook) and flame (burn) fill, 0..1.
[[nodiscard]] float cookProgress(const voxel::FurnaceData& furnace) noexcept;
[[nodiscard]] float burnProgress(const voxel::FurnaceData& furnace) noexcept;

} // namespace betacraft::crafting
