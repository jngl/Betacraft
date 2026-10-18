// P2.3 - Retraction: removing a source makes its flow decay away and converge to dry.
#include "FluidTestUtils.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <vector>

using namespace fluidtest;

TEST(FluidRetraction, RemovingSourceDriesTheWholeDiamond) {
    auto g = makeFlatWorld(17, 3, 17);
    FluidSimulator sim(*g);
    const BlockPos src{8, 1, 8};
    sim.placeSource(src, FluidType::Water);
    sim.runUntilIdle(1000);
    ASSERT_EQ(g->countFluid(), 113U);

    sim.removeFluid(src);
    EXPECT_TRUE(g->getFluid(src).isEmpty());
    const uint64_t ticks = sim.runUntilIdle(5000);
    EXPECT_LT(ticks, 5000U) << "retraction must converge";
    EXPECT_EQ(g->countFluid(), 0U) << dumpLayer(*g, 1);
    EXPECT_EQ(sim.pendingUpdates(), 0U);
}

TEST(FluidRetraction, MaxLevelNeverIncreasesAndNoSourceAppears) {
    // Beta retraction is not cell-wise monotone: neighbours still holding water briefly
    // re-flow into the emptied cells (count-to-infinity) before decaying. What does hold
    // is that the strongest remaining level never grows and no source is ever created.
    auto g = makeFlatWorld(13, 3, 13);
    FluidSimulator sim(*g);
    sim.placeSource({6, 1, 6}, FluidType::Water);
    sim.runUntilIdle(1000);
    sim.removeFluid({6, 1, 6});
    auto maxLevel = [&]() {
        int m = -1;
        for (int x = 0; x < 13; ++x) {
            for (int z = 0; z < 13; ++z) {
                const FluidState f = g->getFluid({x, 1, z});
                EXPECT_FALSE(f.isSource()) << "(" << x << "," << z << ") became a source";
                m = std::max(m, f.isEmpty() ? -1 : int(f.level));
            }
        }
        return m;
    };
    int previous = maxLevel();
    EXPECT_EQ(previous, 6);
    int t = 0;
    for (; t < 2000 && sim.pendingUpdates() > 0; ++t) {
        sim.tick();
        const int now = maxLevel();
        EXPECT_LE(now, previous) << "max level grew at tick " << sim.currentTick();
        previous = now;
    }
    EXPECT_LT(t, 2000);
    EXPECT_EQ(previous, -1);
    EXPECT_EQ(g->countFluid(), 0U);
}

TEST(FluidRetraction, FallingColumnDrainsWhenSourceRemoved) {
    auto g = makeFlatWorld(9, 8, 9);
    const BlockPos src{4, 6, 4};
    for (const BlockPos& d : {BlockPos{1, 0, 0}, BlockPos{-1, 0, 0}, BlockPos{0, 0, 1}, BlockPos{0, 0, -1}}) {
        g->setBlock(src.offset(d.x, d.y, d.z), BlockId::Stone);
    }
    FluidSimulator sim(*g);
    sim.placeSource(src, FluidType::Water);
    sim.runUntilIdle(2000);
    ASSERT_TRUE(g->getFluid({4, 1, 4}).isFalling());
    sim.removeFluid(src);
    sim.runUntilIdle(5000);
    EXPECT_EQ(g->countFluid(), 0U);
}

TEST(FluidRetraction, RemovingOneOfTwoSourcesRelevelsToTheOther) {
    auto g = makeFlatWorld(20, 3, 1);
    FluidSimulator sim(*g);
    sim.placeSource({2, 1, 0}, FluidType::Water);
    sim.placeSource({14, 1, 0}, FluidType::Water);
    sim.runUntilIdle(2000);
    sim.removeFluid({14, 1, 0});
    sim.runUntilIdle(5000);
    for (int x = 0; x < 20; ++x) {
        const int d = std::abs(x - 2);
        const FluidState f = g->getFluid({x, 1, 0});
        if (d == 0) {
            EXPECT_TRUE(f.isSource());
        } else if (d <= 7) {
            EXPECT_EQ(f, FluidState::flowing(FluidType::Water, static_cast<uint8_t>(7 - d))) << "x=" << x;
        } else {
            EXPECT_TRUE(f.isEmpty()) << "x=" << x;
        }
    }
}

TEST(FluidRetraction, LavaRetractsToo) {
    auto g = makeFlatWorld(9, 3, 9);
    FluidSimulator sim(*g);
    sim.placeSource({4, 1, 4}, FluidType::Lava);
    sim.runUntilIdle(5000);
    ASSERT_EQ(g->countFluid(FluidType::Lava), 25U);
    sim.removeFluid({4, 1, 4});
    sim.runUntilIdle(10000);
    EXPECT_EQ(g->countFluid(), 0U);
}

TEST(FluidRetraction, BlockingTheFeedRetractsDownstream) {
    // A wall placed into a channel cuts the stream; the downstream part dries out.
    auto g = makeFlatWorld(12, 3, 1);
    FluidSimulator sim(*g);
    sim.placeSource({0, 1, 0}, FluidType::Water);
    sim.runUntilIdle(1000);
    ASSERT_FALSE(g->getFluid({6, 1, 0}).isEmpty());
    g->setBlock({2, 1, 0}, BlockId::Stone);
    sim.notifyBlockChanged({2, 1, 0});
    sim.runUntilIdle(5000);
    EXPECT_EQ(g->getFluid({1, 1, 0}), FluidState::flowing(FluidType::Water, 6));
    for (int x = 3; x < 12; ++x) {
        EXPECT_TRUE(g->getFluid({x, 1, 0}).isEmpty()) << "x=" << x;
    }
}

TEST(FluidRetraction, RemoveFluidOnDryOrUnloadedCellIsHarmless) {
    auto g = makeFlatWorld(3, 3, 3);
    FluidSimulator sim(*g);
    sim.removeFluid({1, 1, 1});
    sim.removeFluid({-5, 1, 1});
    EXPECT_EQ(sim.pendingUpdates(), 0U);
    EXPECT_EQ(g->getBlock({1, 0, 1}), BlockId::Stone);
    // Removing on a solid cell must not erase the block.
    sim.removeFluid({1, 0, 1});
    EXPECT_EQ(g->getBlock({1, 0, 1}), BlockId::Stone);
}

namespace {

// Lava corridor source -> decay 2 -> 4 -> 6, settled; returns ticks needed to drain it
// after the source is removed. The decay-2 cell is fed by its decay-4 neighbour, so it
// has to *thin* (Beta's slowed step), not dry out at once.
uint64_t ticksToDrainLavaCorridor(const FluidConfig& cfg) {
    auto g = makeFlatWorld(8, 3, 1);
    FluidSimulator sim(*g, cfg);
    sim.placeSource({0, 1, 0}, FluidType::Lava);
    sim.runUntilIdle(5000);
    EXPECT_EQ(g->countFluid(FluidType::Lava), 4U);
    sim.removeFluid({0, 1, 0});
    const uint64_t start = sim.currentTick();
    sim.runUntilIdle(100000);
    EXPECT_EQ(g->countFluid(), 0U);
    return sim.currentTick() - start;
}

} // namespace

TEST(FluidRetraction, LavaThinningIsSlowedLikeBetaByDefault) {
    EXPECT_EQ(FluidConfig{}.lavaThinningOdds, 4U);
    FluidConfig fast{};
    fast.lavaThinningOdds = 1; // every thinning attempt succeeds
    const uint64_t fastTicks = ticksToDrainLavaCorridor(fast);
    // Averaged over seeds, Beta's 1-in-4 thinning makes retraction clearly slower.
    uint64_t slowTotal = 0;
    for (uint64_t seed = 0; seed < 8; ++seed) {
        FluidConfig slow{};
        slow.randomSeed = seed;
        slowTotal += ticksToDrainLavaCorridor(slow);
    }
    EXPECT_GT(slowTotal, 8U * fastTicks + 8U * 30U) << "fast " << fastTicks << " slow total " << slowTotal;
}

TEST(FluidRetraction, LavaThinningSlowdownIsSeededAndDeterministic) {
    FluidConfig a{};
    a.randomSeed = 77;
    EXPECT_EQ(ticksToDrainLavaCorridor(a), ticksToDrainLavaCorridor(a));
    std::vector<uint64_t> perSeed;
    for (uint64_t seed = 0; seed < 8; ++seed) {
        FluidConfig c{};
        c.randomSeed = seed;
        perSeed.push_back(ticksToDrainLavaCorridor(c));
    }
    EXPECT_NE(*std::min_element(perSeed.begin(), perSeed.end()), *std::max_element(perSeed.begin(), perSeed.end()))
        << "the seed must actually drive the slowdown";
}

TEST(FluidRetraction, WaterThinningIsNeverSlowed) {
    // The same self-feeding corridor shape with water drains identically for any odds.
    auto run = [](uint32_t odds) {
        auto g = makeFlatWorld(10, 3, 1);
        FluidConfig cfg{};
        cfg.lavaThinningOdds = odds;
        FluidSimulator sim(*g, cfg);
        sim.placeSource({0, 1, 0}, FluidType::Water);
        sim.runUntilIdle(5000);
        sim.removeFluid({0, 1, 0});
        const uint64_t start = sim.currentTick();
        sim.runUntilIdle(100000);
        EXPECT_EQ(g->countFluid(), 0U);
        return sim.currentTick() - start;
    };
    EXPECT_EQ(run(1), run(4));
}

TEST(FluidRetraction, SkippedLavaThinningKeepsTheCellQueued) {
    // A skipped thinning must not strand the cell at its stale level: it stays pending
    // (stand-in for Beta's random ticks) until it eventually thins and drains.
    FluidConfig cfg{};
    cfg.lavaThinningOdds = 1000000; // practically never thins on the first attempts
    auto g = makeFlatWorld(8, 3, 1);
    FluidSimulator sim(*g, cfg);
    sim.placeSource({0, 1, 0}, FluidType::Lava);
    sim.runUntilIdle(5000);
    sim.removeFluid({0, 1, 0});
    for (int i = 0; i < 300; ++i) {
        sim.tick();
    }
    EXPECT_EQ(g->getFluid({1, 1, 0}), FluidState::flowing(FluidType::Lava, 5)) << "thinning was skipped";
    EXPECT_GT(sim.pendingUpdates(), 0U) << "skipped cell must be re-queued, not dropped";
}
