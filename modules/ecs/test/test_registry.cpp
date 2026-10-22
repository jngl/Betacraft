// P2.1 — Registry: entity lifecycle (versioned handles) and component CRUD.
#include "ecs/Registry.hpp"

#include <gtest/gtest.h>

#include <set>
#include <stdexcept>
#include <vector>

using namespace betacraft::ecs;

namespace {

struct Position {
    float x{0.0f};
    float y{0.0f};
};

struct Velocity {
    float dx{0.0f};
    float dy{0.0f};
};

struct Name {
    int id{0};
};

} // namespace

// ---- Entity lifecycle -------------------------------------------------------------------

TEST(EcsRegistry, CreateReturnsDistinctValidEntities) {
    Registry reg;
    const EntityId a = reg.create();
    const EntityId b = reg.create();
    EXPECT_NE(a, NULL_ENTITY);
    EXPECT_NE(b, NULL_ENTITY);
    EXPECT_NE(a, b);
    EXPECT_TRUE(reg.valid(a));
    EXPECT_TRUE(reg.valid(b));
    EXPECT_EQ(reg.alive(), 2U);
}

TEST(EcsRegistry, FirstEntitiesUseSequentialIndicesAndGenerationZero) {
    Registry reg;
    for (uint32_t i = 1; i <= 5; ++i) { // index 0 is reserved
        const EntityId e = reg.create();
        EXPECT_EQ(entityIndex(e), i);
        EXPECT_EQ(entityGeneration(e), 0U);
    }
}

TEST(EcsRegistry, NullAndUnknownHandlesAreInvalid) {
    Registry reg;
    EXPECT_FALSE(reg.valid(NULL_ENTITY));
    EXPECT_FALSE(reg.valid(makeEntity(0, 0)));
    EXPECT_FALSE(reg.valid(makeEntity(12345, 0)));
}

TEST(EcsRegistry, DestroyInvalidatesHandle) {
    Registry reg;
    const EntityId e = reg.create();
    EXPECT_TRUE(reg.destroy(e));
    EXPECT_FALSE(reg.valid(e));
    EXPECT_EQ(reg.alive(), 0U);
}

TEST(EcsRegistry, DoubleDestroyAndNullDestroyAreNoOps) {
    Registry reg;
    const EntityId e = reg.create();
    EXPECT_TRUE(reg.destroy(e));
    EXPECT_FALSE(reg.destroy(e));
    EXPECT_FALSE(reg.destroy(NULL_ENTITY));
    EXPECT_FALSE(reg.destroy(makeEntity(999, 0)));
    EXPECT_EQ(reg.alive(), 0U);
}

TEST(EcsRegistry, RecycledSlotGetsBumpedGeneration) {
    Registry reg(Registry::MAX_ENTITIES, 0); // no aging reserve: recycle immediately
    const EntityId first = reg.create();
    reg.destroy(first);
    const EntityId second = reg.create();
    EXPECT_EQ(entityIndex(second), entityIndex(first));
    EXPECT_EQ(entityGeneration(second), entityGeneration(first) + 1U);
    EXPECT_FALSE(reg.valid(first));
    EXPECT_TRUE(reg.valid(second));
}

TEST(EcsRegistry, StaleHandleCannotDestroyRecycledEntity) {
    Registry reg(Registry::MAX_ENTITIES, 0);
    const EntityId stale = reg.create();
    reg.destroy(stale);
    const EntityId fresh = reg.create();
    ASSERT_EQ(entityIndex(fresh), entityIndex(stale));
    EXPECT_FALSE(reg.destroy(stale));
    EXPECT_TRUE(reg.valid(fresh));
}

TEST(EcsRegistry, NotYetIssuedGenerationIsInvalid) {
    Registry reg;
    const EntityId e = reg.create();
    reg.destroy(e);
    // The next handle for this slot exists only once create() hands it out.
    EXPECT_FALSE(reg.valid(makeEntity(entityIndex(e), entityGeneration(e) + 1U)));
}

TEST(EcsRegistry, GenerationWrapsAroundAfter256Recycles) {
    Registry reg(1);
    EntityId e = reg.create();
    const EntityId first = e;
    const uint32_t slot = entityIndex(e);
    std::set<EntityId> seen{e};
    for (uint32_t gen = 1; gen <= 0xFFU; ++gen) {
        reg.destroy(e);
        e = reg.create();
        ASSERT_EQ(entityIndex(e), slot);
        ASSERT_EQ(entityGeneration(e), gen);
        ASSERT_TRUE(seen.insert(e).second); // 256 distinct handles per slot
    }
    for (const EntityId old : seen) {
        EXPECT_EQ(reg.valid(old), old == e);
    }
    // The 257th handle of the slot reuses generation 0: the slot is never retired (that would
    // leak it). This aliasing window is inherent to 8-bit generations and is pushed out by
    // FIFO recycling with an aging reserve (see test_registry_recycling.cpp).
    reg.destroy(e);
    const EntityId next = reg.create();
    EXPECT_EQ(next, first);
    EXPECT_EQ(reg.alive(), 1U);
}

TEST(EcsRegistry, CapacityLimitReturnsNullEntity) {
    Registry reg(3);
    EXPECT_EQ(reg.capacity(), 3U);
    const EntityId a = reg.create();
    (void)reg.create();
    (void)reg.create();
    EXPECT_EQ(reg.create(), NULL_ENTITY);
    EXPECT_EQ(reg.alive(), 3U);
    reg.destroy(a);
    const EntityId again = reg.create();
    EXPECT_NE(again, NULL_ENTITY);
    EXPECT_EQ(entityIndex(again), entityIndex(a));
}

TEST(EcsRegistry, CapacityIsClampedToEncodableIndices) {
    Registry reg(0xFFFFFFFFU);
    EXPECT_EQ(reg.capacity(), MAX_ENTITY_INDEX); // indices 1..MAX_ENTITY_INDEX (0 is reserved)
    EXPECT_EQ(Registry::MAX_ENTITIES, MAX_ENTITY_INDEX);
    Registry zero(0);
    EXPECT_EQ(zero.create(), NULL_ENTITY);
}

TEST(EcsRegistry, ForEachEntityVisitsExactlyAliveEntities) {
    Registry reg;
    std::vector<EntityId> ents;
    for (int i = 0; i < 10; ++i) {
        ents.push_back(reg.create());
    }
    reg.destroy(ents[3]);
    reg.destroy(ents[7]);
    std::set<EntityId> visited;
    reg.forEachEntity([&](EntityId e) { visited.insert(e); });
    EXPECT_EQ(visited.size(), 8U);
    EXPECT_EQ(visited.count(ents[3]), 0U);
    EXPECT_EQ(visited.count(ents[0]), 1U);
}

TEST(EcsRegistry, ClearDestroysEverything) {
    Registry reg;
    const EntityId a = reg.create();
    reg.emplace<Position>(a, 1.0f, 2.0f);
    reg.clear();
    EXPECT_FALSE(reg.valid(a));
    EXPECT_EQ(reg.alive(), 0U);
    EXPECT_EQ(reg.count<Position>(), 0U);
    const EntityId b = reg.create();
    EXPECT_TRUE(reg.valid(b));
    EXPECT_FALSE(reg.has<Position>(b));
}

// ---- Components -------------------------------------------------------------------------

TEST(EcsRegistry, EmplaceThenGet) {
    Registry reg;
    const EntityId e = reg.create();
    Position& p = reg.emplace<Position>(e, 3.0f, 4.0f);
    EXPECT_FLOAT_EQ(p.x, 3.0f);
    EXPECT_TRUE(reg.has<Position>(e));
    EXPECT_FLOAT_EQ(reg.get<Position>(e).y, 4.0f);
    EXPECT_EQ(reg.tryGet<Position>(e), &p);
    EXPECT_EQ(reg.count<Position>(), 1U);
}

TEST(EcsRegistry, HasRequiresAllListedComponents) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplace<Position>(e);
    EXPECT_TRUE(reg.has<Position>(e));
    EXPECT_FALSE((reg.has<Position, Velocity>(e)));
    reg.emplace<Velocity>(e);
    EXPECT_TRUE((reg.has<Position, Velocity>(e)));
    EXPECT_TRUE((reg.hasAny<Name, Velocity>(e)));
    EXPECT_FALSE(reg.hasAny<Name>(e));
}

TEST(EcsRegistry, EmplaceOnInvalidEntityThrows) {
    Registry reg;
    const EntityId e = reg.create();
    reg.destroy(e);
    EXPECT_THROW(reg.emplace<Position>(e), std::invalid_argument);
    EXPECT_THROW(reg.emplace<Position>(NULL_ENTITY), std::invalid_argument);
    EXPECT_EQ(reg.count<Position>(), 0U);
}

TEST(EcsRegistry, DuplicateEmplaceThrowsAndKeepsValue) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplace<Position>(e, 1.0f, 1.0f);
    EXPECT_THROW(reg.emplace<Position>(e, 5.0f, 5.0f), std::logic_error);
    EXPECT_FLOAT_EQ(reg.get<Position>(e).x, 1.0f);
}

TEST(EcsRegistry, EmplaceOrReplaceOverwrites) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplaceOrReplace<Position>(e, 1.0f, 1.0f);
    Position& p = reg.emplaceOrReplace<Position>(e, 7.0f, 8.0f);
    EXPECT_FLOAT_EQ(p.x, 7.0f);
    EXPECT_EQ(reg.count<Position>(), 1U);
    EXPECT_THROW(reg.emplaceOrReplace<Position>(NULL_ENTITY), std::invalid_argument);
}

TEST(EcsRegistry, GetMissingComponentThrows) {
    Registry reg;
    const EntityId e = reg.create();
    EXPECT_THROW((void)reg.get<Position>(e), std::out_of_range);
    reg.emplace<Velocity>(e);
    EXPECT_THROW((void)reg.get<Position>(e), std::out_of_range);
    const Registry& creg = reg;
    EXPECT_THROW((void)creg.get<Position>(e), std::out_of_range);
}

TEST(EcsRegistry, TryGetReturnsNullWhenAbsent) {
    Registry reg;
    const EntityId e = reg.create();
    EXPECT_EQ(reg.tryGet<Position>(e), nullptr);
    EXPECT_EQ(reg.tryGet<Position>(NULL_ENTITY), nullptr);
    const Registry& creg = reg;
    EXPECT_EQ(creg.tryGet<Position>(e), nullptr); // must not create the pool
    EXPECT_EQ(creg.count<Position>(), 0U);
}

TEST(EcsRegistry, RemoveComponent) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplace<Position>(e);
    reg.emplace<Velocity>(e);
    EXPECT_TRUE(reg.remove<Position>(e));
    EXPECT_FALSE(reg.remove<Position>(e));
    EXPECT_FALSE(reg.has<Position>(e));
    EXPECT_TRUE(reg.has<Velocity>(e));
    EXPECT_FALSE(reg.remove<Name>(e));
    EXPECT_FALSE(reg.remove<Position>(NULL_ENTITY));
}

TEST(EcsRegistry, DestroyRemovesAllComponents) {
    Registry reg;
    const EntityId a = reg.create();
    const EntityId b = reg.create();
    reg.emplace<Position>(a, 1.0f, 0.0f);
    reg.emplace<Velocity>(a);
    reg.emplace<Name>(a, 7);
    reg.emplace<Position>(b, 2.0f, 0.0f);
    reg.destroy(a);
    EXPECT_EQ(reg.count<Position>(), 1U);
    EXPECT_EQ(reg.count<Velocity>(), 0U);
    EXPECT_EQ(reg.count<Name>(), 0U);
    EXPECT_FLOAT_EQ(reg.get<Position>(b).x, 2.0f);
}

TEST(EcsRegistry, RecycledEntityStartsWithoutComponents) {
    Registry reg(Registry::MAX_ENTITIES, 0);
    const EntityId a = reg.create();
    reg.emplace<Position>(a, 9.0f, 9.0f);
    reg.destroy(a);
    const EntityId b = reg.create();
    ASSERT_EQ(entityIndex(a), entityIndex(b));
    EXPECT_FALSE(reg.has<Position>(b));
    EXPECT_EQ(reg.tryGet<Position>(a), nullptr);
    reg.emplace<Position>(b, 1.0f, 1.0f);
    // The stale handle must not see the new occupant's component.
    EXPECT_FALSE(reg.has<Position>(a));
    EXPECT_EQ(reg.tryGet<Position>(a), nullptr);
}

TEST(EcsRegistry, DenseComponentsSpanMatchesPool) {
    Registry reg;
    for (int i = 0; i < 4; ++i) {
        reg.emplace<Name>(reg.create(), i);
    }
    const auto dense = reg.getDenseComponents<Name>();
    ASSERT_EQ(dense.size(), 4U);
    int sum = 0;
    for (const Name& n : dense) {
        sum += n.id;
    }
    EXPECT_EQ(sum, 0 + 1 + 2 + 3);
    EXPECT_TRUE(reg.getDenseComponents<Velocity>().empty());
}

TEST(EcsRegistry, SpecAliasesBehaveLikeCoreApi) {
    Registry reg;
    const EntityId e = reg.createEntity();
    reg.addComponent(e, Position{1.0f, 2.0f});
    ASSERT_NE(reg.getComponent<Position>(e), nullptr);
    EXPECT_FLOAT_EQ(reg.getComponent<Position>(e)->y, 2.0f);
    reg.addComponent(e, Position{5.0f, 6.0f}); // replaces
    EXPECT_FLOAT_EQ(reg.getComponent<Position>(e)->x, 5.0f);
    reg.destroyEntity(e);
    EXPECT_FALSE(reg.valid(e));
    EXPECT_EQ(reg.getComponent<Position>(e), nullptr);
}

TEST(EcsRegistry, ComponentReferencesSurviveUnrelatedPoolGrowth) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplace<Position>(e, 1.0f, 2.0f);
    for (int i = 0; i < 1000; ++i) {
        reg.emplace<Velocity>(reg.create());
    }
    EXPECT_FLOAT_EQ(reg.get<Position>(e).y, 2.0f);
}

TEST(EcsRegistry, IsMovable) {
    Registry a;
    const EntityId e = a.create();
    a.emplace<Position>(e, 4.0f, 0.0f);
    Registry b(std::move(a));
    EXPECT_TRUE(b.valid(e));
    EXPECT_FLOAT_EQ(b.get<Position>(e).x, 4.0f);
}

TEST(EcsRegistry, ThousandsOfEntitiesCreateDestroy) {
    Registry reg;
    std::vector<EntityId> ents;
    for (int i = 0; i < 5000; ++i) {
        const EntityId e = reg.create();
        reg.emplace<Position>(e, static_cast<float>(i), 0.0f);
        ents.push_back(e);
    }
    for (size_t i = 0; i < ents.size(); i += 2) {
        reg.destroy(ents[i]);
    }
    EXPECT_EQ(reg.alive(), 2500U);
    EXPECT_EQ(reg.count<Position>(), 2500U);
    for (size_t i = 1; i < ents.size(); i += 2) {
        ASSERT_FLOAT_EQ(reg.get<Position>(ents[i]).x, static_cast<float>(i));
    }
}
