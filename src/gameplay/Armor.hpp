#pragma once

#include "gameplay/ItemIds.hpp"
#include "gameplay/ItemStack.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace betacraft::gameplay {

// S6.7 Beta 1.7.3 armor (ItemArmor 298..317, InventoryPlayer armor slots, EntityPlayer
// damageEntity). Slots are indexed in this order everywhere (Player, level.dat `armor` keys).
enum class ArmorSlot : uint8_t { Helmet, Chestplate, Leggings, Boots };
inline constexpr size_t kArmorSlotCount = 4;

// Material rows of the item ids, in Beta's numbering order: 298 + 4 x material + slot.
enum class ArmorMaterial : uint8_t { Leather, Chain, Iron, Diamond, Gold };

struct ArmorInfo {
    ArmorSlot slot{ArmorSlot::Helmet};
    int32_t reduce{0};    // Beta damageReduceAmount: the same for every material
    int32_t maxDamage{0}; // Beta maxDamage: the piece breaks once its damage exceeds it
};

inline constexpr ItemId kFirstArmorItem = 298; // Leather helmet
inline constexpr ItemId kLastArmorItem = 317;  // Gold boots

namespace detail {
// Beta ItemArmor.damageReduceAmountArray / maxDamageArray, indexed by ArmorSlot.
inline constexpr std::array<int32_t, kArmorSlotCount> kArmorReduce{3, 8, 6, 3};
inline constexpr std::array<int32_t, kArmorSlotCount> kArmorBaseDurability{11, 16, 15, 13};
// Beta ItemArmor `material` argument (the durability shift): leather 0, chain 1, iron 2,
// diamond 3, gold 1.
inline constexpr std::array<int32_t, 5> kArmorLevel{0, 1, 2, 3, 1};
} // namespace detail

[[nodiscard]] constexpr ItemId armorItemId(ArmorMaterial material, ArmorSlot slot) noexcept {
    return static_cast<ItemId>(kFirstArmorItem + 4U * static_cast<uint32_t>(material) + static_cast<uint32_t>(slot));
}

[[nodiscard]] constexpr std::optional<ArmorMaterial> armorMaterial(ItemId id) noexcept {
    if (id < kFirstArmorItem || id > kLastArmorItem) {
        return std::nullopt;
    }
    return static_cast<ArmorMaterial>((id - kFirstArmorItem) / 4U);
}

// nullopt for every id that is not one of the 20 armor pieces.
[[nodiscard]] constexpr std::optional<ArmorInfo> armorInfo(ItemId id) noexcept {
    const std::optional<ArmorMaterial> material = armorMaterial(id);
    if (!material) {
        return std::nullopt;
    }
    const auto slot = static_cast<size_t>((id - kFirstArmorItem) % 4U);
    // Beta: maxDamageArray[type] * 3 << material.
    const int32_t maxDamage = (detail::kArmorBaseDurability[slot] * 3)
                              << detail::kArmorLevel[static_cast<size_t>(*material)];
    return ArmorInfo{static_cast<ArmorSlot>(slot), detail::kArmorReduce[slot], maxDamage};
}

// "Leather Helmet", "Iron Chestplate"...; empty for non-armor ids.
[[nodiscard]] std::string_view armorName(ItemId id) noexcept;

// A one-item stack of the piece with its damage and maxDamage filled in (empty for non-armor).
[[nodiscard]] ItemStack makeArmorStack(ItemId id, int32_t damage = 0) noexcept;

// Beta InventoryPlayer.getTotalArmorValue: ((sum reduce - 1) x durability left) / total
// durability + 1 over the worn pieces, 0 without armor. A full fresh set is worth 20.
[[nodiscard]] int32_t totalArmorValue(std::span<const ItemStack, kArmorSlotCount> armor) noexcept;

// Beta EntityPlayer.damageEntity: k = dmg x (25 - armor) + remainder, returns k / 25 and keeps
// k % 25 in `remainder` for the next hit.
[[nodiscard]] int32_t absorbDamage(int32_t dmg, int32_t armor, int32_t& remainder) noexcept;

// Beta InventoryPlayer.damageArmor: every worn piece takes `amount` points of wear; a piece
// worn past its maxDamage breaks and its slot empties.
void damageArmor(std::span<ItemStack, kArmorSlotCount> armor, int32_t amount) noexcept;

} // namespace betacraft::gameplay
