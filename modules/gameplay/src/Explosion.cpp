#include "gameplay/Explosion.hpp"

namespace betacraft::gameplay {

namespace detail {

const std::array<glm::vec3, kExplosionRayCount>& explosionRayDirections() noexcept {
    static const std::array<glm::vec3, kExplosionRayCount> kDirections = [] {
        std::array<glm::vec3, kExplosionRayCount> dirs{};
        size_t n = 0;
        constexpr int32_t last = kExplosionGrid - 1;
        for (int32_t i = 0; i < kExplosionGrid; ++i) {
            for (int32_t j = 0; j < kExplosionGrid; ++j) {
                for (int32_t k = 0; k < kExplosionGrid; ++k) {
                    if (i != 0 && i != last && j != 0 && j != last && k != 0 && k != last) {
                        continue; // Only the shell of the grid
                    }
                    const glm::vec3 d(static_cast<float>(i) / static_cast<float>(last) * 2.0f - 1.0f,
                                      static_cast<float>(j) / static_cast<float>(last) * 2.0f - 1.0f,
                                      static_cast<float>(k) / static_cast<float>(last) * 2.0f - 1.0f);
                    dirs[n++] = glm::normalize(d);
                }
            }
        }
        return dirs;
    }();
    return kDirections;
}

} // namespace detail

namespace {

[[nodiscard]] float impactAt(float distance, float power, float exposure) noexcept {
    const float radius = kExplosionDamageRadiusFactor * power;
    if (radius <= 0.0f || distance >= radius) {
        return -1.0f; // Out of reach
    }
    return (1.0f - distance / radius) * std::clamp(exposure, 0.0f, 1.0f);
}

} // namespace

int32_t explosionDamage(float distance, float power, float exposure) noexcept {
    const float impact = impactAt(distance, power, exposure);
    if (impact < 0.0f) {
        return 0;
    }
    const float radius = kExplosionDamageRadiusFactor * power;
    return static_cast<int32_t>((impact * impact + impact) / 2.0f * 8.0f * radius + 1.0f);
}

glm::vec3 explosionKnockback(const glm::vec3& centre, const glm::vec3& entityPos, float power,
                             float exposure) noexcept {
    const glm::vec3 d = entityPos - centre;
    const float distance = glm::length(d);
    const float impact = impactAt(distance, power, exposure);
    if (impact <= 0.0f || distance < 1e-6f) {
        return glm::vec3(0.0f);
    }
    return d / distance * impact;
}

} // namespace betacraft::gameplay
