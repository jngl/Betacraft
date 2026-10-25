// Robustness of the systems against hostile inputs: non-finite time steps (a paused
// or broken frame timer), non-finite positions (a NaN from a gameplay bug) and teleports.
// A single NaN must never poison the whole owning group or hang the collision sweep.
#include "ecs/Components.hpp"
#include "ecs/Registry.hpp"
#include "ecs/Systems.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <vector>

using namespace betacraft;
using namespace betacraft::ecs;

namespace {

constexpr float NaN = std::numeric_limits<float>::quiet_NaN();
constexpr float INF = std::numeric_limits<float>::infinity();

EntityId spawnBody(Registry& reg, glm::vec3 pos, glm::vec3 vel) {
    const EntityId e = reg.create();
    reg.emplace<TransformComp>(e, TransformComp{pos, pos, 0.0f, 0.0f});
    VelocityComp v{};
    v.linear = vel;
    reg.emplace<VelocityComp>(e, v);
    return e;
}

bool isFinite(const glm::vec3& v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

// Solid half-space below y = 0 plus a wall at x in [5, 6).
bool floorAndWall(int32_t x, int32_t y, int32_t /*z*/) {
    return y < 0 || x == 5;
}

void collide(Registry& reg) {
    VoxelCollisionSystem::update(reg, [](const math::AABB& box) {
        return VoxelCollisionSystem::gridCollides(box, floorAndWall);
    });
}

} // namespace

// ---- Non-finite time steps -------------------------------------------------------------

TEST(EcsRobustness, MovementIgnoresNonFiniteDeltaTime) {
    for (const float dt : {NaN, INF, -INF}) {
        Registry reg;
        const EntityId e = spawnBody(reg, {1.0f, 2.0f, 3.0f}, {4.0f, 0.0f, 0.0f});
        MovementPhysicsSystem::update(reg, dt);
        EXPECT_EQ(reg.get<TransformComp>(e).pos, glm::vec3(1.0f, 2.0f, 3.0f)) << dt;
        EXPECT_EQ(reg.get<VelocityComp>(e).linear, glm::vec3(4.0f, 0.0f, 0.0f)) << dt;
    }
}

TEST(EcsRobustness, ItemDropsIgnoreNonFiniteDeltaTime) {
    for (const float dt : {NaN, INF, -INF}) {
        Registry reg;
        const EntityId e = EntityFactory::spawnItemDrop(reg, {}, voxel::BlockId::Dirt, 1);
        EXPECT_EQ(ItemDropSystem::update(reg, dt), 0U) << dt;
        ASSERT_TRUE(reg.valid(e)) << dt;
        EXPECT_FLOAT_EQ(reg.get<ItemDropComp>(e).ageSeconds, 0.0f) << dt;
        EXPECT_FLOAT_EQ(reg.get<ItemDropComp>(e).pickupDelaySeconds, 0.5f) << dt;
        // The item still ages normally afterwards.
        (void)ItemDropSystem::update(reg, 0.25f);
        EXPECT_FLOAT_EQ(reg.get<ItemDropComp>(e).ageSeconds, 0.25f) << dt;
    }
}

TEST(EcsRobustness, HealthTimersIgnoreNonFiniteDeltaTime) {
    for (const float dt : {NaN, INF, -INF}) {
        Registry reg;
        const EntityId e = reg.create();
        reg.emplace<HealthComp>(e);
        ASSERT_TRUE(HealthSystem::applyDamage(reg, e, 3));
        (void)HealthSystem::update(reg, dt);
        EXPECT_FLOAT_EQ(reg.get<HealthComp>(e).hurtInvulnerabilityTimer,
                        HealthSystem::HURT_INVULNERABILITY_SECONDS) << dt;
    }
}

TEST(EcsRobustness, RenderInterpolationTreatsNonFiniteAlphaAsCurrentTick) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplace<TransformComp>(e, TransformComp{{10.0f, 0.0f, 0.0f}, {0.0f, 0.0f, 0.0f}, 0.0f, 0.0f});
    std::vector<RenderInstance> out;
    for (const float alpha : {NaN, INF, -INF}) {
        EntityRenderInterpolationSystem::collect(reg, alpha, out);
        ASSERT_EQ(out.size(), 1U);
        EXPECT_EQ(out[0].pos, glm::vec3(10.0f, 0.0f, 0.0f)) << alpha;
    }
}

// ---- Non-finite positions & teleports in the collision sweep ---------------------------

TEST(EcsRobustness, CollisionRevertsNonFinitePositionToPreviousTick) {
    for (const float bad : {NaN, INF, -INF}) {
        Registry reg;
        const EntityId e = spawnBody(reg, {1.5f, 0.0f, 1.5f}, {0.0f, 0.0f, 0.0f});
        reg.emplace<AABBColliderComp>(e);
        auto& t = reg.get<TransformComp>(e);
        t.pos = glm::vec3(1.5f, bad, 1.5f);
        reg.get<VelocityComp>(e).linear = glm::vec3(bad, 1.0f, 0.0f);
        collide(reg);
        EXPECT_EQ(reg.get<TransformComp>(e).pos, glm::vec3(1.5f, 0.0f, 1.5f)) << bad;
        EXPECT_EQ(reg.get<VelocityComp>(e).linear, glm::vec3(0.0f)) << bad;
    }
}

TEST(EcsRobustness, NonFiniteVelocityDoesNotEscapeOneFullTick) {
    Registry reg;
    const EntityId bad = spawnBody(reg, {1.5f, 0.0f, 1.5f}, {NaN, 0.0f, 0.0f});
    reg.emplace<AABBColliderComp>(bad);
    const EntityId good = spawnBody(reg, {2.5f, 3.0f, 2.5f}, {0.0f, 0.0f, 0.0f});
    reg.emplace<AABBColliderComp>(good);
    for (int i = 0; i < 40; ++i) {
        MovementPhysicsSystem::update(reg, 0.05f);
        collide(reg);
    }
    EXPECT_TRUE(isFinite(reg.get<TransformComp>(bad).pos));
    EXPECT_TRUE(isFinite(reg.get<VelocityComp>(bad).linear));
    EXPECT_NEAR(reg.get<TransformComp>(bad).pos.y, 0.0f, 1e-3f);
    // The healthy neighbour fell and landed regardless.
    EXPECT_NEAR(reg.get<TransformComp>(good).pos.y, 0.0f, 1e-3f);
    EXPECT_TRUE(reg.get<VelocityComp>(good).onGround);
}

TEST(EcsRobustness, BodyWithoutAnyFinitePositionIsSkippedSafely) {
    Registry reg;
    const EntityId e = spawnBody(reg, {NaN, NaN, NaN}, {0.0f, 0.0f, 0.0f});
    reg.emplace<AABBColliderComp>(e);
    EXPECT_NO_FATAL_FAILURE(collide(reg));
    EXPECT_TRUE(reg.valid(e));
}

TEST(EcsRobustness, DisplacementBeyondSweepLimitIsCappedAndStillCollides) {
    Registry reg;
    const EntityId e = spawnBody(reg, {0.5f, 0.0f, 0.5f}, {3.0f, 0.0f, 0.0f});
    reg.emplace<AABBColliderComp>(e);
    // Assigning pos directly is a displacement, not a teleport (use ecs::teleport for that):
    // it is capped to MAX_SWEEP_DISTANCE and swept, so the wall at x = 5 stops it.
    reg.get<TransformComp>(e).pos = glm::vec3(0.5f + VoxelCollisionSystem::MAX_SWEEP_DISTANCE + 1.0f, 0.0f, 0.5f);
    collide(reg);
    const float halfW = AABBColliderComp{}.width * 0.5f;
    EXPECT_NEAR(reg.get<TransformComp>(e).pos.x, 5.0f - halfW, 1e-2f);
    EXPECT_FLOAT_EQ(reg.get<VelocityComp>(e).linear.x, 0.0f);
}

TEST(EcsRobustness, DisplacementWithinSweepLimitIsStillSwept) {
    Registry reg;
    const EntityId e = spawnBody(reg, {0.5f, 0.0f, 0.5f}, {3.0f, 0.0f, 0.0f});
    reg.emplace<AABBColliderComp>(e);
    reg.get<TransformComp>(e).pos = glm::vec3(0.5f + VoxelCollisionSystem::MAX_SWEEP_DISTANCE - 1.0f, 0.0f, 0.5f);
    collide(reg);
    const float halfW = AABBColliderComp{}.width * 0.5f;
    EXPECT_NEAR(reg.get<TransformComp>(e).pos.x, 5.0f - halfW, 1e-2f);
    EXPECT_FLOAT_EQ(reg.get<VelocityComp>(e).linear.x, 0.0f);
}

// Cost bound for huge displacements is pinned deterministically (query count, no wall clock)
// in test_collision_hardening.cpp: CollisionQueriesPerBodyAreBoundedForHugeInputs.
TEST(EcsRobustness, HugeDisplacementsStayFiniteAndBounded) {
    Registry reg;
    for (int i = 0; i < 1000; ++i) {
        const EntityId e = spawnBody(reg, {0.5f, 0.0f, 0.5f}, {});
        reg.emplace<AABBColliderComp>(e);
        reg.get<TransformComp>(e).pos = glm::vec3(-1.0e30f, 0.0f, 0.5f);
    }
    collide(reg);
    reg.view<TransformComp>().each([](const TransformComp& t) {
        EXPECT_TRUE(isFinite(t.pos));
        EXPECT_NEAR(t.pos.x, 0.5f - VoxelCollisionSystem::MAX_SWEEP_DISTANCE, 1e-3f);
    });
}

TEST(EcsRobustness, TeleportResetsInterpolationAndSkipsSweep) {
    Registry reg;
    const EntityId e = spawnBody(reg, {0.5f, 0.0f, 0.5f}, {});
    reg.emplace<AABBColliderComp>(e);
    teleport(reg.get<TransformComp>(e), glm::vec3(10.5f, 0.0f, 0.5f)); // behind the wall
    collide(reg);
    EXPECT_EQ(reg.get<TransformComp>(e).pos, glm::vec3(10.5f, 0.0f, 0.5f));
    std::vector<RenderInstance> out;
    EntityRenderInterpolationSystem::collect(reg, 0.0f, out);
    ASSERT_EQ(out.size(), 1U);
    EXPECT_EQ(out[0].pos, glm::vec3(10.5f, 0.0f, 0.5f)); // no smear across the map
}
