// P2.1 Impact: "plus de 1 000 entités dynamiques ... à 60+ FPS". Wall-clock assertions are
// nondeterministic (sanitizers, loaded CI), so the budget is pinned as work counts instead:
// with 2 500 dynamic entities, a full simulation tick costs a bounded number of collision
// queries per body, every system touches every entity once, and the world stays sane.
#include "ecs/Components.hpp"
#include "ecs/Registry.hpp"
#include "ecs/Systems.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace betacraft;
using namespace betacraft::ecs;

namespace {

constexpr float DT = 0.05f;
constexpr int MOBS = 1500;
constexpr int ITEMS = 1000;

bool floorBelowZero(int32_t /*x*/, int32_t y, int32_t /*z*/) {
    return y < 0;
}

} // namespace

TEST(EcsEntityBudget, TwoThousandFiveHundredEntitiesStayWithinThePerTickWorkBudget) {
    Registry reg;
    const EntityId player = EntityFactory::spawnPlayer(reg, {0.5f, 0.0f, 0.5f});
    reg.get<PlayerInputComp>(player).input.forward = true;
    for (int i = 0; i < MOBS; ++i) {
        const glm::vec3 at(static_cast<float>(i % 50) * 3.0f, 0.0f, static_cast<float>(i / 50) * 3.0f);
        (void)EntityFactory::spawnMob(reg, at, 20, 0.6f, 1.8f);
    }
    for (int i = 0; i < ITEMS; ++i) {
        const glm::vec3 at(static_cast<float>(i % 40) * 2.0f + 0.5f, 1.0f, -static_cast<float>(i / 40) * 2.0f);
        (void)EntityFactory::spawnItemDrop(reg, at, world::BlockId::Dirt, 1, {1.0f, 2.0f, 0.0f});
    }
    const size_t bodies = reg.count<AABBColliderComp>();
    ASSERT_EQ(bodies, static_cast<size_t>(MOBS + ITEMS + 1));

    size_t queries = 0;
    size_t worstTick = 0;
    std::vector<RenderInstance> instances;
    constexpr int TICKS = 40;
    for (int tick = 0; tick < TICKS; ++tick) {
        size_t tickQueries = 0;
        PlayerControlSystem::update(reg, DT);
        MobAISystem::update(reg, DT);
        MovementPhysicsSystem::update(reg, DT);
        VoxelCollisionSystem::update(reg, [&](const math::AABB& box) {
            ++tickQueries;
            return VoxelCollisionSystem::gridCollides(box, floorBelowZero);
        });
        (void)HealthSystem::update(reg, DT);
        EXPECT_EQ(ItemDropSystem::update(reg, DT), 0U);
        EntityRenderInterpolationSystem::collect(reg, 0.5f, instances);
        ASSERT_EQ(instances.size(), bodies) << tick;
        queries += tickQueries;
        worstTick = std::max(worstTick, tickQueries);
    }
    // Normal speeds need one sub-step: 1 embed check + 3 axis moves + 12 bisection steps when
    // resting on the floor (+1 ground probe when not moving vertically) = 17 at most.
    const double perBodyTick = static_cast<double>(queries) / static_cast<double>(bodies * TICKS);
    EXPECT_LE(perBodyTick, 17.0);
    EXPECT_LE(worstTick, bodies * 17U);
    // Movement went through the stride-1 owning group, which holds every body.
    EXPECT_EQ((reg.group<TransformComp, VelocityComp>().size()), bodies);

    size_t grounded = 0;
    reg.view<TransformComp, VelocityComp>().each([&](const TransformComp& t, const VelocityComp& v) {
        ASSERT_TRUE(std::isfinite(t.pos.x) && std::isfinite(t.pos.y) && std::isfinite(t.pos.z));
        EXPECT_GE(t.pos.y, -1e-3f);
        grounded += v.onGround ? 1U : 0U;
    });
    EXPECT_EQ(grounded, bodies);
}
