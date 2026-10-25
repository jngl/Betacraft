#include "gameplay/ItemStack.hpp"

#include "gameplay/Armor.hpp"

#include <algorithm>

namespace betacraft::gameplay {

namespace {
[[nodiscard]] ToolType toolTypeOf(const ItemStack& stack) noexcept {
    const auto def = toolOf(stack);
    return def ? def->type : ToolType::None;
}
} // namespace

float ItemStack::durabilityFraction() const noexcept {
    if (!isDamageable()) {
        return 1.0f;
    }
    const float used = static_cast<float>(std::clamp(damage, 0, maxDamage));
    return 1.0f - used / static_cast<float>(maxDamage);
}

WearResult ItemStack::applyWear(int32_t amount) noexcept {
    if (amount <= 0 || !isDamageable()) {
        return WearResult::Unchanged;
    }
    // Saturating add: the stack breaks anyway, but avoid signed overflow on absurd inputs.
    damage = (damage > maxDamage - amount) ? maxDamage + 1 : damage + amount;
    if (damage <= maxDamage) {
        return WearResult::Worn;
    }
    --count;
    damage = 0;
    if (count <= 0) {
        *this = ItemStack{};
    }
    return WearResult::Broken;
}

std::optional<ToolDef> toolOf(const ItemStack& stack) noexcept {
    if (stack.empty()) {
        return std::nullopt;
    }
    return findTool(stack.id);
}

ItemStack makeStack(ItemId id, int32_t count) noexcept {
    const int32_t clamped = std::clamp(count, 0, maxStackSize(id));
    if (clamped == 0) {
        return {};
    }
    if (const std::optional<ArmorInfo> armor = armorInfo(id)) {
        return ItemStack{id, clamped, 0, armor->maxDamage}; // armor wears too
    }
    if (id == items::FlintAndSteel) {
        return ItemStack{id, clamped, 0, kFlintAndSteelMaxDamage};
    }
    const auto def = findTool(id);
    return ItemStack{def ? def->id : id, clamped, 0, def ? def->maxDamage : 0};
}

WearResult onBlockBroken(ItemStack& held) noexcept {
    return held.applyWear(wearForBlockBreak(toolTypeOf(held)));
}

WearResult onEntityHit(ItemStack& held) noexcept {
    return held.applyWear(wearForEntityHit(toolTypeOf(held)));
}

WearResult onSoilTilled(ItemStack& held) noexcept {
    return toolTypeOf(held) == ToolType::Hoe ? held.applyWear(1) : WearResult::Unchanged;
}

} // namespace betacraft::gameplay
