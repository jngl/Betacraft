#pragma once

#include "gameplay/ItemIds.hpp"
#include "voxel/Block.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace betacraft::ui {

// The 36 blocks the inventory screen offers for free (click to equip in the selected slot).
// The two pickaxes are listed by their legacy BlockId for ui::CraftingSystem, which is not
// migrated; the game hands out paletteItem(), in the Beta item numbering (S2.10).
inline constexpr std::array<world::BlockId, 36> kCreativePalette{{
    world::BlockId::Stone, world::BlockId::Grass, world::BlockId::Dirt, world::BlockId::Cobblestone,
    world::BlockId::OakPlanks, world::BlockId::OakLog, world::BlockId::BirchLog, world::BlockId::PineLog,
    world::BlockId::Leaves, world::BlockId::Glass, world::BlockId::Sand, world::BlockId::Sandstone,
    world::BlockId::Gravel, world::BlockId::Bricks, world::BlockId::CobbleSlab, world::BlockId::MossyCobble,
    world::BlockId::Obsidian, world::BlockId::Bookshelf, world::BlockId::CraftingTable, world::BlockId::Furnace,
    world::BlockId::Piston, world::BlockId::TNT, world::BlockId::Torch, world::BlockId::Glowstone,
    world::BlockId::Wool, world::BlockId::SnowBlock, world::BlockId::Ice, world::BlockId::Clay,
    world::BlockId::Cactus, world::BlockId::Dandelion, world::BlockId::Rose, world::BlockId::TallGrass,
    world::BlockId::Sapling, world::BlockId::SugarCane, world::BlockId::StonePickaxe, world::BlockId::DiamondPickaxe
}};

[[nodiscard]] inline std::span<const world::BlockId> creativePalette() noexcept {
    return kCreativePalette;
}

// The item a click on palette slot `index` equips (kNoItem past the end).
[[nodiscard]] constexpr gameplay::ItemId paletteItem(size_t index) noexcept {
    return index < kCreativePalette.size() ? gameplay::canonicalize(gameplay::toItemId(kCreativePalette[index]))
                                           : gameplay::kNoItem;
}

} // namespace betacraft::ui
