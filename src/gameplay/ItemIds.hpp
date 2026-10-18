#pragma once

#include "world/Block.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace betacraft::gameplay {

// The engine's single item id scheme. The tools/combat code (this module) owns it and the
// crafting module (src/crafting/ItemIds.hpp) re-exports it, so both always agree:
//  * 0..255 : world::BlockId values, unchanged. 54 (Chest) and 62 (lit furnace) are real
//             blocks with Beta's block ids.
//  * 256..  : vanilla Beta 1.7.3 item numbers, the canonical form of every item (S2.10,
//             items_scheme 2). Stick, coal and the four pickaxes moved there too.
//  * items::legacy::* are the item-only BlockIds 100..105 that older saves (items_scheme 1)
//             and the unmigrated ui::CraftingSystem use for those six items. The loaders fold
//             them onto the Beta numbers with canonicalize(); nothing produces them any more.
//             No new item is ever added to world::BlockId.
using ItemId = uint16_t;
inline constexpr ItemId kNoItem = 0;

[[nodiscard]] constexpr ItemId toItemId(world::BlockId id) noexcept {
    return static_cast<ItemId>(id);
}

namespace items {
inline constexpr ItemId Chest = toItemId(world::BlockId::Chest);
inline constexpr ItemId IronShovel = 256;
inline constexpr ItemId IronPickaxe = 257;
inline constexpr ItemId IronAxe = 258;
inline constexpr ItemId FlintAndSteel = 259; // Lights TNT (S6.6)
inline constexpr ItemId Apple = 260;
inline constexpr ItemId Bow = 261;
inline constexpr ItemId Arrow = 262;
inline constexpr ItemId Coal = 263;
inline constexpr ItemId Diamond = 264;
inline constexpr ItemId IronIngot = 265;
inline constexpr ItemId GoldIngot = 266;
inline constexpr ItemId IronSword = 267;
inline constexpr ItemId WoodSword = 268;
inline constexpr ItemId WoodShovel = 269;
inline constexpr ItemId WoodPickaxe = 270;
inline constexpr ItemId WoodAxe = 271;
inline constexpr ItemId StoneSword = 272;
inline constexpr ItemId StoneShovel = 273;
inline constexpr ItemId StonePickaxe = 274;
inline constexpr ItemId StoneAxe = 275;
inline constexpr ItemId DiamondSword = 276;
inline constexpr ItemId DiamondShovel = 277;
inline constexpr ItemId DiamondPickaxe = 278;
inline constexpr ItemId DiamondAxe = 279;
inline constexpr ItemId Stick = 280;
inline constexpr ItemId Bowl = 281;
inline constexpr ItemId MushroomStew = 282;
inline constexpr ItemId GoldSword = 283;
inline constexpr ItemId GoldShovel = 284;
inline constexpr ItemId GoldPickaxe = 285;
inline constexpr ItemId GoldAxe = 286;
inline constexpr ItemId String = 287;
inline constexpr ItemId Feather = 288;
inline constexpr ItemId Gunpowder = 289;
inline constexpr ItemId WoodHoe = 290;
inline constexpr ItemId StoneHoe = 291;
inline constexpr ItemId IronHoe = 292;
inline constexpr ItemId DiamondHoe = 293;
inline constexpr ItemId GoldHoe = 294;
inline constexpr ItemId Seeds = 295;
inline constexpr ItemId Wheat = 296;
inline constexpr ItemId Bread = 297;
inline constexpr ItemId Flint = 318;
inline constexpr ItemId RawPorkchop = 319;
inline constexpr ItemId CookedPorkchop = 320;
inline constexpr ItemId GoldenApple = 322;
inline constexpr ItemId WoodenDoor = 324; // Places BlockId::WoodDoor (S5.6)
inline constexpr ItemId Bucket = 325;
inline constexpr ItemId WaterBucket = 326;
inline constexpr ItemId LavaBucket = 327;
inline constexpr ItemId IronDoor = 330; // Places BlockId::IronDoor (S8.5)
inline constexpr ItemId Redstone = 331;  // Places BlockId::RedstoneWire (S8.5)
inline constexpr ItemId Snowball = 332;
inline constexpr ItemId MilkBucket = 335;
inline constexpr ItemId Leather = 334;
inline constexpr ItemId Paper = 339;
inline constexpr ItemId Book = 340;
inline constexpr ItemId Egg = 344;
inline constexpr ItemId GlowstoneDust = 348;
inline constexpr ItemId Bone = 352;
inline constexpr ItemId Sugar = 353;
inline constexpr ItemId Cake = 354;
inline constexpr ItemId Bed = 355; // Places BlockId::Bed (S5.8)
inline constexpr ItemId Cookie = 357;
// The vanilla numbers of the six items that used to have legacy ids; since S2.10 they are
// the canonical ids themselves.
namespace beta {
inline constexpr ItemId IronPickaxe = items::IronPickaxe;
inline constexpr ItemId Coal = items::Coal;
inline constexpr ItemId WoodPickaxe = items::WoodPickaxe;
inline constexpr ItemId StonePickaxe = items::StonePickaxe;
inline constexpr ItemId DiamondPickaxe = items::DiamondPickaxe;
inline constexpr ItemId Stick = items::Stick;
} // namespace beta
// items_scheme 1 ids of those items (item-only BlockIds): read from old saves, never produced.
namespace legacy {
inline constexpr ItemId Stick = toItemId(world::BlockId::Stick);
inline constexpr ItemId Coal = toItemId(world::BlockId::CoalItem);
inline constexpr ItemId WoodPickaxe = toItemId(world::BlockId::WoodPickaxe);
inline constexpr ItemId StonePickaxe = toItemId(world::BlockId::StonePickaxe);
inline constexpr ItemId IronPickaxe = toItemId(world::BlockId::IronPickaxe);
inline constexpr ItemId DiamondPickaxe = toItemId(world::BlockId::DiamondPickaxe);
inline constexpr ItemId First = Stick;
inline constexpr ItemId Last = DiamondPickaxe;
} // namespace legacy
} // namespace items

// Tool kinds in table order; ToolDefinitions.hpp adds ToolType (= ToolKind + None).
enum class ToolKind : uint8_t { Pickaxe, Axe, Shovel, Sword, Hoe };

// Material tiers of Beta 1.7.3 tools (EnumToolMaterial). Gold is deliberately fast but
// fragile and only harvest level 0, exactly like the original game.
enum class ToolMaterial : uint8_t {
    Wood = 0,
    Stone,
    Iron,
    Gold,
    Diamond
};

struct ToolMaterialStats {
    int32_t harvestLevel{0}; // 0..3: which ores the tier can harvest
    int32_t maxUses{0};      // Beta "maxDamage": the stack breaks once damage exceeds it
    float efficiency{1.0f};  // Mining speed multiplier on blocks the tool is effective against
    int32_t damageBonus{0};  // Added to the tool type's base entity damage
};

[[nodiscard]] constexpr ToolMaterialStats materialStats(ToolMaterial material) noexcept {
    switch (material) {
        case ToolMaterial::Wood:    return {0, 59, 2.0f, 0};
        case ToolMaterial::Stone:   return {1, 131, 4.0f, 1};
        case ToolMaterial::Iron:    return {2, 250, 6.0f, 2};
        case ToolMaterial::Gold:    return {0, 32, 12.0f, 0};
        case ToolMaterial::Diamond: return {3, 1561, 8.0f, 3};
    }
    return {};
}

struct ToolInfo {
    ToolKind kind{ToolKind::Pickaxe};
    ToolMaterial material{ToolMaterial::Wood};
};

namespace detail {

// Indexed [ToolKind][ToolMaterial], vanilla Beta 1.7.3 item numbers.
inline constexpr std::array<std::array<ItemId, 5>, 5> kToolTable{{
    {items::WoodPickaxe, items::StonePickaxe, items::IronPickaxe, items::GoldPickaxe, items::DiamondPickaxe},
    {items::WoodAxe, items::StoneAxe, items::IronAxe, items::GoldAxe, items::DiamondAxe},
    {items::WoodShovel, items::StoneShovel, items::IronShovel, items::GoldShovel, items::DiamondShovel},
    {items::WoodSword, items::StoneSword, items::IronSword, items::GoldSword, items::DiamondSword},
    {items::WoodHoe, items::StoneHoe, items::IronHoe, items::GoldHoe, items::DiamondHoe},
}};

struct Alias {
    ItemId legacy; // items_scheme 1 id (item-only BlockId)
    ItemId canonical;
    world::BlockId block; // Its BlockDef still holds the name and the sprite
};

inline constexpr std::array<Alias, 6> kAliases{{
    {items::legacy::Stick, items::Stick, world::BlockId::Stick},
    {items::legacy::Coal, items::Coal, world::BlockId::CoalItem},
    {items::legacy::WoodPickaxe, items::WoodPickaxe, world::BlockId::WoodPickaxe},
    {items::legacy::StonePickaxe, items::StonePickaxe, world::BlockId::StonePickaxe},
    {items::legacy::IronPickaxe, items::IronPickaxe, world::BlockId::IronPickaxe},
    {items::legacy::DiamondPickaxe, items::DiamondPickaxe, world::BlockId::DiamondPickaxe},
}};

// Every BlockId from 100 up is one of the six legacy items: nothing else may join them.
static_assert(static_cast<size_t>(world::BlockId::Count) == items::legacy::Last + 1U,
              "no new item in world::BlockId: use a Beta item number (>= 256) in gameplay::items");
static_assert(static_cast<size_t>(world::BlockId::Count) <= 256, "block ids must fit below the item range");

// Beta 1.7.3 Item.maxStackSize overrides for non-tool items, by vanilla item id.
[[nodiscard]] constexpr int32_t betaItemStackLimit(ItemId id) noexcept {
    if (id >= 298 && id <= 317) {
        return 1; // Armour (leather .. gold boots)
    }
    switch (id) {
        case 259:  // Flint and steel
        case 261:  // Bow
        case 260:  // Apple (Beta ItemFood is unstackable)
        case 282:  // Mushroom stew
        case 297:  // Bread
        case 319:  // Raw porkchop
        case 320:  // Cooked porkchop
        case 322:  // Golden apple
        case 349:  // Raw fish
        case 350:  // Cooked fish
        case 323:  // Sign
        case items::WoodenDoor:
        case items::IronDoor:
        case items::Bucket:
        case items::WaterBucket:
        case items::LavaBucket:
        case items::MilkBucket:
        case 328:  // Minecart
        case 342:  // Storage minecart
        case 343:  // Powered minecart
        case 329:  // Saddle
        case 333:  // Boat
        case 346:  // Fishing rod
        case items::Cake:
        case items::Bed:
        case 358:  // Map
        case 359:  // Shears
        case 2256: // Record "13"
        case 2257: // Record "cat"
            return 1;
        case items::Snowball:
        case items::Egg:
            return 16;
        case items::Cookie:
            return 8;
        default:
            return 64;
    }
}

} // namespace detail

// Folds the items_scheme 1 ids 100..105 onto their Beta numbers; every other id is already
// canonical. The loaders (inventory, chest) rely on it; it is idempotent.
[[nodiscard]] constexpr ItemId canonicalize(ItemId id) noexcept {
    for (const detail::Alias& a : detail::kAliases) {
        if (a.legacy == id) {
            return a.canonical;
        }
    }
    return id;
}

[[nodiscard]] constexpr bool isLegacyItemId(ItemId id) noexcept {
    return id >= items::legacy::First && id <= items::legacy::Last;
}

// The vanilla Beta number: the canonical id since S2.10.
[[nodiscard]] constexpr ItemId toVanillaId(ItemId id) noexcept {
    return canonicalize(id);
}

// The item-only BlockDef that describes a former legacy item (name, sprite), for the
// renderer; nullopt for every other id.
[[nodiscard]] constexpr std::optional<world::BlockId> legacyItemBlock(ItemId id) noexcept {
    const ItemId c = canonicalize(id);
    for (const detail::Alias& a : detail::kAliases) {
        if (a.canonical == c) {
            return a.block;
        }
    }
    return std::nullopt;
}

[[nodiscard]] constexpr ItemId toolItemId(ToolKind kind, ToolMaterial material) noexcept {
    const auto k = static_cast<size_t>(kind);
    const auto m = static_cast<size_t>(material);
    if (k >= detail::kToolTable.size() || m >= detail::kToolTable[k].size()) {
        return kNoItem;
    }
    return detail::kToolTable[k][m];
}

// Accepts canonical and vanilla-alias ids; nullopt for kNoItem and non-tools.
[[nodiscard]] constexpr std::optional<ToolInfo> toolInfo(ItemId id) noexcept {
    const ItemId c = canonicalize(id);
    if (c == kNoItem) {
        return std::nullopt;
    }
    // 25 entries: a linear scan is cheaper than any hashing and keeps the table constexpr.
    for (size_t k = 0; k < detail::kToolTable.size(); ++k) {
        for (size_t m = 0; m < detail::kToolTable[k].size(); ++m) {
            if (detail::kToolTable[k][m] == c) {
                return ToolInfo{static_cast<ToolKind>(k), static_cast<ToolMaterial>(m)};
            }
        }
    }
    return std::nullopt;
}

// Beta 1.7.3 Item.maxStackSize: 1 for tools, armour, food, buckets, signs, doors, vehicles...,
// 16 for eggs / snowballs, 8 for cookies, 64 otherwise; 0 for kNoItem.
[[nodiscard]] constexpr int32_t maxStackSize(ItemId id) noexcept {
    if (id == kNoItem) {
        return 0;
    }
    const ItemId c = canonicalize(id);
    return toolInfo(c).has_value() ? 1 : detail::betaItemStackLimit(c);
}

} // namespace betacraft::gameplay

namespace betacraft::world {

// Item ids below 256 are block ids, so an inventory or drop id compares against a BlockId
// directly (found by ADL on BlockId; C++20 also rewrites the reversed and != forms).
[[nodiscard]] constexpr bool operator==(gameplay::ItemId item, BlockId block) noexcept {
    return item == gameplay::toItemId(block);
}

} // namespace betacraft::world
