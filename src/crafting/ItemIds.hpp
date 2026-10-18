#pragma once

#include "gameplay/ItemIds.hpp"
#include "world/Block.hpp"

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

// Numbers below 256 are reserved for world::BlockId (the legacy block/item
// space); it says nothing about whether the codebase defines that number.
[[nodiscard]] constexpr bool isBlockRange(ItemId id) noexcept {
    return id < 256;
}

namespace detail {

// Every enumerator world::BlockId defines (Air excluded). The enum has gaps
// (19, 22, ...) and stops at Count = 106, so a range check alone would hand out
// numbers without a BlockDef. Test ItemIds.ToBlockIdAgreesWithBlockRegistry
// fails if this list drifts from BlockRegistry.
inline constexpr world::BlockId kDefinedBlockIds[] = {
    world::BlockId::Stone, world::BlockId::Grass, world::BlockId::Dirt, world::BlockId::Cobblestone,
    world::BlockId::OakPlanks, world::BlockId::Sapling, world::BlockId::Bedrock, world::BlockId::Water,
    world::BlockId::StillWater, world::BlockId::Lava, world::BlockId::StillLava, world::BlockId::Sand,
    world::BlockId::Gravel, world::BlockId::GoldOre, world::BlockId::IronOre,
    world::BlockId::CoalOre, world::BlockId::OakLog, world::BlockId::Leaves, world::BlockId::Glass,
    world::BlockId::LapisOre, world::BlockId::Sandstone, world::BlockId::TallGrass, world::BlockId::DeadBush,
    world::BlockId::Piston, world::BlockId::PistonHead, world::BlockId::Wool, world::BlockId::Dandelion,
    world::BlockId::Rose, world::BlockId::CobbleSlab, world::BlockId::Bricks, world::BlockId::TNT,
    world::BlockId::Bookshelf, world::BlockId::MossyCobble, world::BlockId::Obsidian, world::BlockId::Torch,
    world::BlockId::MobSpawner,
    world::BlockId::Chest, world::BlockId::DiamondOre, world::BlockId::CraftingTable, world::BlockId::Furnace,
    world::BlockId::LitFurnace,
    world::BlockId::RedstoneOre, world::BlockId::SnowGrass, world::BlockId::Ice, world::BlockId::SnowBlock,
    world::BlockId::Cactus, world::BlockId::Clay, world::BlockId::SugarCane, world::BlockId::Glowstone,
    world::BlockId::BirchLog, world::BlockId::PineLog, world::BlockId::Stick, world::BlockId::CoalItem,
    world::BlockId::WoodPickaxe, world::BlockId::StonePickaxe, world::BlockId::IronPickaxe,
    world::BlockId::DiamondPickaxe,
    // S5.4 shapes
    world::BlockId::Bed, world::BlockId::WoodStairs, world::BlockId::Crops, world::BlockId::Farmland,
    world::BlockId::WoodDoor, world::BlockId::Ladder, world::BlockId::CobbleStairs, world::BlockId::Fence,
    // S8.5 redstone
    world::BlockId::RedstoneWire, world::BlockId::Lever, world::BlockId::StonePressurePlate, world::BlockId::IronDoor,
    world::BlockId::WoodPressurePlate, world::BlockId::RedstoneTorchOff, world::BlockId::RedstoneTorchOn,
    world::BlockId::StoneButton
};

[[nodiscard]] constexpr bool isDefinedBlockId(ItemId id) noexcept {
    for (const world::BlockId b : kDefinedBlockIds) {
        if (static_cast<ItemId>(b) == id) {
            return true;
        }
    }
    return false;
}

} // namespace detail

// The world::BlockId with this number, only if the codebase defines it (so
// the result can index BlockRegistry). The legacy item-only ids 100..105 are
// included as numbers, but nothing produces them since S2.10 (stick, coal and
// the pickaxes are Beta numbers now). Every item >= 256 returns nullopt.
[[nodiscard]] constexpr std::optional<world::BlockId> toBlockId(ItemId id) noexcept {
    if (id == kAir) {
        return world::BlockId::Air;
    }
    if (!isBlockRange(id) || !detail::isDefinedBlockId(id)) {
        return std::nullopt;
    }
    return static_cast<world::BlockId>(id);
}

// True when the item can be put down as a world block: a defined BlockId that
// is not item-only. Item-only ids start at 100 (world::BlockId::Stick).
[[nodiscard]] constexpr bool isPlaceableBlock(ItemId id) noexcept {
    return id != kAir && id < toItemId(world::BlockId::Stick) && detail::isDefinedBlockId(id);
}

namespace detail {
[[nodiscard]] constexpr bool everyPlaceableBlockFitsAByte() noexcept {
    for (const world::BlockId b : kDefinedBlockIds) {
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
