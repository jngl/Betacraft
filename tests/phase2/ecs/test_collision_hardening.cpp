// P2.1 — Hardening of VoxelCollisionSystem (Diagramme 2, S2) against inputs that used to be
// undefined behaviour or to freeze bodies: finite but huge coordinates (float -> int32 casts),
// very fast bodies (tunnelling) and bodies that start a tick embedded in solid blocks.
#include "ecs/Components.hpp"
#include "ecs/Registry.hpp"
#include "ecs/Systems.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

using namespace betacraft;
using namespace betacraft::ecs;

namespace {

EntityId spawnBody(Registry& reg, glm::vec3 pos, glm::vec3 vel) {
    const EntityId e = reg.create();
    reg.emplace<TransformComp>(e, TransformComp{pos, pos, 0.0f, 0.0f});
    VelocityComp v{};
    v.linear = vel;
    reg.emplace<VelocityComp>(e, v);
    reg.emplace<AABBColliderComp>(e);
    return e;
}

struct Cell {
    int32_t x, y, z;
};

} // namespace

// ---- Finite but huge coordinates --------------------------------------------------------

TEST(EcsCollisionHardening, GridCollidesKeepsCellsOfHugeBoxesInRangeAndOnTheRightSide) {
    for (const float far : {1.0e30f, -1.0e30f, 3.0e9f, -3.0e9f, std::numeric_limits<float>::max()}) {
        std::vector<Cell> visited;
        const physics::AABB box{{far, far, far}, {far, far, far}};
        (void)VoxelCollisionSystem::gridCollides(box, [&](int32_t x, int32_t y, int32_t z) {
            visited.push_back({x, y, z});
            return false;
        });
        // The conversion must be range-checked: an out-of-range float -> int32 cast is UB and
        // on x86 yields INT_MIN, which flips the sign of positive coordinates.
        ASSERT_FALSE(visited.empty()) << far;
        EXPECT_LE(visited.size(), 8U) << far;
        for (const Cell& c : visited) {
            EXPECT_EQ(c.x > 0, far > 0.0f) << far;
            EXPECT_EQ(c.y > 0, far > 0.0f) << far;
            EXPECT_EQ(c.z > 0, far > 0.0f) << far;
            EXPECT_LE(std::abs(static_cast<int64_t>(c.x)), int64_t{VoxelCollisionSystem::MAX_CELL_COORD}) << far;
        }
    }
}

// ---- Very fast bodies: no tunnelling, bounded cost ---------------------------------------

namespace {

// Solid half-space below y = 0 plus a wall at x in [5, 6).
bool floorAndWall(int32_t x, int32_t y, int32_t /*z*/) {
    return y < 0 || x == 5;
}

struct CountingCollider {
    size_t* calls;
    bool operator()(const physics::AABB& box) const {
        ++*calls;
        return VoxelCollisionSystem::gridCollides(box, floorAndWall);
    }
};

void collide(Registry& reg, size_t* calls = nullptr) {
    size_t dummy = 0;
    VoxelCollisionSystem::update(reg, CountingCollider{calls != nullptr ? calls : &dummy});
}

constexpr float HALF_W = core::EngineConfig::PLAYER_WIDTH * 0.5f;

} // namespace

TEST(EcsCollisionHardening, KnockbackFasterThanSweepLimitStillHitsTheWall) {
    for (const float speed : {1300.0f, 1.0e6f, 1.0e12f, 1.0e30f}) {
        Registry reg;
        const EntityId e = spawnBody(reg, {0.5f, 0.0f, 0.5f}, {speed, 0.0f, 0.0f});
        reg.get<VelocityComp>(e).onGround = true;
        MovementPhysicsSystem::update(reg, 0.05f, MovementParams{.gravity = 0.0f});
        collide(reg);
        EXPECT_NEAR(reg.get<TransformComp>(e).pos.x, 5.0f - HALF_W, 1e-2f) << speed;
        EXPECT_FLOAT_EQ(reg.get<VelocityComp>(e).linear.x, 0.0f) << speed;
    }
}

TEST(EcsCollisionHardening, UnobstructedDisplacementIsCappedToTheSweepLimit) {
    Registry reg;
    const EntityId e = spawnBody(reg, {0.5f, 10.0f, 0.5f}, {0.0f, 0.0f, -3.0f});
    reg.get<TransformComp>(e).pos = glm::vec3(0.5f, 10.0f, 0.5f - 1.0e9f); // away from the wall
    collide(reg);
    EXPECT_NEAR(reg.get<TransformComp>(e).pos.z, 0.5f - VoxelCollisionSystem::MAX_SWEEP_DISTANCE, 1e-3f);
    EXPECT_FLOAT_EQ(reg.get<VelocityComp>(e).linear.z, -3.0f); // free motion keeps its speed
}

// Replaces a wall-clock assertion: the cost of a sweep is bounded by a deterministic number of
// collision queries however large the displacement or the coordinates are.
TEST(EcsCollisionHardening, CollisionQueriesPerBodyAreBoundedForHugeInputs) {
    const size_t maxSteps = static_cast<size_t>(
        std::ceil(VoxelCollisionSystem::MAX_SWEEP_DISTANCE / VoxelCollisionSystem::MAX_AXIS_STEP));
    const size_t budget = 3U * (maxSteps + VoxelCollisionSystem::CONTACT_BISECTION_STEPS) + 64U;
    const glm::vec3 starts[] = {{0.5f, 0.0f, 0.5f}, {1.0e30f, 0.0f, 0.5f}, {-3.0e9f, 5.0f, 2.0e9f}};
    const glm::vec3 targets[] = {{1.0e30f, 0.0f, 0.5f}, {1.0e30f, 0.0f, 0.5f}, {3.0e9f, -1.0e20f, 0.0f}};
    for (size_t i = 0; i < std::size(starts); ++i) {
        Registry reg;
        const EntityId e = spawnBody(reg, starts[i], {});
        reg.get<TransformComp>(e).pos = targets[i];
        size_t calls = 0;
        collide(reg, &calls);
        EXPECT_LE(calls, budget) << i;
        EXPECT_TRUE(std::isfinite(reg.get<TransformComp>(e).pos.x)) << i;
    }
}

// ---- Bodies that start a tick inside solid blocks ----------------------------------------

TEST(EcsCollisionHardening, BodySunkIntoTheFloorIsPushedOutUpwardsAndLands) {
    Registry reg;
    const EntityId e = spawnBody(reg, {0.5f, -0.2f, 0.5f}, {});
    for (int i = 0; i < 3; ++i) {
        MovementPhysicsSystem::update(reg, 0.05f);
        collide(reg);
    }
    EXPECT_NEAR(reg.get<TransformComp>(e).pos.y, 0.0f, 1e-3f);
    EXPECT_TRUE(reg.get<VelocityComp>(e).onGround);
}

TEST(EcsCollisionHardening, BlockPlacedOnABodyPushesItToTheNearestFreeSideThenItMoves) {
    Registry reg;
    // Centre at x = 5.2 overlaps the wall cell [5, 6): the -X face (0.5 away) is the nearest
    // exit; +X needs 1.1 and the wall is infinitely tall.
    const EntityId e = spawnBody(reg, {5.2f, 0.0f, 0.5f}, {});
    collide(reg);
    EXPECT_NEAR(reg.get<TransformComp>(e).pos.x, 5.0f - HALF_W, 1e-2f);
    EXPECT_NEAR(reg.get<TransformComp>(e).pos.y, 0.0f, 1e-3f);
    // No longer frozen: it can walk away from the wall.
    reg.get<VelocityComp>(e).linear = glm::vec3(-2.0f, 0.0f, 0.0f);
    MovementPhysicsSystem::update(reg, 0.05f, MovementParams{.gravity = 0.0f});
    collide(reg);
    EXPECT_NEAR(reg.get<TransformComp>(e).pos.x, 5.0f - HALF_W - 0.1f, 1e-2f);
    EXPECT_FLOAT_EQ(reg.get<VelocityComp>(e).linear.x, -2.0f);
}

TEST(EcsCollisionHardening, FullyBuriedBodyStaysPutWithinTheQueryBudget) {
    Registry reg;
    const EntityId e = spawnBody(reg, {0.5f, -20.0f, 0.5f}, {1.0f, 0.0f, 0.0f});
    size_t calls = 0;
    collide(reg, &calls);
    EXPECT_EQ(reg.get<TransformComp>(e).pos, glm::vec3(0.5f, -20.0f, 0.5f));
    EXPECT_LE(calls, VoxelCollisionSystem::MAX_PUSH_OUT_QUERIES + 3U * (1U + VoxelCollisionSystem::CONTACT_BISECTION_STEPS) + 2U);
}
