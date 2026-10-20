// P2.1 — Behavioural parity of the basic gameplay systems with Beta 1.7.3 and with the legacy
// physics::Player they will replace ("Player devient une entité comme les autres"):
// damage/heal arithmetic, hurt-window and knockback rules (EntityLiving.attackEntityFrom /
// knockBack), dropped-item drag (EntityItem.onUpdate) and player walking control.
#include "ecs/Components.hpp"
#include "ecs/Registry.hpp"
#include "ecs/Systems.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

using namespace betacraft;
using namespace betacraft::ecs;

namespace {

constexpr float DT = 0.05f;

} // namespace

// ---- HealthSystem arithmetic ------------------------------------------------------------

TEST(EcsBetaParity, HealWithHugeAmountClampsWithoutOverflow) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplace<HealthComp>(e, HealthComp{5, 20, 0.0f});
    EXPECT_TRUE(HealthSystem::heal(reg, e, std::numeric_limits<int>::max()));
    EXPECT_EQ(reg.get<HealthComp>(e).current, 20);
    EXPECT_FALSE(reg.get<HealthComp>(e).isDead());
}

TEST(EcsBetaParity, HugeDamageClampsAtZero) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplace<HealthComp>(e);
    EXPECT_TRUE(HealthSystem::applyDamage(reg, e, std::numeric_limits<int>::max()));
    EXPECT_EQ(reg.get<HealthComp>(e).current, 0);
}

// ---- Hurt window (EntityLiving.attackEntityFrom: heartsLife = 20, window while > 10) -------

TEST(EcsBetaParity, StrongerHitDuringHurtWindowDealsOnlyTheDifference) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplace<HealthComp>(e);
    ASSERT_TRUE(HealthSystem::applyDamage(reg, e, 4));
    EXPECT_EQ(reg.get<HealthComp>(e).current, 16);
    EXPECT_FALSE(HealthSystem::applyDamage(reg, e, 3)); // weaker: absorbed
    EXPECT_FALSE(HealthSystem::applyDamage(reg, e, 4)); // equal: absorbed
    EXPECT_TRUE(HealthSystem::applyDamage(reg, e, 6));  // stronger: 6 - 4 more
    EXPECT_EQ(reg.get<HealthComp>(e).current, 14);
    EXPECT_FALSE(HealthSystem::applyDamage(reg, e, 6)); // the bar is now 6
    EXPECT_TRUE(HealthSystem::applyDamage(reg, e, 9));
    EXPECT_EQ(reg.get<HealthComp>(e).current, 11);
    // The window is not extended by the overriding hits.
    EXPECT_FLOAT_EQ(reg.get<HealthComp>(e).hurtInvulnerabilityTimer, HealthSystem::HURT_INVULNERABILITY_SECONDS);
}

TEST(EcsBetaParity, AfterTheHurtWindowAnyHitAppliesInFull) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplace<HealthComp>(e);
    ASSERT_TRUE(HealthSystem::applyDamage(reg, e, 9));
    (void)HealthSystem::update(reg, HealthSystem::HURT_INVULNERABILITY_SECONDS);
    EXPECT_TRUE(HealthSystem::applyDamage(reg, e, 1));
    EXPECT_EQ(reg.get<HealthComp>(e).current, 10);
    EXPECT_FALSE(HealthSystem::applyDamage(reg, e, 1)); // new window, bar = 1
    EXPECT_TRUE(HealthSystem::applyDamage(reg, e, 2));
    EXPECT_EQ(reg.get<HealthComp>(e).current, 9);
}

// ---- Knockback (EntityLiving.knockBack) ---------------------------------------------------

TEST(EcsBetaParity, KnockbackHalvesCurrentMotionAddsPushAndCapsUpwardSpeed) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplace<HealthComp>(e);
    VelocityComp v{};
    v.linear = glm::vec3(4.0f, 2.0f, -2.0f);
    v.onGround = true;
    reg.emplace<VelocityComp>(e, v);
    ASSERT_TRUE(HealthSystem::applyDamage(reg, e, 2, {6.0f, 8.0f, 0.0f}));
    const glm::vec3 got = reg.get<VelocityComp>(e).linear;
    EXPECT_FLOAT_EQ(got.x, 2.0f + 6.0f);
    EXPECT_FLOAT_EQ(got.y, HealthSystem::KNOCKBACK_MAX_UPWARD_SPEED); // 1 + 8 capped to 0.4 b/tick
    EXPECT_FLOAT_EQ(got.z, -1.0f);
    EXPECT_FLOAT_EQ(HealthSystem::KNOCKBACK_MAX_UPWARD_SPEED, 0.4f * 20.0f);
    EXPECT_FALSE(reg.get<VelocityComp>(e).onGround);
}

TEST(EcsBetaParity, SourcelessDamageAndOverridingHitsDoNotKnockBack) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplace<HealthComp>(e);
    VelocityComp v{};
    v.linear = glm::vec3(4.0f, 0.0f, 0.0f);
    reg.emplace<VelocityComp>(e, v);
    ASSERT_TRUE(HealthSystem::applyDamage(reg, e, 2)); // fall damage, lava...: no source
    EXPECT_EQ(reg.get<VelocityComp>(e).linear, glm::vec3(4.0f, 0.0f, 0.0f));
    // A stronger hit inside the window deals the difference but applies no knockback (Beta).
    ASSERT_TRUE(HealthSystem::applyDamage(reg, e, 5, {6.0f, 4.0f, 0.0f}));
    EXPECT_EQ(reg.get<VelocityComp>(e).linear, glm::vec3(4.0f, 0.0f, 0.0f));
}

// ---- Dropped items (EntityItem.onUpdate) --------------------------------------------------

namespace {

bool floorBelowZero(int32_t /*x*/, int32_t y, int32_t /*z*/) {
    return y < 0;
}

void tick(Registry& reg, const MovementParams& params = {}) {
    MovementPhysicsSystem::update(reg, DT, params);
    VoxelCollisionSystem::update(reg, [](const math::AABB& box) {
        return VoxelCollisionSystem::gridCollides(box, floorBelowZero);
    });
}

} // namespace

TEST(EcsBetaParity, AirborneItemLosesTwoPercentPerTickOnEveryAxis) {
    Registry reg;
    const EntityId e = EntityFactory::spawnItemDrop(reg, {0.5f, 50.0f, 0.5f}, world::BlockId::Dirt, 1,
                                                    {10.0f, 10.0f, -10.0f});
    MovementParams noGravity;
    noGravity.gravity = 0.0f;
    for (int i = 0; i < 20; ++i) {
        tick(reg, noGravity);
    }
    const float expected = 10.0f * std::pow(0.98f, 20.0f);
    const glm::vec3 v = reg.get<VelocityComp>(e).linear;
    EXPECT_NEAR(v.x, expected, 1e-3f);
    EXPECT_NEAR(v.y, expected, 1e-3f); // vertical drag too
    EXPECT_NEAR(v.z, -expected, 1e-3f);
}

TEST(EcsBetaParity, GroundedItemStopsSlidingWithinAFewTicks) {
    Registry reg;
    const EntityId e = EntityFactory::spawnItemDrop(reg, {0.5f, 0.0f, 0.5f}, world::BlockId::Dirt, 1,
                                                    {4.0f, 0.0f, 0.0f});
    tick(reg);
    ASSERT_TRUE(reg.get<VelocityComp>(e).onGround);
    const float afterFirst = reg.get<VelocityComp>(e).linear.x;
    for (int i = 0; i < 9; ++i) {
        tick(reg);
    }
    // On the ground Beta multiplies by slipperiness(0.6) * 0.98 = 0.588 per tick.
    EXPECT_NEAR(reg.get<VelocityComp>(e).linear.x, afterFirst * std::pow(0.588f, 9.0f), 1e-3f);
    EXPECT_LT(reg.get<VelocityComp>(e).linear.x, 0.05f);
    EXPECT_LT(reg.get<TransformComp>(e).pos.x, 0.5f + 0.75f); // slides well under a block
}

TEST(EcsBetaParity, MobsAndPlayersHaveNoImplicitDrag) {
    Registry reg;
    const EntityId m = EntityFactory::spawnMob(reg, {0.5f, 0.0f, 0.5f}, 10, 0.6f, 1.8f);
    const EntityId p = EntityFactory::spawnPlayer(reg, {10.5f, 0.0f, 0.5f});
    reg.get<VelocityComp>(m).onGround = true;
    reg.get<VelocityComp>(m).linear.x = 2.0f;
    reg.get<VelocityComp>(p).onGround = true;
    reg.get<VelocityComp>(p).linear.x = 2.0f;
    MovementPhysicsSystem::update(reg, DT);
    EXPECT_FLOAT_EQ(reg.get<VelocityComp>(m).linear.x, 2.0f); // their controllers own X/Z
    EXPECT_FLOAT_EQ(reg.get<VelocityComp>(p).linear.x, 2.0f);
}

// ---- PlayerControlSystem vs legacy Player::fixedUpdate -------------------------------------

namespace {

EntityId groundedPlayer(Registry& reg, glm::vec3 at = {0.5f, 0.0f, 0.5f}) {
    const EntityId p = EntityFactory::spawnPlayer(reg, at);
    reg.get<VelocityComp>(p).onGround = true;
    return p;
}

float horizontalSpeed(const Registry& reg, EntityId p) {
    const glm::vec3 v = reg.get<VelocityComp>(p).linear;
    return std::hypot(v.x, v.z);
}

} // namespace

TEST(EcsBetaParity, SprintOnlyAppliesWhenMovingForward) {
    Registry reg;
    const EntityId p = groundedPlayer(reg);
    auto& in = reg.get<PlayerInputComp>(p).input;
    in.sprint = true;
    for (const int dir : {0, 1, 2}) {
        in.backward = dir == 0;
        in.left = dir == 1;
        in.right = dir == 2;
        PlayerControlSystem::update(reg, 1.0f); // a long step converges onto the target speed
        EXPECT_NEAR(horizontalSpeed(reg, p), core::EngineConfig::WALK_SPEED, 1e-4f) << dir;
    }
    in = {};
    in.sprint = true;
    in.forward = true;
    in.left = true; // forward-diagonal sprint is allowed
    PlayerControlSystem::update(reg, 1.0f);
    EXPECT_NEAR(horizontalSpeed(reg, p), core::EngineConfig::SPRINT_SPEED, 1e-4f);
}

TEST(EcsBetaParity, VelocityEasesTowardsTargetWithGroundAndAirAcceleration) {
    Registry reg;
    const EntityId p = groundedPlayer(reg);
    reg.get<PlayerInputComp>(p).input.forward = true;
    PlayerControlSystem::update(reg, DT);
    // Legacy: v = mix(v, target, min(1, dt * accel)), accel 18 on the ground.
    EXPECT_NEAR(reg.get<VelocityComp>(p).linear.x, core::EngineConfig::WALK_SPEED * 0.9f, 1e-4f);

    reg.get<VelocityComp>(p).linear = glm::vec3(0.0f);
    reg.get<VelocityComp>(p).onGround = false; // air control: accel 6
    PlayerControlSystem::update(reg, DT);
    EXPECT_NEAR(reg.get<VelocityComp>(p).linear.x, core::EngineConfig::WALK_SPEED * 0.3f, 1e-4f);

    // Releasing the keys mid-air keeps most of the momentum instead of stopping dead.
    reg.get<VelocityComp>(p).linear = glm::vec3(4.0f, 0.0f, 0.0f);
    reg.get<PlayerInputComp>(p).input = {};
    PlayerControlSystem::update(reg, DT);
    EXPECT_NEAR(reg.get<VelocityComp>(p).linear.x, 4.0f * 0.7f, 1e-4f);
}

TEST(EcsBetaParity, PlayerControlIgnoresNonFiniteDeltaTime) {
    Registry reg;
    const EntityId p = groundedPlayer(reg);
    reg.get<PlayerInputComp>(p).input.forward = true;
    for (const float dt : {std::numeric_limits<float>::quiet_NaN(), -1.0f}) {
        PlayerControlSystem::update(reg, dt);
        EXPECT_FLOAT_EQ(reg.get<VelocityComp>(p).linear.x, 0.0f) << dt;
    }
}

namespace {

// Floor below y = 0 that ends at x = 1 (a ledge over the void).
bool ledge(int32_t x, int32_t y, int32_t /*z*/) {
    return y < 0 && x < 1;
}

void walkTicks(Registry& reg, int n) {
    for (int i = 0; i < n; ++i) {
        PlayerControlSystem::update(reg, DT);
        MovementPhysicsSystem::update(reg, DT);
        VoxelCollisionSystem::update(reg, [](const math::AABB& box) {
            return VoxelCollisionSystem::gridCollides(box, ledge);
        });
    }
}

} // namespace

TEST(EcsBetaParity, SneakingPlayerDoesNotWalkOffLedges) {
    Registry reg;
    const EntityId p = groundedPlayer(reg);
    auto& in = reg.get<PlayerInputComp>(p).input;
    in.forward = true; // yaw 0 faces +X, towards the ledge
    in.sneak = true;
    walkTicks(reg, 60);
    const TransformComp& t = reg.get<TransformComp>(p);
    EXPECT_NEAR(t.pos.y, 0.0f, 1e-3f);
    EXPECT_LT(t.pos.x, 1.0f + core::EngineConfig::PLAYER_WIDTH * 0.5f); // still over the floor
    EXPECT_GT(t.pos.x, 1.0f);                                            // but did reach the edge
    EXPECT_TRUE(reg.get<VelocityComp>(p).onGround);
}

TEST(EcsBetaParity, WalkingPlayerDoesFallOffLedges) {
    Registry reg;
    const EntityId p = groundedPlayer(reg);
    reg.get<PlayerInputComp>(p).input.forward = true;
    walkTicks(reg, 60);
    EXPECT_LT(reg.get<TransformComp>(p).pos.y, -1.0f);
}
