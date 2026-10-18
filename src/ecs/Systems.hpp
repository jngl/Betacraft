#pragma once

#include "ai/VoxelPathfinder.hpp"
#include "core/Config.hpp"
#include "ecs/Components.hpp"
#include "ecs/Registry.hpp"
#include "physics/AABB.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>
#include <type_traits>
#include <vector>

namespace betacraft::world { class World; }

namespace betacraft::ecs {

// Systems of Diagramme 2. They are stateless: all state lives in the Registry, so the
// Application can run them in a fixed order every 50 ms simulation tick.

struct MovementParams {
    float gravity{core::EngineConfig::GRAVITY};
    float terminalVelocity{core::EngineConfig::TERMINAL_VELOCITY};
    float waterGravityFactor{0.22f}; // same buoyancy factor as Player::fixedUpdate
    float maxDt{0.05f};              // one 20 TPS tick; protects against frame hitches
};

// S1: integrates velocity into position over the (Transform, Velocity) owning group, i.e. a
// stride-1 loop over two index-aligned dense arrays with no sparse indirection (Fiche 3 §3).
class MovementPhysicsSystem {
public:
    static void update(Registry& reg, float dt, const MovementParams& params = {});
};

// S2: resolves the displacement prevPos -> pos of every collider against voxel geometry,
// axis by axis (Y, X, Z like Beta entities). Displacements are split in sub-steps of at
// most MAX_AXIS_STEP so fast bodies (terminal velocity = 2.75 blocks/tick) cannot tunnel, and
// a tick never moves a body further than MAX_SWEEP_DISTANCE along any axis.
class VoxelCollisionSystem {
public:
    static constexpr float MAX_AXIS_STEP = 0.45f;
    static constexpr int CONTACT_BISECTION_STEPS = 12; // contact precision ~ step / 4096
    static constexpr float GROUND_PROBE = 0.02f;
    static constexpr float EDGE_GUARD_PROBE = 0.06f; // legacy Player stepCheck
    static constexpr float MAX_SWEEP_DISTANCE = 64.0f;
    // Cell coordinates are clamped to +-2^30: converting a float outside the int32 range is
    // undefined behaviour ([conv.fpint]), and the margin keeps `++x` in the loops below from
    // overflowing. Real worlds end far below (Beta's border is at 3.2e7).
    static constexpr int32_t MAX_CELL_COORD = 1 << 30;
    // Push-out search for bodies that start a tick embedded in solids (a block placed on them,
    // a drop spawned inside terrain): marched in PUSH_OUT_STEP increments up to
    // MAX_PUSH_OUT_DISTANCE along each of the 6 directions, then bisected to end flush.
    static constexpr float PUSH_OUT_STEP = 0.125f;
    static constexpr float MAX_PUSH_OUT_DISTANCE = 2.0f;
    static constexpr int PUSH_OUT_BISECTION_STEPS = 8;
    static constexpr size_t MAX_PUSH_OUT_QUERIES =
        6U * (static_cast<size_t>(MAX_PUSH_OUT_DISTANCE / PUSH_OUT_STEP) + PUSH_OUT_BISECTION_STEPS);

    // floor(v) as a cell coordinate, defined for every float (NaN maps to 0).
    [[nodiscard]] static int32_t toCell(float v) noexcept {
        constexpr auto LIMIT = static_cast<float>(MAX_CELL_COORD);
        const float f = std::floor(v);
        if (!(f > -LIMIT)) {
            return std::isnan(f) ? 0 : -MAX_CELL_COORD;
        }
        return f < LIMIT ? static_cast<int32_t>(f) : MAX_CELL_COORD;
    }

    // True when box overlaps a solid unit cell; isSolid(x, y, z) -> bool. Touching faces do not
    // count, matching Player::collidesWithWorld.
    template<typename IsSolid>
    [[nodiscard]] static bool gridCollides(const physics::AABB& box, IsSolid&& isSolid) {
        const int32_t minX = toCell(box.min.x);
        const int32_t maxX = toCell(box.max.x - 1e-5f);
        const int32_t minY = toCell(box.min.y);
        const int32_t maxY = toCell(box.max.y - 1e-5f);
        const int32_t minZ = toCell(box.min.z);
        const int32_t maxZ = toCell(box.max.z - 1e-5f);
        for (int32_t y = minY; y <= maxY; ++y) {
            for (int32_t z = minZ; z <= maxZ; ++z) {
                for (int32_t x = minX; x <= maxX; ++x) {
                    if (isSolid(x, y, z)) {
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // Generic form: boxCollides(const physics::AABB&) -> bool. Keeps the system testable
    // without a World and lets callers plug in any geometry source.
    template<typename BoxCollides>
        requires std::is_invocable_r_v<bool, BoxCollides&, const physics::AABB&>
    static void update(Registry& reg, BoxCollides&& boxCollides) {
        reg.view<TransformComp, VelocityComp, AABBColliderComp>().each(
            [&](TransformComp& t, VelocityComp& v, AABBColliderComp& c) {
                resolveBody(t, v, c, boxCollides);
            });
    }

    // World form: uses the real block shapes (slabs, cactus...) via physics::AABB::fromBlock.
    static void update(Registry& reg, const world::World& world);

private:
    [[nodiscard]] static bool isFinite(const glm::vec3& p) noexcept {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }

    // pos - prevPos, scaled down (direction kept) so that no axis exceeds MAX_SWEEP_DISTANCE.
    // Skipping collision for long displacements would let fast bodies (huge knockback) pass
    // through terrain; capping keeps the anti-tunnelling guarantee and bounds the sweep cost.
    // Real teleports go through ecs::teleport(), which leaves no displacement to sweep.
    // Computed in double: the difference of two finite floats may overflow float.
    [[nodiscard]] static glm::vec3 clampedDisplacement(const glm::vec3& from, const glm::vec3& to) noexcept {
        const glm::dvec3 d = glm::dvec3(to) - glm::dvec3(from);
        const double largest = std::max({std::abs(d.x), std::abs(d.y), std::abs(d.z)});
        if (largest <= static_cast<double>(MAX_SWEEP_DISTANCE)) {
            return glm::vec3(d);
        }
        return glm::vec3(d * (static_cast<double>(MAX_SWEEP_DISTANCE) / largest));
    }

    template<typename BoxCollides>
    static void resolveBody(TransformComp& t, VelocityComp& v, const AABBColliderComp& c,
                            BoxCollides& boxCollides) {
        if (!isFinite(t.prevPos)) {
            return; // nothing sane to sweep from or fall back to
        }
        if (!isFinite(t.pos) || !isFinite(v.linear)) {
            t.pos = t.prevPos;
            v.linear = glm::vec3(0.0f);
        }
        glm::vec3 delta = clampedDisplacement(t.prevPos, t.pos);
        glm::vec3 pos = t.prevPos;
        if (boxCollides(c.boundsAt(pos))) {
            // Every move from an embedded box collides, so without this the body would be
            // frozen forever (Beta's pushOutOfBlocks plays the same role).
            pos = pushOut(pos, c, boxCollides);
        }
        if (v.edgeGuard && v.onGround) {
            applyEdgeGuard(pos, delta, v, c, boxCollides);
        }
        const float largest = std::max({std::abs(delta.x), std::abs(delta.y), std::abs(delta.z)});
        const int steps = std::max(1, static_cast<int>(std::ceil(largest / MAX_AXIS_STEP)));
        const glm::vec3 sub = delta / static_cast<float>(steps);

        bool blocked[3] = {false, false, false};
        constexpr int order[3] = {1, 0, 2}; // Y first, then X, then Z
        for (int s = 0; s < steps; ++s) {
            for (const int axis : order) {
                if (!blocked[axis] && sub[axis] != 0.0f) {
                    blocked[axis] = moveAxis(pos, axis, sub[axis], c, boxCollides);
                }
            }
        }

        v.onGround = blocked[1] && delta.y < 0.0f;
        v.collidedHorizontally = blocked[0] || blocked[2];
        if (delta.y == 0.0f) {
            v.onGround = boxCollides(c.boundsAt(pos - glm::vec3(0.0f, GROUND_PROBE, 0.0f)));
        }
        for (const int axis : order) {
            if (blocked[axis]) {
                v.linear[axis] = 0.0f;
            }
        }
        t.pos = pos;
    }

    // Legacy sneak ledge guard (Player::fixedUpdate): a grounded body cancels any horizontal
    // axis move after which its box, lowered by EDGE_GUARD_PROBE, would no longer rest on
    // something. Each axis is tested on its own from the start position, as in the legacy code.
    template<typename BoxCollides>
    static void applyEdgeGuard(const glm::vec3& pos, glm::vec3& delta, VelocityComp& v,
                               const AABBColliderComp& c, BoxCollides& boxCollides) {
        for (const int axis : {0, 2}) {
            if (delta[axis] == 0.0f) {
                continue;
            }
            glm::vec3 probe = pos;
            probe[axis] += delta[axis];
            probe.y -= EDGE_GUARD_PROBE;
            if (!boxCollides(c.boundsAt(probe))) {
                delta[axis] = 0.0f;
                v.linear[axis] = 0.0f;
            }
        }
    }

    // Nearest position (up preferred on ties, then -X, +X, -Z, +Z, -Y) whose box is free, or pos
    // unchanged when the body is buried deeper than MAX_PUSH_OUT_DISTANCE on every side
    // (suffocation is a gameplay rule, not a collision one).
    template<typename BoxCollides>
    [[nodiscard]] static glm::vec3 pushOut(const glm::vec3& pos, const AABBColliderComp& c,
                                           BoxCollides& boxCollides) {
        constexpr glm::vec3 DIRECTIONS[6] = {
            {0.0f, 1.0f, 0.0f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f},
            {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f, 1.0f}, {0.0f, -1.0f, 0.0f}
        };
        constexpr int PROBES = static_cast<int>(MAX_PUSH_OUT_DISTANCE / PUSH_OUT_STEP);
        float best = MAX_PUSH_OUT_DISTANCE + 1.0f;
        glm::vec3 bestDir(0.0f);
        for (const glm::vec3& dir : DIRECTIONS) {
            for (int k = 1; k <= PROBES; ++k) {
                const float far = PUSH_OUT_STEP * static_cast<float>(k);
                if (far >= best) {
                    break; // cannot beat the best exit found so far
                }
                if (boxCollides(c.boundsAt(pos + dir * far))) {
                    continue;
                }
                float lo = far - PUSH_OUT_STEP; // colliding (or the embedded start)
                float hi = far;                 // free
                for (int i = 0; i < PUSH_OUT_BISECTION_STEPS; ++i) {
                    const float mid = 0.5f * (lo + hi);
                    if (boxCollides(c.boundsAt(pos + dir * mid))) {
                        lo = mid;
                    } else {
                        hi = mid;
                    }
                }
                best = hi;
                bestDir = dir;
                break;
            }
        }
        return best <= MAX_PUSH_OUT_DISTANCE ? pos + bestDir * best : pos;
    }

    // Moves pos along one axis; on contact, bisects to the largest free fraction so the body
    // ends flush with the obstacle. Returns true when the move was blocked.
    template<typename BoxCollides>
    static bool moveAxis(glm::vec3& pos, int axis, float d, const AABBColliderComp& c,
                         BoxCollides& boxCollides) {
        glm::vec3 candidate = pos;
        candidate[axis] += d;
        if (!boxCollides(c.boundsAt(candidate))) {
            pos = candidate;
            return false;
        }
        float lo = 0.0f;
        float hi = 1.0f;
        for (int i = 0; i < CONTACT_BISECTION_STEPS; ++i) {
            const float mid = 0.5f * (lo + hi);
            candidate = pos;
            candidate[axis] += d * mid;
            if (boxCollides(c.boundsAt(candidate))) {
                hi = mid;
            } else {
                lo = mid;
            }
        }
        pos[axis] += d * lo;
        return true;
    }
};

// S4 (basic part, Diagramme 2 "CombatDamageSystem"): Beta 1.7.3 damage, knockback, healing.
// Melee/tool rules belong to the combat module (P2.5) which builds on these primitives.
//
// Hurt window (EntityLiving.attackEntityFrom): a fresh hit opens a 10-tick window
// (heartsLife = 20, window while > 10). Inside it a hit no stronger than the strongest hit so
// far (lastDamage) is absorbed, a stronger one deals only the difference and raises the bar,
// without knockback and without extending the window.
// Knockback (EntityLiving.knockBack), on fresh hits that have a source (non-zero vector):
// the current velocity is halved, the push added, and the upward speed capped at 0.4
// blocks/tick. Callers pass Beta's push converted to blocks/s (0.4 b/tick -> 8 b/s).
class HealthSystem {
public:
    static constexpr float HURT_INVULNERABILITY_SECONDS = 0.5f;
    static constexpr float KNOCKBACK_MAX_UPWARD_SPEED = 0.4f * 20.0f; // blocks per second

    // Returns false (no effect) for invalid entities, entities without HealthComp, dead
    // entities, non-positive amounts and hits absorbed by the hurt window.
    static bool applyDamage(Registry& reg, EntityId e, int amount,
                            const glm::vec3& knockback = glm::vec3(0.0f));
    // Never revives: returns false for dead entities and non-positive amounts.
    static bool heal(Registry& reg, EntityId e, int amount);
    // Ticks invulnerability timers and returns the entities whose health reached zero.
    [[nodiscard]] static std::vector<EntityId> update(Registry& reg, float dt);
};

struct MobAIParams {
    float arrivalRadius{0.5f};         // horizontal distance at which a goal counts as reached
    float loseTargetRange{16.0f};      // Beta zombies give up beyond 16 blocks
    float wanderRadius{8.0f};
    float wanderTimeoutSeconds{10.0f}; // a wander goal behind a wall must not trap the mob
    float idleSecondsMin{1.0f};
    float idleSecondsMax{4.0f};
};

// Node budget of one PathPlanningSystem::update: `nodesPerTick` over every search, at most
// `nodesPerSearch` each. `spent` reports what the call used.
struct PathBudget {
    int32_t nodesPerTick{2000};
    int32_t nodesPerSearch{ai::PathSearch::NODES_PER_TICK};
    int32_t spent{0};
};

// S8.3 A* planning for mobs that Chase or Wander and carry a MobPathComp; runs before
// MobAISystem. A mob plans again when its goal moved more than REPATH_DISTANCE blocks from
// the cell its path leads to, when it has been stuck for more than STUCK_TICKS ticks, or when
// it ran out of waypoints short of the goal, and never more than once per
// REPATH_COOLDOWN_TICKS. Searches (ai::PathSearch) live here, time-sliced: each tick the
// global budget goes round-robin over them, continuing after the last one served. A search
// whose entity was destroyed, lost its MobPathComp or stopped chasing/wandering is dropped.
// Mobs plan with mobDef(kind).navExtents / canOpenDoors / climbsWalls (a zombie without MobKindComp).
class PathPlanningSystem {
public:
    static constexpr uint16_t REPATH_COOLDOWN_TICKS = 20;
    static constexpr uint8_t STUCK_TICKS = 10;
    static constexpr float REPATH_DISTANCE = 2.0f;
    static constexpr float STUCK_EPSILON = 0.01f; // blocks moved in one tick that count as stuck

    void update(Registry& reg, const ai::INavVoxelQuery& query, PathBudget& budget);

    [[nodiscard]] size_t activeSearches() const noexcept { return m_searches.size(); }
    [[nodiscard]] bool hasSearch(EntityId e) const noexcept;
    // Nodes expanded so far by the running search of `e`, or -1 without one.
    [[nodiscard]] int32_t searchProgress(EntityId e) const noexcept;
    void clear() noexcept;

    // The navigation box of a mob entity (MobKindComp, else a zombie's).
    [[nodiscard]] static ai::NavAgent agentOf(const Registry& reg, EntityId e) noexcept;
    // Footprint corner cell of a body whose feet are at `feet`.
    [[nodiscard]] static glm::ivec3 cornerCell(const glm::vec3& feet, const glm::ivec3& footprint) noexcept;

private:
    struct Active {
        EntityId entity{NULL_ENTITY};
        ai::PathSearch search;
    };
    // Removes the searches matching `drop`, keeping m_cursor on the same next search.
    template<typename Pred>
    void eraseKeepingCursor(Pred&& drop) {
        size_t cursor = m_cursor;
        size_t out = 0;
        for (size_t i = 0; i < m_searches.size(); ++i) {
            if (drop(m_searches[i])) {
                if (i < m_cursor) {
                    --cursor;
                }
                continue;
            }
            if (out != i) {
                m_searches[out] = std::move(m_searches[i]);
            }
            ++out;
        }
        m_searches.erase(m_searches.begin() + static_cast<std::ptrdiff_t>(out), m_searches.end());
        m_cursor = m_searches.empty() ? 0 : cursor % m_searches.size();
    }

    std::vector<Active> m_searches; // round-robin order
    size_t m_cursor{0};
};

// S3 (Diagramme 2, "AI & TP"): steering state machine over (MobAIComp, Transform, Velocity).
//   Idle   -> stands still; when stateTimer runs out, picks a random goal and Wanders.
//   Wander -> walks straight to goal; on arrival or timeout, Idles for a random duration.
//   Chase  -> walks to target; stops within arrivalRadius; Idles once the target is stale,
//             has no Transform or is beyond loseTargetRange.
//   Flee   -> walks away from target until stateTimer runs out (or the target vanishes).
// It writes horizontal velocity and yaw only: gravity and collisions stay with the physics
// systems (S8.3 path following, below, replaces the straight line when planned). Mobs in
// the air during their hurt window (knocked back) are left alone until they land. A grounded
// mob that Chases or Wanders into a wall (VelocityComp::collidedHorizontally) jumps at
// JUMP_SPEED (Beta EntityLiving.jump, 0.42 blocks/tick), which climbs one block; a mob whose
// MobDef climbsWalls (spider, S6.3) climbs at CLIMB_SPEED instead, airborne or not. A mob
// with MobAIComp::leaping set is left alone until it lands.
// S8.3: a Chasing or Wandering mob with a MobPathComp walks to the centre of its footprint at
// waypoints[index] instead, moving on once within WAYPOINT_REACHED blocks (horizontally),
// and jumps when that waypoint is a block higher than its feet; past the last waypoint (or
// without a path) it steers straight again.
class MobAISystem {
public:
    static constexpr float JUMP_SPEED = 8.4f; // blocks per second
    static constexpr float WAYPOINT_REACHED = 0.35f;
    static constexpr float CLIMB_SPEED = 0.2f * 20.0f; // Beta ladder climbing, blocks per second
    static void update(Registry& reg, float dt, const MobAIParams& params = {});
};

// Dropped item lifetime (Beta: collectable after 10 ticks, despawns after 5 minutes).
class ItemDropSystem {
public:
    static constexpr float DESPAWN_SECONDS = 300.0f;

    // Ages items and destroys expired ones; returns how many were despawned.
    static size_t update(Registry& reg, float dt);
    [[nodiscard]] static constexpr bool canPickUp(const ItemDropComp& item) noexcept {
        return item.pickupDelaySeconds <= 0.0f;
    }
};

struct RenderInstance {
    EntityId entity{NULL_ENTITY};
    glm::vec3 pos{0.0f};
    float yaw{0.0f};
    float pitch{0.0f};
};

// S5: produces interpolated render transforms (no GL here; the renderer consumes the list).
class EntityRenderInterpolationSystem {
public:
    static void collect(Registry& reg, float alpha, std::vector<RenderInstance>& out);
};

// Converts PlayerInputComp into horizontal velocity and jumps: the walking subset of
// Player::fixedUpdate (flying/swimming stay in the legacy Player until migration).
//   * speed: sneak (not in water) > sprint (only while pressing forward) > walk;
//   * X/Z ease towards the target: v = mix(v, target, min(1, dt * accel)), accel = 18 on the
//     ground and 6 in the air;
//   * sneaking sets VelocityComp::edgeGuard, which VoxelCollisionSystem uses as the legacy
//     sneak ledge guard.
class PlayerControlSystem {
public:
    static constexpr float GROUND_ACCELERATION = 18.0f;
    static constexpr float AIR_ACCELERATION = 6.0f;

    static void update(Registry& reg, float dt);
};

// Archetype helpers. Each returns NULL_ENTITY (and creates nothing) when the registry is full.
class EntityFactory {
public:
    static constexpr float ITEM_SIZE = 0.25f;
    static constexpr float ITEM_GRAVITY_SCALE = 0.5f;
    // EntityItem.onUpdate: motion *= 0.98 per tick on all axes; on the ground X/Z are
    // multiplied by slipperiness * 0.98 instead (0.6 for ordinary blocks; per-block
    // slipperiness such as ice is not modelled yet). As rates: -20 * ln(factor).
    static constexpr float ITEM_AIR_DRAG = 0.40405414f;        // -20 ln 0.98
    static constexpr float ITEM_GROUND_FRICTION = 10.216512f;  // -20 ln 0.6

    static EntityId spawnPlayer(Registry& reg, const glm::vec3& feetPos);
    static EntityId spawnItemDrop(Registry& reg, const glm::vec3& pos, gameplay::ItemId id, int32_t count,
                                  const glm::vec3& initialVelocity = glm::vec3(0.0f), int32_t damage = 0);
    static EntityId spawnItemDrop(Registry& reg, const glm::vec3& pos, world::BlockId id, int32_t count,
                                  const glm::vec3& initialVelocity = glm::vec3(0.0f)) {
        return spawnItemDrop(reg, pos, gameplay::toItemId(id), count, initialVelocity);
    }
    static EntityId spawnMob(Registry& reg, const glm::vec3& feetPos, int16_t maxHealth,
                             float width, float height);
    // S4.2: a mob of `kind` built from gameplay::mobDef (health, box, walking speed), with
    // MobKindComp and, for hostiles, MobAttackComp (the melee cooldown, or the bow's for a
    // ranged attacker); a mob with an ExplodeAttack (creeper) also gets a FuseComp.
    static EntityId spawnMob(Registry& reg, const glm::vec3& feetPos, gameplay::MobKind kind);
    // The player's stand-in for mob targeting: Transform, AABBCollider and PlayerTagComp only.
    // No VelocityComp (the movement systems would move it) and no HealthComp (melee picking
    // would hit the player's own box); the owner copies the player's position into it.
    static EntityId spawnPlayerProxy(Registry& reg, const glm::vec3& feetPos);
};

// Roadmap names: P2.1 lists CollisionResolutionSystem / EntityRenderSystem, Diagramme 2 lists
// VoxelCollisionSystem / CombatDamageSystem / MobPathfindingAISystem (MobAISystem, which
// follows PathPlanningSystem's A* paths since S8.3) / EntityRenderInterpolationSystem.
using CollisionResolutionSystem = VoxelCollisionSystem;
using EntityRenderSystem = EntityRenderInterpolationSystem;
using CombatDamageSystem = HealthSystem;
using MobPathfindingAISystem = MobAISystem;

} // namespace betacraft::ecs
