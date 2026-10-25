#pragma once

#include <glm/glm.hpp>

#include <cstdint>

namespace betacraft::gameplay {

// V = normalize(look.xz) * 8.5 m/s + up * 4.2 m/s. Beta's knockBack used
// 0.4 blocks/tick (8 m/s) both ways; these values are tunable defaults.
enum class KnockbackDirection : uint8_t {
    AttackerLook,     // normalize(look.xz) of the attacker (player swings)
    AttackerToTarget  // Beta EntityLiving.knockBack: away from the attacker's position (mobs, arrows)
};

struct KnockbackParams {
    float horizontalSpeed{8.5f};  // m/s along the attacker's horizontal heading
    float verticalSpeed{4.2f};    // m/s lift, also the cap on the resulting vertical speed
    float velocityRetention{0.5f}; // Beta halves the target's motion before adding the impulse
    KnockbackDirection direction{KnockbackDirection::AttackerLook};
};

// Horizontal unit heading (y == 0) of the push. Tries the preferred source of `direction`
// first, then the other one (look is vertical / non-finite, or both entities share a
// column), then +Z, so the result is always finite and deterministic (Beta used a random
// jitter here).
[[nodiscard]] glm::vec3 knockbackHeading(const glm::vec3& attackerLook, const glm::vec3& attackerPos,
                                         const glm::vec3& targetPos,
                                         KnockbackDirection direction = KnockbackDirection::AttackerLook) noexcept;

[[nodiscard]] glm::vec3 computeKnockbackImpulse(const glm::vec3& attackerLook,
                                                const glm::vec3& attackerPos,
                                                const glm::vec3& targetPos,
                                                const KnockbackParams& params = {}) noexcept;

// New velocity of the target: velocity * retention + impulse, vertical speed capped at
// params.verticalSpeed. Non-finite input components are treated as zero.
[[nodiscard]] glm::vec3 applyKnockback(const glm::vec3& velocity, const glm::vec3& impulse,
                                       const KnockbackParams& params = {}) noexcept;

} // namespace betacraft::gameplay
