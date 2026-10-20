#pragma once

#include "core/Config.hpp"
#include "core/Types.hpp"
#include "ecs/Entity.hpp"
#include "gameplay/ItemIds.hpp"
#include "gameplay/MobDefs.hpp"
#include "math/AABB.hpp"
#include "physics/Player.hpp"
#include "world/Block.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <glm/glm.hpp>

namespace betacraft::ecs {

// Plain-old-data gameplay components (roadmap P2.1, Fiche 3 §2.2, Diagramme 2). They carry no
// behaviour beyond trivial helpers so that pools stay contiguous and trivially relocatable.

struct TransformComp {
    glm::vec3 pos{0.0f};     // feet position (same convention as physics::Player)
    glm::vec3 prevPos{0.0f}; // position at the previous fixed tick, for render interpolation
    float yaw{0.0f};         // degrees; 0 faces +X like Player::getLookDirection
    float pitch{0.0f};       // degrees
};

// Moves an entity without sweeping or interpolating across the jump: with prevPos == pos the
// collision system sees no displacement and the renderer does not smear the body over the map.
constexpr void teleport(TransformComp& t, const glm::vec3& pos) noexcept {
    t.pos = pos;
    t.prevPos = pos;
}

struct VelocityComp {
    glm::vec3 linear{0.0f}; // blocks per second
    bool onGround{false};
    bool inWater{false};
    // Set by a controller (sneaking player): VoxelCollisionSystem then refuses horizontal moves
    // that would walk a grounded body off a ledge.
    bool edgeGuard{false};
    // Per-entity tuning so one owning-group loop serves players, mobs and items alike
    // (Beta items fall at half the mob gravity).
    float gravityScale{1.0f};
    // Exponential decay rates (1/s); exp(-rate * 0.05 s) is the per-tick factor. Rates add
    // up, i.e. per-tick factors multiply, as in Beta (airborne 0.98, grounded 0.6 * 0.98).
    float horizontalDrag{0.0f}; // X/Z, always
    float verticalDrag{0.0f};   // Y, always
    float groundFriction{0.0f}; // X/Z, extra while onGround (block slipperiness)
    // Set by VoxelCollisionSystem when the last sweep was stopped on X or Z (Beta
    // isCollidedHorizontally): walking mobs jump on it, spiders will climb (S6.3).
    bool collidedHorizontally{false};
};

struct HealthComp {
    int16_t current{20};
    int16_t max{20};
    float hurtInvulnerabilityTimer{0.0f}; // seconds left in the hurt window (Beta: 10 ticks)
    int16_t lastDamage{0};                // strongest hit of the current window (Beta lastDamage)

    [[nodiscard]] constexpr bool isDead() const noexcept { return current <= 0; }
};

// Axis-aligned box anchored at the feet: centred on X/Z, extending upwards on Y.
struct AABBColliderComp {
    float width{core::EngineConfig::PLAYER_WIDTH};
    float height{core::EngineConfig::PLAYER_HEIGHT};

    [[nodiscard]] constexpr math::AABB boundsAt(const glm::vec3& feet) const noexcept {
        const float halfW = width * 0.5f;
        return math::AABB{
            .min = {feet.x - halfW, feet.y, feet.z - halfW},
            .max = {feet.x + halfW, feet.y + height, feet.z + halfW}
        };
    }
};

// Ids follow the shared item scheme (gameplay/ItemIds.hpp), so tools and items at 256 and
// above can lie on the ground too; `damage` keeps a dropped tool's wear.
struct ItemDropComp {
    gameplay::ItemId id{gameplay::kNoItem};
    int32_t count{1};
    int32_t damage{0};
    float ageSeconds{0.0f};
    float pickupDelaySeconds{0.5f}; // Beta: 10 ticks before a fresh drop can be collected
};

enum class MobAIState : uint8_t {
    Idle = 0,
    Wander = 1,
    Chase = 2,
    Flee = 3
};

// AI blackboard driven by MobAISystem; with a MobPathComp it follows A* paths (S8.3).
struct MobAIComp {
    MobAIState state{MobAIState::Idle};
    EntityId target{NULL_ENTITY};
    float stateTimer{0.0f};
    float moveSpeed{2.3f};
    glm::vec3 goal{0.0f};
    // Per-mob xorshift32 state; 0 means "not seeded yet" and is replaced by a hash of the
    // entity handle on first use, so runs are reproducible yet mobs never move in lockstep.
    uint32_t rngState{0};
    // S6.3 set by a leap (spider): MobAISystem leaves the mob's velocity alone until it lands.
    bool leaping{false};
};

// S8.3 the A* path a mob follows (PathPlanningSystem writes it, MobAISystem walks it). Fixed
// capacity: a component must stay standard layout and nothrow movable, so no std::vector.
// `waypoints[0..count)` are footprint corner cells (ai::VoxelPathfinder nodes) after the
// start; `index` is the next one to reach. `goalCell` is the goal the path was planned for.
// A mob without this component steers in a straight line.
struct MobPathComp {
    static constexpr size_t CAPACITY = 32;
    std::array<glm::ivec3, CAPACITY> waypoints{};
    uint8_t count{0}, index{0};
    glm::ivec3 goalCell{};
    uint16_t repathCooldown{0};
    uint8_t stuckTicks{0};
};

// S4.2: what kind of mob an entity is (gameplay::mobDef) and how long it has lived; the
// spawner ages it every despawn check (Beta entityAge).
struct MobKindComp {
    gameplay::MobKind kind{gameplay::MobKind::Pig};
    int32_t ageTicks{0};
};

// Melee cooldown of hostile mobs: a contact hit, then COOLDOWN_TICKS before the next one
// (Beta EntityMob.attackTime = 20).
struct MobAttackComp {
    static constexpr int32_t COOLDOWN_TICKS = 20;
    int32_t cooldownTicks{0};
};

// On fire for `ticks` more game ticks (Beta Entity.fire); daylight sets 160 on zombies.
struct BurningComp {
    int32_t ticks{0};
};

// S5.3 sand or gravel falling as an entity (Beta EntityFallingSand); EntitySimulation turns it
// back into a block where it lands, or into an item.
struct FallingBlockComp {
    world::BlockId id{world::BlockId::Sand};
    int32_t ticks{0}; // Game ticks in flight
};

// S6.6 lit TNT (Beta EntityTNTPrimed): explodes with power 4 when `fuse` game ticks have run
// out. Breaking TNT or lighting it starts at 80; TNT caught in a blast at rand(20) + 10.
struct PrimedTntComp {
    static constexpr int32_t DEFAULT_FUSE_TICKS = 80;
    int32_t fuse{DEFAULT_FUSE_TICKS};
};

// S6.2 an arrow in flight or stuck in a block (Beta EntityArrow); game/Projectiles.hpp moves
// it. `flightTicks` counts the ticks since it was shot (the shooter cannot be hit for the
// first 5), `stuckIn` is the cell it is stuck in while `inGround`.
struct ArrowComp {
    EntityId shooter{NULL_ENTITY};
    int32_t damage{4};
    int32_t groundTicks{0};
    bool inGround{false};
    int32_t flightTicks{0};
    core::BlockPos stuckIn{};
};

// S6.3 creeper fuse (Beta EntityCreeper timeSinceIgnited / creeperState): `ticks` moves by
// `dir` (+1 lit, -1 going out) every tick, and the creeper explodes when it reaches
// gameplay::ExplodeAttack::fuseTicks.
struct FuseComp {
    int32_t ticks{0};
    int8_t dir{-1};
};

// Reuses the legacy input struct so the Application can feed the ECS player unchanged.
struct PlayerInputComp {
    physics::PlayerInput input{};
};

// Empty marker: identifies the local player entity in views.
struct PlayerTagComp {};

// Roadmap names (P2.1, Diagramme 2); the short *Comp names follow Fiche 3 §2.2.
using TransformComponent = TransformComp;
using VelocityComponent = VelocityComp;
using AABBColliderComponent = AABBColliderComp;
using HealthComponent = HealthComp;
using ItemDropComponent = ItemDropComp;
using MobAIComponent = MobAIComp;
using PlayerInputComponent = PlayerInputComp;

} // namespace betacraft::ecs
