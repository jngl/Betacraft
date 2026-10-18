// P2.1 — MobAISystem (Diagramme 2, "AI & TP -> S3"): a steering state machine over
// (MobAIComp, TransformComp, VelocityComp). It writes horizontal velocity only; gravity and
// collisions stay with MovementPhysicsSystem / VoxelCollisionSystem. A* path following
// (P2.2) arrives in Phase 3 and will replace the straight-line steering towards `goal`.
#include "ecs/Components.hpp"
#include "ecs/Registry.hpp"
#include "ecs/Systems.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <set>
#include <vector>

using namespace betacraft;
using namespace betacraft::ecs;

namespace {

constexpr float DT = 0.05f;

EntityId spawnMobAt(Registry& reg, glm::vec3 pos) {
    return EntityFactory::spawnMob(reg, pos, 20, 0.6f, 1.8f);
}

EntityId spawnTargetAt(Registry& reg, glm::vec3 pos) {
    const EntityId e = reg.create();
    reg.emplace<TransformComp>(e, TransformComp{pos, pos, 0.0f, 0.0f});
    return e;
}

void chase(Registry& reg, EntityId mob, EntityId target) {
    auto& ai = reg.get<MobAIComp>(mob);
    ai.state = MobAIState::Chase;
    ai.target = target;
}

float horizontalSpeed(const VelocityComp& v) {
    return std::hypot(v.linear.x, v.linear.z);
}

} // namespace

TEST(EcsMobAI, ChaseSteersTowardTargetAtMoveSpeed) {
    Registry reg;
    const EntityId mob = spawnMobAt(reg, {0.0f, 64.0f, 0.0f});
    const EntityId target = spawnTargetAt(reg, {10.0f, 64.0f, 0.0f});
    chase(reg, mob, target);
    reg.get<VelocityComp>(mob).linear.y = -1.0f;
    MobAISystem::update(reg, DT);
    const auto& v = reg.get<VelocityComp>(mob);
    const float speed = reg.get<MobAIComp>(mob).moveSpeed;
    EXPECT_NEAR(v.linear.x, speed, 1e-5f);
    EXPECT_NEAR(v.linear.z, 0.0f, 1e-5f);
    EXPECT_FLOAT_EQ(v.linear.y, -1.0f); // vertical motion belongs to the physics system
    EXPECT_NEAR(reg.get<TransformComp>(mob).yaw, 0.0f, 1e-4f);
    EXPECT_EQ(reg.get<MobAIComp>(mob).state, MobAIState::Chase);
}

TEST(EcsMobAI, ChaseFacesTargetAndIgnoresHeightDifference) {
    Registry reg;
    const EntityId mob = spawnMobAt(reg, {0.0f, 64.0f, 0.0f});
    const EntityId target = spawnTargetAt(reg, {0.0f, 70.0f, 5.0f});
    chase(reg, mob, target);
    MobAISystem::update(reg, DT);
    const auto& v = reg.get<VelocityComp>(mob);
    EXPECT_NEAR(v.linear.x, 0.0f, 1e-5f);
    EXPECT_NEAR(v.linear.z, reg.get<MobAIComp>(mob).moveSpeed, 1e-5f);
    EXPECT_NEAR(reg.get<TransformComp>(mob).yaw, 90.0f, 1e-3f); // yaw 90 faces +Z
}

TEST(EcsMobAI, ChaseStopsWithinArrivalRadius) {
    Registry reg;
    const EntityId mob = spawnMobAt(reg, {0.0f, 64.0f, 0.0f});
    const MobAIParams params{};
    const EntityId target = spawnTargetAt(reg, {params.arrivalRadius * 0.5f, 64.0f, 0.0f});
    chase(reg, mob, target);
    reg.get<VelocityComp>(mob).linear = glm::vec3(3.0f, 0.0f, 3.0f);
    MobAISystem::update(reg, DT, params);
    EXPECT_FLOAT_EQ(horizontalSpeed(reg.get<VelocityComp>(mob)), 0.0f);
    EXPECT_EQ(reg.get<MobAIComp>(mob).state, MobAIState::Chase); // still engaged
}

TEST(EcsMobAI, StaleTargetDropsBackToIdle) {
    Registry reg;
    const EntityId mob = spawnMobAt(reg, {});
    const EntityId target = spawnTargetAt(reg, {5.0f, 0.0f, 0.0f});
    chase(reg, mob, target);
    MobAISystem::update(reg, DT);
    ASSERT_GT(horizontalSpeed(reg.get<VelocityComp>(mob)), 0.0f);
    ASSERT_TRUE(reg.destroy(target));
    (void)spawnTargetAt(reg, {5.0f, 0.0f, 0.0f}); // recycles the slot with a new generation
    MobAISystem::update(reg, DT);
    const auto& ai = reg.get<MobAIComp>(mob);
    EXPECT_EQ(ai.state, MobAIState::Idle);
    EXPECT_EQ(ai.target, NULL_ENTITY);
    EXPECT_FLOAT_EQ(horizontalSpeed(reg.get<VelocityComp>(mob)), 0.0f);
}

TEST(EcsMobAI, TargetWithoutTransformDropsBackToIdle) {
    Registry reg;
    const EntityId mob = spawnMobAt(reg, {});
    const EntityId bodiless = reg.create();
    chase(reg, mob, bodiless);
    MobAISystem::update(reg, DT);
    EXPECT_EQ(reg.get<MobAIComp>(mob).state, MobAIState::Idle);
    EXPECT_EQ(reg.get<MobAIComp>(mob).target, NULL_ENTITY);
}

TEST(EcsMobAI, ChaseIsAbandonedBeyondLoseRange) {
    Registry reg;
    const MobAIParams params{};
    const EntityId mob = spawnMobAt(reg, {});
    const EntityId near = spawnTargetAt(reg, {params.loseTargetRange - 0.5f, 0.0f, 0.0f});
    chase(reg, mob, near);
    MobAISystem::update(reg, DT, params);
    EXPECT_EQ(reg.get<MobAIComp>(mob).state, MobAIState::Chase);
    teleport(reg.get<TransformComp>(near), {params.loseTargetRange + 0.5f, 0.0f, 0.0f});
    MobAISystem::update(reg, DT, params);
    EXPECT_EQ(reg.get<MobAIComp>(mob).state, MobAIState::Idle);
    EXPECT_FLOAT_EQ(horizontalSpeed(reg.get<VelocityComp>(mob)), 0.0f);
}

TEST(EcsMobAI, FleeRunsAwayUntilTimerExpires) {
    Registry reg;
    const EntityId mob = spawnMobAt(reg, {});
    const EntityId threat = spawnTargetAt(reg, {-3.0f, 0.0f, 0.0f});
    auto& ai = reg.get<MobAIComp>(mob);
    ai.state = MobAIState::Flee;
    ai.target = threat;
    ai.stateTimer = 0.12f;
    MobAISystem::update(reg, DT);
    EXPECT_NEAR(reg.get<VelocityComp>(mob).linear.x, reg.get<MobAIComp>(mob).moveSpeed, 1e-5f);
    EXPECT_EQ(reg.get<MobAIComp>(mob).state, MobAIState::Flee);
    MobAISystem::update(reg, DT);
    MobAISystem::update(reg, DT); // 0.15 s elapsed > 0.12 s
    EXPECT_EQ(reg.get<MobAIComp>(mob).state, MobAIState::Idle);
    EXPECT_FLOAT_EQ(horizontalSpeed(reg.get<VelocityComp>(mob)), 0.0f);
}

TEST(EcsMobAI, FleeFromCoincidentThreatStillPicksADirection) {
    Registry reg;
    const EntityId mob = spawnMobAt(reg, {});
    const EntityId threat = spawnTargetAt(reg, {});
    auto& ai = reg.get<MobAIComp>(mob);
    ai.state = MobAIState::Flee;
    ai.target = threat;
    ai.stateTimer = 1.0f;
    MobAISystem::update(reg, DT);
    const auto& v = reg.get<VelocityComp>(mob);
    EXPECT_TRUE(std::isfinite(v.linear.x) && std::isfinite(v.linear.z));
    EXPECT_NEAR(horizontalSpeed(v), reg.get<MobAIComp>(mob).moveSpeed, 1e-4f);
}

TEST(EcsMobAI, IdleStandsStillThenStartsWanderingWithinRadius) {
    Registry reg;
    const MobAIParams params{};
    const glm::vec3 home(100.0f, 64.0f, -50.0f);
    const EntityId mob = spawnMobAt(reg, home);
    reg.get<MobAIComp>(mob).stateTimer = 0.12f; // expires during the third tick
    reg.get<VelocityComp>(mob).linear = glm::vec3(1.0f, 0.0f, 1.0f);
    MobAISystem::update(reg, DT, params);
    EXPECT_EQ(reg.get<MobAIComp>(mob).state, MobAIState::Idle);
    EXPECT_FLOAT_EQ(horizontalSpeed(reg.get<VelocityComp>(mob)), 0.0f);
    MobAISystem::update(reg, DT, params);
    MobAISystem::update(reg, DT, params);
    const auto& ai = reg.get<MobAIComp>(mob);
    ASSERT_EQ(ai.state, MobAIState::Wander);
    const glm::vec3 offset = ai.goal - home;
    EXPECT_LE(std::hypot(offset.x, offset.z), params.wanderRadius + 1e-4f);
    EXPECT_GE(std::hypot(offset.x, offset.z), params.arrivalRadius); // never a no-op goal
    EXPECT_FLOAT_EQ(ai.goal.y, home.y);
    EXPECT_FLOAT_EQ(ai.stateTimer, params.wanderTimeoutSeconds);
}

TEST(EcsMobAI, WanderWalksToGoalThenIdlesForRandomDuration) {
    Registry reg;
    const MobAIParams params{};
    const EntityId mob = spawnMobAt(reg, {});
    auto& ai = reg.get<MobAIComp>(mob);
    ai.state = MobAIState::Wander;
    ai.goal = glm::vec3(0.0f, 0.0f, -4.0f);
    ai.stateTimer = params.wanderTimeoutSeconds;
    MobAISystem::update(reg, DT, params);
    EXPECT_NEAR(reg.get<VelocityComp>(mob).linear.z, -ai.moveSpeed, 1e-5f);
    EXPECT_NEAR(reg.get<TransformComp>(mob).yaw, -90.0f, 1e-3f);
    teleport(reg.get<TransformComp>(mob), {0.0f, 0.0f, -4.0f + params.arrivalRadius * 0.5f});
    MobAISystem::update(reg, DT, params);
    EXPECT_EQ(ai.state, MobAIState::Idle);
    EXPECT_FLOAT_EQ(horizontalSpeed(reg.get<VelocityComp>(mob)), 0.0f);
    EXPECT_GE(ai.stateTimer, params.idleSecondsMin);
    EXPECT_LE(ai.stateTimer, params.idleSecondsMax);
}

TEST(EcsMobAI, WanderGivesUpAfterTimeout) {
    Registry reg;
    const MobAIParams params{};
    const EntityId mob = spawnMobAt(reg, {});
    auto& ai = reg.get<MobAIComp>(mob);
    ai.state = MobAIState::Wander;
    ai.goal = glm::vec3(1000.0f, 0.0f, 0.0f); // e.g. blocked by a wall forever
    ai.stateTimer = 0.07f;
    MobAISystem::update(reg, DT, params);
    EXPECT_EQ(ai.state, MobAIState::Wander);
    MobAISystem::update(reg, DT, params);
    EXPECT_EQ(ai.state, MobAIState::Idle);
}

TEST(EcsMobAI, RandomChoicesAreDeterministicAndDifferPerMob) {
    const auto goalsOf = [](int mobs) {
        Registry reg;
        std::vector<EntityId> ids;
        for (int i = 0; i < mobs; ++i) {
            ids.push_back(spawnMobAt(reg, {}));
            reg.get<MobAIComp>(ids.back()).stateTimer = 0.0f;
        }
        MobAISystem::update(reg, DT);
        std::vector<glm::vec3> goals;
        for (const EntityId e : ids) {
            EXPECT_EQ(reg.get<MobAIComp>(e).state, MobAIState::Wander);
            goals.push_back(reg.get<MobAIComp>(e).goal);
        }
        return goals;
    };
    const auto a = goalsOf(16);
    const auto b = goalsOf(16);
    EXPECT_EQ(a, b); // same seeds -> same world
    std::set<std::pair<float, float>> distinct;
    for (const auto& g : a) {
        distinct.emplace(g.x, g.z);
    }
    EXPECT_EQ(distinct.size(), a.size()); // mobs do not wander in lockstep
}

TEST(EcsMobAI, IgnoresNonFiniteDeltaTime) {
    Registry reg;
    const EntityId mob = spawnMobAt(reg, {});
    reg.get<MobAIComp>(mob).stateTimer = 1.0f;
    MobAISystem::update(reg, std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(reg.get<MobAIComp>(mob).stateTimer, 1.0f);
    EXPECT_EQ(reg.get<MobAIComp>(mob).state, MobAIState::Idle);
}

TEST(EcsMobAI, MobsWithoutVelocityAreLeftAlone) {
    Registry reg;
    const EntityId statue = reg.create();
    reg.emplace<TransformComp>(statue);
    reg.emplace<MobAIComp>(statue);
    reg.get<MobAIComp>(statue).stateTimer = 0.0f;
    EXPECT_NO_THROW(MobAISystem::update(reg, DT));
    EXPECT_EQ(reg.get<MobAIComp>(statue).state, MobAIState::Idle);
}

TEST(EcsMobAI, ChasingMobCatchesUpThroughFullPipeline) {
    Registry reg;
    const EntityId player = EntityFactory::spawnPlayer(reg, {8.5f, 0.0f, 6.5f});
    const EntityId mob = spawnMobAt(reg, {0.5f, 0.0f, 0.5f});
    chase(reg, mob, player);
    const auto floor = [](const physics::AABB& box) {
        return VoxelCollisionSystem::gridCollides(box, [](int32_t, int32_t y, int32_t) { return y < 0; });
    };
    for (int i = 0; i < 200; ++i) { // 10 s, enough for 10 blocks at 2.3 blocks/s
        MobAISystem::update(reg, DT);
        MovementPhysicsSystem::update(reg, DT);
        VoxelCollisionSystem::update(reg, floor);
    }
    const glm::vec3 d = reg.get<TransformComp>(player).pos - reg.get<TransformComp>(mob).pos;
    EXPECT_LE(std::hypot(d.x, d.z), MobAIParams{}.arrivalRadius + 0.2f);
    EXPECT_NEAR(reg.get<TransformComp>(mob).pos.y, 0.0f, 1e-3f); // walked, never sank or flew
}
