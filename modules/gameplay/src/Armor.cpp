#include "gameplay/Armor.hpp"

#include <algorithm>

namespace betacraft::gameplay {

namespace {

constexpr std::array<std::array<std::string_view, kArmorSlotCount>, 5> kArmorNames{{
    {"Leather Cap", "Leather Tunic", "Leather Pants", "Leather Boots"},
    {"Chain Helmet", "Chain Chestplate", "Chain Leggings", "Chain Boots"},
    {"Iron Helmet", "Iron Chestplate", "Iron Leggings", "Iron Boots"},
    {"Diamond Helmet", "Diamond Chestplate", "Diamond Leggings", "Diamond Boots"},
    {"Gold Helmet", "Gold Chestplate", "Gold Leggings", "Gold Boots"},
}};

} // namespace

std::string_view armorName(ItemId id) noexcept {
    const std::optional<ArmorMaterial> material = armorMaterial(id);
    if (!material) {
        return {};
    }
    return kArmorNames[static_cast<size_t>(*material)][static_cast<size_t>((id - kFirstArmorItem) % 4U)];
}

ItemStack makeArmorStack(ItemId id, int32_t damage) noexcept {
    const std::optional<ArmorInfo> info = armorInfo(id);
    if (!info) {
        return {};
    }
    return ItemStack{id, 1, std::clamp(damage, 0, info->maxDamage), info->maxDamage};
}

int32_t totalArmorValue(std::span<const ItemStack, kArmorSlotCount> armor) noexcept {
    int32_t reduce = 0;
    int32_t left = 0;
    int32_t total = 0;
    for (const ItemStack& piece : armor) {
        if (piece.empty()) {
            continue;
        }
        const std::optional<ArmorInfo> info = armorInfo(piece.id);
        if (!info) {
            continue;
        }
        total += info->maxDamage;
        left += info->maxDamage - std::clamp(piece.damage, 0, info->maxDamage);
        reduce += info->reduce;
    }
    if (total == 0) {
        return 0;
    }
    return ((reduce - 1) * left) / total + 1;
}

int32_t absorbDamage(int32_t dmg, int32_t armor, int32_t& remainder) noexcept {
    if (dmg <= 0) {
        return 0;
    }
    const int32_t k = dmg * (25 - std::clamp(armor, 0, 25)) + std::max(0, remainder);
    remainder = k % 25;
    return k / 25;
}

void damageArmor(std::span<ItemStack, kArmorSlotCount> armor, int32_t amount) noexcept {
    if (amount <= 0) {
        return;
    }
    for (ItemStack& piece : armor) {
        const std::optional<ArmorInfo> info = piece.empty() ? std::nullopt : armorInfo(piece.id);
        if (!info) {
            continue;
        }
        piece.maxDamage = info->maxDamage;
        (void)piece.applyWear(amount);
    }
}

} // namespace betacraft::gameplay
