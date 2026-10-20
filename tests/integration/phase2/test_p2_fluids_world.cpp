// Phase 2 integration, P2.3 fluids in the world tick: World owns a FluidSimulator over a
// World-backed IFluidVoxelAccess (block id + Beta metadata nibble), advanced once per
// 20 TPS game tick by World::tick(). Block edits, chunk loads and saved mid-flow water all
// feed it; every voxel it writes marks the touched chunks for re-meshing.
#include "tests/test_helpers.hpp"

#include "P2WorldgenSupport.hpp"
#include "core/Config.hpp"
#include "fluids/FluidSimulator.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <memory>
#include <vector>

using namespace betacraft;
using fluids::FluidEvent;
using fluids::FluidEventKind;
using fluids::FluidState;
using fluids::FluidType;
using voxel::BlockId;
using voxel::Chunk;

namespace {

// High above the generated terrain of the test seed, on a glass floor laid by the test.
constexpr int32_t kFloorY = 119;
constexpr int32_t kY = 120;
constexpr int32_t kWaterTicks = 5;
constexpr int32_t kLavaTicks = 30;

class P2FluidsWorld : public ::testing::Test {
protected:
    void SetUp() override {
        voxel::BlockRegistry::initialize();
        m_world = std::make_unique<world::World>(test::p2worldgen::kSeed, m_dir.path());
        test::p2worldgen::loadAll(*m_world, test::p2worldgen::squareRegion({0, 0}, 1));
        // Glass keeps the floor cheap to light; it blocks flow like any solid block. The
        // margins exceed the water reach (7) plus the flow-search depth (4).
        layFloor(*m_world, -12, 31, -4, 20);
        (void)m_world->takeFluidEvents();
    }

    static void layFloor(world::World& w, int32_t x0, int32_t x1, int32_t z0, int32_t z1) {
        for (int32_t z = z0; z <= z1; ++z) {
            for (int32_t x = x0; x <= x1; ++x) {
                (void)w.setBlock(x, kFloorY, z, BlockId::Glass);
            }
        }
    }

    void tickN(int32_t n) {
        for (int32_t i = 0; i < n; ++i) {
            (void)m_world->tick();
        }
    }

    [[nodiscard]] FluidState fluidAt(int32_t x, int32_t z) const { return m_world->getFluid({x, kY, z}); }

    [[nodiscard]] size_t countFluidOnFloor(FluidType type) const {
        size_t n = 0;
        for (int32_t z = -4; z <= 20; ++z) {
            for (int32_t x = -12; x <= 31; ++x) {
                if (m_world->getFluid({x, kY, z}).type == type) ++n;
            }
        }
        return n;
    }

    void clearMeshFlags() {
        for (const auto& [coord, chunk] : m_world->getChunks()) {
            chunk->setMeshDirty(false);
        }
    }

    [[nodiscard]] bool meshDirty(core::ChunkCoord c) const {
        const Chunk* chunk = m_world->getChunk(c);
        return chunk != nullptr && chunk->isMeshDirty();
    }

    test::TempDir m_dir;
    std::unique_ptr<world::World> m_world;
};

[[nodiscard]] bool hasEvent(const std::vector<FluidEvent>& events, FluidEventKind kind, core::BlockPos pos,
                            BlockId block) {
    return std::find(events.begin(), events.end(), FluidEvent{kind, pos, block}) != events.end();
}

} // namespace

TEST_F(P2FluidsWorld, GameTickRateIsTwentyPerSecond) {
    static_assert(core::EngineConfig::GAME_TICKS_PER_SECOND == 20);
    const uint64_t before = m_world->getFluidSimulator().currentTick();
    tickN(3);
    EXPECT_EQ(m_world->getFluidSimulator().currentTick(), before + 3);
}

TEST_F(P2FluidsWorld, FluidStateComesFromBlockIdAndBetaMetadata) {
    ASSERT_TRUE(m_world->setBlockWithMetadata(3, kY, 3, BlockId::StillWater, 3));
    EXPECT_EQ(m_world->getFluid({3, kY, 3}), FluidState::fromBetaDecay(FluidType::Water, 3));
    ASSERT_TRUE(m_world->setBlockWithMetadata(3, kY, 3, BlockId::Lava, 8));
    EXPECT_EQ(m_world->getFluid({3, kY, 3}), FluidState::falling(FluidType::Lava));
    ASSERT_TRUE(m_world->setBlock(3, kY, 3, BlockId::Glass));
    EXPECT_TRUE(m_world->getFluid({3, kY, 3}).isEmpty());
    EXPECT_TRUE(m_world->getFluid({3, 200, 3}).isEmpty());
    EXPECT_TRUE(m_world->getFluid({5000, 70, 5000}).isEmpty()) << "unloaded chunk";
}

// Application::handleRightClickAction places the held block through World::setBlock.
TEST_F(P2FluidsWorld, PlacedWaterSourceSpreadsOneRingPerWaterTick) {
    ASSERT_TRUE(m_world->setBlock(8, kY, 8, BlockId::Water));
    EXPECT_TRUE(fluidAt(8, 8).isSource());

    tickN(kWaterTicks - 1);
    EXPECT_TRUE(fluidAt(9, 8).isEmpty()) << "water ticks every 5 game ticks";
    tickN(1);
    EXPECT_EQ(fluidAt(9, 8), FluidState::fromBetaDecay(FluidType::Water, 1));
    EXPECT_EQ(m_world->getBlock(9, kY, 8), BlockId::Water);
    EXPECT_EQ(m_world->getMetadata(9, kY, 8), 1) << "World stores Beta's decay, not toNibble()";
    EXPECT_EQ(fluidAt(8, 7), FluidState::fromBetaDecay(FluidType::Water, 1));

    tickN(kWaterTicks * 8);
    EXPECT_EQ(fluidAt(15, 8), FluidState::fromBetaDecay(FluidType::Water, 7));
    EXPECT_EQ(fluidAt(1, 8), FluidState::fromBetaDecay(FluidType::Water, 7));
    EXPECT_EQ(fluidAt(11, 11), FluidState::fromBetaDecay(FluidType::Water, 6));
    EXPECT_TRUE(fluidAt(16, 8).isEmpty()) << "7 flowing cells, then dry";
    EXPECT_TRUE(fluidAt(12, 12).isEmpty());
    EXPECT_EQ(countFluidOnFloor(FluidType::Water), 1U + 4U * 28U);
    EXPECT_EQ(m_world->getFluidSimulator().pendingUpdates(), 0U) << "the diamond is stable";
}

TEST_F(P2FluidsWorld, FlowingWaterFallsOffTheFloorEdge) {
    // The floor ends at z = 20; z = 21 lies in loaded chunk (0,1) with open air below.
    ASSERT_TRUE(m_world->setBlock(8, kY, 18, BlockId::Water));
    tickN(kWaterTicks * 4);
    EXPECT_EQ(m_world->getFluid({8, kY, 21}).type, FluidType::Water) << "flows past the edge";
    EXPECT_TRUE(m_world->getFluid({8, kY - 1, 21}).isFalling()) << "and falls";
    EXPECT_TRUE(fluidAt(7, 18).isEmpty()) << "Beta flows only towards the nearest drop";
}

TEST_F(P2FluidsWorld, ReplacingTheSourceRetractsTheFlow) {
    ASSERT_TRUE(m_world->setBlock(8, kY, 8, BlockId::Water));
    tickN(kWaterTicks * 9);
    ASSERT_GT(countFluidOnFloor(FluidType::Water), 100U);

    // The player removes a source by placing a block into it (water is replaceable).
    ASSERT_TRUE(m_world->setBlock(8, kY, 8, BlockId::Cobblestone));
    tickN(kWaterTicks * 30);
    EXPECT_EQ(countFluidOnFloor(FluidType::Water), 0U);
    EXPECT_EQ(m_world->getBlock(9, kY, 8), BlockId::Air);
    EXPECT_EQ(m_world->getMetadata(9, kY, 8), 0);
}

TEST_F(P2FluidsWorld, BreakingAWallLetsWaterThrough) {
    // A glass wall around the source; mining one side opens the flow.
    ASSERT_TRUE(m_world->setBlock(8, kY, 8, BlockId::Water));
    for (const auto [dx, dz] : {std::pair{1, 0}, std::pair{-1, 0}, std::pair{0, 1}, std::pair{0, -1}}) {
        (void)m_world->setBlock(8 + dx, kY, 8 + dz, BlockId::Glass);
    }
    tickN(kWaterTicks * 3);
    ASSERT_EQ(countFluidOnFloor(FluidType::Water), 1U);
    ASSERT_EQ(m_world->getFluidSimulator().pendingUpdates(), 0U);

    ASSERT_TRUE(m_world->setBlock(9, kY, 8, BlockId::Air));
    tickN(kWaterTicks);
    EXPECT_EQ(fluidAt(9, 8), FluidState::fromBetaDecay(FluidType::Water, 1));
}

TEST_F(P2FluidsWorld, LavaPlacedNextToWaterSourceTurnsToObsidianAndFizzes) {
    ASSERT_TRUE(m_world->setBlock(8, kY, 8, BlockId::Lava));
    ASSERT_TRUE(m_world->setBlock(9, kY, 8, BlockId::Water));
    EXPECT_EQ(m_world->getBlock(8, kY, 8), BlockId::Obsidian) << "Beta hardens on the neighbour change";
    const std::vector<FluidEvent> events = m_world->takeFluidEvents();
    EXPECT_TRUE(hasEvent(events, FluidEventKind::Fizz, {8, kY, 8}, BlockId::Obsidian));
    EXPECT_TRUE(m_world->takeFluidEvents().empty()) << "events are drained";
}

TEST_F(P2FluidsWorld, WaterReachingFlowingLavaTurnsItToCobblestone) {
    ASSERT_TRUE(m_world->setBlock(2, kY, 8, BlockId::Lava));
    tickN(kLavaTicks);
    ASSERT_EQ(fluidAt(3, 8), FluidState::fromBetaDecay(FluidType::Lava, 2));

    ASSERT_TRUE(m_world->setBlock(5, kY, 8, BlockId::Water));
    tickN(kWaterTicks);
    EXPECT_EQ(m_world->getBlock(3, kY, 8), BlockId::Cobblestone);
    EXPECT_EQ(fluidAt(2, 8), FluidState::source(FluidType::Lava)) << "the source is not touched by water";
    EXPECT_TRUE(hasEvent(m_world->takeFluidEvents(), FluidEventKind::Fizz, {3, kY, 8}, BlockId::Cobblestone));
}

TEST_F(P2FluidsWorld, LavaFlowsThreeCellsOnTheOverworld) {
    ASSERT_TRUE(m_world->setBlock(8, kY, 8, BlockId::Lava));
    tickN(kLavaTicks * 5);
    EXPECT_EQ(fluidAt(11, 8), FluidState::fromBetaDecay(FluidType::Lava, 6));
    EXPECT_TRUE(fluidAt(12, 8).isEmpty());
    EXPECT_EQ(m_world->getBlockLight(11, kY, 8), 15) << "flowing lava emits light";
}

TEST_F(P2FluidsWorld, WaterWashesAwayAFlowerAndReportsIt) {
    ASSERT_TRUE(m_world->setBlock(10, kY, 8, BlockId::Dandelion));
    ASSERT_TRUE(m_world->setBlock(8, kY, 8, BlockId::Water));
    tickN(kWaterTicks * 2);
    EXPECT_EQ(m_world->getBlock(10, kY, 8), BlockId::Water);
    EXPECT_TRUE(hasEvent(m_world->takeFluidEvents(), FluidEventKind::BlockWashedAway, {10, kY, 8}, BlockId::Dandelion));
}

TEST_F(P2FluidsWorld, FlowWritesMarkOnlyTheTouchedChunksForRemesh) {
    // Source at local x 4 of chunk (1,0); the flow reaches chunk (0,0) after 5 rings.
    ASSERT_TRUE(m_world->setBlock(20, kY, 8, BlockId::Water));
    clearMeshFlags();
    tickN(kWaterTicks);
    EXPECT_TRUE(meshDirty({1, 0}));
    EXPECT_FALSE(meshDirty({0, 0})) << "first ring stays inside chunk (1,0)";
    EXPECT_FALSE(meshDirty({1, 1}));

    clearMeshFlags();
    tickN(kWaterTicks * 5);
    EXPECT_EQ(fluidAt(15, 8).type, FluidType::Water);
    EXPECT_TRUE(meshDirty({0, 0})) << "the flow crossed into chunk (0,0)";
    EXPECT_TRUE(meshDirty({1, 0}));
    EXPECT_FALSE(meshDirty({-1, 0}));
}

TEST_F(P2FluidsWorld, FluidTickStatsAreReported) {
    ASSERT_TRUE(m_world->setBlock(8, kY, 8, BlockId::Water));
    tickN(kWaterTicks - 1);
    const fluids::FluidTickStats stats = m_world->tick();
    EXPECT_EQ(stats.processed, 1U);
    EXPECT_GE(stats.pending, 4U);
}

// --- Chunk streaming ---------------------------------------------------------------

TEST(P2FluidsStreaming, FlowStoppedAtAnUnloadedChunkResumesWhenItLoads) {
    voxel::BlockRegistry::initialize();
    test::TempDir dir;
    world::World w(test::p2worldgen::kSeed, dir.path());
    (void)w.ensureChunkLoaded({0, 0});
    for (int32_t z = 0; z < Chunk::DEPTH; ++z)
        for (int32_t x = 0; x < Chunk::WIDTH; ++x) (void)w.setBlock(x, kFloorY, z, BlockId::Glass);

    ASSERT_TRUE(w.setBlock(12, kY, 8, BlockId::Water));
    for (int32_t i = 0; i < kWaterTicks * 9; ++i) (void)w.tick();
    ASSERT_EQ(w.getFluid({15, kY, 8}), FluidState::fromBetaDecay(FluidType::Water, 3));
    ASSERT_TRUE(w.getFluid({16, kY, 8}).isEmpty()) << "unloaded cells are walls";
    ASSERT_EQ(w.getFluidSimulator().pendingUpdates(), 0U);

    (void)w.ensureChunkLoaded({1, 0});
    for (int32_t i = 0; i < kWaterTicks; ++i) (void)w.tick();
    EXPECT_EQ(w.getFluid({16, kY, 8}).type, FluidType::Water) << "border fluid woke on load";
}

TEST(P2FluidsStreaming, WaterSavedMidFlowResumesAfterReload) {
    voxel::BlockRegistry::initialize();
    test::TempDir dir;
    const auto region = test::p2worldgen::squareRegion({0, 0}, 1);
    {
        world::World w(test::p2worldgen::kSeed, dir.path());
        test::p2worldgen::loadAll(w, region);
        for (int32_t z = -4; z <= 20; ++z)
            for (int32_t x = -4; x <= 20; ++x) (void)w.setBlock(x, kFloorY, z, BlockId::Glass);
        ASSERT_TRUE(w.setBlock(8, kY, 8, BlockId::Water));
        for (int32_t i = 0; i < kWaterTicks * 2; ++i) (void)w.tick();
        ASSERT_EQ(w.getFluid({10, kY, 8}), FluidState::fromBetaDecay(FluidType::Water, 2));
        ASSERT_TRUE(w.getFluid({11, kY, 8}).isEmpty());
        w.saveModifiedChunks(storage::WorldLevelData{});
    }
    world::World w(test::p2worldgen::kSeed, dir.path());
    test::p2worldgen::loadAll(w, region);
    EXPECT_EQ(w.getFluid({10, kY, 8}), FluidState::fromBetaDecay(FluidType::Water, 2)) << "level survives the save";
    EXPECT_GT(w.getFluidSimulator().pendingUpdates(), 0U) << "unsettled water is re-queued on load";
    for (int32_t i = 0; i < kWaterTicks * 6; ++i) (void)w.tick();
    EXPECT_EQ(w.getFluid({15, kY, 8}), FluidState::fromBetaDecay(FluidType::Water, 7));
}

TEST(P2FluidsStreaming, SettledOceanQueuesNoFluidWorkOnLoad) {
    // The open sea around chunk (-5,8): thousands of generated water sources that can't
    // move must not flood the queue (and delay the player's own water behind them).
    voxel::BlockRegistry::initialize();
    test::TempDir dir;
    world::World w(test::p2worldgen::kSeed, dir.path());
    test::p2worldgen::loadAll(w, test::p2worldgen::squareRegion({-5, 8}, 1));
    size_t water = 0;
    for (const auto& [coord, chunk] : w.getChunks()) {
        for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
            for (int32_t z = 0; z < Chunk::DEPTH; ++z)
                for (int32_t x = 0; x < Chunk::WIDTH; ++x)
                    if (voxel::isWaterBlock(chunk->getBlock(x, y, z))) ++water;
    }
    ASSERT_GT(water, 5000U) << "test region must be sea";
    EXPECT_LT(w.getFluidSimulator().pendingUpdates(), water / 100U);
}

TEST(P2FluidsStreaming, UnsettledGeneratedFluidIsScheduledOnLoad) {
    // Worldgen writes fluid without the simulator; the load scan must queue any generated
    // fluid that can move (e.g. cave lava or aquifer water open to a carved pocket).
    voxel::BlockRegistry::initialize();
    test::TempDir dir;
    world::World w(test::p2worldgen::kSeed, dir.path());
    test::p2worldgen::loadAll(w, test::p2worldgen::squareRegion({0, 0}, 2));
    const fluids::FluidSimulator& sim = w.getFluidSimulator();
    size_t unsettled = 0;
    for (const auto& [coord, chunk] : w.getChunks()) {
        for (int32_t y = 1; y < Chunk::HEIGHT; ++y)
            for (int32_t z = 0; z < Chunk::DEPTH; ++z)
                for (int32_t x = 0; x < Chunk::WIDTH; ++x) {
                    const core::BlockPos p{coord.x * Chunk::WIDTH + x, y, coord.z * Chunk::DEPTH + z};
                    if (voxel::BlockRegistry::isLiquid(chunk->getBlock(x, y, z)) && !sim.isSettled(p)) ++unsettled;
                }
    }
    ASSERT_GT(unsettled, 0U) << "seed 173173173 generates movable fluid around spawn";
    EXPECT_GE(sim.pendingUpdates(), unsettled) << "every unsettled generated cell is queued";
    // Whatever was queued settles, within the per-tick budget that keeps a tick a few ms
    // (cave lava touching aquifer water hardens to obsidian, relighting every cell).
    uint64_t ticks = 0;
    while (w.getFluidSimulator().pendingUpdates() > 0 && ticks < 2000) {
        EXPECT_LE(w.tick().processed, 256U);
        ++ticks;
    }
    EXPECT_EQ(w.getFluidSimulator().pendingUpdates(), 0U) << "after " << ticks << " ticks";
}

TEST(P2FluidsStreaming, FluidSeedFollowsTheWorldSeed) {
    voxel::BlockRegistry::initialize();
    test::TempDir dir;
    world::World w(1234ULL, dir.path());
    EXPECT_EQ(w.getFluidSimulator().config().randomSeed, 1234ULL);
    w.setSeed(98765ULL);
    EXPECT_EQ(w.getFluidSimulator().config().randomSeed, 98765ULL);
}
