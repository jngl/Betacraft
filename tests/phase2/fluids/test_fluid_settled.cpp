// P2.3 - FluidSimulator::isSettled: which fluid cells a freshly loaded or generated region
// must schedule. A settled cell's update would change nothing, so the World only queues
// the unsettled ones instead of every fluid cell of an ocean.
#include "FluidTestUtils.hpp"

#include <gtest/gtest.h>

#include <random>

using namespace fluidtest;

namespace {

// Stone box with a 3x3 basin at y = 1 whose rim is stone: the centre cell is enclosed.
std::unique_ptr<GridFluidVoxelAccess> makeBasin() {
    auto g = makeFlatWorld(5, 4, 5);
    g->fill({0, 1, 0}, {4, 1, 4}, BlockId::Stone);
    return g;
}

// Writes `state` at `pos` directly in the grid (no simulator wake-up), like worldgen does.
void putFluid(GridFluidVoxelAccess& g, const BlockPos& pos, FluidState state) { g.setFluid(pos, state); }

} // namespace

TEST(FluidSettled, DryCellsAreSettled) {
    auto g = makeFlatWorld(4, 3, 4);
    FluidSimulator sim(*g);
    EXPECT_TRUE(sim.isSettled({1, 1, 1}));  // air
    EXPECT_TRUE(sim.isSettled({1, 0, 1}));  // stone
    EXPECT_TRUE(sim.isSettled({-5, 1, 1})); // unloaded
}

TEST(FluidSettled, EnclosedSourceIsSettled) {
    auto g = makeBasin();
    putFluid(*g, {2, 1, 2}, FluidState::source(FluidType::Water));
    FluidSimulator sim(*g);
    EXPECT_TRUE(sim.isSettled({2, 1, 2}));
}

TEST(FluidSettled, SourcesBesideTheirOwnFluidAreSettled) {
    // A row of sources (an ocean slice): neighbours of the same fluid cannot be displaced.
    auto g = makeBasin();
    for (int32_t x = 1; x <= 3; ++x) {
        putFluid(*g, {x, 1, 2}, FluidState::source(FluidType::Water));
    }
    FluidSimulator sim(*g);
    for (int32_t x = 1; x <= 3; ++x) {
        EXPECT_TRUE(sim.isSettled({x, 1, 2})) << x;
    }
}

TEST(FluidSettled, SourceWithAirBesideItIsNotSettled) {
    auto g = makeFlatWorld(5, 3, 5);
    putFluid(*g, {2, 1, 2}, FluidState::source(FluidType::Water));
    FluidSimulator sim(*g);
    EXPECT_FALSE(sim.isSettled({2, 1, 2}));
}

TEST(FluidSettled, SourceAboveAirIsNotSettled) {
    auto g = makeBasin();
    g->setBlock({2, 0, 2}, BlockId::Air);
    g->setBlock({2, 1, 2}, BlockId::Air);
    putFluid(*g, {2, 2, 2}, FluidState::source(FluidType::Water));
    g->fill({1, 2, 2}, {1, 2, 2}, BlockId::Stone);
    g->fill({3, 2, 2}, {3, 2, 2}, BlockId::Stone);
    g->fill({2, 2, 1}, {2, 2, 1}, BlockId::Stone);
    g->fill({2, 2, 3}, {2, 2, 3}, BlockId::Stone);
    FluidSimulator sim(*g);
    EXPECT_FALSE(sim.isSettled({2, 2, 2}));
}

TEST(FluidSettled, SourceBesideAWashablePlantIsNotSettled) {
    auto g = makeBasin();
    g->setBlock({3, 1, 2}, BlockId::Dandelion);
    putFluid(*g, {2, 1, 2}, FluidState::source(FluidType::Water));
    FluidSimulator sim(*g);
    EXPECT_FALSE(sim.isSettled({2, 1, 2}));
}

TEST(FluidSettled, FlowingAndFallingCellsAreNeverSettled) {
    // Their level depends on neighbours that may have changed while they were not loaded.
    auto g = makeBasin();
    putFluid(*g, {2, 1, 2}, FluidState::flowing(FluidType::Water, 5));
    FluidSimulator sim(*g);
    EXPECT_FALSE(sim.isSettled({2, 1, 2}));
    putFluid(*g, {2, 1, 2}, FluidState::falling(FluidType::Water));
    EXPECT_FALSE(sim.isSettled({2, 1, 2}));
}

TEST(FluidSettled, LavaTouchingWaterIsNotSettledButTheWaterIs) {
    auto g = makeBasin();
    g->setBlock({1, 1, 2}, BlockId::Air);
    putFluid(*g, {1, 1, 2}, FluidState::source(FluidType::Lava));
    putFluid(*g, {2, 1, 2}, FluidState::source(FluidType::Water));
    FluidSimulator sim(*g);
    EXPECT_FALSE(sim.isSettled({1, 1, 2})) << "lava next to water hardens on its update";
    EXPECT_TRUE(sim.isSettled({2, 1, 2})) << "water never displaces lava";
}

TEST(FluidSettled, LavaAboveWaterIsNotSettled) {
    auto g = makeBasin();
    putFluid(*g, {2, 1, 2}, FluidState::source(FluidType::Water));
    putFluid(*g, {2, 2, 2}, FluidState::source(FluidType::Lava));
    g->fill({1, 2, 2}, {1, 2, 2}, BlockId::Stone);
    g->fill({3, 2, 2}, {3, 2, 2}, BlockId::Stone);
    g->fill({2, 2, 1}, {2, 2, 1}, BlockId::Stone);
    g->fill({2, 2, 3}, {2, 2, 3}, BlockId::Stone);
    FluidSimulator sim(*g);
    EXPECT_FALSE(sim.isSettled({2, 2, 2}));
}

TEST(FluidSettled, UnloadedNeighboursCountAsWalls) {
    // Fluid stopped at an unloaded border is settled until that region loads; the loader
    // then re-checks the border with notifyRegionLoaded or isSettled.
    auto g = makeFlatWorld(5, 3, 5);
    g->fill({0, 1, 0}, {4, 1, 4}, BlockId::Stone);
    g->setBlock({4, 1, 2}, BlockId::Air);
    putFluid(*g, {4, 1, 2}, FluidState::source(FluidType::Water));
    g->setBlock({3, 1, 2}, BlockId::Air);
    g->setLoaded({3, 1, 2}, {3, 1, 2}, false);
    FluidSimulator sim(*g);
    EXPECT_TRUE(sim.isSettled({4, 1, 2}));
    g->setLoaded({3, 1, 2}, {3, 1, 2}, true);
    EXPECT_FALSE(sim.isSettled({4, 1, 2}));
}

// Property: updating a cell reported as settled never writes nor emits anything, on random
// terrain with randomly scattered water and lava sources and flowing cells.
TEST(FluidSettled, SettledCellsNeverChangeWhenUpdated) {
    constexpr int32_t kS = 12;
    constexpr int32_t kH = 6;
    size_t settledSeen = 0;
    size_t unsettledSeen = 0;
    for (uint32_t seed = 1; seed <= 24; ++seed) {
        std::mt19937 rng(seed);
        std::uniform_int_distribution<int> roll(0, 99);
        GridFluidVoxelAccess g(kS, kH, kS);
        for (int32_t y = 0; y < kH; ++y) {
            for (int32_t z = 0; z < kS; ++z) {
                for (int32_t x = 0; x < kS; ++x) {
                    const int r = roll(rng);
                    const BlockPos p{x, y, z};
                    if (r < 35) {
                        g.setBlock(p, BlockId::Stone);
                    } else if (r < 60) {
                        g.setFluid(p, FluidState::source(FluidType::Water));
                    } else if (r < 66) {
                        g.setFluid(p, FluidState::source(FluidType::Lava));
                    } else if (r < 70) {
                        g.setFluid(p, FluidState::flowing(FluidType::Water, static_cast<uint8_t>(r % 7)));
                    } else if (r < 73) {
                        g.setBlock(p, BlockId::TallGrass);
                    }
                }
            }
        }
        FluidSimulator probe(g);
        for (int32_t y = 0; y < kH; ++y) {
            for (int32_t z = 0; z < kS; ++z) {
                for (int32_t x = 0; x < kS; ++x) {
                    const BlockPos p{x, y, z};
                    const FluidState f = g.getFluid(p);
                    if (f.isEmpty()) continue;
                    if (!probe.isSettled(p)) {
                        ++unsettledSeen;
                        continue;
                    }
                    ++settledSeen;
                    auto copy = g.clone();
                    FluidSimulator sim(*copy);
                    sim.scheduleUpdate(p);
                    const uint64_t writesBefore = copy->writeCount();
                    for (uint32_t t = 0; t < sim.tickRateFor(f.type); ++t) {
                        (void)sim.tick();
                    }
                    EXPECT_EQ(copy->writeCount(), writesBefore)
                        << "seed " << seed << " cell (" << x << "," << y << "," << z << ")";
                    EXPECT_TRUE(sim.events().empty()) << "seed " << seed;
                }
            }
        }
    }
    // Both branches must be exercised for the property to mean anything.
    EXPECT_GT(settledSeen, 100U);
    EXPECT_GT(unsettledSeen, 100U);
}
