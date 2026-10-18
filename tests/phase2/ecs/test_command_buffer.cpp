// P2.1 — Deferred structural changes. References handed to View::each / Group::each point
// into the pools' std::vector storage: an emplace into an iterated pool may reallocate it, a
// group join swaps payloads and a destroy swap-and-pops them. A CommandBuffer records such
// changes during iteration and applies them afterwards, so the references stay valid.
#include "ecs/CommandBuffer.hpp"
#include "ecs/Components.hpp"
#include "ecs/Registry.hpp"
#include "ecs/Systems.hpp"

#include <gtest/gtest.h>

#include <map>
#include <vector>

using namespace betacraft;
using namespace betacraft::ecs;

namespace {

struct Pos {
    EntityId owner{NULL_ENTITY};
    float x{0.0f};
};
struct Vel {
    float dx{0.0f};
};
struct Tag {};

} // namespace

TEST(EcsCommandBuffer, RecordedCommandsWaitForFlush) {
    Registry reg;
    const EntityId a = reg.create();
    const EntityId b = reg.create();
    reg.emplace<Vel>(b);
    CommandBuffer cb;
    EXPECT_TRUE(cb.empty());
    cb.emplaceOrReplace(a, Pos{a, 3.0f});
    cb.remove<Vel>(b);
    cb.destroy(b);
    EXPECT_EQ(cb.size(), 3U);
    EXPECT_FALSE(reg.has<Pos>(a));
    EXPECT_TRUE(reg.valid(b));
    EXPECT_EQ(cb.flush(reg), 3U);
    EXPECT_TRUE(cb.empty());
    ASSERT_TRUE(reg.has<Pos>(a));
    EXPECT_FLOAT_EQ(reg.get<Pos>(a).x, 3.0f);
    EXPECT_FALSE(reg.valid(b));
}

TEST(EcsCommandBuffer, FlushAppliesInRecordingOrder) {
    Registry reg;
    const EntityId a = reg.create();
    const EntityId b = reg.create();
    CommandBuffer cb;
    cb.emplaceOrReplace(a, Pos{a, 1.0f});
    cb.remove<Pos>(a);
    cb.remove<Pos>(b);
    cb.emplaceOrReplace(b, Pos{b, 1.0f});
    cb.emplaceOrReplace(b, Pos{b, 2.0f});
    (void)cb.flush(reg);
    EXPECT_FALSE(reg.has<Pos>(a));
    ASSERT_TRUE(reg.has<Pos>(b));
    EXPECT_FLOAT_EQ(reg.get<Pos>(b).x, 2.0f);
}

TEST(EcsCommandBuffer, CommandsOnEntitiesDeadAtFlushTimeAreSkipped) {
    Registry reg(1);
    const EntityId a = reg.create();
    CommandBuffer cb;
    cb.destroy(a);
    cb.emplaceOrReplace(a, Pos{a, 1.0f}); // a is dead by then: must not throw
    cb.remove<Pos>(a);
    cb.destroy(a);
    cb.destroy(NULL_ENTITY);
    EXPECT_EQ(cb.flush(reg), 1U); // only the first destroy did anything
    // The slot gets recycled: a stale command must not hit the new occupant.
    const EntityId fresh = reg.create();
    ASSERT_EQ(entityIndex(fresh), entityIndex(a));
    cb.emplaceOrReplace(a, Pos{a, 9.0f});
    cb.destroy(a);
    EXPECT_EQ(cb.flush(reg), 0U);
    EXPECT_TRUE(reg.valid(fresh));
    EXPECT_FALSE(reg.has<Pos>(fresh));
}

TEST(EcsCommandBuffer, CommandsRecordedDuringFlushRunOnTheNextFlush) {
    Registry reg;
    CommandBuffer cb;
    int runs = 0;
    cb.defer([&](Registry&) {
        ++runs;
        cb.defer([&](Registry&) { ++runs; });
    });
    EXPECT_EQ(cb.flush(reg), 1U);
    EXPECT_EQ(runs, 1);
    EXPECT_EQ(cb.size(), 1U);
    EXPECT_EQ(cb.flush(reg), 1U);
    EXPECT_EQ(runs, 2);
}

TEST(EcsCommandBuffer, ClearDiscardsPendingCommands) {
    Registry reg;
    const EntityId a = reg.create();
    CommandBuffer cb;
    cb.destroy(a);
    cb.clear();
    EXPECT_TRUE(cb.empty());
    EXPECT_EQ(cb.flush(reg), 0U);
    EXPECT_TRUE(reg.valid(a));
}

// The integration pattern from the review: mob death -> loot spawn. Spawning while iterating
// would emplace TransformComp into the iterated pool and could reallocate it under `t`.
TEST(EcsCommandBuffer, DeathHandlerSpawnsLootWithoutTouchingIteratedPools) {
    Registry reg;
    std::map<EntityId, glm::vec3> deathPos;
    for (int i = 0; i < 300; ++i) {
        const glm::vec3 p(static_cast<float>(i), 64.0f, -static_cast<float>(i));
        const EntityId m = EntityFactory::spawnMob(reg, p, 10, 0.6f, 1.8f);
        if (i % 3 == 0) {
            reg.get<HealthComp>(m).current = 0;
            deathPos.emplace(m, p);
        }
    }
    CommandBuffer cb;
    const size_t poolBefore = reg.count<TransformComp>();
    reg.view<TransformComp, HealthComp>().each([&](EntityId e, TransformComp& t, HealthComp& h) {
        if (!h.isDead()) {
            return;
        }
        const glm::vec3 at = t.pos;
        cb.defer([at](Registry& r) {
            (void)EntityFactory::spawnItemDrop(r, at, world::BlockId::Dirt, 1);
            (void)EntityFactory::spawnItemDrop(r, at, world::BlockId::Sand, 1);
        });
        cb.destroy(e);
        EXPECT_EQ(t.pos, deathPos.at(e)); // still this entity's component: nothing moved yet
        EXPECT_EQ(reg.count<TransformComp>(), poolBefore);
    });
    EXPECT_EQ(cb.flush(reg), 2U * deathPos.size());
    EXPECT_EQ(reg.count<ItemDropComp>(), 2U * deathPos.size());
    EXPECT_EQ(reg.count<MobAIComp>(), 300U - deathPos.size());
    std::map<float, int> dropsAtX;
    reg.view<ItemDropComp, TransformComp>().each([&](ItemDropComp&, TransformComp& t) { ++dropsAtX[t.pos.x]; });
    for (const auto& [mob, p] : deathPos) {
        EXPECT_EQ(dropsAtX[p.x], 2) << p.x;
    }
}

// Documented contract when changes are made in place instead: re-fetch after the change.
TEST(EcsCommandBuffer, InPlaceChangesRequireRefetchingTheComponent) {
    Registry reg;
    auto g = reg.group<Pos, Vel>();
    std::vector<EntityId> es;
    for (int i = 0; i < 64; ++i) {
        const EntityId e = reg.create();
        reg.emplace<Pos>(e, Pos{e, 0.0f});
        reg.emplace<Tag>(e);
        es.push_back(e);
    }
    reg.view<Pos, Tag>().each([&](EntityId e, Pos&, Tag&) {
        reg.emplace<Vel>(e);      // joins the group: Pos payloads are swapped
        reg.get<Pos>(e).x = 1.0f; // re-fetched, so it is this entity's Pos
    });
    EXPECT_EQ(g.size(), es.size());
    for (const EntityId e : es) {
        EXPECT_EQ(reg.get<Pos>(e).owner, e);
        EXPECT_FLOAT_EQ(reg.get<Pos>(e).x, 1.0f);
    }
}
