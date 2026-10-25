// Extended randomized differential testing of the Registry against a naive
// std::map model: a three-way owning group, a view mixing owned and free pools, full
// registry clears and stale handles surviving across clears and slot recycling.
#include "ecs/Registry.hpp"

#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <stdexcept>
#include <random>
#include <set>
#include <vector>

using namespace betacraft::ecs;

namespace {

struct P {
    EntityId owner{NULL_ENTITY};
    int v{0};
};
struct Q {
    EntityId owner{NULL_ENTITY};
    int v{0};
};
struct R {
    EntityId owner{NULL_ENTITY};
    int v{0};
};
struct S {
    EntityId owner{NULL_ENTITY};
    int v{0};
};

struct Model {
    std::optional<int> p, q, r, s;
};

template<typename T>
void checkPool(Registry& reg) {
    const ComponentPool<T>& pool = reg.pool<T>();
    ASSERT_EQ(pool.components().size(), pool.size());
    for (size_t i = 0; i < pool.size(); ++i) {
        ASSERT_EQ(pool.components()[i].owner, pool.entities()[i]);
        ASSERT_EQ(pool.index(pool.entities()[i]), i);
    }
}

void check(Registry& reg, const std::map<EntityId, Model>& model, const Group<P, Q, R>& g) {
    checkPool<P>(reg);
    checkPool<Q>(reg);
    checkPool<R>(reg);
    checkPool<S>(reg);
    size_t inGroup = 0;
    std::set<EntityId> expectedView;
    for (const auto& [e, m] : model) {
        ASSERT_TRUE(reg.valid(e));
        ASSERT_EQ(reg.tryGet<P>(e) != nullptr, m.p.has_value());
        ASSERT_EQ(reg.tryGet<Q>(e) != nullptr, m.q.has_value());
        ASSERT_EQ(reg.tryGet<R>(e) != nullptr, m.r.has_value());
        ASSERT_EQ(reg.tryGet<S>(e) != nullptr, m.s.has_value());
        if (m.p) { ASSERT_EQ(reg.get<P>(e).v, *m.p); }
        if (m.q) { ASSERT_EQ(reg.get<Q>(e).v, *m.q); }
        if (m.r) { ASSERT_EQ(reg.get<R>(e).v, *m.r); }
        if (m.s) { ASSERT_EQ(reg.get<S>(e).v, *m.s); }
        const bool member = m.p && m.q && m.r;
        ASSERT_EQ(g.contains(e), member);
        inGroup += member ? 1U : 0U;
        if (m.p && m.s) {
            expectedView.insert(e);
        }
    }
    ASSERT_EQ(reg.alive(), model.size());
    ASSERT_EQ(g.size(), inGroup);
    // Packed prefix: same entity at the same index in all three owned pools.
    const auto ps = g.get<P>();
    const auto qs = g.get<Q>();
    const auto rs = g.get<R>();
    for (size_t i = 0; i < g.size(); ++i) {
        const EntityId e = g.entities()[i];
        ASSERT_EQ(ps[i].owner, e);
        ASSERT_EQ(qs[i].owner, e);
        ASSERT_EQ(rs[i].owner, e);
    }
    // Group::each (the hot path) must visit exactly the members, each once, with its own
    // components, in reverse packed order.
    std::set<EntityId> expectedGroup;
    for (const auto& [e, m] : model) {
        if (m.p && m.q && m.r) {
            expectedGroup.insert(e);
        }
    }
    std::vector<EntityId> groupVisits;
    g.each([&](EntityId e, P& p, Q& q, R& r) {
        ASSERT_EQ(p.owner, e);
        ASSERT_EQ(q.owner, e);
        ASSERT_EQ(r.owner, e);
        ASSERT_EQ(p.v, *model.at(e).p);
        groupVisits.push_back(e);
    });
    ASSERT_EQ(std::set<EntityId>(groupVisits.begin(), groupVisits.end()), expectedGroup);
    ASSERT_EQ(groupVisits.size(), expectedGroup.size());
    for (size_t i = 0; i < groupVisits.size(); ++i) {
        ASSERT_EQ(groupVisits[i], g.entities()[g.size() - 1U - i]);
    }
    size_t componentOnlyVisits = 0;
    g.each([&](P&, Q&, R&) { ++componentOnlyVisits; });
    ASSERT_EQ(componentOnlyVisits, expectedGroup.size());

    std::set<EntityId> viewed;
    reg.view<P, S>().each([&](EntityId e, P& p, S& s) {
        ASSERT_EQ(p.owner, e);
        ASSERT_EQ(s.owner, e);
        ASSERT_TRUE(viewed.insert(e).second);
    });
    ASSERT_EQ(viewed, expectedView);
    ASSERT_EQ((reg.view<P, S>().count()), expectedView.size());
}

template<typename T>
void toggle(Registry& reg, std::mt19937& rng, EntityId e, std::optional<int>& slot) {
    const int v = static_cast<int>(rng() % 1000U);
    if (rng() % 2U == 0U) {
        reg.emplaceOrReplace<T>(e, T{e, v});
        slot = v;
    } else {
        ASSERT_EQ(reg.remove<T>(e), slot.has_value());
        slot.reset();
    }
}

void run(uint32_t seed, int steps) {
    std::mt19937 rng(seed);
    Registry reg(512); // small cap: forces heavy slot recycling
    auto g = reg.group<P, Q, R>();
    std::map<EntityId, Model> model;
    std::vector<EntityId> handles;
    for (int step = 0; step < steps; ++step) {
        const uint32_t op = rng() % 1000U;
        if (op < 2) {
            reg.clear();
            model.clear();
        } else if (op < 160 || handles.empty()) {
            const EntityId e = reg.create();
            if (e == NULL_ENTITY) {
                // Slots are never retired: creation fails only when the cap is fully alive.
                ASSERT_EQ(reg.alive(), reg.capacity());
                continue;
            }
            ASSERT_EQ(model.count(e), 0U) << "a live handle was reissued";
            handles.push_back(e);
            model.emplace(e, Model{});
        } else if (op < 260) {
            const EntityId e = handles[rng() % handles.size()];
            ASSERT_EQ(reg.destroy(e), model.erase(e) == 1U);
        } else {
            const EntityId e = handles[rng() % handles.size()];
            const auto it = model.find(e);
            if (it == model.end()) {
                ASSERT_FALSE(reg.valid(e));
                ASSERT_FALSE((reg.hasAny<P, Q, R, S>(e)));
                ASSERT_EQ(reg.tryGet<P>(e), nullptr);
                ASSERT_THROW(reg.emplace<S>(e, S{e, 0}), std::invalid_argument);
                continue;
            }
            switch (rng() % 4U) {
                case 0: toggle<P>(reg, rng, e, it->second.p); break;
                case 1: toggle<Q>(reg, rng, e, it->second.q); break;
                case 2: toggle<R>(reg, rng, e, it->second.r); break;
                default: toggle<S>(reg, rng, e, it->second.s); break;
            }
        }
        if (step % 61 == 0) {
            check(reg, model, g);
            ASSERT_FALSE(::testing::Test::HasFatalFailure()) << "seed " << seed << " step " << step;
        }
    }
    check(reg, model, g);
}

} // namespace

TEST(EcsDifferentialExtended, ThreeWayGroupViewsClearsAndRecycling) {
    for (const uint32_t seed : {11U, 23U, 2025U, 31337U}) {
        run(seed, 8000);
        ASSERT_FALSE(HasFatalFailure()) << "seed " << seed;
    }
}
