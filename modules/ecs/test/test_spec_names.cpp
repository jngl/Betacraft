// Spec names. The original design spec named the components and systems differently from the
// implementation; aliases let integration code use either name.
#include "ecs/Components.hpp"
#include "ecs/Registry.hpp"
#include "ecs/Systems.hpp"

#include <gtest/gtest.h>

#include <type_traits>

using namespace betacraft::ecs;

static_assert(std::is_same_v<TransformComponent, TransformComp>);
static_assert(std::is_same_v<VelocityComponent, VelocityComp>);
static_assert(std::is_same_v<AABBColliderComponent, AABBColliderComp>);
static_assert(std::is_same_v<HealthComponent, HealthComp>);
static_assert(std::is_same_v<ItemDropComponent, ItemDropComp>);
static_assert(std::is_same_v<MobAIComponent, MobAIComp>);
static_assert(std::is_same_v<PlayerInputComponent, PlayerInputComp>);

static_assert(std::is_same_v<CollisionResolutionSystem, VoxelCollisionSystem>);
static_assert(std::is_same_v<EntityRenderSystem, EntityRenderInterpolationSystem>);
static_assert(std::is_same_v<CombatDamageSystem, HealthSystem>);
static_assert(std::is_same_v<MobPathfindingAISystem, MobAISystem>);

// The dense transforms and velocities of an owning group are index-aligned. That holds
// for the owning group's packed prefix; getDenseComponents returns the whole pool, so only
// its first group.size() entries are index-aligned with the partner pool.
TEST(EcsSpecNames, DenseSpansAreIndexAlignedWithinTheOwningGroupPrefix) {
    Registry reg;
    auto g = reg.group<TransformComponent, VelocityComponent>();
    for (int i = 0; i < 100; ++i) {
        const EntityId e = reg.createEntity();
        reg.addComponent(e, TransformComponent{{static_cast<float>(i), 0.0f, 0.0f}, {}, 0.0f, 0.0f});
        if (i % 3 != 0) {
            VelocityComponent v{};
            v.linear.x = static_cast<float>(i);
            reg.addComponent(e, v);
        }
        if (i % 7 == 0) {
            reg.destroyEntity(e);
        }
    }
    const auto transforms = reg.getDenseComponents<TransformComponent>();
    const auto velocities = reg.getDenseComponents<VelocityComponent>();
    ASSERT_GE(transforms.size(), g.size());
    ASSERT_GE(velocities.size(), g.size());
    ASSERT_GT(g.size(), 0U);
    for (size_t i = 0; i < g.size(); ++i) {
        EXPECT_FLOAT_EQ(transforms[i].pos.x, velocities[i].linear.x) << i;
    }
}
