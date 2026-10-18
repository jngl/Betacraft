// P2.1 — Paged sparse set (dense array + sparse index, O(1) insert/remove via swap-and-pop).
#include "ecs/SparseSet.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <set>
#include <vector>

using namespace betacraft::ecs;

namespace {

// Every dense slot must point back to itself through the sparse index.
void expectConsistent(const SparseSet& set) {
    const auto ents = set.entities();
    ASSERT_EQ(ents.size(), set.size());
    for (size_t i = 0; i < ents.size(); ++i) {
        ASSERT_TRUE(set.contains(ents[i]));
        ASSERT_EQ(set.index(ents[i]), i);
    }
}

} // namespace

TEST(EcsSparseSet, StartsEmpty) {
    SparseSet set;
    EXPECT_TRUE(set.empty());
    EXPECT_EQ(set.size(), 0U);
    EXPECT_FALSE(set.contains(makeEntity(0, 0)));
    EXPECT_EQ(set.index(makeEntity(0, 0)), SparseSet::NPOS);
    EXPECT_EQ(set.allocatedPages(), 0U);
}

TEST(EcsSparseSet, InsertMakesEntityContainedAtDenseEnd) {
    SparseSet set;
    const EntityId a = makeEntity(10, 0);
    const EntityId b = makeEntity(3, 2);
    EXPECT_TRUE(set.insert(a));
    EXPECT_TRUE(set.insert(b));
    EXPECT_EQ(set.size(), 2U);
    EXPECT_FALSE(set.empty());
    EXPECT_TRUE(set.contains(a));
    EXPECT_TRUE(set.contains(b));
    EXPECT_EQ(set.index(a), 0U);
    EXPECT_EQ(set.index(b), 1U);
    ASSERT_EQ(set.entities().size(), 2U);
    EXPECT_EQ(set.entities()[0], a);
    EXPECT_EQ(set.entities()[1], b);
}

TEST(EcsSparseSet, DuplicateInsertIsRejected) {
    SparseSet set;
    const EntityId a = makeEntity(5, 1);
    EXPECT_TRUE(set.insert(a));
    EXPECT_FALSE(set.insert(a));
    EXPECT_EQ(set.size(), 1U);
}

TEST(EcsSparseSet, InsertingOtherGenerationOfOccupiedSlotIsRejected) {
    SparseSet set;
    EXPECT_TRUE(set.insert(makeEntity(5, 1)));
    EXPECT_FALSE(set.insert(makeEntity(5, 2)));
    EXPECT_EQ(set.size(), 1U);
    EXPECT_TRUE(set.contains(makeEntity(5, 1)));
}

TEST(EcsSparseSet, NullEntityCanNeverBeInserted) {
    SparseSet set;
    EXPECT_FALSE(set.insert(NULL_ENTITY));
    EXPECT_FALSE(set.contains(NULL_ENTITY));
    EXPECT_TRUE(set.empty());
}

TEST(EcsSparseSet, ContainsRejectsStaleGeneration) {
    SparseSet set;
    set.insert(makeEntity(7, 3));
    EXPECT_TRUE(set.contains(makeEntity(7, 3)));
    EXPECT_FALSE(set.contains(makeEntity(7, 2)));
    EXPECT_FALSE(set.contains(makeEntity(7, 4)));
    EXPECT_EQ(set.index(makeEntity(7, 4)), SparseSet::NPOS);
}

TEST(EcsSparseSet, RemoveUsesSwapAndPop) {
    SparseSet set;
    const EntityId a = makeEntity(0, 0);
    const EntityId b = makeEntity(1, 0);
    const EntityId c = makeEntity(2, 0);
    set.insert(a);
    set.insert(b);
    set.insert(c);
    EXPECT_TRUE(set.remove(a));
    EXPECT_EQ(set.size(), 2U);
    EXPECT_FALSE(set.contains(a));
    // The last element was moved into the hole.
    EXPECT_EQ(set.entities()[0], c);
    EXPECT_EQ(set.index(c), 0U);
    EXPECT_EQ(set.index(b), 1U);
    expectConsistent(set);
}

TEST(EcsSparseSet, RemoveLastElementAndAbsentElement) {
    SparseSet set;
    const EntityId a = makeEntity(0, 0);
    set.insert(a);
    EXPECT_FALSE(set.remove(makeEntity(1, 0)));
    EXPECT_FALSE(set.remove(makeEntity(0, 1))); // stale generation must not remove
    EXPECT_EQ(set.size(), 1U);
    EXPECT_TRUE(set.remove(a));
    EXPECT_TRUE(set.empty());
    EXPECT_FALSE(set.remove(a));
    EXPECT_FALSE(set.remove(NULL_ENTITY));
}

TEST(EcsSparseSet, ReinsertAfterRemoveWithNewGeneration) {
    SparseSet set;
    set.insert(makeEntity(9, 0));
    set.remove(makeEntity(9, 0));
    EXPECT_TRUE(set.insert(makeEntity(9, 1)));
    EXPECT_TRUE(set.contains(makeEntity(9, 1)));
    EXPECT_FALSE(set.contains(makeEntity(9, 0)));
}

TEST(EcsSparseSet, PagesAreAllocatedOnDemand) {
    SparseSet set;
    set.insert(makeEntity(0, 0));
    EXPECT_EQ(set.allocatedPages(), 1U);
    set.insert(makeEntity(SparseSet::PAGE_SIZE - 1, 0));
    EXPECT_EQ(set.allocatedPages(), 1U);
    set.insert(makeEntity(SparseSet::PAGE_SIZE, 0));
    EXPECT_EQ(set.allocatedPages(), 2U);
    // A far-away index must only allocate its own page, not the gap in between.
    set.insert(makeEntity(100 * SparseSet::PAGE_SIZE + 17, 0));
    EXPECT_EQ(set.allocatedPages(), 3U);
    EXPECT_TRUE(set.contains(makeEntity(100 * SparseSet::PAGE_SIZE + 17, 0)));
    EXPECT_FALSE(set.contains(makeEntity(50 * SparseSet::PAGE_SIZE, 0)));
    expectConsistent(set);
}

TEST(EcsSparseSet, MaxIndexIsSupported) {
    SparseSet set;
    const EntityId e = makeEntity(MAX_ENTITY_INDEX, 0xFF);
    EXPECT_TRUE(set.insert(e));
    EXPECT_TRUE(set.contains(e));
    EXPECT_EQ(set.allocatedPages(), 1U);
    EXPECT_TRUE(set.remove(e));
}

TEST(EcsSparseSet, LookupsBeyondAllocatedPagesAreSafe) {
    SparseSet set;
    set.insert(makeEntity(1, 0));
    EXPECT_FALSE(set.contains(makeEntity(MAX_ENTITY_INDEX, 0)));
    EXPECT_FALSE(set.remove(makeEntity(MAX_ENTITY_INDEX, 0)));
    EXPECT_EQ(set.index(makeEntity(MAX_ENTITY_INDEX, 0)), SparseSet::NPOS);
}

// swapPositions is reserved to owning groups; a test-only subclass re-exposes it.
struct SwappableSet : SparseSet {
    using SparseSet::swapPositions;
};

TEST(EcsSparseSet, SwapPositionsKeepsSparseIndexInSync) {
    SwappableSet set;
    const EntityId a = makeEntity(4, 0);
    const EntityId b = makeEntity(8, 0);
    const EntityId c = makeEntity(15, 0);
    set.insert(a);
    set.insert(b);
    set.insert(c);
    set.swapPositions(0, 2);
    EXPECT_EQ(set.entities()[0], c);
    EXPECT_EQ(set.entities()[2], a);
    EXPECT_EQ(set.index(a), 2U);
    EXPECT_EQ(set.index(c), 0U);
    set.swapPositions(1, 1); // self-swap is a no-op
    EXPECT_EQ(set.index(b), 1U);
    expectConsistent(set);
}

TEST(EcsSparseSet, ClearEmptiesButKeepsSetUsable) {
    SparseSet set;
    for (uint32_t i = 0; i < 100; ++i) {
        set.insert(makeEntity(i * 7, 0));
    }
    set.clear();
    EXPECT_TRUE(set.empty());
    for (uint32_t i = 0; i < 100; ++i) {
        EXPECT_FALSE(set.contains(makeEntity(i * 7, 0)));
    }
    EXPECT_TRUE(set.insert(makeEntity(7, 0)));
    expectConsistent(set);
}

TEST(EcsSparseSet, MoveTransfersContents) {
    SparseSet a;
    a.insert(makeEntity(1, 0));
    a.insert(makeEntity(2, 0));
    SparseSet b(std::move(a));
    EXPECT_EQ(b.size(), 2U);
    EXPECT_TRUE(b.contains(makeEntity(2, 0)));
}

TEST(EcsSparseSet, RandomizedAgainstStdSet) {
    std::mt19937 rng(0xEC5U);
    std::uniform_int_distribution<uint32_t> idxDist(0, 20000);
    std::uniform_int_distribution<int> opDist(0, 2);
    SparseSet set;
    std::set<EntityId> model;
    for (int step = 0; step < 20000; ++step) {
        const EntityId e = makeEntity(idxDist(rng), 0);
        if (opDist(rng) == 0) {
            ASSERT_EQ(set.remove(e), model.erase(e) == 1U);
        } else {
            ASSERT_EQ(set.insert(e), model.insert(e).second);
        }
        ASSERT_EQ(set.size(), model.size());
    }
    expectConsistent(set);
    std::vector<EntityId> dense(set.entities().begin(), set.entities().end());
    std::sort(dense.begin(), dense.end());
    EXPECT_TRUE(std::equal(dense.begin(), dense.end(), model.begin(), model.end()));
}
