#pragma once

#include "gameplay/ItemIds.hpp"
#include "voxel/Block.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace betacraft::crafting {

// Item ids, tool tables, aliases and stack limits come from the single scheme owned by the
// tools/combat module (gameplay/ItemIds.hpp); this header re-exports it under the crafting
// names and adds the BlockId bridge the crafting grid needs.
using ItemId = gameplay::ItemId;
using gameplay::ToolInfo;
using gameplay::ToolKind;
using gameplay::ToolMaterial;
using gameplay::canonicalize;
using gameplay::toItemId;
using gameplay::toolInfo;
using gameplay::toolItemId;
using gameplay::toVanillaId;
namespace items = gameplay::items;

inline constexpr ItemId kAir = gameplay::kNoItem;
inline constexpr uint8_t kMaxStackSize = 64;

// Numbers below 256 are reserved for voxel::BlockId (the legacy block/item
// space); it says nothing about whether the codebase defines that number.
[[nodiscard]] constexpr bool isBlockRange(ItemId id) noexcept {
    return id < 256;
}

namespace detail {

// Every enumerator voxel::BlockId defines (Air excluded). The enum has gaps
// (19, 22, ...) and stops at Count = 106, so a range check alone would hand out
// numbers without a BlockDef. Test ItemIds.ToBlockIdAgreesWithBlockRegistry
// fails if this list drifts from BlockRegistry.
inline constexpr voxel::BlockId kDefinedBlockIds[] = {
    voxel::BlockId::Stone, voxel::BlockId::Grass, voxel::BlockId::Dirt, voxel::BlockId::Cobblestone,
    voxel::BlockId::OakPlanks, voxel::BlockId::Sapling, voxel::BlockId::Bedrock, voxel::BlockId::Water,
    voxel::BlockId::StillWater, voxel::BlockId::Lava, voxel::BlockId::StillLava, voxel::BlockId::Sand,
    voxel::BlockId::Gravel, voxel::BlockId::GoldOre, voxel::BlockId::IronOre,
    voxel::BlockId::CoalOre, voxel::BlockId::OakLog, voxel::BlockId::Leaves, voxel::BlockId::Glass,
    voxel::BlockId::LapisOre, voxel::BlockId::Sandstone, voxel::BlockId::TallGrass, voxel::BlockId::DeadBush,
    voxel::BlockId::Piston, voxel::BlockId::PistonHead, voxel::BlockId::Wool, voxel::BlockId::Dandelion,
    voxel::BlockId::Rose, voxel::BlockId::CobbleSlab, voxel::BlockId::Bricks, voxel::BlockId::TNT,
    voxel::BlockId::Bookshelf, voxel::BlockId::MossyCobble, voxel::BlockId::Obsidian, voxel::BlockId::Torch,
    voxel::BlockId::MobSpawner,
    voxel::BlockId::Chest, voxel::BlockId::DiamondOre, voxel::BlockId::CraftingTable, voxel::BlockId::Furnace,
    voxel::BlockId::LitFurnace,
    voxel::BlockId::RedstoneOre, voxel::BlockId::SnowGrass, voxel::BlockId::Ice, voxel::BlockId::SnowBlock,
    voxel::BlockId::Cactus, voxel::BlockId::Clay, voxel::BlockId::SugarCane, voxel::BlockId::Glowstone,
    voxel::BlockId::BirchLog, voxel::BlockId::PineLog, voxel::BlockId::Stick, voxel::BlockId::CoalItem,
    voxel::BlockId::WoodPickaxe, voxel::BlockId::StonePickaxe, voxel::BlockId::IronPickaxe,
    voxel::BlockId::DiamondPickaxe,
    // S5.4 shapes
    voxel::BlockId::Bed, voxel::BlockId::WoodStairs, voxel::BlockId::Crops, voxel::BlockId::Farmland,
    voxel::BlockId::WoodDoor, voxel::BlockId::Ladder, voxel::BlockId::CobbleStairs, voxel::BlockId::Fence,
    // S8.5 redstone
    voxel::BlockId::RedstoneWire, voxel::BlockId::Lever, voxel::BlockId::StonePressurePlate, voxel::BlockId::IronDoor,
    voxel::BlockId::WoodPressurePlate, voxel::BlockId::RedstoneTorchOff, voxel::BlockId::RedstoneTorchOn,
    voxel::BlockId::StoneButton
};

[[nodiscard]] constexpr bool isDefinedBlockId(ItemId id) noexcept {
    for (const voxel::BlockId b : kDefinedBlockIds) {
        if (static_cast<ItemId>(b) == id) {
            return true;
        }
    }
    return false;
}

} // namespace detail

// The voxel::BlockId with this number, only if the codebase defines it (so
// the result can index BlockRegistry). The legacy item-only ids 100..105 are
// included as numbers, but nothing produces them since S2.10 (stick, coal and
// the pickaxes are Beta numbers now). Every item >= 256 returns nullopt.
[[nodiscard]] constexpr std::optional<voxel::BlockId> toBlockId(ItemId id) noexcept {
    if (id == kAir) {
        return voxel::BlockId::Air;
    }
    if (!isBlockRange(id) || !detail::isDefinedBlockId(id)) {
        return std::nullopt;
    }
    return static_cast<voxel::BlockId>(id);
}

// True when the item can be put down as a world block: a defined BlockId that
// is not item-only. Item-only ids start at 100 (voxel::BlockId::Stick).
[[nodiscard]] constexpr bool isPlaceableBlock(ItemId id) noexcept {
    return id != kAir && id < toItemId(voxel::BlockId::Stick) && detail::isDefinedBlockId(id);
}

namespace detail {
[[nodiscard]] constexpr bool everyPlaceableBlockFitsAByte() noexcept {
    for (const voxel::BlockId b : kDefinedBlockIds) {
        if (isPlaceableBlock(toItemId(b)) && toItemId(b) > 255) {
            return false;
        }
    }
    return true;
}
} // namespace detail

// S2.10: a placeable block keeps an id below 256, the chunk format's byte (items are >= 256).
static_assert(detail::everyPlaceableBlockFitsAByte(), "a placeable BlockId exceeds 255");

// 0 means "not damageable". Same budget the combat module wears down (Beta: the tool breaks
// once damage exceeds it, so it has maxDurability + 1 uses).
[[nodiscard]] constexpr uint16_t maxDurability(ItemId id) noexcept {
    const auto info = toolInfo(id);
    return info ? static_cast<uint16_t>(gameplay::materialStats(info->material).maxUses) : uint16_t{0};
}

// Beta Item.maxStackSize, narrowed to the SlotStack count type.
[[nodiscard]] constexpr uint8_t maxStackSize(ItemId id) noexcept {
    return static_cast<uint8_t>(gameplay::maxStackSize(id));
}

} // namespace betacraft::crafting
