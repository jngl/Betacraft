// MobAISystem additions: spiders (MobDef::climbsWalls) climb the walls they walk into at
// CLIMB_SPEED where other mobs jump, and a leaping mob (MobAIComp::leaping) keeps its impulse
// until it lands. Creepers get a FuseComp from the factory.
#include "ecs/Components.hpp"
#include "ecs/Registry.hpp"
#include "ecs/Systems.hpp"
#include "gameplay/MobDefs.hpp"

#include <gtest/gtest.h>

using namespace betacraft;
using namespace betacraft::ecs;
using gameplay::MobKind;

namespace {

constexpr float DT = 0.05f;

// Flat floor at y < 0 and a wall at x == 3, `height` blocks high.
struct Grid {
    int height{6};
    [[nodiscard]] bool operator()(int32_t x, int32_t y, int32_t) const noexcept {
        return y < 0 || (x == 3 && y < height);
    }
};

void step(Registry& reg, const Grid& grid) {
    MobAISystem::update(reg, DT);
    MovementPhysicsSystem::update(reg, DT);
    VoxelCollisionSystem::update(reg, [&](const math::AABB& box) {
        return VoxelCollisionSystem::gridCollides(box, grid);
    });
}

EntityId walkingIntoTheWall(Registry& reg, MobKind kind) {
    const EntityId e = EntityFactory::spawnMob(reg, {0.5f, 0.0f, 0.5f}, kind);
    auto& ai = reg.get<MobAIComp>(e);
    ai.state = MobAIState::Wander;
    ai.goal = {20.0f, 0.0f, 0.5f};
    ai.stateTimer = 1000.0f;
    return e;
}

} // namespace

TEST(EcsS6MobAi, SpidersClimbWallsOthersOnlyJump) {
    Registry reg;
    const Grid grid{};
    const EntityId spider = walkingIntoTheWall(reg, MobKind::Spider);
    const EntityId zombie = walkingIntoTheWall(reg, MobKind::Zombie);
    reg.get<TransformComp>(zombie).pos.z = reg.get<TransformComp>(zombie).prevPos.z = 5.5f;
    float spiderTop = 0.0f;
    float zombieTop = 0.0f;
    for (int i = 0; i < 80; ++i) {
        step(reg, grid);
        spiderTop = std::max(spiderTop, reg.get<TransformComp>(spider).pos.y);
        zombieTop = std::max(zombieTop, reg.get<TransformComp>(zombie).pos.y);
    }
    EXPECT_GT(spiderTop, 5.5f); // Up the 6-block wall
    EXPECT_LT(zombieTop, 1.6f); // One jump at most
}

TEST(EcsS6MobAi, ClimbingSetsTheLadderSpeedEvenInTheAir) {
    Registry reg;
    const EntityId spider = walkingIntoTheWall(reg, MobKind::Spider);
    auto& v = reg.get<VelocityComp>(spider);
    v.collidedHorizontally = true;
    v.onGround = false;
    v.linear.y = -3.0f;
    MobAISystem::update(reg, DT);
    EXPECT_FLOAT_EQ(v.linear.y, MobAISystem::CLIMB_SPEED);
    EXPECT_FLOAT_EQ(MobAISystem::CLIMB_SPEED, 4.0f); // 0.2 blocks per tick
}

TEST(EcsS6MobAi, ALeapIsNotSteeredUntilTheMobLands) {
    Registry reg;
    const EntityId spider = walkingIntoTheWall(reg, MobKind::Spider);
    auto& ai = reg.get<MobAIComp>(spider);
    auto& v = reg.get<VelocityComp>(spider);
    ai.leaping = true;
    v.onGround = false;
    v.linear = {-8.0f, 8.0f, 0.0f}; // Away from its goal
    MobAISystem::update(reg, DT);
    EXPECT_FLOAT_EQ(v.linear.x, -8.0f);
    EXPECT_TRUE(ai.leaping);
    v.onGround = true;
    MobAISystem::update(reg, DT);
    EXPECT_FALSE(ai.leaping);
    EXPECT_GT(v.linear.x, 0.0f); // Steering to the goal again
}

TEST(EcsS6MobAi, FactoryGivesCreepersAFuse) {
    Registry reg;
    const EntityId creeper = EntityFactory::spawnMob(reg, {0.5f, 0.0f, 0.5f}, MobKind::Creeper);
    const EntityId skeleton = EntityFactory::spawnMob(reg, {0.5f, 0.0f, 0.5f}, MobKind::Skeleton);
    ASSERT_TRUE(reg.has<FuseComp>(creeper));
    EXPECT_EQ(reg.get<FuseComp>(creeper).ticks, 0);
    EXPECT_EQ(reg.get<FuseComp>(creeper).dir, -1);
    EXPECT_FALSE(reg.has<FuseComp>(skeleton));
    EXPECT_TRUE(reg.has<MobAttackComp>(skeleton)); // Its bow cooldown
}
