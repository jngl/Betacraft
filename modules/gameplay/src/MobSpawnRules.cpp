#include "gameplay/MobSpawnRules.hpp"

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

namespace betacraft::gameplay {

int32_t skylightSubtracted(float timeOfDay, float rain, float thunder) noexcept {
    // Beta's celestial angle is 0 at noon; ours is 0.25 at noon, so cos(2 pi (t - 0.25))
    // becomes sin(2 pi t).
    const float s = std::sin(timeOfDay * 6.2831853f);
    float f = 1.0f - std::clamp(1.0f - (s * 2.0f + 0.5f), 0.0f, 1.0f);
    f *= 1.0f - rain * 5.0f / 16.0f;
    f *= 1.0f - thunder * 5.0f / 16.0f;
    f = 1.0f - f;
    return std::clamp(static_cast<int32_t>(f * 11.0f), 0, 11);
}

uint8_t effectiveLight(uint8_t sky, uint8_t block, int32_t subtracted) noexcept {
    const int32_t dimmedSky = std::max(0, static_cast<int32_t>(sky) - subtracted);
    return static_cast<uint8_t>(std::max(static_cast<int32_t>(block), dimmedSky));
}

bool hostileLightOk(uint8_t sky, uint8_t effective, SplitMix64& rng) noexcept {
    if (static_cast<int32_t>(sky) > rng.nextInt(32)) {
        return false;
    }
    return static_cast<int32_t>(effective) <= rng.nextInt(8);
}

bool passiveSpawnOk(voxel::BlockId below, uint8_t effective) noexcept {
    return below == voxel::BlockId::Grass && effective > 8;
}

int32_t mobCap(MobCategory category, size_t eligibleChunks) noexcept {
    const bool hostile = category == MobCategory::Hostile;
    const auto base = static_cast<size_t>(hostile ? kHostileCapBase : kPassiveCapBase);
    const int32_t hard = hostile ? kHostileHardCap : kPassiveHardCap;
    // Clamped so the product cannot overflow; the hard cap is reached long before.
    const size_t chunks = std::min(eligibleChunks, static_cast<size_t>(1) << 24U);
    return static_cast<int32_t>(std::min(base * chunks / kCapChunkDivisor, static_cast<size_t>(hard)));
}

float sunBrightness(float timeOfDay) noexcept {
    const float angle = timeOfDay * 6.2831853f;
    const glm::vec3 sunDir = glm::normalize(glm::vec3(std::cos(angle), std::sin(angle), 0.18f));
    const float dayFactor = std::clamp((sunDir.y + 0.15f) / 0.45f, 0.0f, 1.0f);
    return std::clamp(0.16f + dayFactor * 0.84f, 0.16f, 1.0f);
}

bool shouldDespawn(float dist, int32_t ageTicks, SplitMix64& rng) noexcept {
    if (dist > kDespawnFarDistance) {
        return true;
    }
    return ageTicks > kDespawnMinAgeTicks && dist > kDespawnNearDistance && rng.nextInt(kDespawnChance) == 0;
}

} // namespace betacraft::gameplay
