// Beta getOptimalFlowDirections(): flowing fluid heads for the nearest drop.
#include "FluidTestUtils.hpp"

#include <gtest/gtest.h>

#include <array>

using namespace fluidtest;

namespace {

// Floor at y = 1 with an air layer underneath (y = 0) so holes in the floor are real drops.
std::unique_ptr<GridFluidVoxelAccess> makeRaisedFloor(int32_t sx, int32_t sz) {
    auto g = std::make_unique<GridFluidVoxelAccess>(sx, 5, sz);
    g->fill({0, 1, 0}, {sx - 1, 1, sz - 1}, BlockId::Stone);
    return g;
}

int wetCellsAt(const GridFluidVoxelAccess& g, int32_t y) {
    int n = 0;
    for (int x = 0; x < g.sizeX(); ++x) {
        for (int z = 0; z < g.sizeZ(); ++z) {
            n += g.getFluid({x, y, z}).isEmpty() ? 0 : 1;
        }
    }
    return n;
}

} // namespace

TEST(FluidPathfinding, SourceOnlyFlowsTowardHoleThreeBlocksAway) {
    auto g = makeRaisedFloor(11, 11);
    g->setBlock({8, 1, 5}, BlockId::Air); // hole
    FluidSimulator sim(*g);
    sim.placeSource({5, 2, 5}, FluidType::Water);
    for (int i = 0; i < 5; ++i) {
        sim.tick();
    }
    EXPECT_EQ(g->getFluid({6, 2, 5}), FluidState::flowing(FluidType::Water, 6));
    EXPECT_TRUE(g->getFluid({4, 2, 5}).isEmpty()) << dumpLayer(*g, 2);
    EXPECT_TRUE(g->getFluid({5, 2, 4}).isEmpty());
    EXPECT_TRUE(g->getFluid({5, 2, 6}).isEmpty());

    sim.runUntilIdle(5000);
    // Only a straight stream to the hole remains at floor height.
    EXPECT_EQ(wetCellsAt(*g, 2), 4) << dumpLayer(*g, 2);
    EXPECT_EQ(g->getFluid({7, 2, 5}), FluidState::flowing(FluidType::Water, 5));
    EXPECT_EQ(g->getFluid({8, 2, 5}), FluidState::flowing(FluidType::Water, 4));
    EXPECT_TRUE(g->getFluid({8, 1, 5}).isFalling()) << "pours into the hole";
}

TEST(FluidPathfinding, AdjacentHoleHasZeroCostAndWins) {
    auto g = makeRaisedFloor(7, 7);
    g->setBlock({3, 1, 4}, BlockId::Air); // hole right next to the source (south)
    g->setBlock({1, 1, 3}, BlockId::Air); // hole two blocks west
    FluidSimulator sim(*g);
    sim.placeSource({3, 2, 3}, FluidType::Water);
    for (int i = 0; i < 5; ++i) {
        sim.tick();
    }
    EXPECT_FALSE(g->getFluid({3, 2, 4}).isEmpty());
    EXPECT_TRUE(g->getFluid({2, 2, 3}).isEmpty()) << dumpLayer(*g, 2);
    EXPECT_TRUE(g->getFluid({4, 2, 3}).isEmpty());
    EXPECT_TRUE(g->getFluid({3, 2, 2}).isEmpty());
}

TEST(FluidPathfinding, EquidistantHolesSplitTheFlow) {
    auto g = makeRaisedFloor(11, 11);
    g->setBlock({7, 1, 5}, BlockId::Air);
    g->setBlock({3, 1, 5}, BlockId::Air);
    FluidSimulator sim(*g);
    sim.placeSource({5, 2, 5}, FluidType::Water);
    for (int i = 0; i < 5; ++i) {
        sim.tick();
    }
    EXPECT_FALSE(g->getFluid({6, 2, 5}).isEmpty());
    EXPECT_FALSE(g->getFluid({4, 2, 5}).isEmpty());
    EXPECT_TRUE(g->getFluid({5, 2, 4}).isEmpty());
    EXPECT_TRUE(g->getFluid({5, 2, 6}).isEmpty());
}

TEST(FluidPathfinding, HoleBeyondSearchRangeIsIgnored) {
    auto g = makeRaisedFloor(17, 17);
    g->setBlock({14, 1, 8}, BlockId::Air); // 6 blocks away: outside Beta's search horizon
    FluidSimulator sim(*g);
    sim.placeSource({8, 2, 8}, FluidType::Water);
    for (int i = 0; i < 5; ++i) {
        sim.tick();
    }
    EXPECT_FALSE(g->getFluid({9, 2, 8}).isEmpty());
    EXPECT_FALSE(g->getFluid({7, 2, 8}).isEmpty());
    EXPECT_FALSE(g->getFluid({8, 2, 9}).isEmpty());
    EXPECT_FALSE(g->getFluid({8, 2, 7}).isEmpty());
}

TEST(FluidPathfinding, SearchDepthIsConfigurable) {
    auto g = makeRaisedFloor(11, 11);
    g->setBlock({8, 1, 5}, BlockId::Air);
    FluidConfig cfg{};
    cfg.maxFlowSearchDepth = 1; // only sees holes at distance <= 2
    FluidSimulator sim(*g, cfg);
    sim.placeSource({5, 2, 5}, FluidType::Water);
    for (int i = 0; i < 5; ++i) {
        sim.tick();
    }
    EXPECT_FALSE(g->getFluid({4, 2, 5}).isEmpty()) << "hole at distance 3 is invisible with depth 1";
}

TEST(FluidPathfinding, WallsAreNotPathedThrough) {
    // Hole two blocks east but behind a wall: the detour north/south is long, so the
    // water does not treat east as special (path cost > what the other sides offer).
    auto g = makeRaisedFloor(11, 11);
    g->setBlock({7, 1, 5}, BlockId::Air);
    g->setBlock({6, 2, 5}, BlockId::Stone);
    FluidSimulator sim(*g);
    sim.placeSource({5, 2, 5}, FluidType::Water);
    for (int i = 0; i < 5; ++i) {
        sim.tick();
    }
    // North and south both lead around the wall to the hole at equal cost; west does not.
    EXPECT_FALSE(g->getFluid({5, 2, 4}).isEmpty()) << dumpLayer(*g, 2);
    EXPECT_FALSE(g->getFluid({5, 2, 6}).isEmpty());
    EXPECT_TRUE(g->getFluid({4, 2, 5}).isEmpty());
}

TEST(FluidPathfinding, LavaAlsoSeeksHoles) {
    auto g = makeRaisedFloor(9, 9);
    g->setBlock({6, 1, 4}, BlockId::Air);
    FluidSimulator sim(*g);
    sim.placeSource({4, 2, 4}, FluidType::Lava);
    for (int i = 0; i < 30; ++i) {
        sim.tick();
    }
    EXPECT_FALSE(g->getFluid({5, 2, 4}).isEmpty());
    EXPECT_TRUE(g->getFluid({3, 2, 4}).isEmpty());
}

namespace {

// After the source's first update: which of the four neighbours got water.
std::array<bool, 4> firstSpread(int holeDistance, int depth) {
    auto g = makeRaisedFloor(17, 17);
    g->setBlock({8 + holeDistance, 1, 8}, BlockId::Air);
    FluidConfig cfg{};
    cfg.maxFlowSearchDepth = depth;
    FluidSimulator sim(*g, cfg);
    sim.placeSource({8, 2, 8}, FluidType::Water);
    for (int i = 0; i < 5; ++i) {
        sim.tick();
    }
    return {!g->getFluid({9, 2, 8}).isEmpty(), !g->getFluid({7, 2, 8}).isEmpty(), !g->getFluid({8, 2, 7}).isEmpty(),
            !g->getFluid({8, 2, 9}).isEmpty()};
}

constexpr std::array<bool, 4> kOnlyEast{true, false, false, false};
constexpr std::array<bool, 4> kEverywhere{true, true, true, true};

} // namespace

TEST(FluidPathfinding, SearchHorizonIsExactlyDepthPlusOne) {
    // Beta calculateFlowCost() with depth 4 sees drops up to 5 cells away, not 6.
    EXPECT_EQ(firstSpread(5, 4), kOnlyEast) << "a hole 5 blocks away is inside the horizon";
    EXPECT_EQ(firstSpread(6, 4), kEverywhere) << "a hole 6 blocks away is outside";
    // Same boundary for a custom depth: depth 1 sees distance 2 but not 3.
    EXPECT_EQ(firstSpread(2, 1), kOnlyEast);
    EXPECT_EQ(firstSpread(3, 1), kEverywhere);
}
