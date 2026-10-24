// Sprint 5 (S5.2, S5.3) scheduled block updates and falling sand: World::setBlock schedules a
// check 3 ticks later for sand and gravel at and above the edited cell; over air or liquid the
// block becomes an entity (EntitySimulation::tickFallingBlocks) that lands back as a block, or as
// an item where it cannot be placed. Eyes inside an opaque solid block suffocate.
#include "tests/integration/support/GameplaySupport.hpp"

#include "game/FallingBlocks.hpp"
#include "gameplay/ItemIds.hpp"
#include "voxel/ScheduledTicks.hpp"

#include <set>
#include <vector>

using namespace betacraft;
using namespace betacraft::test::p2gameplay;
using game::DamageCause;

namespace {

class FallingSand : public GameplayWorld {
protected:
    // One full game tick as Application runs it: the session, then the world.
    void gameTick() {
        m_lastEvents = m_session.tick(*m_world, m_player, {});
        (void)m_world->tick();
    }

    [[nodiscard]] size_t fallingCount() const { return m_session.entities().count<ecs::FallingBlockComp>(); }

    // Ticks until no falling block is in flight and no update is pending (or `maxTicks`),
    // counting the falling entities that appeared.
    int32_t settle(int32_t maxTicks = 400) {
        int32_t spawned = 0;
        for (int32_t t = 0; t < maxTicks; ++t) {
            gameTick();
            m_session.entities().view<ecs::FallingBlockComp>().each(
                [&](ecs::EntityId, const ecs::FallingBlockComp& f) { spawned += f.ticks == 1 ? 1 : 0; });
            if (fallingCount() == 0 && m_world->getScheduledTicks().empty() && t > 5) break;
        }
        return spawned;
    }

    [[nodiscard]] int32_t droppedCount(BlockId id) {
        int32_t n = 0;
        m_session.entities().view<ecs::ItemDropComp>().each([&](ecs::EntityId, const ecs::ItemDropComp& item) {
            n += item.id == gameplay::toItemId(id) ? item.count : 0;
        });
        return n;
    }

    game::GameTickEvents m_lastEvents{};
};

} // namespace

// --- S5.2 ScheduledTickQueue ---------------------------------------------------------------

TEST(ScheduledBlockTicks, DrainsInDueOrderThenSchedulingOrder) {
    voxel::ScheduledTickQueue q;
    EXPECT_TRUE(q.schedule({0, 1, 0}, BlockId::Sand, 5));
    EXPECT_TRUE(q.schedule({1, 1, 0}, BlockId::Sand, 3));
    EXPECT_TRUE(q.schedule({2, 1, 0}, BlockId::Gravel, 5));
    EXPECT_TRUE(q.schedule({3, 1, 0}, BlockId::Sand, 9));
    std::vector<voxel::ScheduledTickQueue::Entry> out;
    EXPECT_EQ(q.drainDue(2, out), 0U);
    EXPECT_EQ(q.drainDue(5, out), 3U);
    ASSERT_EQ(out.size(), 3U);
    EXPECT_EQ(out[0].pos, (core::BlockPos{1, 1, 0}));
    EXPECT_EQ(out[1].pos, (core::BlockPos{0, 1, 0}));
    EXPECT_EQ(out[2].pos, (core::BlockPos{2, 1, 0}));
    EXPECT_EQ(out[2].block, BlockId::Gravel);
    EXPECT_EQ(q.size(), 1U);
}

TEST(ScheduledBlockTicks, OnePendingEntryPerPosition) {
    voxel::ScheduledTickQueue q;
    EXPECT_TRUE(q.schedule({4, 5, 6}, BlockId::Sand, 10));
    EXPECT_FALSE(q.schedule({4, 5, 6}, BlockId::Gravel, 2)) << "duplicates are ignored";
    EXPECT_TRUE(q.isScheduled({4, 5, 6}));
    std::vector<voxel::ScheduledTickQueue::Entry> out;
    EXPECT_EQ(q.drainDue(9, out), 0U);
    EXPECT_EQ(q.drainDue(10, out), 1U);
    EXPECT_EQ(out[0].block, BlockId::Sand);
    EXPECT_FALSE(q.isScheduled({4, 5, 6}));
    EXPECT_TRUE(q.schedule({4, 5, 6}, BlockId::Sand, 11)) << "free again once drained";
}

TEST(ScheduledBlockTicks, DrainRespectsTheCap) {
    voxel::ScheduledTickQueue q;
    for (int32_t i = 0; i < 10; ++i) ASSERT_TRUE(q.schedule({i, 0, 0}, BlockId::Sand, 1));
    std::vector<voxel::ScheduledTickQueue::Entry> out;
    EXPECT_EQ(q.drainDue(1, out, 4), 4U);
    EXPECT_EQ(q.size(), 6U);
    EXPECT_EQ(out.back().pos.x, 3);
}

TEST_F(FallingSand, WorldTickRunsAtMost512DueUpdates) {
    // Gravel high above the floor, held up by glass, all scheduled for the same tick.
    for (int32_t z = 0; z < 12; ++z) {
        for (int32_t x = 0; x < 12; ++x) {
            for (int32_t y = kY + 2; y < kY + 6; ++y) {
                ASSERT_TRUE(m_world->setBlock(x, y, z, BlockId::Glass));
            }
        }
    }
    int32_t scheduled = 0;
    for (int32_t z = 0; z < 12; ++z) {
        for (int32_t x = 0; x < 12; ++x) {
            for (int32_t y = kY + 2; y < kY + 6; ++y) {
                scheduled += m_world->scheduleBlockTick({x, y, z}, 1) ? 1 : 0;
            }
        }
    }
    ASSERT_EQ(scheduled, 576);
    const uint64_t before = m_world->getTickCount();
    (void)m_world->tick();
    EXPECT_EQ(m_world->getTickCount(), before + 1);
    EXPECT_EQ(m_world->getScheduledTicks().size(), 576U - world::World::kMaxScheduledTicksPerTick);
    (void)m_world->tick();
    EXPECT_TRUE(m_world->getScheduledTicks().empty());
}

TEST_F(FallingSand, SandFallsThreeTicksAfterItsSupportGoes) {
    ASSERT_TRUE(m_world->setBlock(6, kY + 2, 6, BlockId::Stone));
    ASSERT_TRUE(m_world->setBlock(6, kY + 3, 6, BlockId::Sand));
    settle();
    EXPECT_EQ(m_world->getBlock(6, kY + 3, 6), BlockId::Sand) << "supported sand stays";

    ASSERT_TRUE(m_world->setBlock(6, kY + 2, 6, BlockId::Air));
    (void)m_world->tick();
    (void)m_world->tick();
    EXPECT_EQ(m_world->getBlock(6, kY + 3, 6), BlockId::Sand);
    (void)m_world->tick();
    EXPECT_EQ(m_world->getBlock(6, kY + 3, 6), BlockId::Air) << "Beta tickRate 3";
    const std::vector<world::World::FallingBlockSpawn> spawns = m_world->takeFallingBlockSpawns();
    ASSERT_EQ(spawns.size(), 1U);
    EXPECT_EQ(spawns[0].pos, (core::BlockPos{6, kY + 3, 6}));
    EXPECT_EQ(spawns[0].id, BlockId::Sand);
}

TEST_F(FallingSand, ColumnOfThreeSandGivesThreeEntitiesThenThreeStackedBlocks) {
    ASSERT_TRUE(m_world->setBlock(6, kY, 6, BlockId::Stone));
    for (int32_t y = kY + 1; y <= kY + 3; ++y) ASSERT_TRUE(m_world->setBlock(6, y, 6, BlockId::Sand));
    (void)settle();
    ASSERT_EQ(fallingCount(), 0U);

    // Mining the stone: the column comes down onto the glass floor.
    ASSERT_TRUE(m_world->setBlock(6, kY, 6, BlockId::Air));
    EXPECT_EQ(settle(), 3);
    for (int32_t y = kY; y < kY + 3; ++y) EXPECT_EQ(m_world->getBlock(6, y, 6), BlockId::Sand) << y;
    EXPECT_EQ(m_world->getBlock(6, kY + 3, 6), BlockId::Air);
    EXPECT_EQ(droppedCount(BlockId::Sand), 0);
}

TEST_F(FallingSand, GravelFallsThroughWater) {
    for (int32_t y = kY; y <= kY + 1; ++y) ASSERT_TRUE(m_world->setBlockWithMetadata(10, y, 10, BlockId::Water, 0));
    ASSERT_TRUE(m_world->setBlock(10, kY + 2, 10, BlockId::Gravel));
    EXPECT_EQ(settle(), 1);
    EXPECT_EQ(m_world->getBlock(10, kY, 10), BlockId::Gravel) << "it replaces the water at the bottom";
}

TEST_F(FallingSand, SandLandingOnATorchDropsAsAnItem) {
    ASSERT_TRUE(m_world->setBlock(6, kY, 6, BlockId::Torch));
    ASSERT_TRUE(m_world->setBlock(6, kY + 4, 6, BlockId::Sand)); // Placed in mid-air: it falls
    EXPECT_EQ(settle(), 1);
    EXPECT_EQ(m_world->getBlock(6, kY, 6), BlockId::Torch);
    for (int32_t y = kY + 1; y <= kY + 4; ++y) EXPECT_EQ(m_world->getBlock(6, y, 6), BlockId::Air);
    EXPECT_EQ(droppedCount(BlockId::Sand), 1);
}

TEST_F(FallingSand, SandOnATorchFallsWhenTheTorchBreaksWithItsFloor) {
    // Stone, torch, sand: removing the stone breaks the torch (column support check), which
    // must wake the sand resting on it.
    ASSERT_TRUE(m_world->setBlock(6, kY, 6, BlockId::Stone));
    ASSERT_TRUE(m_world->setBlock(6, kY + 1, 6, BlockId::Torch));
    ASSERT_TRUE(m_world->setBlock(6, kY + 2, 6, BlockId::Sand));
    EXPECT_EQ(settle(40), 0) << "resting on the torch";
    ASSERT_TRUE(m_world->setBlock(6, kY, 6, BlockId::Air));
    EXPECT_EQ(m_world->getBlock(6, kY + 1, 6), BlockId::Air) << "the torch broke with its floor";
    EXPECT_EQ(settle(), 1);
    EXPECT_EQ(m_world->getBlock(6, kY + 2, 6), BlockId::Air);
    EXPECT_EQ(m_world->getBlock(6, kY, 6), BlockId::Sand);
}

TEST_F(FallingSand, FloatingGeneratedSandFallsOnlyAfterANeighbourChanges) {
    // Worldgen writes straight into the chunk: no scheduled update.
    voxel::Chunk* chunk = m_world->getChunkMutable({0, 0});
    ASSERT_NE(chunk, nullptr);
    chunk->setBlock(6, kY + 3, 6, BlockId::Sand, false);
    (void)settle(40);
    EXPECT_EQ(m_world->getBlock(6, kY + 3, 6), BlockId::Sand) << "floating generated sand stays put";
    EXPECT_EQ(fallingCount(), 0U);

    // A plant appears under it and is broken again: that edit wakes the sand.
    ASSERT_TRUE(m_world->setBlock(6, kY + 2, 6, BlockId::TallGrass));
    (void)settle(20);
    EXPECT_EQ(m_world->getBlock(6, kY + 3, 6), BlockId::Sand) << "resting on the plant";
    ASSERT_TRUE(m_world->setBlock(6, kY + 2, 6, BlockId::Air));
    EXPECT_EQ(settle(), 1);
    EXPECT_EQ(m_world->getBlock(6, kY + 3, 6), BlockId::Air);
    EXPECT_EQ(m_world->getBlock(6, kY, 6), BlockId::Sand);
}

TEST_F(FallingSand, PlacedSandFallsLikeBeta) {
    hold({gameplay::toItemId(BlockId::Sand), 4, 0});
    physics::RaycastHit target = targetAt({8, kY + 3, 2}, BlockId::Stone);
    ASSERT_TRUE(m_world->setBlock(8, kY + 3, 2, BlockId::Stone));
    ASSERT_EQ(game::placeHeldBlock(*m_world, m_player, target), BlockId::Sand);
    EXPECT_EQ(settle(), 1);
    EXPECT_EQ(m_world->getBlock(7, kY, 2), BlockId::Sand);
}

TEST_F(FallingSand, FallingBlockGivesUpAfterSixHundredTicks) {
    ecs::TransformComp t{};
    t.pos = {0.5f, 200.0f, 0.5f};
    ecs::VelocityComp v{};
    ecs::FallingBlockComp f{BlockId::Sand, game::kFallingBlockMaxTicks};
    EXPECT_EQ(game::resolveFallingBlock(*m_world, t, v, f).outcome, game::FallingBlockOutcome::Falling);
    f.ticks = game::kFallingBlockMaxTicks + 1;
    EXPECT_EQ(game::resolveFallingBlock(*m_world, t, v, f).outcome, game::FallingBlockOutcome::Drop);
    EXPECT_EQ(game::fallingBlockCell({3.5f, 70.0f, -1.5f}), (core::BlockPos{3, 70, -2}));
}

TEST_F(FallingSand, SuffocationHurtsOneEveryTickThroughTheResistanceWindow) {
    const glm::vec3 eye = m_player.getEyePosition();
    const core::BlockPos eyeCell{static_cast<int32_t>(std::floor(eye.x)), static_cast<int32_t>(std::floor(eye.y)),
                                 static_cast<int32_t>(std::floor(eye.z))};
    ASSERT_TRUE(m_world->setBlock(eyeCell, BlockId::Stone));

    const game::GameTickEvents first = tickIdle();
    ASSERT_EQ(first.playerHurt.size(), 1U);
    EXPECT_EQ(first.playerHurt[0].cause, DamageCause::Suffocation);
    EXPECT_EQ(first.playerHurt[0].amount, 1);
    // A 1-damage hit every tick: the 10-tick resistance window lets one through per window.
    tickIdle(39);
    EXPECT_EQ(m_player.getHealth(), 16);

    // Glass is solid but not opaque: breathing again.
    ASSERT_TRUE(m_world->setBlock(eyeCell, BlockId::Glass));
    tickIdle(20);
    EXPECT_EQ(m_player.getHealth(), 16);
}

TEST_F(FallingSand, NoSuffocationInCreative) {
    m_player.setGameMode(core::GameMode::Creative);
    const glm::vec3 eye = m_player.getEyePosition();
    ASSERT_TRUE(m_world->setBlock(static_cast<int32_t>(std::floor(eye.x)), static_cast<int32_t>(std::floor(eye.y)),
                                  static_cast<int32_t>(std::floor(eye.z)), BlockId::Stone));
    EXPECT_TRUE(tickIdle().playerHurt.empty());
}
