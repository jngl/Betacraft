// Multi-component views: iterate the smallest pool, filter by the others.
#include "ecs/Registry.hpp"
#include "ecs/View.hpp"

#include <gtest/gtest.h>

#include <map>
#include <random>
#include <set>
#include <span>
#include <vector>

using namespace betacraft::ecs;

namespace {

struct A {
    int v{0};
};
struct B {
    int v{0};
};
struct C {
    int v{0};
};

} // namespace

TEST(EcsView, SingleComponentViewVisitsAllOwners) {
    Registry reg;
    std::set<EntityId> expected;
    for (int i = 0; i < 10; ++i) {
        const EntityId e = reg.create();
        if (i % 3 == 0) {
            reg.emplace<A>(e, i);
            expected.insert(e);
        }
    }
    std::set<EntityId> visited;
    reg.view<A>().each([&](EntityId e, A& a) {
        visited.insert(e);
        EXPECT_EQ(a.v % 3, 0);
    });
    EXPECT_EQ(visited, expected);
}

TEST(EcsView, MultiComponentViewOnlyVisitsIntersection) {
    Registry reg;
    std::set<EntityId> both;
    for (int i = 0; i < 50; ++i) {
        const EntityId e = reg.create();
        if (i % 2 == 0) {
            reg.emplace<A>(e, i);
        }
        if (i % 5 == 0) {
            reg.emplace<B>(e, i * 10);
        }
        if (i % 2 == 0 && i % 5 == 0) {
            both.insert(e);
        }
    }
    std::set<EntityId> visited;
    reg.view<A, B>().each([&](EntityId e, A& a, B& b) {
        visited.insert(e);
        EXPECT_EQ(b.v, a.v * 10);
    });
    EXPECT_EQ(visited, both);
    EXPECT_EQ((reg.view<A, B>().count()), both.size());
}

TEST(EcsView, CallbackWithoutEntityParameter) {
    Registry reg;
    for (int i = 0; i < 4; ++i) {
        const EntityId e = reg.create();
        reg.emplace<A>(e, 1);
        reg.emplace<B>(e, 2);
    }
    int sum = 0;
    reg.view<A, B>().each([&](A& a, B& b) { sum += a.v + b.v; });
    EXPECT_EQ(sum, 12);
}

TEST(EcsView, MutationsThroughViewArePersisted) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplace<A>(e, 1);
    reg.view<A>().each([](A& a) { a.v = 42; });
    EXPECT_EQ(reg.get<A>(e).v, 42);
}

TEST(EcsView, LeadingPoolIsTheSmallest) {
    Registry reg;
    for (int i = 0; i < 100; ++i) {
        const EntityId e = reg.create();
        reg.emplace<A>(e);
        if (i < 3) {
            reg.emplace<B>(e);
        }
    }
    auto v = reg.view<A, B>();
    EXPECT_EQ(v.sizeHint(), 3U);
    auto v2 = reg.view<B, A>();
    EXPECT_EQ(v2.sizeHint(), 3U);
}

// sizeHint() alone cannot tell which pool each() walks: the visit order can. The walked pool's
// dense order is observable, and a probe counter shows the non-leading pools are only probed
// for the leading pool's candidates (cost O(smallest pool), not O(first pool)).
TEST(EcsView, EachWalksTheSmallestPoolInItsDenseOrder) {
    Registry reg;
    std::vector<EntityId> es;
    for (int i = 0; i < 200; ++i) {
        const EntityId e = reg.create();
        reg.emplace<A>(e, i);
        es.push_back(e);
    }
    // B holds 5 entities in reverse creation order, so A-order and B-order differ.
    std::vector<EntityId> bOrder;
    for (int i = 199; i >= 0; i -= 40) {
        reg.emplace<B>(es[static_cast<size_t>(i)], i);
        bOrder.push_back(es[static_cast<size_t>(i)]);
    }
    for (int round = 0; round < 2; ++round) {
        std::vector<EntityId> visited;
        size_t calls = 0;
        if (round == 0) {
            reg.view<A, B>().each([&](EntityId e, A&, B&) { visited.push_back(e); ++calls; });
        } else {
            reg.view<B, A>().each([&](EntityId e, B&, A&) { visited.push_back(e); ++calls; });
        }
        const std::span<const EntityId> dense = reg.pool<B>().entities();
        EXPECT_EQ(visited, std::vector<EntityId>(dense.begin(), dense.end())) << round;
        EXPECT_EQ(visited, bOrder) << round;
        EXPECT_EQ(calls, 5U);
    }
    // Make A the smaller pool: the order now follows A's dense array.
    for (size_t i = 0; i < es.size(); ++i) {
        if (!reg.has<B>(es[i])) {
            reg.emplace<B>(es[i]);
        }
        if (i % 50 != 0) {
            reg.remove<A>(es[i]);
        }
    }
    std::vector<EntityId> visited;
    reg.view<B, A>().each([&](EntityId e, B&, A&) { visited.push_back(e); });
    const std::span<const EntityId> aDense = reg.pool<A>().entities();
    EXPECT_EQ(visited, std::vector<EntityId>(aDense.begin(), aDense.end()));
    EXPECT_EQ(visited.size(), 4U);
}

TEST(EcsView, EmptyWhenAnyPoolIsEmpty) {
    Registry reg;
    const EntityId e = reg.create();
    reg.emplace<A>(e);
    int calls = 0;
    reg.view<A, C>().each([&](A&, C&) { ++calls; });
    EXPECT_EQ(calls, 0);
    EXPECT_EQ((reg.view<A, C>().sizeHint()), 0U);
}

TEST(EcsView, ContainsMatchesHas) {
    Registry reg;
    const EntityId e1 = reg.create();
    const EntityId e2 = reg.create();
    reg.emplace<A>(e1);
    reg.emplace<B>(e1);
    reg.emplace<A>(e2);
    auto v = reg.view<A, B>();
    EXPECT_TRUE(v.contains(e1));
    EXPECT_FALSE(v.contains(e2));
    EXPECT_FALSE(v.contains(NULL_ENTITY));
}

TEST(EcsView, DestroyingVisitedEntityDuringIterationIsSafe) {
    Registry reg;
    std::set<EntityId> all;
    for (int i = 0; i < 200; ++i) {
        const EntityId e = reg.create();
        reg.emplace<A>(e, i);
        reg.emplace<B>(e, i);
        all.insert(e);
    }
    std::multiset<EntityId> visited;
    reg.view<A, B>().each([&](EntityId e, A& a, B&) {
        visited.insert(e);
        if (a.v % 2 == 0) {
            reg.destroy(e);
        }
    });
    EXPECT_EQ(visited.size(), all.size()); // every entity exactly once
    for (const EntityId e : all) {
        EXPECT_EQ(visited.count(e), 1U);
    }
    EXPECT_EQ(reg.count<A>(), 100U);
}

TEST(EcsView, RemovingIteratedComponentDuringIterationIsSafe) {
    Registry reg;
    for (int i = 0; i < 64; ++i) {
        const EntityId e = reg.create();
        reg.emplace<A>(e, i);
    }
    int visits = 0;
    reg.view<A>().each([&](EntityId e, A&) {
        ++visits;
        reg.remove<A>(e);
    });
    EXPECT_EQ(visits, 64);
    EXPECT_EQ(reg.count<A>(), 0U);
}

TEST(EcsView, RandomizedViewMatchesNaiveFilter) {
    std::mt19937 rng(77U);
    Registry reg;
    std::vector<EntityId> ents;
    for (int i = 0; i < 2000; ++i) {
        ents.push_back(reg.create());
    }
    for (int step = 0; step < 6000; ++step) {
        const EntityId e = ents[rng() % ents.size()];
        switch (rng() % 6) {
            case 0: if (!reg.has<A>(e)) { reg.emplace<A>(e); } break;
            case 1: if (!reg.has<B>(e)) { reg.emplace<B>(e); } break;
            case 2: if (!reg.has<C>(e)) { reg.emplace<C>(e); } break;
            case 3: reg.remove<A>(e); break;
            case 4: reg.remove<B>(e); break;
            default: reg.remove<C>(e); break;
        }
    }
    std::set<EntityId> expected;
    for (const EntityId e : ents) {
        if (reg.has<A, B, C>(e)) {
            expected.insert(e);
        }
    }
    std::set<EntityId> visited;
    reg.view<A, B, C>().each([&](EntityId e, A&, B&, C&) {
        EXPECT_TRUE(visited.insert(e).second);
    });
    EXPECT_EQ(visited, expected);
}
