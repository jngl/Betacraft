// Simulator scheduling: water updates every 5 ticks, lava every 30.
#include "FluidTestUtils.hpp"

#include <gtest/gtest.h>

using namespace fluidtest;

TEST(FluidTickRates, DefaultConfigMatchesBeta) {
    const FluidConfig cfg{};
    EXPECT_EQ(cfg.waterTickRate, 5U);
    EXPECT_EQ(cfg.lavaTickRate, 30U);
    EXPECT_EQ(cfg.waterDecayStep, 1);
    EXPECT_EQ(cfg.lavaDecayStep, 2);
    EXPECT_EQ(cfg.maxFlowSearchDepth, 4);
    EXPECT_GT(cfg.maxUpdatesPerTick, 0U);
}

TEST(FluidTickRates, TickRateForEachFluid) {
    auto g = makeFlatWorld(3, 3, 3);
    FluidSimulator sim(*g);
    EXPECT_EQ(sim.tickRateFor(FluidType::Water), 5U);
    EXPECT_EQ(sim.tickRateFor(FluidType::Lava), 30U);
}

TEST(FluidTickRates, PlacingSourceWritesItAndSchedulesOneUpdate) {
    auto g = makeFlatWorld(5, 3, 5);
    FluidSimulator sim(*g);
    EXPECT_EQ(sim.currentTick(), 0U);
    sim.placeSource({2, 1, 2}, FluidType::Water);
    EXPECT_TRUE(g->getFluid({2, 1, 2}).isSource());
    EXPECT_EQ(sim.pendingUpdates(), 1U);
}

TEST(FluidTickRates, WaterSpreadsExactlyOnTickFive) {
    auto g = makeFlatWorld(5, 3, 5);
    FluidSimulator sim(*g);
    sim.placeSource({2, 1, 2}, FluidType::Water);
    for (int i = 0; i < 4; ++i) {
        sim.tick();
        EXPECT_EQ(g->countFluid(), 1U) << "tick " << sim.currentTick();
    }
    sim.tick();
    EXPECT_EQ(sim.currentTick(), 5U);
    EXPECT_EQ(g->countFluid(), 5U);
    for (const BlockPos& n : {BlockPos{1, 1, 2}, BlockPos{3, 1, 2}, BlockPos{2, 1, 1}, BlockPos{2, 1, 3}}) {
        EXPECT_EQ(g->getFluid(n), FluidState::flowing(FluidType::Water, 6));
    }
}

TEST(FluidTickRates, LavaSpreadsExactlyOnTickThirtyWithDoubleDecay) {
    auto g = makeFlatWorld(5, 3, 5);
    FluidSimulator sim(*g);
    sim.placeSource({2, 1, 2}, FluidType::Lava);
    for (int i = 0; i < 29; ++i) {
        sim.tick();
    }
    EXPECT_EQ(g->countFluid(), 1U);
    sim.tick();
    EXPECT_EQ(g->countFluid(FluidType::Lava), 5U);
    EXPECT_EQ(g->getFluid({3, 1, 2}), FluidState::flowing(FluidType::Lava, 5));
}

TEST(FluidTickRates, SecondRingAppearsOneTickRateLater) {
    auto g = makeFlatWorld(7, 3, 7);
    FluidSimulator sim(*g);
    sim.placeSource({3, 1, 3}, FluidType::Water);
    for (int i = 0; i < 9; ++i) {
        sim.tick();
    }
    EXPECT_EQ(g->countFluid(), 5U);
    sim.tick(); // tick 10
    EXPECT_EQ(g->getFluid({5, 1, 3}), FluidState::flowing(FluidType::Water, 5));
    EXPECT_EQ(g->getFluid({4, 1, 4}), FluidState::flowing(FluidType::Water, 5));
}

TEST(FluidTickRates, IdleWorldHasNoPendingWork) {
    auto g = makeFlatWorld(4, 3, 4);
    FluidSimulator sim(*g);
    const auto stats = sim.tick();
    EXPECT_EQ(stats.processed, 0U);
    EXPECT_EQ(sim.pendingUpdates(), 0U);
    EXPECT_EQ(sim.currentTick(), 1U);
}

TEST(FluidTickRates, RunUntilIdleStopsWhenQueueDrains) {
    auto g = makeFlatWorld(3, 3, 3);
    FluidSimulator sim(*g);
    sim.placeSource({1, 1, 1}, FluidType::Water);
    const uint64_t ran = sim.runUntilIdle(10000);
    EXPECT_LT(ran, 10000U);
    EXPECT_EQ(sim.pendingUpdates(), 0U);
    EXPECT_EQ(sim.currentTick(), ran);
}

TEST(FluidTickRates, PlaceSourceOnUnloadedCellIsNoOp) {
    auto g = makeFlatWorld(3, 3, 3);
    FluidSimulator sim(*g);
    sim.placeSource({-1, 1, 1}, FluidType::Water);
    sim.placeSource({1, 1, 1}, FluidType::None);
    EXPECT_EQ(sim.pendingUpdates(), 0U);
    EXPECT_EQ(g->countFluid(), 0U);
}

TEST(FluidTickRates, LavaReplacingPendingWaterWaitsTheFullLavaRate) {
    // A water cell is pending for tick 5; lava replaces it on tick 1. The lava must keep
    // its own 30-tick rate (first spread on tick 31), not run on the water's tick 5.
    auto g = makeFlatWorld(5, 3, 5);
    FluidSimulator sim(*g);
    sim.placeSource({2, 1, 2}, FluidType::Water);
    sim.tick();
    g->setFluid({2, 1, 2}, FluidState::source(FluidType::Lava));
    sim.notifyBlockChanged({2, 1, 2});
    for (int i = 0; i < 29; ++i) {
        sim.tick();
        ASSERT_EQ(g->countFluid(), 1U) << "lava spread early on tick " << sim.currentTick();
    }
    sim.tick(); // tick 31
    EXPECT_EQ(g->countFluid(FluidType::Lava), 5U);
}

TEST(FluidTickRates, StaleEntryForAFluidThatChangedTypeIsSkipped) {
    // The queued entry was for water; the cell now holds lava that nobody re-scheduled
    // (external edit without notification). Beta skips an entry whose block id no longer
    // matches, so the lava must not update on the water's tick.
    auto g = makeFlatWorld(5, 3, 5);
    FluidSimulator sim(*g);
    sim.placeSource({2, 1, 2}, FluidType::Water);
    g->setFluid({2, 1, 2}, FluidState::source(FluidType::Lava));
    for (int i = 0; i < 40; ++i) {
        sim.tick();
    }
    EXPECT_EQ(g->countFluid(), 1U);
    EXPECT_EQ(sim.pendingUpdates(), 0U);
}
