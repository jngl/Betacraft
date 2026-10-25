// Structural changes made by a callback while a view or group is being iterated.
// Systems routinely do this (an AI system emplacing VelocityComp to "wake" a mob, combat
// destroying its target), so the contract must hold even when the iterated pools are owned
// by a group and get reordered underneath the iteration.
//
// View contract: every entity that matches when each() starts and still matches when it is
//   reached is visited exactly once; entities that start matching during the iteration are not
//   visited, whatever structural change the callback makes.
// Group contract: the callback may freely change the visited entity and may add components to
//   any entity; joiners are not visited and nobody is skipped or visited twice.
// Both contracts are about WHICH entities are visited. The component references received by
// the callback are invalidated by its own structural changes (see test_command_buffer.cpp).
#include "ecs/Registry.hpp"

#include <gtest/gtest.h>

#include <map>
#include <random>
#include <set>
#include <vector>

using namespace betacraft::ecs;

namespace {

struct Pos {
    int v{0};
};
struct Vel {
    int v{0};
};
struct Tag {
    int v{0};
};

} // namespace

TEST(EcsIterationSafety, ViewOverOwnedPoolSurvivesVisitedEntityJoiningGroup) {
    Registry reg;
    auto g = reg.group<Pos, Vel>();
    std::vector<EntityId> all;
    for (int i = 0; i < 20; ++i) {
        const EntityId e = reg.create();
        reg.emplace<Pos>(e, i);
        reg.emplace<Tag>(e, i);
        if (i % 4 == 0) {
            reg.emplace<Vel>(e, i); // some already packed at the front of Pos
        }
        all.push_back(e);
    }
    std::map<EntityId, int> visits;
    // Lead pool is Pos (20) vs Tag (20): either way Pos is reordered by the group's pack.
    reg.view<Pos, Tag>().each([&](EntityId e, Pos&, Tag&) {
        ++visits[e];
        if (!reg.has<Vel>(e)) {
            reg.emplace<Vel>(e, 0); // joins the group -> swapped into the packed prefix
        }
    });
    EXPECT_EQ(visits.size(), all.size());
    for (const EntityId e : all) {
        EXPECT_EQ(visits[e], 1) << "entity index " << entityIndex(e);
    }
    EXPECT_EQ(g.size(), all.size());
}

TEST(EcsIterationSafety, ViewSurvivesDestroyingOtherEntities) {
    Registry reg;
    std::vector<EntityId> all;
    for (int i = 0; i < 30; ++i) {
        const EntityId e = reg.create();
        reg.emplace<Pos>(e, i);
        all.push_back(e);
    }
    std::map<EntityId, int> visits;
    std::set<EntityId> destroyed;
    reg.view<Pos>().each([&](EntityId e, Pos& p) {
        ++visits[e];
        // Kill the entity "three slots further" (by creation order), visited or not.
        const EntityId victim = all[static_cast<size_t>(p.v + 3) % all.size()];
        if (reg.destroy(victim)) {
            destroyed.insert(victim);
        }
    });
    for (const EntityId e : all) {
        const auto it = visits.find(e);
        const int n = it == visits.end() ? 0 : it->second;
        EXPECT_LE(n, 1) << "visited twice: " << entityIndex(e);
        if (destroyed.count(e) == 0U) {
            EXPECT_EQ(n, 1) << "survivor skipped: " << entityIndex(e);
        }
    }
}

TEST(EcsIterationSafety, ViewDoesNotVisitEntitiesCreatedDuringIteration) {
    Registry reg;
    for (int i = 0; i < 10; ++i) {
        reg.emplace<Pos>(reg.create(), i);
    }
    int visits = 0;
    reg.view<Pos>().each([&](Pos&) {
        ++visits;
        reg.emplace<Pos>(reg.create(), -1); // spawning (e.g. a mob dropping an item)
    });
    EXPECT_EQ(visits, 10);
    EXPECT_EQ(reg.count<Pos>(), 20U);
}

TEST(EcsIterationSafety, ViewSkipsEntityDestroyedAndRecycledBeforeBeingReached) {
    Registry reg(Registry::MAX_ENTITIES, 0); // recycle immediately
    std::vector<EntityId> all;
    for (int i = 0; i < 8; ++i) {
        const EntityId e = reg.create();
        reg.emplace<Pos>(e, i);
        all.push_back(e);
    }
    std::set<EntityId> visited;
    bool done = false;
    reg.view<Pos>().each([&](EntityId e, Pos&) {
        visited.insert(e);
        if (!done) {
            done = true;
            for (const EntityId other : all) {
                if (other != e && !visited.count(other)) {
                    ASSERT_TRUE(reg.destroy(other));
                    const EntityId reborn = reg.create(); // same slot, new generation
                    ASSERT_EQ(entityIndex(reborn), entityIndex(other));
                    reg.emplace<Pos>(reborn, 99);
                    break;
                }
            }
        }
    });
    for (const EntityId e : visited) {
        ASSERT_TRUE(reg.valid(e));
        EXPECT_NE(reg.get<Pos>(e).v, 99) << "a recycled handle was visited";
    }
    EXPECT_EQ(visited.size(), 7U);
}

TEST(EcsIterationSafety, GroupEachDoesNotVisitJoinersAndSkipsNobody) {
    Registry reg;
    auto g = reg.group<Pos, Vel>();
    std::vector<EntityId> members;
    std::vector<EntityId> outsiders;
    for (int i = 0; i < 16; ++i) {
        const EntityId e = reg.create();
        reg.emplace<Pos>(e, i);
        if (i % 2 == 0) {
            reg.emplace<Vel>(e, i);
            members.push_back(e);
        } else {
            outsiders.push_back(e);
        }
    }
    std::map<EntityId, int> visits;
    size_t next = 0;
    g.each([&](EntityId e, Pos&, Vel&) {
        ++visits[e];
        if (next < outsiders.size()) {
            reg.emplace<Vel>(outsiders[next++], 0);
        }
    });
    EXPECT_EQ(visits.size(), members.size());
    for (const EntityId e : members) {
        EXPECT_EQ(visits[e], 1);
    }
    EXPECT_EQ(g.size(), 16U);
}

TEST(EcsIterationSafety, RandomizedStructuralChurnInsideViewKeepsContract) {
    for (const uint32_t seed : {3U, 17U, 4242U}) {
        std::mt19937 rng(seed);
        Registry reg;
        (void)reg.group<Pos, Vel>();
        std::vector<EntityId> handles;
        for (int i = 0; i < 200; ++i) {
            const EntityId e = reg.create();
            handles.push_back(e);
            if (rng() % 4U != 0U) { reg.emplace<Pos>(e, i); }
            if (rng() % 2U != 0U) { reg.emplace<Vel>(e, i); }
            if (rng() % 3U != 0U) { reg.emplace<Tag>(e, i); }
        }
        std::set<EntityId> initial;
        reg.view<Pos, Tag>().each([&](EntityId e, Pos&, Tag&) { initial.insert(e); });

        std::map<EntityId, int> visits;
        std::set<EntityId> touched; // lost a component or died before possibly being reached
        reg.view<Pos, Tag>().each([&](EntityId e, Pos&, Tag&) {
            ++visits[e];
            for (int k = 0; k < 3; ++k) {
                const EntityId other = handles[rng() % handles.size()];
                switch (rng() % 6U) {
                    case 0: if (reg.destroy(other)) { touched.insert(other); } break;
                    case 1: if (reg.remove<Pos>(other)) { touched.insert(other); } break;
                    case 2: if (reg.valid(other)) { (void)reg.emplaceOrReplace<Vel>(other, 1); } break;
                    case 3: (void)reg.remove<Vel>(other); break;
                    case 4: {
                        const EntityId n = reg.create();
                        if (n != NULL_ENTITY) {
                            handles.push_back(n);
                            reg.emplace<Pos>(n, -1);
                            reg.emplace<Tag>(n, -1);
                        }
                        break;
                    }
                    default: if (reg.valid(other)) { (void)reg.emplaceOrReplace<Tag>(other, 2); } break;
                }
            }
        });
        for (const auto& [e, n] : visits) {
            EXPECT_EQ(n, 1) << "seed " << seed << " visited twice";
            EXPECT_EQ(initial.count(e), 1U) << "seed " << seed << " visited a newcomer";
        }
        for (const EntityId e : initial) {
            if (touched.count(e) == 0U) {
                EXPECT_EQ(visits.count(e), 1U) << "seed " << seed << " skipped a survivor";
            }
        }
    }
}
