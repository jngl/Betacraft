// Horizontal spreading on a flat floor: diamond shapes with decreasing levels.
#include "FluidTestUtils.hpp"

#include <gtest/gtest.h>

using namespace fluidtest;
using betacraft::fluids::FluidEvent;
using betacraft::fluids::FluidEventKind;

namespace {

// Expected level at Manhattan distance d from a source with decay step `step`.
int expectedLevel(int d, int step) {
    const int decay = d * step;
    return decay >= 8 ? -1 : 7 - decay;
}

void expectDiamond(const GridFluidVoxelAccess& g, const BlockPos& src, FluidType type, int step) {
    for (int32_t x = 0; x < g.sizeX(); ++x) {
        for (int32_t z = 0; z < g.sizeZ(); ++z) {
            const BlockPos p{x, src.y, z};
            const int lvl = expectedLevel(manhattan2D(p, src), step);
            const FluidState f = g.getFluid(p);
            if (lvl < 0) {
                EXPECT_TRUE(f.isEmpty()) << "(" << x << "," << z << ") should be dry";
            } else {
                EXPECT_EQ(f.type, type) << "(" << x << "," << z << ")";
                EXPECT_EQ(int(f.level), lvl) << "(" << x << "," << z << ")";
                EXPECT_FALSE(f.isFalling());
            }
        }
    }
}

} // namespace

TEST(FluidSpread, WaterFormsSevenBlockDiamondOnFlatFloor) {
    auto g = makeFlatWorld(21, 4, 21);
    FluidSimulator sim(*g);
    const BlockPos src{10, 1, 10};
    sim.placeSource(src, FluidType::Water);
    sim.runUntilIdle(1000);
    ASSERT_EQ(sim.pendingUpdates(), 0U) << dumpLayer(*g, 1);
    expectDiamond(*g, src, FluidType::Water, 1);
    // 1 source + 4*(1+..+7) flowing cells.
    EXPECT_EQ(g->countFluid(FluidType::Water), 1U + 4U * 28U) << dumpLayer(*g, 1);
    EXPECT_TRUE(g->getFluid({10, 1, 3}).type == FluidType::Water); // distance 7 reached
    EXPECT_TRUE(g->getFluid({10, 1, 2}).isEmpty());                // distance 8 dry
}

TEST(FluidSpread, LavaSpreadsOnlyThreeBlocksInOverworld) {
    auto g = makeFlatWorld(11, 4, 11);
    FluidSimulator sim(*g);
    const BlockPos src{5, 1, 5};
    sim.placeSource(src, FluidType::Lava);
    sim.runUntilIdle(2000);
    ASSERT_EQ(sim.pendingUpdates(), 0U);
    expectDiamond(*g, src, FluidType::Lava, 2);
    EXPECT_EQ(g->getFluid({8, 1, 5}), FluidState::flowing(FluidType::Lava, 1));
    EXPECT_TRUE(g->getFluid({9, 1, 5}).isEmpty());
    EXPECT_EQ(g->countFluid(FluidType::Lava), 1U + 4U * 6U);
}

TEST(FluidSpread, NetherStyleLavaWithStepOneReachesSeven) {
    auto g = makeFlatWorld(17, 3, 17);
    FluidConfig cfg{};
    cfg.lavaDecayStep = 1;
    FluidSimulator sim(*g, cfg);
    sim.placeSource({8, 1, 8}, FluidType::Lava);
    sim.runUntilIdle(5000);
    expectDiamond(*g, {8, 1, 8}, FluidType::Lava, 1);
}

TEST(FluidSpread, WallsStopFlowAndChannelsCarryItAlong) {
    // 1-wide corridor along +x: the fluid only travels along the channel.
    auto g = makeFlatWorld(12, 3, 3);
    g->fill({0, 1, 0}, {11, 1, 0}, BlockId::Stone);
    g->fill({0, 1, 2}, {11, 1, 2}, BlockId::Stone);
    FluidSimulator sim(*g);
    sim.placeSource({0, 1, 1}, FluidType::Water);
    sim.runUntilIdle(1000);
    for (int x = 1; x <= 7; ++x) {
        EXPECT_EQ(g->getFluid({x, 1, 1}), FluidState::flowing(FluidType::Water, static_cast<uint8_t>(7 - x)));
    }
    EXPECT_TRUE(g->getFluid({8, 1, 1}).isEmpty());
    EXPECT_EQ(g->getBlock({3, 1, 0}), BlockId::Stone);
}

TEST(FluidSpread, FlowingWaterTakesTheShorterPathAroundAnObstacle) {
    // Wall segment next to the source: the cell behind it is reached via a detour, so
    // its level reflects the Manhattan path length around the wall, not straight-line.
    auto g = makeFlatWorld(9, 3, 9);
    g->fill({5, 1, 2}, {5, 1, 6}, BlockId::Stone); // wall x=5, z in [2,6]
    FluidSimulator sim(*g);
    sim.placeSource({4, 1, 4}, FluidType::Water);
    sim.runUntilIdle(1000);
    // (6,1,4) is behind the wall: shortest path 4->(4,1,1)... = via z=1 or z=7: 3+2+3 = 8 > 7.
    EXPECT_TRUE(g->getFluid({6, 1, 4}).isEmpty()) << dumpLayer(*g, 1);
    // (6,1,1): path (4,4)->(4,1) 3 steps ->(6,1) 2 steps = 5 -> level 2.
    EXPECT_EQ(g->getFluid({6, 1, 1}), FluidState::flowing(FluidType::Water, 2)) << dumpLayer(*g, 1);
}

TEST(FluidSpread, PlantsAndTorchesAreWashedAwayWithEvent) {
    auto g = makeFlatWorld(5, 3, 5);
    g->setBlock({3, 1, 2}, BlockId::TallGrass);
    g->setBlock({1, 1, 2}, BlockId::Torch);
    FluidSimulator sim(*g);
    sim.placeSource({2, 1, 2}, FluidType::Water);
    sim.runUntilIdle(1000);
    EXPECT_EQ(g->getFluid({3, 1, 2}).type, FluidType::Water);
    EXPECT_EQ(g->getFluid({1, 1, 2}).type, FluidType::Water);
    const std::vector<FluidEvent> events = sim.takeEvents();
    int washed = 0;
    for (const FluidEvent& e : events) {
        if (e.kind == FluidEventKind::BlockWashedAway) {
            ++washed;
            EXPECT_TRUE(e.block == BlockId::TallGrass || e.block == BlockId::Torch);
        }
    }
    EXPECT_EQ(washed, 2);
    EXPECT_TRUE(sim.takeEvents().empty()); // take drains the buffer
}

TEST(FluidSpread, SugarCaneIsNotWashedAway) {
    auto g = makeFlatWorld(5, 3, 5);
    g->setBlock({3, 1, 2}, BlockId::SugarCane);
    FluidSimulator sim(*g);
    sim.placeSource({2, 1, 2}, FluidType::Water);
    sim.runUntilIdle(1000);
    EXPECT_EQ(g->getBlock({3, 1, 2}), BlockId::SugarCane);
    EXPECT_TRUE(g->getFluid({3, 1, 2}).isEmpty());
}

TEST(FluidSpread, GridEdgeActsAsWall) {
    auto g = makeFlatWorld(3, 3, 3);
    FluidSimulator sim(*g);
    sim.placeSource({0, 1, 0}, FluidType::Water);
    sim.runUntilIdle(1000);
    EXPECT_EQ(g->getFluid({2, 1, 2}), FluidState::flowing(FluidType::Water, 3));
    EXPECT_EQ(sim.pendingUpdates(), 0U);
}

TEST(FluidSpread, SettledPoolIsAFixedPoint) {
    // Re-waking every fluid cell of a converged pool must not change anything.
    auto g = makeFlatWorld(15, 3, 15);
    FluidSimulator sim(*g);
    sim.placeSource({7, 1, 7}, FluidType::Water);
    sim.runUntilIdle(1000);
    const uint64_t writesBefore = g->writeCount();
    for (int x = 0; x < 15; ++x) {
        for (int z = 0; z < 15; ++z) {
            sim.notifyBlockChanged({x, 1, z});
        }
    }
    sim.runUntilIdle(1000);
    EXPECT_EQ(g->writeCount(), writesBefore);
}

namespace {

// Corridor floor along x; the reference run has everything loaded from the start.
std::unique_ptr<GridFluidVoxelAccess> settledCorridorReference() {
    auto ref = makeFlatWorld(20, 3, 1);
    FluidSimulator sim(*ref);
    sim.placeSource({4, 1, 0}, FluidType::Water);
    sim.runUntilIdle(5000);
    return ref;
}

} // namespace

TEST(FluidChunkBorders, FlowFrozenAtUnloadedBorderResumesWhenRegionLoads) {
    auto g = makeFlatWorld(20, 3, 1);
    g->setLoaded({10, 0, 0}, {19, 2, 0}, false);
    FluidSimulator sim(*g);
    sim.placeSource({4, 1, 0}, FluidType::Water);
    sim.runUntilIdle(5000);
    ASSERT_EQ(sim.pendingUpdates(), 0U);
    ASSERT_FALSE(g->getFluid({9, 1, 0}).isEmpty());
    g->setLoaded({10, 0, 0}, {19, 2, 0}, true);
    EXPECT_TRUE(g->getFluid({10, 1, 0}).isEmpty()) << "stopped at the border";
    sim.notifyRegionLoaded({10, 0, 0}, {19, 2, 0});
    sim.runUntilIdle(5000);
    EXPECT_TRUE(g->sameContents(*settledCorridorReference())) << dumpLayer(*g, 1);
}

TEST(FluidChunkBorders, UpdatesLostToAnUnloadAreRecoveredWithInteriorWake) {
    // The region unloads while its water is mid-flow: queued entries for it are dropped
    // (nothing readable there). The walled corridor lies strictly inside the region, so a
    // border-only wake cannot see it; wakeInterior re-queues its fluid cells.
    const auto makeWalledCorridor = [] {
        auto g = makeFlatWorld(20, 5, 5);
        g->fill({0, 1, 1}, {19, 1, 1}, BlockId::Stone);
        g->fill({0, 1, 3}, {19, 1, 3}, BlockId::Stone);
        return g;
    };
    auto ref = makeWalledCorridor();
    {
        FluidSimulator sim(*ref);
        sim.placeSource({4, 1, 2}, FluidType::Water);
        sim.runUntilIdle(5000);
    }
    const BlockPos lo{0, 0, 0};
    const BlockPos hi{19, 4, 4};
    for (const bool wakeInterior : {false, true}) {
        auto g = makeWalledCorridor();
        FluidSimulator sim(*g);
        sim.placeSource({4, 1, 2}, FluidType::Water);
        for (int i = 0; i < 15; ++i) {
            sim.tick();
        }
        g->setLoaded(lo, hi, false);
        sim.runUntilIdle(5000);
        EXPECT_EQ(sim.pendingUpdates(), 0U) << "entries for unloaded cells are dropped";
        g->setLoaded(lo, hi, true);
        sim.notifyRegionLoaded(lo, hi, wakeInterior);
        sim.runUntilIdle(5000);
        EXPECT_EQ(g->sameContents(*ref), wakeInterior) << "wakeInterior=" << wakeInterior << "\n" << dumpLayer(*g, 1);
    }
}

TEST(FluidChunkBorders, BorderWakeOnlyTouchesTheShellNotTheInterior) {
    // Without wakeInterior only fluid cells on the region's boundary (and just outside it)
    // are queued, keeping a chunk load O(surface) instead of O(volume).
    auto g = makeFlatWorld(9, 3, 9);
    for (int x = 0; x < 9; ++x) {
        for (int z = 0; z < 9; ++z) {
            g->setFluid({x, 1, z}, FluidState::source(FluidType::Water));
        }
    }
    FluidSimulator sim(*g);
    // Full-height 5x5 column region (like a chunk). Queued: the fluid cells of the 7x7
    // box grown by one, minus the 3x3 interior that touches nothing outside the region.
    sim.notifyRegionLoaded({2, 0, 2}, {6, 2, 6});
    EXPECT_EQ(sim.pendingUpdates(), 7U * 7U - 3U * 3U);
    sim.notifyRegionLoaded({2, 0, 2}, {6, 2, 6}, true);
    EXPECT_EQ(sim.pendingUpdates(), 7U * 7U);
}

TEST(FluidSpread, AdapterContractOutermostRingIsNibbleZeroNotDry) {
    // Adapter contract (see IFluidVoxelAccess): a "0 = dry" nibble cannot hold
    // with a 7-block water reach (source + 7 flowing levels + dry = 9 states > 8), so the
    // 7th ring is stored as nibble 0 and dryness comes from the block id alone. A World
    // adapter that decoded nibble 0 as dry would erase this ring.
    auto g = makeFlatWorld(17, 3, 17);
    FluidSimulator sim(*g);
    sim.placeSource({8, 1, 8}, FluidType::Water);
    sim.runUntilIdle(1000);
    const FluidState rim = g->getFluid({15, 1, 8});
    ASSERT_EQ(rim.type, FluidType::Water);
    EXPECT_EQ(rim.toNibble(), 0U);
    EXPECT_EQ(rim.betaDecay(), 7);
    EXPECT_EQ(FluidState::fromNibble(FluidType::Water, rim.toNibble()), rim) << "nibble 0 decodes back to wet";
    EXPECT_TRUE(g->getFluid({16, 1, 8}).isEmpty());
}
