// P2.3 - Per-tick update budget and active-set (dirty frontier) cost bounds.
#include "FluidTestUtils.hpp"

#include <gtest/gtest.h>

using namespace fluidtest;
using betacraft::fluids::FluidTickStats;

TEST(FluidBudget, ProcessesAtMostBudgetPerTickAndCarriesTheRest) {
    auto g = makeFlatWorld(32, 3, 3);
    FluidConfig cfg{};
    cfg.maxUpdatesPerTick = 3;
    FluidSimulator sim(*g, cfg);
    for (int i = 0; i < 10; ++i) {
        sim.placeSource({i * 3, 1, 1}, FluidType::Water);
    }
    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(sim.tick().processed, 0U);
    }
    FluidTickStats s = sim.tick(); // tick 5: 10 sources due, only 3 run
    EXPECT_EQ(s.processed, 3U);
    EXPECT_TRUE(s.budgetExhausted);
    EXPECT_EQ(s.pending, sim.pendingUpdates());
    s = sim.tick(); // overdue work keeps being served first
    EXPECT_EQ(s.processed, 3U);
    EXPECT_TRUE(s.budgetExhausted);
}

TEST(FluidBudget, NotExhaustedWhenWorkFitsInBudget) {
    auto g = makeFlatWorld(5, 3, 5);
    FluidSimulator sim(*g);
    sim.placeSource({2, 1, 2}, FluidType::Water);
    FluidTickStats s{};
    for (int i = 0; i < 5; ++i) {
        s = sim.tick();
    }
    EXPECT_EQ(s.processed, 1U);
    EXPECT_FALSE(s.budgetExhausted);
}

TEST(FluidBudget, OverdueUpdatesRunInOriginalOrder) {
    // Budget 1: cells due on the same tick still execute in schedule order.
    auto g = makeFlatWorld(9, 3, 1);
    FluidConfig cfg{};
    cfg.maxUpdatesPerTick = 1;
    FluidSimulator sim(*g, cfg);
    sim.placeSource({0, 1, 0}, FluidType::Water);
    sim.placeSource({8, 1, 0}, FluidType::Water);
    for (int i = 0; i < 5; ++i) {
        sim.tick();
    }
    EXPECT_FALSE(g->getFluid({1, 1, 0}).isEmpty()) << "first scheduled source ran first";
    EXPECT_TRUE(g->getFluid({7, 1, 0}).isEmpty());
    sim.tick();
    EXPECT_FALSE(g->getFluid({7, 1, 0}).isEmpty());
}

TEST(FluidBudget, TinyBudgetStillConvergesToSameDiamond) {
    auto reference = makeFlatWorld(17, 3, 17);
    {
        FluidSimulator sim(*reference);
        sim.placeSource({8, 1, 8}, FluidType::Water);
        sim.runUntilIdle(10000);
    }
    for (const uint32_t budget : {1U, 2U, 7U, 64U}) {
        auto g = makeFlatWorld(17, 3, 17);
        FluidConfig cfg{};
        cfg.maxUpdatesPerTick = budget;
        FluidSimulator sim(*g, cfg);
        sim.placeSource({8, 1, 8}, FluidType::Water);
        uint32_t maxSeen = 0;
        for (int t = 0; t < 100000 && sim.pendingUpdates() > 0; ++t) {
            maxSeen = std::max(maxSeen, sim.tick().processed);
        }
        EXPECT_LE(maxSeen, budget);
        EXPECT_EQ(sim.pendingUpdates(), 0U);
        EXPECT_TRUE(g->sameContents(*reference)) << "budget " << budget << "\n" << dumpLayer(*g, 1);
    }
}

TEST(FluidBudget, ZeroBudgetIsClampedToOne) {
    auto g = makeFlatWorld(3, 3, 3);
    FluidConfig cfg{};
    cfg.maxUpdatesPerTick = 0;
    cfg.maxSearchNodesPerTick = 0; // would otherwise stop every tick before any update
    cfg.waterTickRate = 0;
    FluidSimulator sim(*g, cfg);
    EXPECT_EQ(sim.config().maxUpdatesPerTick, 1U);
    EXPECT_EQ(sim.config().maxSearchNodesPerTick, 1U);
    EXPECT_EQ(sim.tickRateFor(FluidType::Water), 1U);
    sim.placeSource({1, 1, 1}, FluidType::Water);
    EXPECT_EQ(sim.tick().processed, 1U);
}

TEST(FluidBudget, WokenSettledLakeCostsOneUpdatePerCellThenGoesIdle) {
    // Waking every cell of a settled lake (e.g. a chunk loaded with wakeInterior) must cost
    // exactly one update per cell, write nothing, run no flow search (every neighbour is a
    // source) and then leave the queue empty: settled fluid never re-schedules itself.
    auto g = makeFlatWorld(32, 3, 32);
    for (int x = 0; x < 32; ++x) {
        for (int z = 0; z < 32; ++z) {
            g->setFluid({x, 1, z}, FluidState::source(FluidType::Water));
        }
    }
    FluidSimulator sim(*g);
    for (int x = 0; x < 32; ++x) {
        for (int z = 0; z < 32; ++z) {
            sim.scheduleUpdate({x, 1, z});
        }
    }
    const uint64_t writesBefore = g->writeCount();
    uint64_t processed = 0;
    uint64_t searchNodes = 0;
    for (int i = 0; i < 100; ++i) {
        const FluidTickStats s = sim.tick();
        processed += s.processed;
        searchNodes += s.searchNodes;
    }
    EXPECT_EQ(processed, 32U * 32U);
    EXPECT_EQ(searchNodes, 0U);
    EXPECT_EQ(g->writeCount(), writesBefore);
    EXPECT_EQ(sim.pendingUpdates(), 0U);
}

TEST(FluidBudget, LocalChangeInLakeOnlyWakesALocalFrontier) {
    auto g = makeFlatWorld(32, 3, 32);
    for (int x = 0; x < 32; ++x) {
        for (int z = 0; z < 32; ++z) {
            g->setFluid({x, 1, z}, FluidState::source(FluidType::Water));
        }
    }
    FluidSimulator sim(*g);
    sim.removeFluid({16, 1, 16}); // scoop one bucket out of the middle
    uint64_t total = 0;
    for (int i = 0; i < 200 && sim.pendingUpdates() > 0; ++i) {
        total += sim.tick().processed;
    }
    EXPECT_EQ(sim.pendingUpdates(), 0U);
    EXPECT_LT(total, 32U) << "only the hole and its neighbours should be updated";
    // Infinite-source rule refills the hole.
    EXPECT_TRUE(g->getFluid({16, 1, 16}).isSource());
}

TEST(FluidBudget, LargeFloodStaysWithinBudgetEveryTick) {
    auto g = makeFlatWorld(48, 3, 48);
    FluidConfig cfg{};
    cfg.maxUpdatesPerTick = 50;
    FluidSimulator sim(*g, cfg);
    for (int i = 0; i < 6; ++i) {
        sim.placeSource({4 + i * 8, 1, 24}, FluidType::Water);
    }
    for (int t = 0; t < 20000 && sim.pendingUpdates() > 0; ++t) {
        ASSERT_LE(sim.tick().processed, 50U);
    }
    EXPECT_EQ(sim.pendingUpdates(), 0U);
}

TEST(FluidBudget, SearchDepthIsClampedSoPerUpdateCostStaysBounded) {
    // Flow search cost grows as 4 * 3^depth: an unvalidated depth of 20 would be ~10^9
    // probes per update, so the config is clamped to [0, kMaxFlowSearchDepth].
    auto g = makeFlatWorld(3, 3, 3);
    FluidConfig deep{};
    deep.maxFlowSearchDepth = 20;
    EXPECT_EQ(FluidSimulator(*g, deep).config().maxFlowSearchDepth, FluidSimulator::kMaxFlowSearchDepth);
    FluidConfig negative{};
    negative.maxFlowSearchDepth = -3;
    EXPECT_EQ(FluidSimulator(*g, negative).config().maxFlowSearchDepth, 0);
    EXPECT_LE(FluidSimulator::kMaxFlowSearchDepth, 8);
}

TEST(FluidBudget, StatsReportFlowSearchWork) {
    // A source on an open floor with no drop in reach explores the full Beta search tree:
    // 4 directions * (1 + 3 + 9 + 27) = 160 calculateFlowCost() nodes at depth 4, minus
    // 2 per direction whose path would step back onto the source itself (a same-fluid
    // source blocks the path, as in Beta) -> 152.
    auto g = makeFlatWorld(21, 3, 21);
    FluidSimulator sim(*g);
    sim.placeSource({10, 1, 10}, FluidType::Water);
    FluidTickStats s{};
    for (int i = 0; i < 5; ++i) {
        s = sim.tick();
    }
    EXPECT_EQ(s.processed, 1U);
    EXPECT_EQ(s.searchNodes, 152U);
    // Next to a hole the search stops early and costs far less.
    auto h = makeFlatWorld(21, 3, 21);
    h->setBlock({11, 0, 10}, BlockId::Air);
    FluidSimulator simHole(*h);
    simHole.placeSource({10, 1, 10}, FluidType::Water);
    for (int i = 0; i < 5; ++i) {
        s = simHole.tick();
    }
    EXPECT_LT(s.searchNodes, 152U);
}

TEST(FluidBudget, SearchWorkBudgetStopsTheTickEarly) {
    // Ten open-floor sources due on the same tick, 152 search nodes each. A 200-node work
    // budget lets the second update start (152 < 200) but not a third (304 >= 200).
    auto g = makeFlatWorld(70, 3, 13); // wide enough that no search tree hits the edge
    FluidConfig cfg{};
    cfg.maxSearchNodesPerTick = 200;
    FluidSimulator sim(*g, cfg);
    for (int i = 0; i < 10; ++i) {
        sim.placeSource({6 + i * 6, 1, 6}, FluidType::Water);
    }
    FluidTickStats s{};
    for (int i = 0; i < 5; ++i) {
        s = sim.tick();
    }
    EXPECT_EQ(s.processed, 2U);
    EXPECT_TRUE(s.budgetExhausted);
    EXPECT_LE(s.searchNodes, 200U + 160U) << "overshoot is at most one update's search";
    // Progress is guaranteed even with a work budget below one update's cost.
    FluidConfig tiny{};
    tiny.maxSearchNodesPerTick = 1;
    auto g2 = makeFlatWorld(17, 3, 17);
    FluidSimulator sim2(*g2, tiny);
    sim2.placeSource({8, 1, 8}, FluidType::Water);
    sim2.runUntilIdle(100000);
    EXPECT_EQ(sim2.pendingUpdates(), 0U);
    EXPECT_EQ(g2->countFluid(), 1U + 4U * (1U + 2U + 3U + 4U + 5U + 6U + 7U));
}
