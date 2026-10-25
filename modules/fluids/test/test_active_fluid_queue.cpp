// ActiveFluidQueue: deduplicated scheduled-tick queue with deterministic ordering.
#include "fluids/ActiveFluidQueue.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <random>
#include <utility>
#include <vector>

using betacraft::core::BlockPos;
using betacraft::fluids::ActiveFluidQueue;
using betacraft::fluids::FluidType;

namespace {

std::vector<BlockPos> drain(ActiveFluidQueue& q, uint64_t now) {
    std::vector<BlockPos> out;
    BlockPos p{};
    while (q.popDue(now, p)) {
        out.push_back(p);
    }
    return out;
}

} // namespace

TEST(ActiveFluidQueue, StartsEmpty) {
    ActiveFluidQueue q;
    EXPECT_TRUE(q.empty());
    EXPECT_EQ(q.size(), 0U);
    EXPECT_FALSE(q.nextDueTick().has_value());
    BlockPos p{};
    EXPECT_FALSE(q.popDue(1000, p));
}

TEST(ActiveFluidQueue, ScheduledEntryNotDueBeforeItsTick) {
    ActiveFluidQueue q;
    EXPECT_TRUE(q.schedule({1, 2, 3}, 5));
    EXPECT_EQ(q.size(), 1U);
    EXPECT_TRUE(q.isPending({1, 2, 3}));
    BlockPos p{};
    EXPECT_FALSE(q.popDue(4, p));
    ASSERT_TRUE(q.popDue(5, p));
    EXPECT_EQ(p, (BlockPos{1, 2, 3}));
    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.isPending({1, 2, 3}));
}

TEST(ActiveFluidQueue, DuplicateScheduleIsIgnored) {
    ActiveFluidQueue q;
    EXPECT_TRUE(q.schedule({0, 0, 0}, 10));
    EXPECT_FALSE(q.schedule({0, 0, 0}, 12));
    EXPECT_EQ(q.size(), 1U);
    EXPECT_EQ(drain(q, 100).size(), 1U);
}

TEST(ActiveFluidQueue, EarlierRescheduleWinsAndStaleEntryIsSkipped) {
    ActiveFluidQueue q;
    EXPECT_TRUE(q.schedule({0, 0, 0}, 30));
    EXPECT_TRUE(q.schedule({0, 0, 0}, 5)); // pulls the update earlier
    EXPECT_EQ(q.size(), 1U);
    EXPECT_EQ(q.nextDueTick(), 5U);
    BlockPos p{};
    ASSERT_TRUE(q.popDue(5, p));
    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.popDue(30, p)); // the stale tick-30 heap entry must not resurface
}

TEST(ActiveFluidQueue, OrdersByDueTickThenInsertionOrder) {
    ActiveFluidQueue q;
    q.schedule({3, 0, 0}, 7);
    q.schedule({1, 0, 0}, 5);
    q.schedule({2, 0, 0}, 5);
    q.schedule({0, 0, 0}, 6);
    const auto order = drain(q, 100);
    ASSERT_EQ(order.size(), 4U);
    EXPECT_EQ(order[0], (BlockPos{1, 0, 0}));
    EXPECT_EQ(order[1], (BlockPos{2, 0, 0}));
    EXPECT_EQ(order[2], (BlockPos{0, 0, 0}));
    EXPECT_EQ(order[3], (BlockPos{3, 0, 0}));
}

TEST(ActiveFluidQueue, EntryIsKeyedByFluidTypeLikeBetaNextTickListEntry) {
    // Beta keys scheduled ticks by (x, y, z, blockID): lava replacing a pending water cell
    // must not inherit the water's earlier due tick, even though it asks for a later one.
    ActiveFluidQueue q;
    EXPECT_TRUE(q.schedule({4, 1, 4}, 5, FluidType::Water));
    EXPECT_TRUE(q.schedule({4, 1, 4}, 31, FluidType::Lava));
    EXPECT_EQ(q.size(), 1U);
    EXPECT_EQ(q.nextDueTick(), 31U);
    BlockPos p{};
    FluidType t = FluidType::None;
    EXPECT_FALSE(q.popDue(30, p, t));
    ASSERT_TRUE(q.popDue(31, p, t));
    EXPECT_EQ(t, FluidType::Lava);
    EXPECT_TRUE(q.empty());
}

TEST(ActiveFluidQueue, SameTypeLaterRequestStillIgnored) {
    ActiveFluidQueue q;
    EXPECT_TRUE(q.schedule({0, 0, 0}, 5, FluidType::Lava));
    EXPECT_FALSE(q.schedule({0, 0, 0}, 30, FluidType::Lava));
    BlockPos p{};
    FluidType t = FluidType::None;
    ASSERT_TRUE(q.popDue(5, p, t));
    EXPECT_EQ(t, FluidType::Lava);
}

TEST(ActiveFluidQueue, CanRescheduleAfterPop) {
    ActiveFluidQueue q;
    q.schedule({0, 0, 0}, 1);
    BlockPos p{};
    ASSERT_TRUE(q.popDue(1, p));
    EXPECT_TRUE(q.schedule({0, 0, 0}, 2));
    ASSERT_TRUE(q.popDue(2, p));
}

TEST(ActiveFluidQueue, ClearRemovesEverything) {
    ActiveFluidQueue q;
    for (int i = 0; i < 10; ++i) {
        q.schedule({i, 0, 0}, static_cast<uint64_t>(i));
    }
    q.clear();
    EXPECT_TRUE(q.empty());
    EXPECT_FALSE(q.isPending({3, 0, 0}));
    BlockPos p{};
    EXPECT_FALSE(q.popDue(1000, p));
    EXPECT_TRUE(q.schedule({3, 0, 0}, 1));
}

TEST(ActiveFluidQueue, NegativeAndExtremeCoordinatesAreDistinct) {
    ActiveFluidQueue q;
    EXPECT_TRUE(q.schedule({-1, 0, 0}, 1));
    EXPECT_TRUE(q.schedule({1, 0, 0}, 1));
    EXPECT_TRUE(q.schedule({INT32_MIN, -64, INT32_MAX}, 1));
    EXPECT_TRUE(q.schedule({INT32_MAX, 255, INT32_MIN}, 1));
    EXPECT_EQ(q.size(), 4U);
}

TEST(ActiveFluidQueue, RandomizedMatchesReferenceModel) {
    // Property: interleaved schedule/pop calls produce exactly what a naive reference
    // model produces: every pop happens on its effective due tick, in (due, sequence)
    // order, with the scheduled fluid type, and each live entry pops exactly once.
    struct ModelEntry {
        uint64_t due;
        uint64_t seq;
        FluidType type;
    };
    std::mt19937 rng(1234U);
    ActiveFluidQueue q;
    std::map<std::pair<int32_t, int32_t>, ModelEntry> model;
    uint64_t seq = 0;
    uint64_t now = 0;
    size_t pops = 0;
    for (int step = 0; step < 4000; ++step) {
        if (rng() % 4U != 0U) {
            const BlockPos p{static_cast<int32_t>(rng() % 12U), 0, static_cast<int32_t>(rng() % 12U)};
            const uint64_t due = now + rng() % 40U;
            const FluidType type = rng() % 3U == 0U ? FluidType::Lava : FluidType::Water;
            const auto key = std::make_pair(p.x, p.z);
            const auto it = model.find(key);
            const bool expectAccepted = it == model.end() || it->second.type != type || due < it->second.due;
            if (expectAccepted) {
                model[key] = {due, seq++, type};
            }
            ASSERT_EQ(q.schedule(p, due, type), expectAccepted) << "step " << step;
            ASSERT_EQ(q.size(), model.size());
            continue;
        }
        now += rng() % 6U;
        // Reference pops: every model entry due by `now`, ordered by (due, seq).
        std::vector<std::pair<std::pair<int32_t, int32_t>, ModelEntry>> due;
        for (const auto& kv : model) {
            if (kv.second.due <= now) {
                due.emplace_back(kv);
            }
        }
        std::sort(due.begin(), due.end(), [](const auto& a, const auto& b) {
            return a.second.due != b.second.due ? a.second.due < b.second.due : a.second.seq < b.second.seq;
        });
        ASSERT_EQ(q.nextDueTick().has_value(), !model.empty());
        for (const auto& [key, entry] : due) {
            BlockPos p{};
            FluidType t = FluidType::None;
            ASSERT_TRUE(q.popDue(now, p, t)) << "missing pop at step " << step;
            EXPECT_EQ(p, (BlockPos{key.first, 0, key.second})) << "wrong order at step " << step;
            EXPECT_EQ(t, entry.type);
            model.erase(key);
            ++pops;
        }
        BlockPos extra{};
        ASSERT_FALSE(q.popDue(now, extra)) << "popped an entry that is not due yet, step " << step;
        ASSERT_EQ(q.size(), model.size());
    }
    EXPECT_GT(pops, 500U) << "scenario must actually exercise pops";
}
