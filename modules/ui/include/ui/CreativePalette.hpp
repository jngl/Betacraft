#pragma once

#include "gameplay/ItemIds.hpp"
#include "voxel/Block.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace betacraft::ui {

// The 36 blocks the inventory screen offers for free (click to equip in the selected slot).
// The two pickaxes are listed by their legacy BlockId for ui::CraftingSystem, which is not
// migrated; the game hands out paletteItem(), in the Beta item numbering.
inline constexpr std::array<voxel::BlockId, 36> kCreativePalette{{
    voxel::BlockId::Stone, voxel::BlockId::Grass, voxel::BlockId::Dirt, voxel::BlockId::Cobblestone,
    voxel::BlockId::OakPlanks, voxel::BlockId::OakLog, voxel::BlockId::BirchLog, voxel::BlockId::PineLog,
    voxel::BlockId::Leaves, voxel::BlockId::Glass, voxel::BlockId::Sand, voxel::BlockId::Sandstone,
    voxel::BlockId::Gravel, voxel::BlockId::Bricks, voxel::BlockId::CobbleSlab, voxel::BlockId::MossyCobble,
    voxel::BlockId::Obsidian, voxel::BlockId::Bookshelf, voxel::BlockId::CraftingTable, voxel::BlockId::Furnace,
    voxel::BlockId::Piston, voxel::BlockId::TNT, voxel::BlockId::Torch, voxel::BlockId::Glowstone,
    voxel::BlockId::Wool, voxel::BlockId::SnowBlock, voxel::BlockId::Ice, voxel::BlockId::Clay,
    voxel::BlockId::Cactus, voxel::BlockId::Dandelion, voxel::BlockId::Rose, voxel::BlockId::TallGrass,
    voxel::BlockId::Sapling, voxel::BlockId::SugarCane, voxel::BlockId::StonePickaxe, voxel::BlockId::DiamondPickaxe
}};

[[nodiscard]] inline std::span<const voxel::BlockId> creativePalette() noexcept {
    return kCreativePalette;
}

// The item a click on palette slot `index` equips (kNoItem past the end).
[[nodiscard]] constexpr gameplay::ItemId paletteItem(size_t index) noexcept {
    return index < kCreativePalette.size() ? gameplay::canonicalize(gameplay::toItemId(kCreativePalette[index]))
                                           : gameplay::kNoItem;
}

} // namespace betacraft::ui
