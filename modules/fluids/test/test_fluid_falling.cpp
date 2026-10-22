// P2.3 - Vertical (gravity) flow has priority; falling fluid resets its level on landing.
#include "FluidTestUtils.hpp"

#include <gtest/gtest.h>

using namespace fluidtest;

TEST(FluidFalling, CollaredSourceFallsStraightDown) {
    // Stone collar around the source: it can only go down. Falling cells in the column
    // do not spread sideways (they rest on fluid, not on a solid block).
    auto g = makeFlatWorld(15, 8, 15);
    const BlockPos src{7, 6, 7};
    for (const BlockPos& d : {BlockPos{1, 0, 0}, BlockPos{-1, 0, 0}, BlockPos{0, 0, 1}, BlockPos{0, 0, -1}}) {
        g->setBlock(src.offset(d.x, d.y, d.z), BlockId::Stone);
    }
    FluidSimulator sim(*g);
    sim.placeSource(src, FluidType::Water);
    sim.runUntilIdle(2000);
    ASSERT_EQ(sim.pendingUpdates(), 0U);
    EXPECT_TRUE(g->getFluid(src).isSource());
    for (int y = 1; y <= 5; ++y) {
        EXPECT_TRUE(g->getFluid({7, y, 7}).isFalling()) << "y=" << y;
        EXPECT_EQ(g->getFluid({7, y, 7}).level, 7U) << "falling fluid resets to full level";
        if (y >= 2) {
            EXPECT_TRUE(g->getFluid({8, y, 7}).isEmpty()) << "column must not spread at y=" << y;
        }
    }
}

TEST(FluidFalling, LandingSpreadsExactlyLikeAFreshSource) {
    auto g = makeFlatWorld(15, 8, 15);
    const BlockPos src{7, 6, 7};
    for (const BlockPos& d : {BlockPos{1, 0, 0}, BlockPos{-1, 0, 0}, BlockPos{0, 0, 1}, BlockPos{0, 0, -1}}) {
        g->setBlock(src.offset(d.x, d.y, d.z), BlockId::Stone);
    }
    FluidSimulator sim(*g);
    sim.placeSource(src, FluidType::Water);
    sim.runUntilIdle(2000);
    const BlockPos landing{7, 1, 7};
    for (int x = 0; x < 15; ++x) {
        for (int z = 0; z < 15; ++z) {
            const BlockPos p{x, 1, z};
            const int d = manhattan2D(p, landing);
            if (d == 0) {
                continue;
            }
            const FluidState f = g->getFluid(p);
            if (d <= 7) {
                EXPECT_EQ(f, FluidState::flowing(FluidType::Water, static_cast<uint8_t>(7 - d)))
                    << "(" << x << "," << z << ")\n" << dumpLayer(*g, 1);
            } else {
                EXPECT_TRUE(f.isEmpty()) << "(" << x << "," << z << ")";
            }
        }
    }
}

TEST(FluidFalling, FloatingSourceFormsPlusWithFiveWaterfalls) {
    // Beta: once the cell below a source holds fluid, the source spreads sideways
    // ("l == 0" branch). The level-6 arms hang over air and pour down; they do not
    // extend further because they rest on their own falling water.
    auto g = makeFlatWorld(15, 8, 15);
    FluidSimulator sim(*g);
    const BlockPos src{7, 6, 7};
    sim.placeSource(src, FluidType::Water);
    sim.runUntilIdle(4000);
    ASSERT_EQ(sim.pendingUpdates(), 0U);
    EXPECT_TRUE(g->getFluid(src).isSource());
    for (const BlockPos& d : {BlockPos{1, 0, 0}, BlockPos{-1, 0, 0}, BlockPos{0, 0, 1}, BlockPos{0, 0, -1}}) {
        const BlockPos arm = src.offset(d.x, d.y, d.z);
        EXPECT_EQ(g->getFluid(arm), FluidState::flowing(FluidType::Water, 6));
        EXPECT_TRUE(g->getFluid(arm.offset(d.x, 0, d.z)).isEmpty()) << dumpLayer(*g, 6);
        EXPECT_TRUE(g->getFluid(arm.offset(0, -1, 0)).isFalling());
    }
    EXPECT_TRUE(g->getFluid({8, 6, 8}).isEmpty()) << "no diagonal at source height";
}

TEST(FluidFalling, FlowingWaterPoursOverALedge) {
    // Platform at y=3 (x <= 4), a cliff down to the floor beyond it.
    auto g = makeFlatWorld(12, 6, 3);
    g->fill({0, 1, 0}, {4, 3, 2}, BlockId::Stone);
    FluidSimulator sim(*g);
    sim.placeSource({2, 4, 1}, FluidType::Water);
    sim.runUntilIdle(2000);
    EXPECT_EQ(g->getFluid({4, 4, 1}), FluidState::flowing(FluidType::Water, 5));
    EXPECT_EQ(g->getFluid({5, 4, 1}), FluidState::flowing(FluidType::Water, 4));
    // The overhanging cell does not keep spreading at height 4: it falls instead.
    EXPECT_TRUE(g->getFluid({6, 4, 1}).isEmpty()) << dumpLayer(*g, 4);
    EXPECT_TRUE(g->getFluid({5, 3, 1}).isFalling());
    EXPECT_TRUE(g->getFluid({5, 2, 1}).isFalling());
    EXPECT_TRUE(g->getFluid({5, 1, 1}).isFalling());
    // The landing re-spreads with level 6 next to the cliff.
    EXPECT_EQ(g->getFluid({6, 1, 1}), FluidState::flowing(FluidType::Water, 6));
}

TEST(FluidFalling, LavaFallsAndSettlesWithStepTwoAroundLanding) {
    // Beta: landing fluid first spreads with decay 1 ("if (l >= 8) k1 = 1"), but each
    // flowing cell then re-derives its decay from the falling neighbour (counted as a
    // source) + step, so lava settles to 5, 3, 1 around the landing point.
    auto g = makeFlatWorld(11, 6, 11);
    const BlockPos src{5, 4, 5};
    for (const BlockPos& d : {BlockPos{1, 0, 0}, BlockPos{-1, 0, 0}, BlockPos{0, 0, 1}, BlockPos{0, 0, -1}}) {
        g->setBlock(src.offset(d.x, d.y, d.z), BlockId::Stone);
    }
    FluidSimulator sim(*g);
    sim.placeSource(src, FluidType::Lava);
    sim.runUntilIdle(5000);
    for (int y = 1; y <= 3; ++y) {
        EXPECT_EQ(g->getFluid({5, y, 5}), FluidState::falling(FluidType::Lava)) << "y=" << y;
    }
    EXPECT_EQ(g->getFluid({6, 1, 5}), FluidState::flowing(FluidType::Lava, 5));
    EXPECT_EQ(g->getFluid({7, 1, 5}), FluidState::flowing(FluidType::Lava, 3));
    EXPECT_EQ(g->getFluid({8, 1, 5}), FluidState::flowing(FluidType::Lava, 1));
    EXPECT_TRUE(g->getFluid({9, 1, 5}).isEmpty());
    EXPECT_EQ(sim.pendingUpdates(), 0U);
}

TEST(FluidFalling, FallingTakesOneTickRatePerBlock) {
    auto g = makeFlatWorld(3, 8, 3);
    FluidSimulator sim(*g);
    sim.placeSource({1, 6, 1}, FluidType::Water);
    for (int i = 0; i < 5; ++i) {
        sim.tick();
    }
    EXPECT_TRUE(g->getFluid({1, 5, 1}).isFalling());
    EXPECT_TRUE(g->getFluid({1, 4, 1}).isEmpty());
    for (int i = 0; i < 5; ++i) {
        sim.tick();
    }
    EXPECT_TRUE(g->getFluid({1, 4, 1}).isFalling());
}

TEST(FluidFalling, FlowingWaterOnTopOfStillWaterDoesNotSpread) {
    // A lake of sources at y=1 with a stone pedestal under the y=2 source. Flowing water
    // resting on water neither falls (same fluid) nor spreads (water is not solid), so the
    // upper layer only extends one cell around the pedestal source.
    auto g = makeFlatWorld(9, 4, 9);
    for (int x = 0; x < 9; ++x) {
        for (int z = 0; z < 9; ++z) {
            g->setFluid({x, 1, z}, FluidState::source(FluidType::Water));
        }
    }
    g->setBlock({4, 1, 4}, BlockId::Stone);
    FluidSimulator sim(*g);
    sim.placeSource({4, 2, 4}, FluidType::Water);
    sim.runUntilIdle(2000);
    EXPECT_EQ(g->getFluid({5, 2, 4}), FluidState::flowing(FluidType::Water, 6));
    EXPECT_TRUE(g->getFluid({6, 2, 4}).isEmpty()) << dumpLayer(*g, 2);
    EXPECT_TRUE(g->getFluid({5, 2, 5}).isEmpty()) << dumpLayer(*g, 2);
    EXPECT_EQ(g->countFluid(), 81U - 1U + 5U);
}

TEST(FluidFalling, SourceOnTopOfWaterSpreads) {
    auto g = makeFlatWorld(9, 4, 9);
    g->fill({0, 1, 0}, {8, 1, 8}, BlockId::Stone);
    g->setFluid({4, 1, 4}, FluidState::source(FluidType::Water));
    FluidSimulator sim(*g);
    sim.placeSource({4, 2, 4}, FluidType::Water);
    for (int i = 0; i < 5; ++i) {
        sim.tick();
    }
    EXPECT_EQ(g->getFluid({5, 2, 4}), FluidState::flowing(FluidType::Water, 6));
}
