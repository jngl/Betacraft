// Strong exception safety of the sparse sets under allocation failure: an
// O(1) insert must never leave a half-inserted entity behind). Allocation failures are
// injected deterministically by replacing the global operator new for this test binary;
// injection is disarmed by default so every other test allocates normally.
#include "ecs/ComponentPool.hpp"
#include "ecs/Registry.hpp"
#include "ecs/SparseSet.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <new>
#include <vector>

namespace {

// < 0: disarmed. Otherwise the number of allocations that still succeed before one throws.
long g_allocationsBeforeFailure = -1;

struct FailAllocationsAfter {
    explicit FailAllocationsAfter(long n) noexcept { g_allocationsBeforeFailure = n; }
    ~FailAllocationsAfter() { g_allocationsBeforeFailure = -1; }
    FailAllocationsAfter(const FailAllocationsAfter&) = delete;
    FailAllocationsAfter& operator=(const FailAllocationsAfter&) = delete;
};

} // namespace

void* operator new(std::size_t size) {
    if (g_allocationsBeforeFailure == 0) {
        g_allocationsBeforeFailure = -1;
        throw std::bad_alloc();
    }
    if (g_allocationsBeforeFailure > 0) {
        --g_allocationsBeforeFailure;
    }
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        return p;
    }
    throw std::bad_alloc();
}

// The nothrow forms must be replaced too: otherwise the runtime's own nothrow new (e.g. ASan's,
// used by std::stable_sort's temporary buffer) pairs with the free() below.
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try {
        return ::operator new(size);
    } catch (const std::bad_alloc&) {
        return nullptr;
    }
}

void operator delete(void* p) noexcept {
    std::free(p);
}

void operator delete(void* p, const std::nothrow_t&) noexcept {
    std::free(p);
}

void operator delete(void* p, std::size_t /*size*/) noexcept {
    std::free(p);
}

using namespace betacraft::ecs;

namespace {

struct Payload {
    EntityId owner{NULL_ENTITY};
    int value{0};
};

struct Partner {
    EntityId owner{NULL_ENTITY};
};

// Fills the set until its dense vector is at capacity, so the next insert must reallocate.
template<typename Set, typename Insert>
std::vector<EntityId> fillToCapacity(Set& set, Insert insert) {
    std::vector<EntityId> inserted;
    for (uint32_t i = 0; i < 4; ++i) {
        const EntityId e = makeEntity(i, 0);
        insert(set, e);
        inserted.push_back(e);
    }
    return inserted;
}

} // namespace

TEST(EcsExceptionSafety, SparseSetInsertThatFailsToGrowLeavesNoDanglingSlot) {
    SparseSet set;
    (void)fillToCapacity(set, [](SparseSet& s, EntityId e) { ASSERT_TRUE(s.insert(e)); });
    const EntityId e = makeEntity(4, 0);
    {
        FailAllocationsAfter guard(0); // the page exists: the only allocation is the dense growth
        EXPECT_THROW((void)set.insert(e), std::bad_alloc);
    }
    // canInsert only reads the sparse page, so it is safe to probe even on a broken set.
    ASSERT_TRUE(set.canInsert(e)) << "sparse slot still points past the dense array";
    EXPECT_FALSE(set.contains(e));
    EXPECT_EQ(set.size(), 4U);
    EXPECT_TRUE(set.insert(e));
    EXPECT_EQ(set.index(e), 4U);
}

TEST(EcsExceptionSafety, PoolEmplaceIsAllOrNothingAtEveryAllocationFailurePoint) {
    for (long failAt = 0;; ++failAt) {
        ComponentPool<Payload> pool;
        (void)fillToCapacity(pool, [](ComponentPool<Payload>& p, EntityId e) {
            ASSERT_NE(p.emplace(e, Payload{e, 1}), nullptr);
        });
        const EntityId e = makeEntity(4, 0);
        bool threw = false;
        {
            FailAllocationsAfter guard(failAt);
            try {
                (void)pool.emplace(e, Payload{e, 7});
            } catch (const std::bad_alloc&) {
                threw = true;
            }
        }
        if (threw) {
            ASSERT_TRUE(pool.canInsert(e)) << "failAt " << failAt;
            EXPECT_FALSE(pool.contains(e)) << failAt;
            EXPECT_EQ(pool.size(), 4U) << failAt;
            EXPECT_EQ(pool.components().size(), 4U) << failAt;
            ASSERT_NE(pool.emplace(e, Payload{e, 7}), nullptr) << failAt;
        }
        ASSERT_EQ(pool.size(), 5U);
        for (size_t i = 0; i < pool.size(); ++i) {
            EXPECT_EQ(pool.components()[i].owner, pool.entities()[i]) << failAt;
        }
        if (!threw) {
            break; // every allocation point has been exercised
        }
    }
}

TEST(EcsExceptionSafety, RegistryEmplaceIntoOwningGroupIsAllOrNothing) {
    for (long failAt = 0;; ++failAt) {
        Registry reg;
        auto g = reg.group<Payload, Partner>();
        std::vector<EntityId> es;
        for (int i = 0; i < 4; ++i) {
            const EntityId e = reg.create();
            reg.emplace<Payload>(e, Payload{e, i});
            reg.emplace<Partner>(e, Partner{e});
            es.push_back(e);
        }
        const EntityId e = reg.create();
        reg.emplace<Partner>(e, Partner{e});
        bool threw = false;
        {
            FailAllocationsAfter guard(failAt);
            try {
                (void)reg.emplace<Payload>(e, Payload{e, 99});
            } catch (const std::bad_alloc&) {
                threw = true;
            }
        }
        if (threw) {
            ASSERT_TRUE(reg.pool<Payload>().canInsert(e)) << failAt;
            EXPECT_FALSE(reg.has<Payload>(e)) << failAt;
            EXPECT_EQ(g.size(), 4U) << failAt;
            (void)reg.emplace<Payload>(e, Payload{e, 99});
        }
        ASSERT_EQ(g.size(), 5U) << failAt;
        for (size_t i = 0; i < g.size(); ++i) {
            EXPECT_EQ(g.get<Payload>()[i].owner, g.entities()[i]) << failAt;
            EXPECT_EQ(g.get<Partner>()[i].owner, g.entities()[i]) << failAt;
        }
        EXPECT_EQ(reg.get<Payload>(e).value, 99);
        if (!threw) {
            break;
        }
    }
}
