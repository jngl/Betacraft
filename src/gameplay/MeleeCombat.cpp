#include "gameplay/MeleeCombat.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace betacraft::gameplay {

namespace {
[[nodiscard]] bool isFinite(const glm::vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}
} // namespace

MeleeResult performMeleeAttack(const AttackerState& attacker, ItemStack& weapon, TargetState& target,
                               const KnockbackParams& knockback) noexcept {
    MeleeResult result{};
    // A removed corpse cannot be targeted. A dying one (death animation still playing) can:
    // applyDamage ignores the hit, but the weapon still wears as in Beta.
    if (isCorpseExpired(target.health)) {
        return result;
    }
    const AttackModifiers modifiers{.attackerFalling = attacker.verticalVelocity < 0.0f};
    result.critical = modifiers.attackerFalling;
    // Damage is read before wear so the swing that breaks a weapon still uses its stats.
    result.damage = attackDamage(weapon, modifiers);
    result.hit = applyDamage(target.health, result.damage);

    if (result.hit.appliesKnockback()) {
        result.impulse = computeKnockbackImpulse(attacker.look, attacker.eyePosition, target.position, knockback);
        target.velocity = applyKnockback(target.velocity, result.impulse, knockback);
    }
    result.weaponWear = onEntityHit(weapon);
    return result;
}

std::optional<float> rayAabbDistance(const glm::vec3& origin, const glm::vec3& direction,
                                     const math::AABB& box) noexcept {
    if (!isFinite(origin) || !isFinite(direction) || glm::dot(direction, direction) <= 0.0f) {
        return std::nullopt;
    }
    float tNear = 0.0f; // Clamped at 0: hits behind the origin are rejected
    float tFar = std::numeric_limits<float>::infinity();
    for (int axis = 0; axis < 3; ++axis) {
        const float o = origin[axis];
        const float d = direction[axis];
        const float lo = box.min[axis];
        const float hi = box.max[axis];
        if (d == 0.0f) {
            // Parallel to this slab: either always inside it or never.
            if (o < lo || o > hi) {
                return std::nullopt;
            }
            continue;
        }
        float t1 = (lo - o) / d;
        float t2 = (hi - o) / d;
        if (t1 > t2) {
            std::swap(t1, t2);
        }
        tNear = std::max(tNear, t1);
        tFar = std::min(tFar, t2);
        if (tNear > tFar) {
            return std::nullopt;
        }
    }
    return tNear;
}

std::optional<TargetHit> pickMeleeTarget(const glm::vec3& eye, const glm::vec3& look,
                                         std::span<const math::AABB> candidates,
                                         float maxDistance) noexcept {
    if (!isFinite(look) || glm::dot(look, look) <= 0.0f) {
        return std::nullopt;
    }
    const glm::vec3 dir = glm::normalize(look); // Unit direction => ray parameter is metres
    std::optional<TargetHit> best;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const auto t = rayAabbDistance(eye, dir, candidates[i]);
        // Written so a NaN maxDistance rejects the hit instead of granting unlimited reach.
        if (!t || !(*t <= maxDistance)) {
            continue;
        }
        if (!best || *t < best->distance) {
            best = TargetHit{i, *t};
        }
    }
    return best;
}

} // namespace betacraft::gameplay
