#pragma once

#include <cstdint>

namespace betacraft::gameplay {

inline constexpr int32_t kPlayerMaxHealth = 20; // Half-hearts
// Beta EntityLiving: heartsLife = 20 after a hit, and hits are "resisted" while
// heartsLife > heartsHalvesLife / 2, i.e. for 10 ticks (0.5 s).
inline constexpr int32_t kHurtResistanceTicks = 10;
// Roadmap P2.5: 0.25 s red flash on damage (20 TPS).
inline constexpr int32_t kHurtFlashTicks = 5;
// Beta deathTime: the corpse is removed (and drops spawned) after 20 ticks.
inline constexpr int32_t kDeathAnimationTicks = 20;
// Upper bound for derived damage values so absurd inputs never overflow int32 arithmetic.
inline constexpr int32_t kMaxFallDamage = 1 << 20;

enum class HitOutcome : uint8_t {
    Ignored,  // Dead target or non-positive damage
    Absorbed, // Inside the resistance window and not stronger than the previous hit
    Partial,  // Inside the window but stronger: only the difference is applied
    Full      // Fresh hit: damage, hurt flash, knockback and a new resistance window
};

struct HitResult {
    HitOutcome outcome{HitOutcome::Ignored};
    int32_t applied{0};  // Health actually removed (clamped to what was left)
    bool killed{false};  // True only on the hit that brought health to 0

    // Beta only knocks back (and plays the hurt sound) on full hits.
    [[nodiscard]] constexpr bool appliesKnockback() const noexcept {
        return outcome == HitOutcome::Full;
    }
};

// Engine-agnostic health component (maps 1:1 onto an ECS HealthComponent).
struct Health {
    int32_t current{kPlayerMaxHealth};
    int32_t max{kPlayerMaxHealth};
    int32_t invulnerableTicks{0};
    int32_t lastDamage{0}; // Strength of the hit that opened/raised the current window
    int32_t hurtFlashTicks{0};
    int32_t deathTicks{0};

    [[nodiscard]] constexpr bool isDead() const noexcept { return current <= 0; }
    [[nodiscard]] constexpr bool isInvulnerable() const noexcept { return invulnerableTicks > 0; }
    [[nodiscard]] constexpr bool isHurtFlashing() const noexcept { return hurtFlashTicks > 0; }

    constexpr bool operator==(const Health&) const noexcept = default;
};

[[nodiscard]] Health makeHealth(int32_t maxHealth) noexcept;

// Beta EntityLiving.attackEntityFrom damage/resistance rules (without knockback, which the
// caller applies when HitResult::appliesKnockback() is true).
HitResult applyDamage(Health& health, int32_t amount) noexcept;

// Same rules, with Beta's damageEntity hook (S6.7 EntityPlayer armor): the hurt window works
// on the raw `amount` (lastDamage keeps it), and `reduce(landing)` turns the part that lands
// (the whole hit, or its excess over the window's hit) into the health removed. `reduce` is
// not called for an absorbed hit. A landing reduced to 0 still opens the window (Full).
template <class Reduce>
HitResult applyDamage(Health& health, int32_t amount, Reduce&& reduce) noexcept {
    HitResult result{};
    if (amount <= 0 || health.isDead()) {
        return result;
    }

    int32_t damage = amount;
    if (health.isInvulnerable()) {
        if (amount <= health.lastDamage) {
            result.outcome = HitOutcome::Absorbed;
            return result;
        }
        // Stronger follow-up inside the window: only the excess lands, the window is not reset.
        damage = amount - health.lastDamage;
        health.lastDamage = amount;
        result.outcome = HitOutcome::Partial;
    } else {
        health.lastDamage = amount;
        health.invulnerableTicks = kHurtResistanceTicks;
        health.hurtFlashTicks = kHurtFlashTicks;
        result.outcome = HitOutcome::Full;
    }

    const int32_t landed = static_cast<int32_t>(reduce(damage));
    result.applied = landed > 0 ? (landed < health.current ? landed : health.current) : 0;
    health.current -= result.applied;
    if (health.isDead()) {
        result.killed = true;
        health.deathTicks = 0;
    }
    return result;
}

// Advances timers by one game tick (20 TPS).
void tickHealth(Health& health) noexcept;

// Returns the amount actually healed; the dead cannot be healed (respawn instead).
int32_t heal(Health& health, int32_t amount) noexcept;

void respawn(Health& health) noexcept;

// 1.0 right after a full hit, fading linearly to 0.0 over kHurtFlashTicks.
[[nodiscard]] float hurtFlashIntensity(const Health& health) noexcept;

// True once the death animation finished: time to scatter drops / show the respawn screen.
[[nodiscard]] bool isCorpseExpired(const Health& health) noexcept;

// Beta EntityLiving.fall: ceil(distance - 3) half-hearts, NaN-safe and saturating.
[[nodiscard]] int32_t fallDamage(float fallDistance) noexcept;

} // namespace betacraft::gameplay
