// P2.1 — Entity slot recycling (EntityId = 24-bit index + 8-bit generation) under long-running
// churn such as dropped items (Fiche 3 §1: "sans ralentissement ni fuite mémoire"):
//   * the all-zero handle is never issued, so zero-initialised EntityId fields never alias;
//   * a capped registry never runs out of slots while fewer than `capacity` entities live;
//   * the index space (and every pool's sparse pages) stays bounded by peak population;
//   * FIFO recycling with a reserve of aging free slots spreads generations, so a stale
//     handle is only aliased after 256 recycles of its slot.
#include "ecs/Registry.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <deque>
#include <random>
#include <set>
#include <vector>

using namespace betacraft::ecs;

namespace {

struct Item {
    int v{0};
};

} // namespace

TEST(EcsRegistryRecycling, ZeroHandleIsNeverIssued) {
    Registry reg(1024, 0);
    for (int round = 0; round < 600; ++round) {
        const EntityId e = reg.create();
        ASSERT_NE(e, NULL_ENTITY);
        ASSERT_NE(entityIndex(e), 0U) << "index 0 is reserved (spec m_nextId{1})";
        ASSERT_NE(e, EntityId{0});
        if (round % 2 == 0) {
            (void)reg.destroy(e);
        }
    }
    EXPECT_FALSE(reg.valid(EntityId{0}));
    EntityId zeroInitialised{};
    EXPECT_FALSE(reg.valid(zeroInitialised));
    EXPECT_EQ(reg.tryGet<Item>(zeroInitialised), nullptr);
}

TEST(EcsRegistryRecycling, CapacityCountsUsableEntities) {
    Registry reg(3);
    EXPECT_NE(reg.create(), NULL_ENTITY);
    EXPECT_NE(reg.create(), NULL_ENTITY);
    EXPECT_NE(reg.create(), NULL_ENTITY);
    EXPECT_EQ(reg.create(), NULL_ENTITY);
    EXPECT_EQ(reg.slotCount(), 3U);
}

TEST(EcsRegistryRecycling, SingleSlotRegistrySurvivesGenerationWrapAround) {
    Registry reg(1);
    const EntityId first = reg.create();
    EntityId e = first;
    for (int i = 0; i < 1000; ++i) {
        ASSERT_TRUE(reg.destroy(e));
        e = reg.create();
        ASSERT_NE(e, NULL_ENTITY) << "cycle " << i << ": slot exhausted although alive() == 0";
        ASSERT_EQ(entityIndex(e), entityIndex(first));
        ASSERT_EQ(entityGeneration(e), static_cast<uint32_t>(i + 1) & ENTITY_GENERATION_MASK);
    }
    EXPECT_EQ(reg.alive(), 1U);
    EXPECT_EQ(reg.slotCount(), 1U);
}

// The exact failure the review describes: a 512-item budget used to stop spawning after
// 512 * 256 creations because exhausted slots counted against the cap.
TEST(EcsRegistryRecycling, CappedItemBudgetNeverStopsSpawningUnderChurn) {
    constexpr uint32_t CAP = 512;
    Registry reg(CAP);
    std::mt19937 rng(7U);
    std::vector<EntityId> live;
    for (int i = 0; i < 160000; ++i) {
        if (live.size() == CAP || (!live.empty() && rng() % 2U == 0U)) {
            const size_t k = rng() % live.size();
            ASSERT_TRUE(reg.destroy(live[k]));
            live[k] = live.back();
            live.pop_back();
        } else {
            const EntityId e = reg.create();
            ASSERT_NE(e, NULL_ENTITY) << "creation " << i << " with alive() = " << reg.alive();
            live.push_back(e);
        }
    }
    EXPECT_LE(reg.slotCount(), CAP);
}

TEST(EcsRegistryRecycling, IndexSpaceStaysBoundedUnderLongChurn) {
    Registry reg;
    std::deque<EntityId> live;
    for (int i = 0; i < 400000; ++i) {
        const EntityId e = reg.create();
        ASSERT_NE(e, NULL_ENTITY);
        reg.emplace<Item>(e, Item{i});
        live.push_back(e);
        if (live.size() > 8) {
            ASSERT_TRUE(reg.destroy(live.front()));
            live.pop_front();
        }
    }
    // Peak population plus the aging reserve: no slot is ever leaked.
    EXPECT_LE(reg.slotCount(), 9U + Registry::DEFAULT_RECYCLE_THRESHOLD + 1U);
    EXPECT_LE(reg.pool<Item>().allocatedPages(), 1U);
}

TEST(EcsRegistryRecycling, FifoRecyclingSpreadsGenerationsAcrossFreeSlots) {
    Registry reg(4096, 0);
    std::vector<EntityId> batch;
    for (int i = 0; i < 2000; ++i) {
        batch.push_back(reg.create());
    }
    for (const EntityId e : batch) {
        (void)reg.destroy(e);
    }
    uint32_t maxGeneration = 0;
    for (int i = 0; i < 8000; ++i) {
        const EntityId e = reg.create();
        maxGeneration = std::max(maxGeneration, entityGeneration(e));
        (void)reg.destroy(e);
    }
    // 8000 churns over 2000 free slots: about 4 recycles each, not 8000 on one slot.
    EXPECT_LE(maxGeneration, 6U);
}

TEST(EcsRegistryRecycling, AgingReserveDefersReuseOfFreshlyFreedSlots) {
    Registry reg; // default threshold
    const EntityId stale = reg.create();
    (void)reg.destroy(stale);
    for (uint32_t i = 0; i < Registry::DEFAULT_RECYCLE_THRESHOLD; ++i) {
        const EntityId e = reg.create();
        ASSERT_NE(entityIndex(e), entityIndex(stale)) << i;
        (void)reg.destroy(e);
    }
    EXPECT_FALSE(reg.valid(stale));
}

TEST(EcsRegistryRecycling, StaleHandlesStayInvalidAcrossLongChurn) {
    Registry reg;
    std::vector<EntityId> stale;
    for (int i = 0; i < 64; ++i) {
        const EntityId e = reg.create();
        stale.push_back(e);
        (void)reg.destroy(e);
    }
    std::deque<EntityId> live;
    // Well below 256 * threshold recycles per slot: aliasing is impossible in this window.
    for (int i = 0; i < 100000; ++i) {
        const EntityId e = reg.create();
        ASSERT_EQ(std::find(stale.begin(), stale.end(), e), stale.end()) << "stale handle reissued at " << i;
        live.push_back(e);
        if (live.size() > 32) {
            (void)reg.destroy(live.front());
            live.pop_front();
        }
    }
    for (const EntityId s : stale) {
        EXPECT_FALSE(reg.valid(s));
    }
}

TEST(EcsRegistryRecycling, MovedFromRegistryStillNeverIssuesTheZeroHandle) {
    Registry a(8, 0);
    (void)a.create();
    Registry b(std::move(a));
    // A moved-from registry is valid but unspecified; it must not hand out handle 0.
    const EntityId e = a.create(); // NOLINT(bugprone-use-after-move)
    if (e != NULL_ENTITY) {
        EXPECT_NE(entityIndex(e), 0U);
    }
    EXPECT_EQ(b.alive(), 1U);
}
