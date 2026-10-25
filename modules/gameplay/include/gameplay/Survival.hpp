#pragma once

#include <cstdint>

namespace betacraft::gameplay {

// Beta EntityLiving: 300 ticks (15 s) of air, then 2 damage every 20 ticks.
inline constexpr int32_t kMaxAirTicks = 300;
inline constexpr int32_t kDrowningIntervalTicks = 20;
inline constexpr int32_t kDrowningDamage = 2;
inline constexpr int32_t kAirBubbleCount = 10; // HUD bubbles for a full lung

// Beta Entity.onEntityUpdate: below Y = -64 the entity takes 4 damage every tick (kill()).
inline constexpr float kVoidDamageY = -64.0f;
inline constexpr int32_t kVoidDamage = 4;

// Beta Entity.fire / setOnFireFromLava / BlockCactus.onEntityCollidedWithBlock.
inline constexpr int32_t kLavaDamage = 4;      // Every tick in lava (the resistance window absorbs most)
inline constexpr int32_t kLavaFireTicks = 600; // Lava (re)sets the fire to 30 s
inline constexpr int32_t kFireIntervalTicks = 20;
inline constexpr int32_t kFireDamage = 1;
inline constexpr int32_t kCactusDamage = 1;
// Beta EntityLiving: every tick with the eyes inside an opaque solid block.
inline constexpr int32_t kSuffocationDamage = 1;

struct Burning {
    int32_t fireTicks{0}; // > 0 while burning; Beta Entity.fire

    [[nodiscard]] constexpr bool isBurning() const noexcept { return fireTicks > 0; }
    constexpr bool operator==(const Burning&) const noexcept = default;
};

struct Breath {
    int32_t air{kMaxAirTicks}; // Dips to -kDrowningIntervalTicks + 1 between drowning hits

    constexpr bool operator==(const Breath&) const noexcept = default;
};

// Advances one game tick and returns the damage to feed into applyDamage (0 or 2).
int32_t tickBreath(Breath& breath, bool headUnderWater) noexcept;

// Full air bubbles shown on the HUD (0..10); 0 while out of water is irrelevant since the
// HUD hides the bar when air == kMaxAirTicks.
[[nodiscard]] int32_t airBubbles(const Breath& breath) noexcept;

// One game tick of fire, in Beta Entity.onEntityUpdate order: water puts the fire out, a
// burning entity loses 1 health when fireTicks % 20 == 0 and the counter drops, then lava
// sets it back to 600. Returns the fire damage (0 or 1); the lava hit itself (kLavaDamage)
// is the caller's.
int32_t tickFire(Burning& burning, bool inLava, bool inWater) noexcept;

// Damage to apply this tick for an entity whose feet are at `y` (0 above the void).
[[nodiscard]] int32_t outOfWorldDamage(float y, float voidY = kVoidDamageY) noexcept;

} // namespace betacraft::gameplay
