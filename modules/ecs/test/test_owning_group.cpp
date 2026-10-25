// Synchronised owning groups ("owning pools"): owned pools keep the group's
// members packed at the front of their dense arrays, in the same order, so that
// dense_A[i] and dense_B[i] always belong to the same entity.
#include "ecs/Group.hpp"
#include "ecs/Registry.hpp"

#include <gtest/gtest.h>

#include <map>
#include <optional>
#include <random>
#include <set>
#include <stdexcept>
#include <vector>

using namespace betacraft::ecs;

namespace {

// Each component records its owner so desynchronisation is detectable from the payload alone.
struct A {
    EntityId owner{NULL_ENTITY};
    int v{0};
};
struct B {
    EntityId owner{NULL_ENTITY};
    int v{0};
};
struct C {
    EntityId owner{NULL_ENTITY};
    int v{0};
};
struct D {
    EntityId owner{NULL_ENTITY};
    int v{0};
};

template<typename T>
void expectPayloadMatchesOwner(Registry& reg) {
    const ComponentPool<T>& p = reg.pool<T>();
    for (size_t i = 0; i < p.size(); ++i) {
        ASSERT_EQ(p.components()[i].owner, p.entities()[i]) << "payload desynchronised at " << i;
    }
}

// The group invariant: the first size() slots of every owned pool hold the same entities in the
// same order, those are exactly the entities owning all components, and nothing else is packed.
template<typename T1, typename T2, typename... Rest>
void expectGroupInvariant(Registry& reg, const Group<T1, T2, Rest...>& g) {
    const ComponentPool<T1>& p1 = reg.pool<T1>();
    const ComponentPool<T2>& p2 = reg.pool<T2>();
    ASSERT_LE(g.size(), p1.size());
    ASSERT_LE(g.size(), p2.size());
    for (size_t i = 0; i < g.size(); ++i) {
        const EntityId e = p1.entities()[i];
        ASSERT_EQ(p2.entities()[i], e) << "owned pools out of step at " << i;
        ((void)[&] { ASSERT_EQ(reg.pool<Rest>().entities()[i], e); }(), ...);
        ASSERT_TRUE((reg.has<T1, T2, Rest...>(e)));
        ASSERT_EQ(g.entities()[i], e);
    }
    // Every entity outside the prefix must be missing at least one owned component.
    for (size_t i = g.size(); i < p1.size(); ++i) {
        ASSERT_FALSE((reg.has<T1, T2, Rest...>(p1.entities()[i])));
    }
    size_t matches = 0;
    for (const EntityId e : p1.entities()) {
        matches += reg.has<T1, T2, Rest...>(e) ? 1U : 0U;
    }
    ASSERT_EQ(matches, g.size());
    expectPayloadMatchesOwner<T1>(reg);
    expectPayloadMatchesOwner<T2>(reg);
}

EntityId makeAB(Registry& reg, int v) {
    const EntityId e = reg.create();
    reg.emplace<A>(e, e, v);
    reg.emplace<B>(e, e, v * 2);
    return e;
}

} // namespace

TEST(EcsOwningGroup, EmptyGroupHasSizeZero) {
    Registry reg;
    auto g = reg.group<A, B>();
    EXPECT_EQ(g.size(), 0U);
    EXPECT_TRUE(g.empty());
    EXPECT_TRUE(g.entities().empty());
    EXPECT_TRUE(g.get<A>().empty());
}

TEST(EcsOwningGroup, AddingBothComponentsJoinsGroup) {
    Registry reg;
    auto g = reg.group<A, B>();
    const EntityId e = reg.create();
    reg.emplace<A>(e, e, 1);
    EXPECT_EQ(g.size(), 0U);
    EXPECT_FALSE(g.contains(e));
    reg.emplace<B>(e, e, 2);
    EXPECT_EQ(g.size(), 1U);
    EXPECT_TRUE(g.contains(e));
    expectGroupInvariant(reg, g);
}

TEST(EcsOwningGroup, BuildsFromPreExistingComponents) {
    Registry reg;
    std::set<EntityId> both;
    for (int i = 0; i < 100; ++i) {
        const EntityId e = reg.create();
        if (i % 2 == 0) {
            reg.emplace<A>(e, e, i);
        }
        if (i % 3 == 0) {
            reg.emplace<B>(e, e, i);
        }
        if (i % 6 == 0) {
            both.insert(e);
        }
    }
    auto g = reg.group<A, B>();
    EXPECT_EQ(g.size(), both.size());
    expectGroupInvariant(reg, g);
    std::set<EntityId> members(g.entities().begin(), g.entities().end());
    EXPECT_EQ(members, both);
}

TEST(EcsOwningGroup, MembersArePackedInterleavedWithNonMembers) {
    Registry reg;
    auto g = reg.group<A, B>();
    const EntityId onlyA = reg.create();
    reg.emplace<A>(onlyA, onlyA, 0);
    const EntityId ab = makeAB(reg, 5);
    // ab was appended after onlyA, but the group must have moved it to the front.
    EXPECT_EQ(reg.pool<A>().entities()[0], ab);
    EXPECT_EQ(reg.pool<A>().entities()[1], onlyA);
    EXPECT_EQ(g.size(), 1U);
    expectGroupInvariant(reg, g);
}

TEST(EcsOwningGroup, RemovingOwnedComponentLeavesGroup) {
    Registry reg;
    auto g = reg.group<A, B>();
    std::vector<EntityId> ents;
    for (int i = 0; i < 10; ++i) {
        ents.push_back(makeAB(reg, i));
    }
    EXPECT_EQ(g.size(), 10U);
    reg.remove<A>(ents[3]);
    EXPECT_EQ(g.size(), 9U);
    EXPECT_FALSE(g.contains(ents[3]));
    EXPECT_TRUE(reg.has<B>(ents[3]));
    expectGroupInvariant(reg, g);
    reg.remove<B>(ents[0]);
    EXPECT_EQ(g.size(), 8U);
    expectGroupInvariant(reg, g);
    reg.remove<B>(ents[3]); // not a member any more: size must not change
    EXPECT_EQ(g.size(), 8U);
    expectGroupInvariant(reg, g);
}

TEST(EcsOwningGroup, DestroyingMemberShrinksGroup) {
    Registry reg;
    auto g = reg.group<A, B>();
    std::vector<EntityId> ents;
    for (int i = 0; i < 20; ++i) {
        ents.push_back(makeAB(reg, i));
    }
    for (size_t i = 0; i < ents.size(); i += 3) {
        reg.destroy(ents[i]);
        expectGroupInvariant(reg, g);
    }
    EXPECT_EQ(g.size(), 13U);
}

TEST(EcsOwningGroup, ReAddingComponentRejoins) {
    Registry reg;
    auto g = reg.group<A, B>();
    const EntityId e = makeAB(reg, 1);
    makeAB(reg, 2);
    reg.remove<A>(e);
    reg.emplace<A>(e, e, 9);
    EXPECT_EQ(g.size(), 2U);
    EXPECT_TRUE(g.contains(e));
    expectGroupInvariant(reg, g);
}

TEST(EcsOwningGroup, EmplaceOrReplaceOnMemberKeepsLayout) {
    Registry reg;
    auto g = reg.group<A, B>();
    const EntityId e = makeAB(reg, 1);
    makeAB(reg, 2);
    reg.emplaceOrReplace<A>(e, e, 77);
    EXPECT_EQ(g.size(), 2U);
    EXPECT_EQ(reg.get<A>(e).v, 77);
    expectGroupInvariant(reg, g);
}

TEST(EcsOwningGroup, DenseSpansAreIndexAligned) {
    Registry reg;
    auto g = reg.group<A, B>();
    for (int i = 0; i < 30; ++i) {
        const EntityId e = reg.create();
        reg.emplace<A>(e, e, i);
        if (i % 4 != 0) {
            reg.emplace<B>(e, e, i * 2);
        }
    }
    const auto as = g.get<A>();
    const auto bs = g.get<B>();
    ASSERT_EQ(as.size(), g.size());
    ASSERT_EQ(bs.size(), g.size());
    for (size_t i = 0; i < g.size(); ++i) {
        EXPECT_EQ(as[i].owner, bs[i].owner);
        EXPECT_EQ(bs[i].v, as[i].v * 2);
    }
}

TEST(EcsOwningGroup, EachVisitsMembersWithMatchingComponents) {
    Registry reg;
    auto g = reg.group<A, B>();
    std::set<EntityId> expected;
    for (int i = 0; i < 40; ++i) {
        const EntityId e = reg.create();
        reg.emplace<A>(e, e, i);
        if (i % 2 == 0) {
            reg.emplace<B>(e, e, i * 2);
            expected.insert(e);
        }
    }
    std::set<EntityId> visited;
    g.each([&](EntityId e, A& a, B& b) {
        EXPECT_EQ(a.owner, e);
        EXPECT_EQ(b.owner, e);
        EXPECT_EQ(b.v, a.v * 2);
        visited.insert(e);
    });
    EXPECT_EQ(visited, expected);
    int n = 0;
    g.each([&](A&, B&) { ++n; });
    EXPECT_EQ(n, static_cast<int>(expected.size()));
}

TEST(EcsOwningGroup, ForEachOwnedGroupAlias) {
    Registry reg;
    for (int i = 0; i < 5; ++i) {
        makeAB(reg, i);
    }
    int sum = 0;
    reg.forEachOwnedGroup<A, B>([&](A& a, B& b) {
        a.v += 1;
        sum += b.v;
    });
    EXPECT_EQ(sum, 2 * (0 + 1 + 2 + 3 + 4));
    int sumA = 0;
    reg.forEachOwnedGroup<A, B>([&](EntityId, A& a, B&) { sumA += a.v; });
    EXPECT_EQ(sumA, (0 + 1 + 2 + 3 + 4) + 5);
}

TEST(EcsOwningGroup, DestroyingVisitedEntityDuringEachIsSafe) {
    Registry reg;
    auto g = reg.group<A, B>();
    std::set<EntityId> all;
    for (int i = 0; i < 100; ++i) {
        all.insert(makeAB(reg, i));
    }
    std::multiset<EntityId> visited;
    g.each([&](EntityId e, A& a, B&) {
        visited.insert(e);
        if (a.v % 3 == 0) {
            reg.destroy(e);
        } else if (a.v % 3 == 1) {
            reg.remove<B>(e);
        }
    });
    EXPECT_EQ(visited.size(), all.size());
    for (const EntityId e : all) {
        EXPECT_EQ(visited.count(e), 1U);
    }
    EXPECT_EQ(g.size(), 33U);
    expectGroupInvariant(reg, g);
}

TEST(EcsOwningGroup, SameTypeSetReturnsSameGroupRegardlessOfOrder) {
    Registry reg;
    auto g1 = reg.group<A, B>();
    makeAB(reg, 1);
    auto g2 = reg.group<B, A>();
    EXPECT_EQ(g2.size(), 1U);
    makeAB(reg, 2);
    EXPECT_EQ(g1.size(), 2U);
    EXPECT_EQ(g2.size(), 2U);
    EXPECT_EQ(reg.groupCount(), 1U);
}

TEST(EcsOwningGroup, OverlappingOwnershipIsRejected) {
    Registry reg;
    (void)reg.group<A, B>();
    EXPECT_THROW((void)(reg.group<A, C>()), std::logic_error);
    EXPECT_THROW((void)(reg.group<B, C, D>()), std::logic_error);
    EXPECT_THROW((void)(reg.group<A>()), std::logic_error);
    EXPECT_NO_THROW((void)(reg.group<C, D>()));
    EXPECT_EQ(reg.groupCount(), 2U);
}

TEST(EcsOwningGroup, TwoDisjointGroupsStayIndependentlyConsistent) {
    Registry reg;
    auto gab = reg.group<A, B>();
    auto gcd = reg.group<C, D>();
    for (int i = 0; i < 50; ++i) {
        const EntityId e = reg.create();
        if (i % 2 == 0) { reg.emplace<A>(e, e, i); }
        if (i % 3 == 0) { reg.emplace<B>(e, e, i); }
        if (i % 5 == 0) { reg.emplace<C>(e, e, i); }
        if (i % 7 != 0) { reg.emplace<D>(e, e, i); }
    }
    expectGroupInvariant(reg, gab);
    expectGroupInvariant(reg, gcd);
    EXPECT_EQ(gab.size(), 9U);  // multiples of 6 in [0,50)
    EXPECT_EQ(gcd.size(), 8U);  // multiples of 5 not multiples of 7 (0 and 35 excluded)
}

TEST(EcsOwningGroup, ThreeComponentGroup) {
    Registry reg;
    auto g = reg.group<A, B, C>();
    std::vector<EntityId> ents;
    for (int i = 0; i < 60; ++i) {
        const EntityId e = reg.create();
        ents.push_back(e);
        reg.emplace<A>(e, e, i);
        if (i % 2 == 0) { reg.emplace<B>(e, e, i); }
        if (i % 3 == 0) { reg.emplace<C>(e, e, i); }
    }
    EXPECT_EQ(g.size(), 10U);
    expectGroupInvariant(reg, g);
    for (size_t i = 0; i < ents.size(); i += 4) {
        reg.remove<C>(ents[i]);
        expectGroupInvariant(reg, g);
    }
    int n = 0;
    g.each([&](EntityId e, A& a, B& b, C& c) {
        EXPECT_EQ(a.owner, e);
        EXPECT_EQ(b.owner, e);
        EXPECT_EQ(c.owner, e);
        ++n;
    });
    EXPECT_EQ(static_cast<size_t>(n), g.size());
}

TEST(EcsOwningGroup, ViewsStillWorkOverOwnedPools) {
    Registry reg;
    auto g = reg.group<A, B>();
    for (int i = 0; i < 20; ++i) {
        const EntityId e = reg.create();
        reg.emplace<A>(e, e, i);
        if (i % 2 == 0) { reg.emplace<B>(e, e, i); }
        if (i % 5 == 0) { reg.emplace<C>(e, e, i); }
    }
    std::set<EntityId> visited;
    reg.view<A, C>().each([&](EntityId e, A& a, C& c) {
        EXPECT_EQ(a.owner, e);
        EXPECT_EQ(c.owner, e);
        visited.insert(e);
    });
    EXPECT_EQ(visited.size(), 4U);
    expectGroupInvariant(reg, g);
}

TEST(EcsOwningGroup, RegistryClearResetsGroup) {
    Registry reg;
    auto g = reg.group<A, B>();
    for (int i = 0; i < 10; ++i) {
        makeAB(reg, i);
    }
    reg.clear();
    EXPECT_EQ(g.size(), 0U);
    makeAB(reg, 1);
    EXPECT_EQ(g.size(), 1U);
    expectGroupInvariant(reg, g);
}

TEST(EcsOwningGroup, ClearingAnOwnedPoolDirectlyResetsGroup) {
    Registry reg;
    auto g = reg.group<A, B>();
    for (int i = 0; i < 10; ++i) {
        makeAB(reg, i);
    }
    reg.pool<A>().clear();
    EXPECT_EQ(g.size(), 0U);
    expectGroupInvariant(reg, g);
}

TEST(EcsOwningGroup, GroupSurvivesRegistryMove) {
    Registry reg;
    (void)reg.group<A, B>();
    makeAB(reg, 1);
    Registry moved(std::move(reg));
    auto g = moved.group<A, B>();
    EXPECT_EQ(g.size(), 1U);
    makeAB(moved, 2);
    EXPECT_EQ(g.size(), 2U);
    expectGroupInvariant(moved, g);
}

TEST(EcsOwningGroup, MoveAssignmentReplacesGroupsSafely) {
    Registry a;
    (void)a.group<A, B>();
    makeAB(a, 1);
    Registry b;
    (void)b.group<C, D>();
    const EntityId e = b.create();
    b.emplace<C>(e, e, 1);
    b.emplace<D>(e, e, 1);
    b = std::move(a); // b's old group must be torn down before its pools
    EXPECT_EQ(b.groupCount(), 1U);
    EXPECT_EQ(b.alive(), 1U);
    EXPECT_EQ(a.alive(), 0U); // NOLINT(bugprone-use-after-move): moved-from state is specified
    auto g = b.group<A, B>();
    makeAB(b, 2);
    EXPECT_EQ(g.size(), 2U);
    expectGroupInvariant(b, g);
    EXPECT_NO_THROW((void)(b.group<C, D>())); // C and D are free again in the new state
}

// ---- Randomised differential testing against a naive model --------------------------------

namespace {

struct ModelEntity {
    std::optional<int> a;
    std::optional<int> b;
    std::optional<int> c;
    std::optional<int> d;
};

void checkAgainstModel(Registry& reg, const std::map<EntityId, ModelEntity>& model,
                       const Group<A, B>& gab, const Group<C, D>& gcd) {
    size_t expectedAB = 0;
    size_t expectedCD = 0;
    for (const auto& [e, m] : model) {
        ASSERT_TRUE(reg.valid(e));
        ASSERT_EQ(reg.has<A>(e), m.a.has_value());
        ASSERT_EQ(reg.has<B>(e), m.b.has_value());
        ASSERT_EQ(reg.has<C>(e), m.c.has_value());
        ASSERT_EQ(reg.has<D>(e), m.d.has_value());
        if (m.a) { ASSERT_EQ(reg.get<A>(e).v, *m.a); }
        if (m.b) { ASSERT_EQ(reg.get<B>(e).v, *m.b); }
        if (m.c) { ASSERT_EQ(reg.get<C>(e).v, *m.c); }
        if (m.d) { ASSERT_EQ(reg.get<D>(e).v, *m.d); }
        const bool inAB = m.a && m.b;
        const bool inCD = m.c && m.d;
        ASSERT_EQ(gab.contains(e), inAB);
        ASSERT_EQ(gcd.contains(e), inCD);
        expectedAB += inAB ? 1U : 0U;
        expectedCD += inCD ? 1U : 0U;
    }
    ASSERT_EQ(reg.alive(), model.size());
    ASSERT_EQ(gab.size(), expectedAB);
    ASSERT_EQ(gcd.size(), expectedCD);
    expectGroupInvariant(reg, gab);
    expectGroupInvariant(reg, gcd);
}

template<typename T>
void randomToggle(Registry& reg, std::mt19937& rng, EntityId e, std::optional<int>& slot) {
    const int v = static_cast<int>(rng() % 100000);
    switch (rng() % 3) {
        case 0:
            if (!slot) {
                reg.emplace<T>(e, e, v);
                slot = v;
            }
            break;
        case 1:
            reg.emplaceOrReplace<T>(e, e, v);
            slot = v;
            break;
        default:
            EXPECT_EQ(reg.remove<T>(e), slot.has_value());
            slot.reset();
            break;
    }
}

void runDifferential(uint32_t seed, int steps, bool groupsFirst) {
    std::mt19937 rng(seed);
    Registry reg;
    std::map<EntityId, ModelEntity> model;
    std::vector<EntityId> handles; // includes stale handles on purpose

    std::optional<Group<A, B>> gab;
    std::optional<Group<C, D>> gcd;
    if (groupsFirst) {
        gab.emplace(reg.group<A, B>());
        gcd.emplace(reg.group<C, D>());
    }

    for (int step = 0; step < steps; ++step) {
        if (!groupsFirst && step == steps / 2) {
            // Late group creation must adopt the existing population.
            gab.emplace(reg.group<A, B>());
            gcd.emplace(reg.group<C, D>());
        }
        const uint32_t op = rng() % 100;
        if (op < 15 || handles.empty()) {
            const EntityId e = reg.create();
            handles.push_back(e);
            model.emplace(e, ModelEntity{});
        } else if (op < 22) {
            const EntityId e = handles[rng() % handles.size()];
            const bool wasAlive = model.erase(e) == 1U;
            ASSERT_EQ(reg.destroy(e), wasAlive);
        } else {
            const EntityId e = handles[rng() % handles.size()];
            auto it = model.find(e);
            if (it == model.end()) {
                // Stale handle: every query must fail and nothing may change.
                ASSERT_FALSE(reg.valid(e));
                ASSERT_FALSE((reg.hasAny<A, B, C, D>(e)));
                ASSERT_FALSE(reg.remove<A>(e));
                continue;
            }
            switch (rng() % 4) {
                case 0: randomToggle<A>(reg, rng, e, it->second.a); break;
                case 1: randomToggle<B>(reg, rng, e, it->second.b); break;
                case 2: randomToggle<C>(reg, rng, e, it->second.c); break;
                default: randomToggle<D>(reg, rng, e, it->second.d); break;
            }
        }
        if (gab && (step % 97 == 0)) {
            checkAgainstModel(reg, model, *gab, *gcd);
            if (::testing::Test::HasFatalFailure()) {
                FAIL() << "seed " << seed << " step " << step;
            }
        }
    }
    checkAgainstModel(reg, model, *gab, *gcd);

    // Groups iterate exactly the model's intersection with the right values.
    std::map<EntityId, std::pair<int, int>> fromGroup;
    gab->each([&](EntityId e, A& a, B& b) { fromGroup.emplace(e, std::make_pair(a.v, b.v)); });
    for (const auto& [e, m] : model) {
        if (m.a && m.b) {
            ASSERT_EQ(fromGroup.at(e), std::make_pair(*m.a, *m.b));
        }
    }
}

} // namespace

TEST(EcsOwningGroupDifferential, RandomOpsGroupsCreatedFirst) {
    for (uint32_t seed : {1U, 2U, 3U, 42U, 1337U}) {
        runDifferential(seed, 6000, true);
        ASSERT_FALSE(HasFatalFailure()) << "seed " << seed;
    }
}

TEST(EcsOwningGroupDifferential, RandomOpsGroupsCreatedMidway) {
    for (uint32_t seed : {7U, 99U, 2024U}) {
        runDifferential(seed, 6000, false);
        ASSERT_FALSE(HasFatalFailure()) << "seed " << seed;
    }
}
