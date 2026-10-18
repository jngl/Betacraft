// P2.1 — Typed component pool: payload array kept parallel to the sparse set's dense array.
#include "ecs/ComponentPool.hpp"

#include <gtest/gtest.h>

#include <map>
#include <random>
#include <stdexcept>
#include <string>

using namespace betacraft::ecs;

namespace {

struct Pos {
    int x{0};
    int y{0};
};

struct Tag {};

// Component whose constructor can throw, to check that a failed emplace leaves no trace.
struct Throwing {
    int v{0};
    explicit Throwing(int value) : v(value) {
        if (value < 0) {
            throw std::runtime_error("negative");
        }
    }
};

template<typename T>
void expectParallel(const ComponentPool<T>& pool) {
    ASSERT_EQ(pool.components().size(), pool.size());
    for (size_t i = 0; i < pool.size(); ++i) {
        ASSERT_EQ(pool.index(pool.entities()[i]), i);
        ASSERT_EQ(pool.tryGet(pool.entities()[i]), &pool.components()[i]);
    }
}

} // namespace

static_assert(ComponentType<Pos>);
static_assert(ComponentType<Tag>);
static_assert(!ComponentType<int*>);

TEST(EcsComponentPool, EmplaceStoresValueRetrievableByEntity) {
    ComponentPool<Pos> pool;
    const EntityId e = makeEntity(3, 0);
    Pos* p = pool.emplace(e, 4, 5);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->x, 4);
    EXPECT_EQ(p->y, 5);
    EXPECT_TRUE(pool.contains(e));
    EXPECT_EQ(pool.tryGet(e), p);
    EXPECT_EQ(pool.get(e).x, 4);
}

TEST(EcsComponentPool, EmplaceWithoutArgumentsValueInitialises) {
    ComponentPool<Pos> pool;
    Pos* p = pool.emplace(makeEntity(0, 0));
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(p->x, 0);
    EXPECT_EQ(p->y, 0);
}

TEST(EcsComponentPool, DuplicateEmplaceFailsAndKeepsOriginal) {
    ComponentPool<Pos> pool;
    const EntityId e = makeEntity(1, 0);
    pool.emplace(e, 1, 1);
    EXPECT_EQ(pool.emplace(e, 9, 9), nullptr);
    EXPECT_EQ(pool.size(), 1U);
    EXPECT_EQ(pool.get(e).x, 1);
    EXPECT_EQ(pool.emplace(NULL_ENTITY, 1, 1), nullptr);
    expectParallel(pool);
}

TEST(EcsComponentPool, TryGetReturnsNullForAbsentOrStale) {
    ComponentPool<Pos> pool;
    pool.emplace(makeEntity(2, 1), 1, 2);
    EXPECT_EQ(pool.tryGet(makeEntity(2, 0)), nullptr);
    EXPECT_EQ(pool.tryGet(makeEntity(2, 2)), nullptr);
    EXPECT_EQ(pool.tryGet(makeEntity(3, 1)), nullptr);
    EXPECT_EQ(pool.tryGet(NULL_ENTITY), nullptr);
    const auto& cpool = pool;
    EXPECT_NE(cpool.tryGet(makeEntity(2, 1)), nullptr);
}

TEST(EcsComponentPool, RemoveSwapsPayloadWithLast) {
    ComponentPool<Pos> pool;
    const EntityId a = makeEntity(0, 0);
    const EntityId b = makeEntity(1, 0);
    const EntityId c = makeEntity(2, 0);
    pool.emplace(a, 10, 0);
    pool.emplace(b, 20, 0);
    pool.emplace(c, 30, 0);
    EXPECT_TRUE(pool.remove(a));
    EXPECT_EQ(pool.size(), 2U);
    EXPECT_EQ(pool.components()[0].x, 30);
    EXPECT_EQ(pool.get(c).x, 30);
    EXPECT_EQ(pool.get(b).x, 20);
    EXPECT_EQ(pool.tryGet(a), nullptr);
    expectParallel(pool);
}

// The base-class mutators that bypass the payload (insert) or the owning group bookkeeping
// (swapPositions) must not be reachable on a pool: reg.pool<T>() is public.
template<typename P>
concept HasPublicInsert = requires(P& p, EntityId e) { p.insert(e); };
template<typename P>
concept HasPublicSwapPositions = requires(P& p) { p.swapPositions(size_t{0}, size_t{1}); };

TEST(EcsComponentPool, PayloadBypassingMutatorsAreNotPublic) {
    EXPECT_TRUE(HasPublicInsert<SparseSet>); // a bare set has no payload to desynchronise
    EXPECT_FALSE(HasPublicInsert<ComponentPool<Pos>>);
    EXPECT_FALSE(HasPublicSwapPositions<ComponentPool<Pos>>);
    EXPECT_FALSE(HasPublicSwapPositions<SparseSet>); // only owning groups may reorder
}

TEST(EcsComponentPool, BareInsertThroughTheBaseClassIsRefused) {
    ComponentPool<Pos> pool;
    pool.emplace(makeEntity(0, 0), 1, 2);
    SparseSet& base = pool;
    EXPECT_FALSE(base.insert(makeEntity(1, 0))); // would add a handle without a payload
    EXPECT_EQ(pool.size(), 1U);
    EXPECT_EQ(pool.tryGet(makeEntity(1, 0)), nullptr);
    expectParallel(pool);
}

TEST(EcsComponentPool, ClearDropsPayload) {
    ComponentPool<Pos> pool;
    pool.emplace(makeEntity(0, 0));
    pool.emplace(makeEntity(1, 0));
    pool.clear();
    EXPECT_TRUE(pool.components().empty());
    EXPECT_EQ(pool.tryGet(makeEntity(0, 0)), nullptr);
}

TEST(EcsComponentPool, ThrowingConstructorLeavesPoolUnchanged) {
    ComponentPool<Throwing> pool;
    pool.emplace(makeEntity(0, 0), 1);
    EXPECT_THROW((void)pool.emplace(makeEntity(1, 0), -1), std::runtime_error);
    EXPECT_EQ(pool.size(), 1U);
    EXPECT_FALSE(pool.contains(makeEntity(1, 0)));
    EXPECT_NE(pool.emplace(makeEntity(1, 0), 2), nullptr);
    expectParallel(pool);
}

TEST(EcsComponentPool, EmptyTagComponentsWork) {
    ComponentPool<Tag> pool;
    EXPECT_NE(pool.emplace(makeEntity(5, 0)), nullptr);
    EXPECT_TRUE(pool.contains(makeEntity(5, 0)));
    EXPECT_TRUE(pool.remove(makeEntity(5, 0)));
}

TEST(EcsComponentPool, ComponentsSpanIsMutable) {
    ComponentPool<Pos> pool;
    for (uint32_t i = 0; i < 10; ++i) {
        pool.emplace(makeEntity(i, 0), static_cast<int>(i), 0);
    }
    for (Pos& p : pool.components()) {
        p.y = p.x * 2;
    }
    for (uint32_t i = 0; i < 10; ++i) {
        EXPECT_EQ(pool.get(makeEntity(i, 0)).y, static_cast<int>(i) * 2);
    }
}

TEST(EcsComponentPool, RandomizedAgainstStdMap) {
    std::mt19937 rng(1234U);
    std::uniform_int_distribution<uint32_t> idxDist(0, 9000);
    std::uniform_int_distribution<int> opDist(0, 3);
    ComponentPool<Pos> pool;
    std::map<EntityId, int> model;
    for (int step = 0; step < 15000; ++step) {
        const EntityId e = makeEntity(idxDist(rng), 0);
        const int op = opDist(rng);
        if (op == 0) {
            ASSERT_EQ(pool.remove(e), model.erase(e) == 1U);
        } else if (op == 1) {
            const int v = static_cast<int>(rng() % 1000);
            Pos* p = pool.emplace(e, v, 0);
            const bool inserted = model.emplace(e, v).second;
            ASSERT_EQ(p != nullptr, inserted);
        } else {
            const Pos* p = pool.tryGet(e);
            const auto it = model.find(e);
            ASSERT_EQ(p != nullptr, it != model.end());
            if (p != nullptr) {
                ASSERT_EQ(p->x, it->second);
            }
        }
    }
    ASSERT_EQ(pool.size(), model.size());
    for (const auto& [e, v] : model) {
        ASSERT_EQ(pool.get(e).x, v);
    }
    expectParallel(pool);
}
