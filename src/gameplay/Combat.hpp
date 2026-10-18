#pragma once

#include "gameplay/ItemStack.hpp"

#include <cstdint>

namespace betacraft::gameplay {

// All damage values are in half-hearts, like Beta (player max health = 20).
inline constexpr int32_t kFistDamage = 1;
inline constexpr int32_t kSwordBaseDamage = 4;

struct AttackModifiers {
    // Beta 1.7.3 grants +1 damage when the attacker's vertical velocity is negative. It is
    // the only "critical hit" of that version (the random crit arrived in Beta 1.8).
    bool attackerFalling{false};
};

// Raw entity damage of the held item (Beta Item.getDamageVsEntity).
[[nodiscard]] int32_t weaponDamage(const ItemStack& held) noexcept;

// Damage actually dealt by a melee swing, including situational modifiers.
[[nodiscard]] int32_t attackDamage(const ItemStack& held, AttackModifiers modifiers = {}) noexcept;

} // namespace betacraft::gameplay
