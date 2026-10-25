#pragma once

#include "ecs/Entity.hpp"
#include "ecs/Registry.hpp"
#include "gameplay/SplitMix64.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <vector>

namespace betacraft::world { class World; }

namespace betacraft::game {

// Arrows (Beta EntityArrow). An arrow is a TransformComp, a VelocityComp without gravity
// (MovementPhysicsSystem only integrates it) and an ecs::ArrowComp; it has no collider, so
// the voxel collision system leaves it alone, and tickArrows does the rest each game tick.

// Beta EntityArrow.onUpdate: after each move the motion is multiplied by
// 0.99 (0.8 in water), then loses 0.05 blocks per tick downwards.
inline constexpr float kArrowDrag = 0.99f;
inline constexpr float kArrowWaterDrag = 0.8f;
inline constexpr float kArrowGravityPerTick = 0.05f * 20.0f; // blocks per second lost each tick
// The shooter cannot be hit by its own arrow for this many ticks after the shot.
inline constexpr int32_t kArrowShooterGraceTicks = 5;
// An arrow stuck in a block disappears after this many ticks (Beta ticksInGround == 1200).
inline constexpr int32_t kArrowGroundLifetimeTicks = 1200;
// Entity boxes are grown by this much for the arrow test (Beta: expand(0.3, 0.3, 0.3)).
inline constexpr float kArrowHitMargin = 0.3f;
// Beta EntityArrow.setArrowHeading: the spread is a gaussian of 0.0075 * spread per axis of
// the unit heading.
inline constexpr float kArrowSpreadScale = 0.0075f;

// Beta setArrowHeading: `direction` normalised, each axis nudged by a gaussian of
// kArrowSpreadScale * spread, then scaled to `speed` (blocks per second).
[[nodiscard]] glm::vec3 arrowHeading(const glm::vec3& direction, float speed, float spread,
                                     gameplay::SplitMix64& rng) noexcept;

// Spawns an arrow at `pos` flying at `velocity` (blocks per second), shot by `shooter`.
// Returns NULL_ENTITY when the registry is full.
ecs::EntityId spawnArrow(ecs::Registry& reg, const glm::vec3& pos, const glm::vec3& velocity, ecs::EntityId shooter,
                         int32_t damage = 4);

// An arrow reaching the player's proxy: the callback of tickArrows turns it into
// EntitySimulation::damagePlayer (DamageCause::Projectile) and says whether the hit landed.
struct ArrowPlayerHit {
    ecs::EntityId arrow{ecs::NULL_ENTITY};
    int32_t damage{0};
    glm::vec3 sourcePos{0.0f}; // The shooter, or where the arrow came from
};
using ArrowPlayerHitFn = std::function<bool(const ArrowPlayerHit&)>;

struct ArrowTickResult {
    std::vector<ecs::EntityId> mobsHurt; // Mobs an arrow damaged this tick
    int32_t playerHits{0};               // Hits on the proxy that landed
    int32_t stuck{0};                    // Arrows that stuck in a block this tick
};

// One game tick of every arrow, after MovementPhysicsSystem moved it from prevPos to pos:
//   * in flight, the segment prevPos -> pos is tested against the solid blocks (Raycaster;
//     flowers, tall grass and torches let it through) and the
//     boxes, grown by kArrowHitMargin, of the living entities with a HealthComp and of the
//     player's proxy (gameplay::pickMeleeTarget), the shooter excluded during its grace
//     ticks. The nearest wins. A mob takes ArrowComp::damage (HealthSystem::applyDamage,
//     knocked back along the flight); the proxy goes through `hitPlayer` (an empty callback
//     counts as landed). A hit that lands removes the arrow; one absorbed by the hurt window
//     bounces back at a tenth of its speed (Beta motion *= -0.1);
//   * a block stops it: it sticks at the hit point (inGround, velocity 0);
//   * otherwise drag and gravity are applied for the next tick and the yaw / pitch follow
//     the velocity;
//   * stuck arrows disappear after kArrowGroundLifetimeTicks, and fall again if their block
//     goes away. Arrows are never picked up.
ArrowTickResult tickArrows(ecs::Registry& reg, const world::World& world, ecs::EntityId proxy,
                           const ArrowPlayerHitFn& hitPlayer = {});

} // namespace betacraft::game
