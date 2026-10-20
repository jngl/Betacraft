#include "gameplay/Combat.hpp"

namespace betacraft::gameplay {

int32_t weaponDamage(const ItemStack& held) noexcept {
    const auto tool = toolOf(held);
    if (!tool) {
        return kFistDamage;
    }
    const int32_t bonus = materialStats(tool->material).damageBonus;
    switch (tool->type) {
        case ToolType::Sword:   return kSwordBaseDamage + bonus * 2;
        case ToolType::Shovel:  return 1 + bonus;
        case ToolType::Pickaxe: return 2 + bonus;
        case ToolType::Axe:     return 3 + bonus;
        case ToolType::Hoe:     // Beta hoes do not override getDamageVsEntity
        case ToolType::None:    return kFistDamage;
    }
    return kFistDamage;
}

int32_t attackDamage(const ItemStack& held, AttackModifiers modifiers) noexcept {
    int32_t damage = weaponDamage(held);
    if (damage > 0 && modifiers.attackerFalling) {
        ++damage;
    }
    return damage;
}

} // namespace betacraft::gameplay
