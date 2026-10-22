#include "gameplay/Survival.hpp"

#include <algorithm>

namespace betacraft::gameplay {

int32_t tickBreath(Breath& breath, bool headUnderWater) noexcept {
    if (!headUnderWater) {
        // Beta refills the lungs in a single tick rather than gradually.
        breath.air = kMaxAirTicks;
        return 0;
    }
    // Clamp first so a corrupted (e.g. deserialised) value cannot skip the -20 trigger.
    breath.air = std::clamp(breath.air, -kDrowningIntervalTicks + 1, kMaxAirTicks) - 1;
    if (breath.air <= -kDrowningIntervalTicks) {
        breath.air = 0;
        return kDrowningDamage;
    }
    return 0;
}

int32_t airBubbles(const Breath& breath) noexcept {
    // Beta GuiIngame: ceil((air - 2) * 10 / 300), done in integers to stay exact. Clamping
    // first keeps the multiplication far from int32 overflow.
    const int32_t air = std::clamp(breath.air, 0, kMaxAirTicks);
    const int32_t scaled = (air - 2) * kAirBubbleCount;
    const int32_t bubbles = scaled > 0 ? (scaled + kMaxAirTicks - 1) / kMaxAirTicks : 0;
    return std::clamp(bubbles, 0, kAirBubbleCount);
}

int32_t tickFire(Burning& burning, bool inLava, bool inWater) noexcept {
    if (inWater) {
        burning.fireTicks = 0; // Beta handleWaterMovement: fire = 0
    }
    int32_t damage = 0;
    if (burning.fireTicks > 0) {
        // Clamp so a corrupted (deserialised) value still burns out in at most 30 s.
        burning.fireTicks = std::min(burning.fireTicks, kLavaFireTicks);
        if (burning.fireTicks % kFireIntervalTicks == 0) {
            damage = kFireDamage;
        }
        --burning.fireTicks;
    } else {
        burning.fireTicks = 0;
    }
    if (inLava) {
        burning.fireTicks = kLavaFireTicks;
    }
    return damage;
}

int32_t outOfWorldDamage(float y, float voidY) noexcept {
    // Strict comparison, and NaN compares false: no damage for invalid positions.
    return y < voidY ? kVoidDamage : 0;
}

} // namespace betacraft::gameplay
