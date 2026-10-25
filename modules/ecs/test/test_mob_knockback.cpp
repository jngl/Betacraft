// Integration hook: a mob knocked back by a hit (HealthComp hurt window open,
// airborne) has no air control, so MobAISystem leaves its velocity to the physics systems
// until it lands. Otherwise the next AI step would overwrite the knockback impulse.
#include "ecs/Components.hpp"
#include "ecs/Registry.hpp"
#include "ecs/Systems.hpp"

#include <gtest/gtest.h>

using namespace betacraft;
using namespace betacraft::ecs;

namespace {

constexpr float DT = 0.05f;

EntityId knockedBackMob(Registry& reg, MobAIState state) {
    const EntityId mob = EntityFactory::spawnMob(reg, {0.5f, 1.0f, 0.5f}, 20, 0.6f, 1.8f);
    auto& ai = reg.get<MobAIComp>(mob);
    ai.state = state;
    ai.stateTimer = 100.0f;
    ai.goal = {-20.0f, 1.0f, 0.5f};
    EXPECT_TRUE(HealthSystem::applyDamage(reg, mob, 1, {8.0f, 4.0f, 0.0f}));
    return mob;
}

} // namespace

TEST(EcsMobKnockback, AirborneHurtMobsKeepTheirKnockback) {
    for (const MobAIState state : {MobAIState::Idle, MobAIState::Wander}) {
        Registry reg;
        const EntityId mob = knockedBackMob(reg, state);
        ASSERT_FALSE(reg.get<VelocityComp>(mob).onGround);
        MobAISystem::update(reg, DT);
        EXPECT_FLOAT_EQ(reg.get<VelocityComp>(mob).linear.x, 8.0f) << static_cast<int>(state);
        EXPECT_FLOAT_EQ(reg.get<VelocityComp>(mob).linear.y, 4.0f);
    }
}

TEST(EcsMobKnockback, LandedMobsSteerAgainInsideTheHurtWindow) {
    Registry reg;
    const EntityId mob = knockedBackMob(reg, MobAIState::Idle);
    reg.get<VelocityComp>(mob).onGround = true;
    MobAISystem::update(reg, DT);
    EXPECT_FLOAT_EQ(reg.get<VelocityComp>(mob).linear.x, 0.0f); // Idle stops it
}

TEST(EcsMobKnockback, AirborneMobsOutsideTheHurtWindowStillSteer) {
    Registry reg;
    const EntityId mob = EntityFactory::spawnMob(reg, {0.5f, 1.0f, 0.5f}, 20, 0.6f, 1.8f);
    auto& ai = reg.get<MobAIComp>(mob);
    ai.state = MobAIState::Wander;
    ai.stateTimer = 100.0f;
    ai.goal = {-20.0f, 1.0f, 0.5f};
    reg.get<VelocityComp>(mob).linear.x = 8.0f;
    MobAISystem::update(reg, DT);
    EXPECT_LT(reg.get<VelocityComp>(mob).linear.x, 0.0f); // Walking to the goal at -X
}
