// Lava/water mixing: source lava -> obsidian, flowing lava -> cobblestone,
// lava falling onto water -> stone (Beta BlockFluid.checkForHarden + BlockFlowing).
#include "FluidTestUtils.hpp"

#include <gtest/gtest.h>

#include <algorithm>

using namespace fluidtest;
using betacraft::fluids::FluidEvent;
using betacraft::fluids::FluidEventKind;

namespace {

// 1-wide corridor along x at z = 1, y = 1 (stone floor, stone walls at z = 0 and z = 2).
std::unique_ptr<GridFluidVoxelAccess> makeCorridor(int32_t length) {
    auto g = makeFlatWorld(length, 4, 3);
    g->fill({0, 1, 0}, {length - 1, 1, 0}, BlockId::Stone);
    g->fill({0, 1, 2}, {length - 1, 1, 2}, BlockId::Stone);
    return g;
}

bool hasFizz(const std::vector<FluidEvent>& events, const BlockPos& pos, BlockId produced) {
    return std::any_of(events.begin(), events.end(), [&](const FluidEvent& e) {
        return e.kind == FluidEventKind::Fizz && e.pos == pos && e.block == produced;
    });
}

} // namespace

TEST(FluidInteractions, WaterReachingLavaSourceMakesObsidian) {
    auto g = makeCorridor(6);
    FluidSimulator sim(*g);
    sim.placeSource({0, 1, 1}, FluidType::Lava);
    sim.placeSource({4, 1, 1}, FluidType::Water);
    for (int i = 0; i < 15; ++i) {
        sim.tick();
    }
    // Water reached x = 1 on tick 15, before the lava's first update on tick 30.
    EXPECT_EQ(g->getBlock({0, 1, 1}), BlockId::Obsidian);
    EXPECT_TRUE(g->getFluid({0, 1, 1}).isEmpty());
    EXPECT_TRUE(hasFizz(sim.takeEvents(), {0, 1, 1}, BlockId::Obsidian));
    sim.runUntilIdle(2000);
    EXPECT_EQ(g->getBlock({0, 1, 1}), BlockId::Obsidian);
    EXPECT_EQ(g->countFluid(FluidType::Lava), 0U);
}

TEST(FluidInteractions, WaterAboveLavaSourceMakesObsidian) {
    auto g = makeFlatWorld(3, 4, 3);
    g->fill({0, 1, 0}, {2, 1, 2}, BlockId::Stone);
    g->setBlock({1, 1, 1}, BlockId::Air);
    FluidSimulator sim(*g);
    sim.placeSource({1, 1, 1}, FluidType::Lava);
    sim.placeSource({1, 2, 1}, FluidType::Water);
    EXPECT_EQ(g->getBlock({1, 1, 1}), BlockId::Obsidian);
}

TEST(FluidInteractions, FlowingLavaTouchingWaterMakesCobblestone) {
    for (const uint8_t level : {uint8_t{5}, uint8_t{3}}) {
        auto g = makeCorridor(5);
        g->setFluid({1, 1, 1}, FluidState::flowing(FluidType::Lava, level));
        FluidSimulator sim(*g);
        sim.placeSource({2, 1, 1}, FluidType::Water);
        EXPECT_EQ(g->getBlock({1, 1, 1}), BlockId::Cobblestone) << "lava level " << int(level);
        EXPECT_TRUE(hasFizz(sim.takeEvents(), {1, 1, 1}, BlockId::Cobblestone));
    }
}

TEST(FluidInteractions, ThinAndFallingLavaDoNotHarden) {
    // Beta only hardens decay <= 4: overworld level-1 lava (decay 6) and falling lava stay.
    for (const FluidState lava : {FluidState::flowing(FluidType::Lava, 1), FluidState::falling(FluidType::Lava)}) {
        auto g = makeCorridor(5);
        g->setFluid({1, 1, 1}, lava);
        FluidSimulator sim(*g);
        sim.placeSource({2, 1, 1}, FluidType::Water);
        EXPECT_EQ(g->getFluid({1, 1, 1}), lava);
    }
}

TEST(FluidInteractions, WaterBelowLavaDoesNotHardenItButLavaTurnsItToStone) {
    auto g = makeFlatWorld(3, 4, 3);
    g->fill({0, 1, 0}, {2, 2, 2}, BlockId::Stone);
    g->setBlock({1, 1, 1}, BlockId::Air);
    g->setBlock({1, 2, 1}, BlockId::Air);
    FluidSimulator sim(*g);
    sim.placeSource({1, 1, 1}, FluidType::Water);
    sim.placeSource({1, 2, 1}, FluidType::Lava);
    EXPECT_TRUE(g->getFluid({1, 2, 1}).isSource()) << "water below does not harden lava";
    for (int i = 0; i < 30; ++i) {
        sim.tick();
    }
    EXPECT_EQ(g->getBlock({1, 1, 1}), BlockId::Stone);
    EXPECT_TRUE(g->getFluid({1, 2, 1}).isSource());
    EXPECT_TRUE(hasFizz(sim.takeEvents(), {1, 1, 1}, BlockId::Stone));
}

TEST(FluidInteractions, LavaFallFlowingIntoPoolMakesStone) {
    // Lava pours off a ledge onto a water pool: the pool cell it lands on becomes stone.
    auto g = makeFlatWorld(6, 6, 3);
    g->fill({0, 1, 0}, {5, 4, 0}, BlockId::Stone);
    g->fill({0, 1, 2}, {5, 4, 2}, BlockId::Stone);
    g->fill({0, 1, 1}, {1, 3, 1}, BlockId::Stone); // ledge top at y = 3
    for (int x = 2; x <= 5; ++x) {
        g->setFluid({x, 1, 1}, FluidState::source(FluidType::Water));
    }
    FluidSimulator sim(*g);
    sim.placeSource({1, 4, 1}, FluidType::Lava);
    sim.runUntilIdle(5000);
    EXPECT_EQ(g->getBlock({2, 1, 1}), BlockId::Stone) << dumpLayer(*g, 1);
}

TEST(FluidInteractions, LavaFlowingTowardWaterFormsCobblestoneGenerator) {
    // Classic cobble generator: lava flows into the gap next to (not yet woken) water.
    auto g = makeCorridor(4);
    g->setFluid({2, 1, 1}, FluidState::source(FluidType::Water));
    g->setBlock({3, 1, 1}, BlockId::Stone);
    g->setFluid({0, 1, 1}, FluidState::source(FluidType::Lava));
    FluidSimulator sim(*g);
    sim.notifyBlockChanged({0, 1, 1}); // only the lava is awake
    EXPECT_TRUE(g->getFluid({0, 1, 1}).isSource());
    for (int i = 0; i < 30; ++i) {
        sim.tick();
    }
    EXPECT_EQ(g->getBlock({1, 1, 1}), BlockId::Cobblestone);
    EXPECT_TRUE(g->getFluid({0, 1, 1}).isSource()) << "the lava source is not touching water";
    EXPECT_TRUE(g->getFluid({2, 1, 1}).isSource());
}

TEST(FluidInteractions, LandingLavaDisplacesAdjacentWaterWithFizz) {
    // Beta liquidCanDisplaceBlock(): lava may flow into water. Falling lava cannot harden,
    // so a lava fall landing next to an isolated water source boils it away; the new
    // lava cell touches no other water and stays lava.
    auto g = makeCorridor(3);
    g->setBlock({2, 1, 1}, BlockId::Stone);
    g->setBlock({1, 3, 1}, BlockId::Stone); // collar the lava source: it can only fall
    g->setBlock({0, 3, 0}, BlockId::Stone);
    g->setBlock({0, 3, 2}, BlockId::Stone);
    g->setFluid({1, 1, 1}, FluidState::source(FluidType::Water));
    FluidSimulator sim(*g);
    sim.placeSource({0, 3, 1}, FluidType::Lava);
    sim.runUntilIdle(5000);
    EXPECT_EQ(g->getFluid({0, 1, 1}), FluidState::falling(FluidType::Lava));
    EXPECT_EQ(g->getFluid({1, 1, 1}), FluidState::flowing(FluidType::Lava, 5)) << dumpLayer(*g, 1);
    EXPECT_EQ(g->countFluid(FluidType::Water), 0U);
    EXPECT_TRUE(hasFizz(sim.takeEvents(), {1, 1, 1}, BlockId::Air));
}

TEST(FluidInteractions, WaterCannotDisplaceLava) {
    auto g = makeCorridor(7);
    FluidSimulator sim(*g);
    sim.placeSource({0, 1, 1}, FluidType::Lava);
    sim.runUntilIdle(5000);
    ASSERT_EQ(g->getFluid({3, 1, 1}), FluidState::flowing(FluidType::Lava, 1));
    sim.placeSource({5, 1, 1}, FluidType::Water);
    sim.runUntilIdle(5000);
    EXPECT_EQ(g->getFluid({3, 1, 1}), FluidState::flowing(FluidType::Lava, 1));
    EXPECT_EQ(g->getFluid({4, 1, 1}), FluidState::flowing(FluidType::Water, 6));
    EXPECT_TRUE(g->getFluid({0, 1, 1}).isSource());
}

TEST(FluidInteractions, PreexistingContactHardensOnLavaTick) {
    // World generation may leave lava touching water without any notification; the lava's
    // own scheduled update resolves it.
    auto g = makeCorridor(4);
    g->setFluid({1, 1, 1}, FluidState::source(FluidType::Lava));
    g->setFluid({2, 1, 1}, FluidState::source(FluidType::Water));
    FluidSimulator sim(*g);
    sim.scheduleUpdate({1, 1, 1});
    sim.runUntilIdle(100);
    EXPECT_EQ(g->getBlock({1, 1, 1}), BlockId::Obsidian);
}

TEST(FluidInteractions, ExternalBlockRemovalBetweenLavaAndWaterHardens) {
    auto g = makeCorridor(5);
    g->setFluid({1, 1, 1}, FluidState::source(FluidType::Lava));
    g->setBlock({2, 1, 1}, BlockId::Stone);
    g->setFluid({3, 1, 1}, FluidState::source(FluidType::Water));
    FluidSimulator sim(*g);
    g->setBlock({2, 1, 1}, BlockId::Air); // player mines the separator
    sim.notifyBlockChanged({2, 1, 1});
    sim.runUntilIdle(200);
    EXPECT_EQ(g->getBlock({1, 1, 1}), BlockId::Obsidian);
}

TEST(FluidInteractions, LavaBurnsPlantsAndTorchesWithFizzAndNoDrop) {
    // Beta flowIntoBlock(): lava triggers the mix effects (fizz) on what it destroys; only
    // water calls dropBlockAsItem. So no BlockWashedAway event, one fizz per burnt block.
    auto g = makeFlatWorld(5, 3, 5);
    g->setBlock({3, 1, 2}, BlockId::TallGrass);
    g->setBlock({1, 1, 2}, BlockId::Torch);
    FluidSimulator sim(*g);
    sim.placeSource({2, 1, 2}, FluidType::Lava);
    sim.runUntilIdle(1000);
    EXPECT_EQ(g->getFluid({3, 1, 2}).type, FluidType::Lava);
    EXPECT_EQ(g->getFluid({1, 1, 2}).type, FluidType::Lava);
    const std::vector<FluidEvent> events = sim.takeEvents();
    EXPECT_TRUE(std::none_of(events.begin(), events.end(),
                             [](const FluidEvent& e) { return e.kind == FluidEventKind::BlockWashedAway; }));
    EXPECT_TRUE(hasFizz(events, {3, 1, 2}, BlockId::Air));
    EXPECT_TRUE(hasFizz(events, {1, 1, 2}, BlockId::Air));
}

TEST(FluidInteractions, HardeningBoundaryIsDecayFour) {
    // Beta checkForHarden(): metadata <= 4 -> cobblestone. Nether-style lava (step 1) has
    // the decay-4 and decay-5 levels that overworld lava skips, so pin both sides here.
    FluidConfig nether{};
    nether.lavaDecayStep = 1;
    const struct {
        uint8_t level;
        bool hardens;
    } cases[] = {{3, true}, {2, false}}; // level 3 = decay 4, level 2 = decay 5
    for (const auto& c : cases) {
        auto g = makeCorridor(5);
        const FluidState lava = FluidState::flowing(FluidType::Lava, c.level);
        g->setFluid({1, 1, 1}, lava);
        FluidSimulator sim(*g, nether);
        sim.placeSource({2, 1, 1}, FluidType::Water);
        if (c.hardens) {
            EXPECT_EQ(g->getBlock({1, 1, 1}), BlockId::Cobblestone) << "level " << int(c.level);
        } else {
            EXPECT_EQ(g->getFluid({1, 1, 1}), lava) << "level " << int(c.level);
        }
    }
}
