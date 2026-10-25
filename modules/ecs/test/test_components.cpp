// Gameplay components are plain data usable in pools.
#include "ecs/Components.hpp"
#include "ecs/Registry.hpp"

#include <gtest/gtest.h>

#include <type_traits>

using namespace betacraft;
using namespace betacraft::ecs;

static_assert(ComponentType<TransformComp>);
static_assert(ComponentType<VelocityComp>);
static_assert(ComponentType<HealthComp>);
static_assert(ComponentType<AABBColliderComp>);
static_assert(ComponentType<ItemDropComp>);
static_assert(ComponentType<MobAIComp>);
static_assert(ComponentType<PlayerInputComp>);
static_assert(ComponentType<PlayerTagComp>);
static_assert(std::is_trivially_copyable_v<TransformComp>);
static_assert(std::is_trivially_copyable_v<VelocityComp>);
static_assert(std::is_trivially_copyable_v<HealthComp>);

TEST(EcsComponents, TransformDefaults) {
    const TransformComp t{};
    EXPECT_EQ(t.pos, glm::vec3(0.0f));
    EXPECT_EQ(t.prevPos, glm::vec3(0.0f));
    EXPECT_FLOAT_EQ(t.yaw, 0.0f);
    EXPECT_FLOAT_EQ(t.pitch, 0.0f);
}

TEST(EcsComponents, VelocityDefaults) {
    const VelocityComp v{};
    EXPECT_EQ(v.linear, glm::vec3(0.0f));
    EXPECT_FALSE(v.onGround);
    EXPECT_FALSE(v.inWater);
    EXPECT_FLOAT_EQ(v.gravityScale, 1.0f);
    EXPECT_FLOAT_EQ(v.horizontalDrag, 0.0f);
}

TEST(EcsComponents, HealthDefaultsMatchBetaPlayer) {
    const HealthComp h{};
    EXPECT_EQ(h.current, 20);
    EXPECT_EQ(h.max, 20);
    EXPECT_FLOAT_EQ(h.hurtInvulnerabilityTimer, 0.0f);
    EXPECT_FALSE(h.isDead());
    HealthComp dead{0, 20, 0.0f};
    EXPECT_TRUE(dead.isDead());
}

TEST(EcsComponents, ColliderDefaultsMatchPlayerHitbox) {
    const AABBColliderComp c{};
    EXPECT_FLOAT_EQ(c.width, core::EngineConfig::PLAYER_WIDTH);
    EXPECT_FLOAT_EQ(c.height, core::EngineConfig::PLAYER_HEIGHT);
}

TEST(EcsComponents, ColliderBoundsAreFeetAnchored) {
    const AABBColliderComp c{0.6f, 1.8f};
    const math::AABB box = c.boundsAt({10.0f, 64.0f, -3.0f});
    EXPECT_FLOAT_EQ(box.min.x, 9.7f);
    EXPECT_FLOAT_EQ(box.max.x, 10.3f);
    EXPECT_FLOAT_EQ(box.min.y, 64.0f);
    EXPECT_FLOAT_EQ(box.max.y, 65.8f);
    EXPECT_FLOAT_EQ(box.min.z, -3.3f);
    EXPECT_FLOAT_EQ(box.max.z, -2.7f);
    // Must agree with the legacy Player hitbox so the later migration is behaviour-preserving.
    const math::AABB legacy = physics::Player::getAABBAt({10.0f, 64.0f, -3.0f});
    EXPECT_FLOAT_EQ(box.min.x, legacy.min.x);
    EXPECT_FLOAT_EQ(box.max.y, legacy.max.y);
}

TEST(EcsComponents, ItemDropDefaults) {
    const ItemDropComp item{};
    EXPECT_EQ(item.id, voxel::BlockId::Air);
    EXPECT_EQ(item.count, 1);
    EXPECT_FLOAT_EQ(item.ageSeconds, 0.0f);
    EXPECT_GT(item.pickupDelaySeconds, 0.0f);
}

TEST(EcsComponents, MobAIDefaults) {
    const MobAIComp ai{};
    EXPECT_EQ(ai.state, MobAIState::Idle);
    EXPECT_EQ(ai.target, NULL_ENTITY);
    EXPECT_EQ(ai.rngState, 0U); // seeded lazily from the entity handle by MobAISystem
}

TEST(EcsComponents, TeleportSetsBothPositions) {
    TransformComp t{{1.0f, 2.0f, 3.0f}, {0.0f, 0.0f, 0.0f}, 10.0f, 5.0f};
    teleport(t, {7.0f, 8.0f, 9.0f});
    EXPECT_EQ(t.pos, glm::vec3(7.0f, 8.0f, 9.0f));
    EXPECT_EQ(t.prevPos, glm::vec3(7.0f, 8.0f, 9.0f));
    EXPECT_FLOAT_EQ(t.yaw, 10.0f); // orientation untouched
}

TEST(EcsComponents, PlayerInputWrapsLegacyInput) {
    PlayerInputComp in{};
    in.input.forward = true;
    EXPECT_TRUE(in.input.forward);
    EXPECT_FALSE(in.input.jump);
}

TEST(EcsComponents, StoredInRegistryAndGroupable) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplace<TransformComp>(e, TransformComp{{1.0f, 2.0f, 3.0f}, {1.0f, 2.0f, 3.0f}, 90.0f, 0.0f});
    reg.emplace<VelocityComp>(e);
    auto g = reg.group<TransformComp, VelocityComp>();
    EXPECT_EQ(g.size(), 1U);
    EXPECT_FLOAT_EQ(g.get<TransformComp>()[0].yaw, 90.0f);
}
