#pragma once

#include "crafting/CraftingGrid.hpp"
#include "crafting/RecipeTypes.hpp"
#include "gameplay/Armor.hpp"
#include "gameplay/ItemStack.hpp"
#include "physics/Player.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace betacraft::game {

// Field-by-field conversions between the three stack types that meet in the game layer:
// the inventory slot, the combat module's wearable ItemStack and the crafting grid's
// SlotStack. Ids are canonicalised so vanilla aliases (stick 280, coal 263...) fold onto the
// engine ids the inventory stores.

[[nodiscard]] inline gameplay::ItemStack toItemStack(const physics::InventorySlot& slot) noexcept {
    if (slot.isEmpty()) {
        return {};
    }
    const gameplay::ItemId id = gameplay::canonicalize(slot.id);
    if (const auto armor = gameplay::armorInfo(id)) {
        return gameplay::ItemStack{id, slot.count, slot.damage, armor->maxDamage}; // S6.7
    }
    if (id == gameplay::items::FlintAndSteel) {
        // S6.6: keeps its wear through every conversion (mining with it, dropping it...).
        return gameplay::ItemStack{id, slot.count, slot.damage, gameplay::kFlintAndSteelMaxDamage};
    }
    const auto tool = gameplay::findTool(id);
    return gameplay::ItemStack{id, slot.count, tool ? slot.damage : 0, tool ? tool->maxDamage : 0};
}

[[nodiscard]] inline physics::InventorySlot toInventorySlot(const gameplay::ItemStack& stack) noexcept {
    if (stack.empty()) {
        return {};
    }
    return physics::InventorySlot{gameplay::canonicalize(stack.id), stack.count, stack.damage};
}

[[nodiscard]] inline crafting::SlotStack toSlotStack(const physics::InventorySlot& slot) noexcept {
    if (slot.isEmpty()) {
        return {};
    }
    return crafting::SlotStack{
        gameplay::canonicalize(slot.id),
        static_cast<uint8_t>(std::clamp(slot.count, 0, static_cast<int32_t>(std::numeric_limits<uint8_t>::max()))),
        static_cast<uint16_t>(std::clamp(slot.damage, 0, static_cast<int32_t>(std::numeric_limits<uint16_t>::max())))};
}

[[nodiscard]] inline physics::InventorySlot toInventorySlot(const crafting::SlotStack& stack) noexcept {
    if (stack.empty()) {
        return {};
    }
    return physics::InventorySlot{gameplay::canonicalize(stack.id), stack.count, stack.damage};
}

[[nodiscard]] inline physics::InventorySlot toInventorySlot(const crafting::RecipeResult& result) noexcept {
    return toInventorySlot(crafting::SlotStack{result.id, result.count, 0});
}

} // namespace betacraft::game
