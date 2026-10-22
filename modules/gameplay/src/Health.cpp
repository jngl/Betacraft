#include "gameplay/Health.hpp"

#include <algorithm>
#include <cmath>

namespace betacraft::gameplay {

namespace {
constexpr float kSafeFallDistance = 3.0f;
} // namespace

Health makeHealth(int32_t maxHealth) noexcept {
    const int32_t m = std::max(1, maxHealth);
    Health h{};
    h.current = m;
    h.max = m;
    return h;
}

HitResult applyDamage(Health& health, int32_t amount) noexcept {
    return applyDamage(health, amount, [](int32_t landing) noexcept { return landing; });
}

void tickHealth(Health& health) noexcept {
    if (health.invulnerableTicks > 0) {
        --health.invulnerableTicks;
    }
    if (health.hurtFlashTicks > 0) {
        --health.hurtFlashTicks;
    }
    if (health.isDead() && health.deathTicks < kDeathAnimationTicks) {
        ++health.deathTicks;
    }
}

int32_t heal(Health& health, int32_t amount) noexcept {
    // current >= max also covers over-max health (max lowered, loaded save): never subtract.
    if (amount <= 0 || health.isDead() || health.current >= health.max) {
        return 0;
    }
    const int32_t healed = std::min(amount, health.max - health.current);
    health.current += healed;
    return healed;
}

void respawn(Health& health) noexcept {
    health = makeHealth(health.max);
}

float hurtFlashIntensity(const Health& health) noexcept {
    return static_cast<float>(std::clamp(health.hurtFlashTicks, 0, kHurtFlashTicks)) /
           static_cast<float>(kHurtFlashTicks);
}

bool isCorpseExpired(const Health& health) noexcept {
    return health.isDead() && health.deathTicks >= kDeathAnimationTicks;
}

int32_t fallDamage(float fallDistance) noexcept {
    // Written so NaN fails the comparison and yields no damage.
    if (!(fallDistance > kSafeFallDistance)) {
        return 0;
    }
    const float raw = std::ceil(fallDistance - kSafeFallDistance);
    if (!(raw < static_cast<float>(kMaxFallDamage))) {
        return kMaxFallDamage;
    }
    return static_cast<int32_t>(raw);
}

} // namespace betacraft::gameplay
