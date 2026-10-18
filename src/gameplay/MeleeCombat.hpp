#pragma once

#include "gameplay/Combat.hpp"
#include "gameplay/Health.hpp"
#include "gameplay/ItemStack.hpp"
#include "gameplay/Knockback.hpp"
#include "physics/AABB.hpp"

#include <cstddef>
#include <optional>
#include <span>

namespace betacraft::gameplay {

// Entity reach in metres. Kept shorter than the block reach (core::EngineConfig::REACH_DISTANCE)
// so melee cannot out-range mining; tunable per call.
inline constexpr float kDefaultMeleeReach = 3.0f;

// Minimal views of the two combatants, so this works on the current Player class as well
// as on future ECS components (Transform/Velocity/Health).
struct AttackerState {
    glm::vec3 eyePosition{0.0f};
    glm::vec3 look{0.0f, 0.0f, 1.0f};
    float verticalVelocity{0.0f}; // m/s, negative while falling
};

struct TargetState {
    Health health{};
    glm::vec3 position{0.0f};
    glm::vec3 velocity{0.0f}; // m/s, updated in place on knockback
};

struct MeleeResult {
    int32_t damage{0};      // Damage of the swing before hurt resistance
    bool critical{false};   // Beta falling bonus applied
    HitResult hit{};
    WearResult weaponWear{WearResult::Unchanged};
    glm::vec3 impulse{0.0f}; // Knockback impulse added to the target (zero if none)
};

// Resolves one connected swing (Beta EntityPlayer.attackTargetEntityWithCurrentItem):
// damage -> hurt-resistance rules -> knockback on full hits -> weapon wear. The weapon wears
// even when the hit is absorbed (spam clicking is punished, as in Beta) and when the target
// is dying (health 0, death animation playing: Beta still calls hitEntity). Only an expired
// corpse (isCorpseExpired) is not attackable and costs nothing.
MeleeResult performMeleeAttack(const AttackerState& attacker, ItemStack& weapon, TargetState& target,
                               const KnockbackParams& knockback = {}) noexcept;

// Slab test. Returns the ray parameter of the entry point (0 if the origin is inside), in
// units of |direction|; nullopt on miss, behind the origin, or degenerate direction.
[[nodiscard]] std::optional<float> rayAabbDistance(const glm::vec3& origin, const glm::vec3& direction,
                                                   const physics::AABB& box) noexcept;

struct TargetHit {
    size_t index{0};     // Index into the candidate span
    float distance{0.0f}; // Metres from the eye
};

// Nearest candidate hit by the look ray within maxDistance (inclusive). Pass
// min(reach, distance to the first solid block) so walls occlude entities.
[[nodiscard]] std::optional<TargetHit> pickMeleeTarget(const glm::vec3& eye, const glm::vec3& look,
                                                       std::span<const physics::AABB> candidates,
                                                       float maxDistance = kDefaultMeleeReach) noexcept;

} // namespace betacraft::gameplay
