// P2.3 - Infinite water source: >= 2 horizontal sources + solid (or water source) below.
#include "FluidTestUtils.hpp"

#include <gtest/gtest.h>

using namespace fluidtest;

namespace {

// 1 x 3 trench along x at y = 1, enclosed by stone (floor at y = 0).
std::unique_ptr<GridFluidVoxelAccess> makeTrench() {
    auto g = makeFlatWorld(5, 4, 3);
    g->fill({0, 1, 0}, {4, 1, 2}, BlockId::Stone);
    g->fill({1, 1, 1}, {3, 1, 1}, BlockId::Air);
    return g;
}

} // namespace

TEST(FluidInfiniteSource, GapBetweenTwoSourcesBecomesSource) {
    auto g = makeTrench();
    FluidSimulator sim(*g);
    sim.placeSource({1, 1, 1}, FluidType::Water);
    sim.placeSource({3, 1, 1}, FluidType::Water);
    sim.runUntilIdle(1000);
    EXPECT_TRUE(g->getFluid({2, 1, 1}).isSource());
}

TEST(FluidInfiniteSource, MiddleIsFlowingFirstThenSource) {
    auto g = makeTrench();
    FluidSimulator sim(*g);
    sim.placeSource({1, 1, 1}, FluidType::Water);
    sim.placeSource({3, 1, 1}, FluidType::Water);
    for (int i = 0; i < 5; ++i) {
        sim.tick();
    }
    EXPECT_EQ(g->getFluid({2, 1, 1}), FluidState::flowing(FluidType::Water, 6));
    for (int i = 0; i < 5; ++i) {
        sim.tick();
    }
    EXPECT_TRUE(g->getFluid({2, 1, 1}).isSource());
}

TEST(FluidInfiniteSource, TwoByTwoPoolFillsFromTwoDiagonalSources) {
    auto g = makeFlatWorld(4, 3, 4);
    g->fill({0, 1, 0}, {3, 1, 3}, BlockId::Stone);
    g->fill({1, 1, 1}, {2, 1, 2}, BlockId::Air);
    FluidSimulator sim(*g);
    sim.placeSource({1, 1, 1}, FluidType::Water);
    sim.placeSource({2, 1, 2}, FluidType::Water);
    sim.runUntilIdle(1000);
    EXPECT_TRUE(g->getFluid({1, 1, 2}).isSource());
    EXPECT_TRUE(g->getFluid({2, 1, 1}).isSource());
}

TEST(FluidInfiniteSource, SingleSourceDoesNotDuplicate) {
    auto g = makeTrench();
    FluidSimulator sim(*g);
    sim.placeSource({1, 1, 1}, FluidType::Water);
    sim.runUntilIdle(1000);
    EXPECT_FALSE(g->getFluid({2, 1, 1}).isSource());
    EXPECT_FALSE(g->getFluid({3, 1, 1}).isSource());
}

TEST(FluidInfiniteSource, WaterSourceBelowAlsoSupportsNewSource) {
    auto g = makeFlatWorld(5, 4, 3);
    g->fill({0, 1, 0}, {4, 2, 2}, BlockId::Stone);
    g->fill({1, 1, 1}, {3, 2, 1}, BlockId::Air);
    // Bottom row of the trench pre-filled with sources.
    for (int x = 1; x <= 3; ++x) {
        g->setFluid({x, 1, 1}, FluidState::source(FluidType::Water));
    }
    FluidSimulator sim(*g);
    sim.placeSource({1, 2, 1}, FluidType::Water);
    sim.placeSource({3, 2, 1}, FluidType::Water);
    sim.runUntilIdle(1000);
    EXPECT_TRUE(g->getFluid({2, 2, 1}).isSource());
}

TEST(FluidInfiniteSource, NoNewSourceOverADrop) {
    auto deep = std::make_unique<GridFluidVoxelAccess>(5, 5, 3);
    // Same trench raised to y = 2 with a hole under the middle cell.
    deep->fill({0, 0, 0}, {4, 1, 2}, BlockId::Stone);
    deep->fill({0, 2, 0}, {4, 2, 2}, BlockId::Stone);
    deep->fill({1, 2, 1}, {3, 2, 1}, BlockId::Air);
    deep->setBlock({2, 1, 1}, BlockId::Air);
    FluidSimulator sim(*deep);
    sim.placeSource({1, 2, 1}, FluidType::Water);
    sim.placeSource({3, 2, 1}, FluidType::Water);
    sim.runUntilIdle(1000);
    EXPECT_FALSE(deep->getFluid({2, 2, 1}).isSource());
    EXPECT_FALSE(deep->getFluid({2, 2, 1}).isEmpty());
    EXPECT_TRUE(deep->getFluid({2, 1, 1}).isFalling() || !deep->getFluid({2, 1, 1}).isSource());
}

TEST(FluidInfiniteSource, LavaNeverCreatesInfiniteSources) {
    auto g = makeTrench();
    FluidSimulator sim(*g);
    sim.placeSource({1, 1, 1}, FluidType::Lava);
    sim.placeSource({3, 1, 1}, FluidType::Lava);
    sim.runUntilIdle(5000);
    EXPECT_EQ(g->getFluid({2, 1, 1}), FluidState::flowing(FluidType::Lava, 5));
}

TEST(FluidInfiniteSource, FallingNeighboursDoNotCountAsSources) {
    // Two falling columns landing on either side of a cell: they feed it but are not
    // sources, so the cell stays flowing.
    auto g = makeTrench();
    FluidSimulator sim(*g);
    g->setFluid({1, 2, 1}, FluidState::source(FluidType::Water));
    g->setFluid({3, 2, 1}, FluidState::source(FluidType::Water));
    // Collar both upper sources so they only pour straight down into the trench.
    g->setBlock({2, 2, 1}, BlockId::Stone);
    g->setBlock({0, 2, 1}, BlockId::Stone);
    g->setBlock({4, 2, 1}, BlockId::Stone);
    g->setBlock({1, 2, 0}, BlockId::Stone);
    g->setBlock({1, 2, 2}, BlockId::Stone);
    g->setBlock({3, 2, 0}, BlockId::Stone);
    g->setBlock({3, 2, 2}, BlockId::Stone);
    sim.notifyBlockChanged({1, 2, 1});
    sim.notifyBlockChanged({3, 2, 1});
    sim.runUntilIdle(1000);
    EXPECT_TRUE(g->getFluid({1, 1, 1}).isFalling());
    EXPECT_TRUE(g->getFluid({3, 1, 1}).isFalling());
    EXPECT_EQ(g->getFluid({2, 1, 1}), FluidState::flowing(FluidType::Water, 6));
}

TEST(FluidInfiniteSource, UnloadedFloorDoesNotCountAsSolid) {
    // An unloaded cell under the gap is unknown, not solid (Beta reads unloaded chunks as
    // air): no source may be created on top of it.
    auto g = makeTrench();
    g->setLoaded({2, 0, 1}, {2, 0, 1}, false);
    FluidSimulator sim(*g);
    sim.placeSource({1, 1, 1}, FluidType::Water);
    sim.placeSource({3, 1, 1}, FluidType::Water);
    sim.runUntilIdle(1000);
    EXPECT_EQ(g->getFluid({2, 1, 1}), FluidState::flowing(FluidType::Water, 6));
}
