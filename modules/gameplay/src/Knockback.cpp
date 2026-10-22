#include "gameplay/Knockback.hpp"

#include <algorithm>
#include <cmath>
#include <optional>

namespace betacraft::gameplay {

namespace {

constexpr float kMinHeadingLength = 1e-4f;
constexpr glm::vec3 kFallbackHeading{0.0f, 0.0f, 1.0f};

[[nodiscard]] std::optional<glm::vec3> horizontalUnit(float x, float z) noexcept {
    if (!std::isfinite(x) || !std::isfinite(z)) {
        return std::nullopt;
    }
    // hypot avoids overflow for very large (but finite) inputs.
    const float len = std::hypot(x, z);
    if (!(len > kMinHeadingLength) || !std::isfinite(len)) {
        return std::nullopt;
    }
    return glm::vec3{x / len, 0.0f, z / len};
}

[[nodiscard]] float finiteOrZero(float v) noexcept {
    return std::isfinite(v) ? v : 0.0f;
}

} // namespace

glm::vec3 knockbackHeading(const glm::vec3& attackerLook, const glm::vec3& attackerPos,
                           const glm::vec3& targetPos, KnockbackDirection direction) noexcept {
    const auto look = horizontalUnit(attackerLook.x, attackerLook.z);
    const auto delta = horizontalUnit(targetPos.x - attackerPos.x, targetPos.z - attackerPos.z);
    const auto& preferred = direction == KnockbackDirection::AttackerLook ? look : delta;
    const auto& secondary = direction == KnockbackDirection::AttackerLook ? delta : look;
    if (preferred) {
        return *preferred;
    }
    if (secondary) {
        return *secondary;
    }
    return kFallbackHeading;
}

glm::vec3 computeKnockbackImpulse(const glm::vec3& attackerLook, const glm::vec3& attackerPos,
                                  const glm::vec3& targetPos, const KnockbackParams& params) noexcept {
    const glm::vec3 heading = knockbackHeading(attackerLook, attackerPos, targetPos, params.direction);
    return heading * params.horizontalSpeed + glm::vec3{0.0f, params.verticalSpeed, 0.0f};
}

glm::vec3 applyKnockback(const glm::vec3& velocity, const glm::vec3& impulse,
                         const KnockbackParams& params) noexcept {
    const glm::vec3 kept{finiteOrZero(velocity.x), finiteOrZero(velocity.y), finiteOrZero(velocity.z)};
    glm::vec3 result = kept * params.velocityRetention + impulse;
    result.y = std::min(result.y, params.verticalSpeed);
    return result;
}

} // namespace betacraft::gameplay
