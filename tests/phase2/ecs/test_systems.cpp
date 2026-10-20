// P2.1 — Systems over the registry (Diagramme 2): movement integration, voxel collision,
// health/damage, item-drop lifetime, render interpolation, player control and spawn helpers.
#include "ecs/Components.hpp"
#include "ecs/Registry.hpp"
#include "ecs/Systems.hpp"

#include "world/World.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <vector>

using namespace betacraft;
using namespace betacraft::ecs;

namespace {

constexpr float DT = 0.05f; // one 20 TPS simulation tick
constexpr float EPS = 1e-3f;

EntityId spawnBody(Registry& reg, glm::vec3 pos, glm::vec3 vel, float gravityScale = 1.0f) {
    const EntityId e = reg.create();
    reg.emplace<TransformComp>(e, TransformComp{pos, pos, 0.0f, 0.0f});
    VelocityComp v{};
    v.linear = vel;
    v.gravityScale = gravityScale;
    reg.emplace<VelocityComp>(e, v);
    return e;
}

// Solid half-space below y = 0 (blocks with y <= -1).
bool floorBelowZero(int32_t /*x*/, int32_t y, int32_t /*z*/) {
    return y < 0;
}

void tick(Registry& reg, auto&& isSolid, float dt = DT) {
    MovementPhysicsSystem::update(reg, dt);
    VoxelCollisionSystem::update(reg, [&](const math::AABB& box) {
        return VoxelCollisionSystem::gridCollides(box, isSolid);
    });
}

} // namespace

// ---- MovementPhysicsSystem -------------------------------------------------------------

TEST(EcsMovementSystem, StoresPreviousPositionBeforeMoving) {
    Registry reg;
    const EntityId e = spawnBody(reg, {1.0f, 2.0f, 3.0f}, {10.0f, 0.0f, 0.0f}, 0.0f);
    MovementPhysicsSystem::update(reg, DT);
    const auto& t = reg.get<TransformComp>(e);
    EXPECT_EQ(t.prevPos, glm::vec3(1.0f, 2.0f, 3.0f));
    EXPECT_NEAR(t.pos.x, 1.5f, 1e-6f);
}

TEST(EcsMovementSystem, ZeroGravityMovesLinearly) {
    Registry reg;
    const EntityId e = spawnBody(reg, {0.0f, 0.0f, 0.0f}, {1.0f, 2.0f, -4.0f}, 0.0f);
    for (int i = 0; i < 20; ++i) {
        MovementPhysicsSystem::update(reg, DT);
    }
    const auto& t = reg.get<TransformComp>(e);
    EXPECT_NEAR(t.pos.x, 1.0f, 1e-4f);
    EXPECT_NEAR(t.pos.y, 2.0f, 1e-4f);
    EXPECT_NEAR(t.pos.z, -4.0f, 1e-4f);
}

TEST(EcsMovementSystem, GravityUsesSemiImplicitEuler) {
    Registry reg;
    const EntityId e = spawnBody(reg, {0.0f, 100.0f, 0.0f}, {0.0f, 0.0f, 0.0f});
    MovementPhysicsSystem::update(reg, DT);
    const float g = core::EngineConfig::GRAVITY;
    EXPECT_NEAR(reg.get<VelocityComp>(e).linear.y, -g * DT, 1e-5f);
    EXPECT_NEAR(reg.get<TransformComp>(e).pos.y, 100.0f - g * DT * DT, 1e-5f);
}

TEST(EcsMovementSystem, GravityScaleAndWaterFactor) {
    Registry reg;
    const EntityId half = spawnBody(reg, {}, {}, 0.5f);
    const EntityId swim = spawnBody(reg, {}, {});
    reg.get<VelocityComp>(swim).inWater = true;
    MovementParams params{};
    MovementPhysicsSystem::update(reg, DT, params);
    EXPECT_NEAR(reg.get<VelocityComp>(half).linear.y, -0.5f * params.gravity * DT, 1e-5f);
    EXPECT_NEAR(reg.get<VelocityComp>(swim).linear.y, -params.waterGravityFactor * params.gravity * DT, 1e-5f);
}

TEST(EcsMovementSystem, TerminalVelocityIsEnforced) {
    Registry reg;
    const EntityId e = spawnBody(reg, {0.0f, 1000.0f, 0.0f}, {0.0f, -54.9f, 0.0f});
    for (int i = 0; i < 10; ++i) {
        MovementPhysicsSystem::update(reg, DT);
    }
    EXPECT_FLOAT_EQ(reg.get<VelocityComp>(e).linear.y, -core::EngineConfig::TERMINAL_VELOCITY);
}

TEST(EcsMovementSystem, DeltaTimeIsClampedToOneTick) {
    Registry reg;
    const EntityId big = spawnBody(reg, {}, {1.0f, 0.0f, 0.0f}, 0.0f);
    MovementPhysicsSystem::update(reg, 10.0f);
    EXPECT_NEAR(reg.get<TransformComp>(big).pos.x, MovementParams{}.maxDt, 1e-6f);
    const EntityId neg = spawnBody(reg, {}, {1.0f, 0.0f, 0.0f});
    MovementPhysicsSystem::update(reg, -1.0f);
    EXPECT_EQ(reg.get<TransformComp>(neg).pos, glm::vec3(0.0f));
    EXPECT_FLOAT_EQ(reg.get<VelocityComp>(neg).linear.y, 0.0f);
}

TEST(EcsMovementSystem, HorizontalDragDecaysExponentially) {
    Registry reg;
    const EntityId e = spawnBody(reg, {}, {10.0f, 0.0f, -10.0f}, 0.0f);
    reg.get<VelocityComp>(e).horizontalDrag = 2.0f;
    MovementPhysicsSystem::update(reg, DT);
    const float k = std::exp(-2.0f * DT);
    EXPECT_NEAR(reg.get<VelocityComp>(e).linear.x, 10.0f * k, 1e-4f);
    EXPECT_NEAR(reg.get<VelocityComp>(e).linear.z, -10.0f * k, 1e-4f);
}

TEST(EcsMovementSystem, OnlyEntitiesWithTransformAndVelocityMove) {
    Registry reg;
    const EntityId onlyT = reg.create();
    reg.emplace<TransformComp>(onlyT, TransformComp{{5.0f, 5.0f, 5.0f}, {}, 0.0f, 0.0f});
    const EntityId onlyV = reg.create();
    reg.emplace<VelocityComp>(onlyV);
    MovementPhysicsSystem::update(reg, DT);
    EXPECT_EQ(reg.get<TransformComp>(onlyT).pos, glm::vec3(5.0f));
    EXPECT_FLOAT_EQ(reg.get<VelocityComp>(onlyV).linear.y, 0.0f);
}

TEST(EcsMovementSystem, UsesTheTransformVelocityOwningGroup) {
    Registry reg;
    (void)spawnBody(reg, {}, {});
    MovementPhysicsSystem::update(reg, DT);
    EXPECT_EQ(reg.groupCount(), 1U);
    EXPECT_EQ((reg.group<TransformComp, VelocityComp>().size()), 1U);
}

TEST(EcsMovementSystem, ThousandsOfEntitiesStayIndependent) {
    Registry reg;
    std::vector<EntityId> ents;
    for (int i = 0; i < 5000; ++i) {
        ents.push_back(spawnBody(reg, {static_cast<float>(i), 0.0f, 0.0f},
                                 {static_cast<float>(i % 7), 0.0f, 0.0f}, 0.0f));
    }
    for (int s = 0; s < 20; ++s) {
        MovementPhysicsSystem::update(reg, DT);
    }
    for (int i = 0; i < 5000; ++i) {
        ASSERT_NEAR(reg.get<TransformComp>(ents[static_cast<size_t>(i)]).pos.x,
                    static_cast<float>(i) + static_cast<float>(i % 7), 1e-2f);
    }
}

// ---- VoxelCollisionSystem --------------------------------------------------------------

TEST(EcsCollisionSystem, GridCollidesDetectsOverlapWithSolidCells) {
    const auto solid = [](int32_t x, int32_t y, int32_t z) { return x == 0 && y == 0 && z == 0; };
    EXPECT_TRUE(VoxelCollisionSystem::gridCollides({{0.2f, 0.2f, 0.2f}, {0.8f, 0.8f, 0.8f}}, solid));
    EXPECT_FALSE(VoxelCollisionSystem::gridCollides({{1.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 1.0f}}, solid));
    // Touching faces is not an overlap (matches Player::collidesWithWorld).
    EXPECT_FALSE(VoxelCollisionSystem::gridCollides({{0.0f, 1.0f, 0.0f}, {1.0f, 2.0f, 1.0f}}, solid));
    EXPECT_TRUE(VoxelCollisionSystem::gridCollides({{-0.5f, -0.5f, -0.5f}, {0.1f, 0.1f, 0.1f}}, solid));
}

TEST(EcsCollisionSystem, FallingBodyLandsOnFloor) {
    Registry reg;
    const EntityId e = spawnBody(reg, {0.5f, 3.0f, 0.5f}, {});
    reg.emplace<AABBColliderComp>(e);
    for (int i = 0; i < 60; ++i) {
        tick(reg, floorBelowZero);
    }
    const auto& t = reg.get<TransformComp>(e);
    const auto& v = reg.get<VelocityComp>(e);
    EXPECT_NEAR(t.pos.y, 0.0f, EPS);
    EXPECT_GE(t.pos.y, 0.0f);
    EXPECT_TRUE(v.onGround);
    EXPECT_FLOAT_EQ(v.linear.y, 0.0f);
}

TEST(EcsCollisionSystem, FastBodyDoesNotTunnelThroughThinFloor) {
    Registry reg;
    // Single-block-thick floor at y == -1; free space everywhere else.
    const auto thinFloor = [](int32_t, int32_t y, int32_t) { return y == -1; };
    const EntityId e = spawnBody(reg, {0.5f, 0.5f, 0.5f}, {0.0f, -55.0f, 0.0f});
    reg.emplace<AABBColliderComp>(e);
    tick(reg, thinFloor);
    EXPECT_NEAR(reg.get<TransformComp>(e).pos.y, 0.0f, EPS);
    EXPECT_TRUE(reg.get<VelocityComp>(e).onGround);
}

TEST(EcsCollisionSystem, WallStopsHorizontalMotion) {
    Registry reg;
    const auto wall = [](int32_t x, int32_t y, int32_t) { return y < 0 || x >= 2; };
    const EntityId e = spawnBody(reg, {0.5f, 0.0f, 0.5f}, {40.0f, 0.0f, 0.0f});
    reg.emplace<AABBColliderComp>(e);
    for (int i = 0; i < 5; ++i) {
        reg.get<VelocityComp>(e).linear.x = 40.0f;
        tick(reg, wall);
    }
    const auto& t = reg.get<TransformComp>(e);
    EXPECT_LE(t.pos.x + 0.3f, 2.0f + 1e-5f);
    EXPECT_NEAR(t.pos.x + 0.3f, 2.0f, EPS);
    EXPECT_FLOAT_EQ(reg.get<VelocityComp>(e).linear.x, 0.0f);
    EXPECT_TRUE(reg.get<VelocityComp>(e).onGround);
}

TEST(EcsCollisionSystem, CeilingStopsJumpWithoutGrounding) {
    Registry reg;
    const auto ceiling = [](int32_t, int32_t y, int32_t) { return y >= 3; };
    const EntityId e = spawnBody(reg, {0.5f, 0.5f, 0.5f}, {0.0f, 30.0f, 0.0f}, 0.0f);
    reg.emplace<AABBColliderComp>(e);
    tick(reg, ceiling);
    tick(reg, ceiling);
    const auto& t = reg.get<TransformComp>(e);
    EXPECT_NEAR(t.pos.y + 1.8f, 3.0f, EPS);
    EXPECT_LE(t.pos.y + 1.8f, 3.0f + 1e-5f);
    EXPECT_FLOAT_EQ(reg.get<VelocityComp>(e).linear.y, 0.0f);
    EXPECT_FALSE(reg.get<VelocityComp>(e).onGround);
}

TEST(EcsCollisionSystem, RestingBodyStaysGroundedAndFloatingBodyDoesNot) {
    Registry reg;
    const EntityId resting = spawnBody(reg, {0.5f, 0.0f, 0.5f}, {}, 0.0f);
    reg.emplace<AABBColliderComp>(resting);
    const EntityId floating = spawnBody(reg, {0.5f, 5.0f, 0.5f}, {}, 0.0f);
    reg.emplace<AABBColliderComp>(floating);
    reg.get<VelocityComp>(floating).onGround = true; // stale flag must be cleared
    tick(reg, floorBelowZero);
    EXPECT_TRUE(reg.get<VelocityComp>(resting).onGround);
    EXPECT_FALSE(reg.get<VelocityComp>(floating).onGround);
}

TEST(EcsCollisionSystem, BodiesWithoutColliderIgnoreTerrain) {
    Registry reg;
    const EntityId ghost = spawnBody(reg, {0.5f, 0.5f, 0.5f}, {0.0f, -20.0f, 0.0f});
    tick(reg, floorBelowZero);
    EXPECT_LT(reg.get<TransformComp>(ghost).pos.y, 0.0f);
}

TEST(EcsCollisionSystem, ThousandItemDropsSettleOnFloor) {
    Registry reg;
    for (int i = 0; i < 1000; ++i) {
        const float x = static_cast<float>(i % 40) + 0.5f;
        const float z = static_cast<float>(i / 40) + 0.5f;
        (void)EntityFactory::spawnItemDrop(reg, {x, 2.0f + static_cast<float>(i % 5), z},
                                           voxel::BlockId::Dirt, 1);
    }
    for (int s = 0; s < 80; ++s) {
        tick(reg, floorBelowZero);
    }
    size_t grounded = 0;
    reg.view<TransformComp, VelocityComp, ItemDropComp>().each(
        [&](TransformComp& t, VelocityComp& v, ItemDropComp&) {
            EXPECT_NEAR(t.pos.y, 0.0f, EPS);
            grounded += v.onGround ? 1U : 0U;
        });
    EXPECT_EQ(grounded, 1000U);
}

TEST(EcsCollisionSystem, WorldOverloadCollidesWithPlacedBlocks) {
    const auto saveDir = std::filesystem::path(::testing::TempDir()) / "betacraft_p2_ecs_world";
    world::World world(1234U, saveDir);
    (void)world.ensureChunkLoaded({0, 0});
    // Build a stone platform high above any generated terrain.
    for (int32_t x = 0; x < 4; ++x) {
        for (int32_t z = 0; z < 4; ++z) {
            ASSERT_TRUE(world.setBlock(x, 110, z, voxel::BlockId::Stone));
        }
    }
    Registry reg;
    const EntityId e = spawnBody(reg, {2.0f, 115.0f, 2.0f}, {});
    reg.emplace<AABBColliderComp>(e);
    for (int i = 0; i < 60; ++i) {
        MovementPhysicsSystem::update(reg, DT);
        VoxelCollisionSystem::update(reg, world);
    }
    EXPECT_NEAR(reg.get<TransformComp>(e).pos.y, 111.0f, EPS);
    EXPECT_TRUE(reg.get<VelocityComp>(e).onGround);
    std::error_code ec;
    std::filesystem::remove_all(saveDir, ec);
}

// ---- HealthSystem ----------------------------------------------------------------------

TEST(EcsHealthSystem, DamageReducesHealthAndGrantsInvulnerability) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplace<HealthComp>(e);
    EXPECT_TRUE(HealthSystem::applyDamage(reg, e, 4));
    EXPECT_EQ(reg.get<HealthComp>(e).current, 16);
    EXPECT_FLOAT_EQ(reg.get<HealthComp>(e).hurtInvulnerabilityTimer, HealthSystem::HURT_INVULNERABILITY_SECONDS);
    EXPECT_FALSE(HealthSystem::applyDamage(reg, e, 4)); // still invulnerable
    EXPECT_EQ(reg.get<HealthComp>(e).current, 16);
}

TEST(EcsHealthSystem, InvulnerabilityExpiresAfterUpdate) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplace<HealthComp>(e);
    ASSERT_TRUE(HealthSystem::applyDamage(reg, e, 1));
    (void)HealthSystem::update(reg, HealthSystem::HURT_INVULNERABILITY_SECONDS * 0.5f);
    EXPECT_FALSE(HealthSystem::applyDamage(reg, e, 1));
    (void)HealthSystem::update(reg, HealthSystem::HURT_INVULNERABILITY_SECONDS);
    EXPECT_FLOAT_EQ(reg.get<HealthComp>(e).hurtInvulnerabilityTimer, 0.0f); // never negative
    EXPECT_TRUE(HealthSystem::applyDamage(reg, e, 1));
    EXPECT_EQ(reg.get<HealthComp>(e).current, 18);
}

TEST(EcsHealthSystem, LethalDamageClampsAtZeroAndReportsDeath) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplace<HealthComp>(e);
    const EntityId other = reg.create();
    reg.emplace<HealthComp>(other);
    EXPECT_TRUE(HealthSystem::applyDamage(reg, e, 1000));
    EXPECT_EQ(reg.get<HealthComp>(e).current, 0);
    EXPECT_TRUE(reg.get<HealthComp>(e).isDead());
    const std::vector<EntityId> dead = HealthSystem::update(reg, DT);
    ASSERT_EQ(dead.size(), 1U);
    EXPECT_EQ(dead[0], e);
    EXPECT_TRUE(reg.valid(e)); // the caller decides what death means (respawn screen, loot...)
    (void)HealthSystem::update(reg, 1.0f);
    EXPECT_FALSE(HealthSystem::applyDamage(reg, e, 1)); // dead entities take no further damage
}

TEST(EcsHealthSystem, InvalidDamageRequestsAreRejected) {
    Registry reg;
    const EntityId noHealth = reg.create();
    EXPECT_FALSE(HealthSystem::applyDamage(reg, noHealth, 3));
    EXPECT_FALSE(HealthSystem::applyDamage(reg, NULL_ENTITY, 3));
    const EntityId e = reg.create();
    reg.emplace<HealthComp>(e);
    EXPECT_FALSE(HealthSystem::applyDamage(reg, e, 0));
    EXPECT_FALSE(HealthSystem::applyDamage(reg, e, -5));
    EXPECT_EQ(reg.get<HealthComp>(e).current, 20);
    reg.destroy(e);
    EXPECT_FALSE(HealthSystem::applyDamage(reg, e, 3));
}

TEST(EcsHealthSystem, KnockbackIsAddedToVelocity) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplace<HealthComp>(e);
    reg.emplace<VelocityComp>(e);
    ASSERT_TRUE(HealthSystem::applyDamage(reg, e, 2, {3.0f, 4.0f, 0.0f}));
    EXPECT_EQ(reg.get<VelocityComp>(e).linear, glm::vec3(3.0f, 4.0f, 0.0f));
    const EntityId noVel = reg.create();
    reg.emplace<HealthComp>(noVel);
    EXPECT_TRUE(HealthSystem::applyDamage(reg, noVel, 2, {3.0f, 4.0f, 0.0f}));
}

TEST(EcsHealthSystem, HealClampsToMaxAndNeverRevives) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplace<HealthComp>(e, HealthComp{10, 20, 0.0f});
    EXPECT_TRUE(HealthSystem::heal(reg, e, 5));
    EXPECT_EQ(reg.get<HealthComp>(e).current, 15);
    EXPECT_TRUE(HealthSystem::heal(reg, e, 100));
    EXPECT_EQ(reg.get<HealthComp>(e).current, 20);
    EXPECT_FALSE(HealthSystem::heal(reg, e, 0));
    reg.get<HealthComp>(e).current = 0;
    EXPECT_FALSE(HealthSystem::heal(reg, e, 5));
    EXPECT_EQ(reg.get<HealthComp>(e).current, 0);
}

// ---- ItemDropSystem --------------------------------------------------------------------

TEST(EcsItemDropSystem, AgesItemsAndCountsDownPickupDelay) {
    Registry reg;
    const EntityId e = EntityFactory::spawnItemDrop(reg, {}, voxel::BlockId::Cobblestone, 3);
    EXPECT_FALSE(ItemDropSystem::canPickUp(reg.get<ItemDropComp>(e)));
    EXPECT_EQ(ItemDropSystem::update(reg, 0.25f), 0U);
    EXPECT_FLOAT_EQ(reg.get<ItemDropComp>(e).ageSeconds, 0.25f);
    (void)ItemDropSystem::update(reg, 10.0f);
    EXPECT_FLOAT_EQ(reg.get<ItemDropComp>(e).pickupDelaySeconds, 0.0f);
    EXPECT_TRUE(ItemDropSystem::canPickUp(reg.get<ItemDropComp>(e)));
}

TEST(EcsItemDropSystem, DespawnsAfterFiveMinutes) {
    Registry reg;
    const EntityId old = EntityFactory::spawnItemDrop(reg, {}, voxel::BlockId::Dirt, 1);
    reg.get<ItemDropComp>(old).ageSeconds = ItemDropSystem::DESPAWN_SECONDS - 0.1f;
    const EntityId young = EntityFactory::spawnItemDrop(reg, {}, voxel::BlockId::Dirt, 1);
    EXPECT_FLOAT_EQ(ItemDropSystem::DESPAWN_SECONDS, 300.0f);
    EXPECT_EQ(ItemDropSystem::update(reg, 0.1f), 1U); // exactly reaching the limit despawns
    EXPECT_FALSE(reg.valid(old));
    EXPECT_TRUE(reg.valid(young));
    EXPECT_EQ(reg.count<TransformComp>(), 1U);
}

TEST(EcsItemDropSystem, MassDespawnKeepsGroupConsistent) {
    Registry reg;
    for (int i = 0; i < 500; ++i) {
        const EntityId e = EntityFactory::spawnItemDrop(reg, {}, voxel::BlockId::Sand, 1);
        if (i % 2 == 0) {
            reg.get<ItemDropComp>(e).ageSeconds = 1000.0f;
        }
    }
    EXPECT_EQ(ItemDropSystem::update(reg, DT), 250U);
    auto g = reg.group<TransformComp, VelocityComp>();
    EXPECT_EQ(g.size(), 250U);
    for (size_t i = 0; i < g.size(); ++i) {
        EXPECT_TRUE(reg.has<ItemDropComp>(g.entities()[i]));
    }
}

// ---- EntityRenderInterpolationSystem ---------------------------------------------------

TEST(EcsRenderInterpolation, InterpolatesBetweenPreviousAndCurrent) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplace<TransformComp>(e, TransformComp{{10.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 45.0f, -10.0f});
    std::vector<RenderInstance> out;
    EntityRenderInterpolationSystem::collect(reg, 0.5f, out);
    ASSERT_EQ(out.size(), 1U);
    EXPECT_EQ(out[0].entity, e);
    EXPECT_NEAR(out[0].pos.x, 5.0f, 1e-6f);
    EXPECT_FLOAT_EQ(out[0].yaw, 45.0f);
    EXPECT_FLOAT_EQ(out[0].pitch, -10.0f);
    EntityRenderInterpolationSystem::collect(reg, 0.0f, out);
    ASSERT_EQ(out.size(), 1U); // output is reset on every call
    EXPECT_FLOAT_EQ(out[0].pos.x, 0.0f);
    EntityRenderInterpolationSystem::collect(reg, 7.0f, out);
    EXPECT_FLOAT_EQ(out[0].pos.x, 10.0f); // alpha clamped to [0, 1]
    EntityRenderInterpolationSystem::collect(reg, -3.0f, out);
    EXPECT_FLOAT_EQ(out[0].pos.x, 0.0f);
}

TEST(EcsRenderInterpolation, CollectsEveryTransform) {
    Registry reg;
    for (int i = 0; i < 100; ++i) {
        (void)spawnBody(reg, {}, {});
    }
    std::vector<RenderInstance> out;
    EntityRenderInterpolationSystem::collect(reg, 1.0f, out);
    EXPECT_EQ(out.size(), 100U);
}

// ---- PlayerControlSystem & EntityFactory -----------------------------------------------
// A 1 s step makes the velocity blend factor min(1, dt * accel) reach 1, i.e. the target
// speed; the easing itself is pinned in test_beta_parity.cpp.

TEST(EcsPlayerControl, WalkDirectionFollowsYaw) {
    Registry reg;
    const EntityId p = EntityFactory::spawnPlayer(reg, {});
    reg.get<PlayerInputComp>(p).input.forward = true;
    PlayerControlSystem::update(reg, 1.0f);
    const glm::vec3 v = reg.get<VelocityComp>(p).linear;
    // yaw 0 faces +X, matching Player::getLookDirection.
    EXPECT_NEAR(v.x, core::EngineConfig::WALK_SPEED, 1e-4f);
    EXPECT_NEAR(v.z, 0.0f, 1e-4f);

    reg.get<TransformComp>(p).yaw = 90.0f;
    PlayerControlSystem::update(reg, 1.0f);
    EXPECT_NEAR(reg.get<VelocityComp>(p).linear.z, core::EngineConfig::WALK_SPEED, 1e-4f);
}

TEST(EcsPlayerControl, SpeedModifiersAndDiagonalNormalisation) {
    Registry reg;
    const EntityId p = EntityFactory::spawnPlayer(reg, {});
    auto& in = reg.get<PlayerInputComp>(p).input;
    in.forward = true;
    in.right = true;
    PlayerControlSystem::update(reg, 1.0f);
    glm::vec3 v = reg.get<VelocityComp>(p).linear;
    EXPECT_NEAR(std::hypot(v.x, v.z), core::EngineConfig::WALK_SPEED, 1e-4f);
    in.sprint = true;
    PlayerControlSystem::update(reg, 1.0f);
    v = reg.get<VelocityComp>(p).linear;
    EXPECT_NEAR(std::hypot(v.x, v.z), core::EngineConfig::SPRINT_SPEED, 1e-4f);
    in.sprint = false;
    in.sneak = true;
    PlayerControlSystem::update(reg, 1.0f);
    v = reg.get<VelocityComp>(p).linear;
    EXPECT_NEAR(std::hypot(v.x, v.z), core::EngineConfig::SNEAK_SPEED, 1e-4f);
    in = {};
    PlayerControlSystem::update(reg, 1.0f);
    v = reg.get<VelocityComp>(p).linear;
    EXPECT_FLOAT_EQ(v.x, 0.0f);
    EXPECT_FLOAT_EQ(v.z, 0.0f);
}

TEST(EcsPlayerControl, JumpOnlyWhenGrounded) {
    Registry reg;
    const EntityId p = EntityFactory::spawnPlayer(reg, {});
    reg.get<PlayerInputComp>(p).input.jump = true;
    PlayerControlSystem::update(reg, 1.0f);
    EXPECT_FLOAT_EQ(reg.get<VelocityComp>(p).linear.y, 0.0f);
    reg.get<VelocityComp>(p).onGround = true;
    PlayerControlSystem::update(reg, 1.0f);
    EXPECT_FLOAT_EQ(reg.get<VelocityComp>(p).linear.y, core::EngineConfig::JUMP_VELOCITY);
    EXPECT_FALSE(reg.get<VelocityComp>(p).onGround);
}

TEST(EcsPlayerControl, PlayerJumpsAndLandsThroughFullPipeline) {
    Registry reg;
    const EntityId p = EntityFactory::spawnPlayer(reg, {0.5f, 0.0f, 0.5f});
    tick(reg, floorBelowZero);
    ASSERT_TRUE(reg.get<VelocityComp>(p).onGround);
    reg.get<PlayerInputComp>(p).input.jump = true;
    PlayerControlSystem::update(reg, DT);
    reg.get<PlayerInputComp>(p).input.jump = false;
    float apex = 0.0f;
    for (int i = 0; i < 40; ++i) {
        tick(reg, floorBelowZero);
        apex = std::max(apex, reg.get<TransformComp>(p).pos.y);
    }
    // Continuous apex is v^2 / 2g ~= 1.305 m; the semi-implicit Euler step shared with the
    // legacy Player (v -= g*dt, then y += v*dt) peaks at sum(v_i*dt) ~= 1.095 m.
    EXPECT_NEAR(apex, 1.095f, 0.01f);
    EXPECT_LT(apex, 1.305f);
    EXPECT_NEAR(reg.get<TransformComp>(p).pos.y, 0.0f, EPS);
    EXPECT_TRUE(reg.get<VelocityComp>(p).onGround);
}

TEST(EcsEntityFactory, PlayerHasStandardComponents) {
    Registry reg;
    const EntityId p = EntityFactory::spawnPlayer(reg, {1.0f, 64.0f, 2.0f});
    EXPECT_TRUE((reg.has<TransformComp, VelocityComp, AABBColliderComp, HealthComp,
                         PlayerInputComp, PlayerTagComp>(p)));
    EXPECT_EQ(reg.get<TransformComp>(p).pos, glm::vec3(1.0f, 64.0f, 2.0f));
    EXPECT_EQ(reg.get<TransformComp>(p).prevPos, glm::vec3(1.0f, 64.0f, 2.0f));
    EXPECT_EQ(reg.get<HealthComp>(p).current, 20);
    EXPECT_TRUE((reg.group<TransformComp, VelocityComp>().contains(p)));
}

TEST(EcsEntityFactory, ItemDropHasSmallColliderAndLighterGravity) {
    Registry reg;
    const EntityId e = EntityFactory::spawnItemDrop(reg, {0.0f, 70.0f, 0.0f}, voxel::BlockId::Sand, 5,
                                                    {1.0f, 2.0f, 0.0f});
    EXPECT_EQ(reg.get<ItemDropComp>(e).id, voxel::BlockId::Sand);
    EXPECT_EQ(reg.get<ItemDropComp>(e).count, 5);
    EXPECT_EQ(reg.get<VelocityComp>(e).linear, glm::vec3(1.0f, 2.0f, 0.0f));
    EXPECT_LT(reg.get<VelocityComp>(e).gravityScale, 1.0f);
    EXPECT_LT(reg.get<AABBColliderComp>(e).width, 0.5f);
    EXPECT_FALSE(reg.has<HealthComp>(e));
}

TEST(EcsEntityFactory, MobHasAiHealthAndCollider) {
    Registry reg;
    const EntityId m = EntityFactory::spawnMob(reg, {0.0f, 70.0f, 0.0f}, 10, 0.6f, 1.8f);
    EXPECT_TRUE((reg.has<TransformComp, VelocityComp, AABBColliderComp, HealthComp, MobAIComp>(m)));
    EXPECT_EQ(reg.get<HealthComp>(m).current, 10);
    EXPECT_EQ(reg.get<HealthComp>(m).max, 10);
    EXPECT_FALSE(reg.has<PlayerTagComp>(m));
}

TEST(EcsEntityFactory, SpawnFailsGracefullyWhenRegistryFull) {
    Registry reg(1);
    EXPECT_NE(EntityFactory::spawnPlayer(reg, {}), NULL_ENTITY);
    EXPECT_EQ(EntityFactory::spawnItemDrop(reg, {}, voxel::BlockId::Dirt, 1), NULL_ENTITY);
    EXPECT_EQ(EntityFactory::spawnMob(reg, {}, 10, 0.6f, 1.8f), NULL_ENTITY);
    EXPECT_EQ(reg.alive(), 1U);
}
