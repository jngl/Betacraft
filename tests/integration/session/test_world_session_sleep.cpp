// GL-free WorldSession sleep (spec #9, ticket #14), bed: refused by day and near
// monsters, the night passes after 100 ticks (the session applies the morning and the bed
// spawn), a hit or a lost bed ends the sleep, and a broken bed is no longer a respawn point.

#include "tests/integration/support/WorldSessionSupport.hpp"

#include "ecs/Systems.hpp"
#include "game/ItemUse.hpp"
#include "gameplay/ItemIds.hpp"

#include <gtest/gtest.h>

#include <cmath>

namespace betacraft::test::worldsession {
namespace {

namespace items = gameplay::items;

class WorldSessionSleep : public WorldSessionSupport {
protected:
    static constexpr int32_t kX = 5;
    static constexpr int32_t kZ = 2;
    static constexpr float kNight = 0.75f;
    // One game tick of the fixture's 1200 s day: tick() advances the time of day by it.
    static constexpr float kOneTick = 1.0f / (1200.0f * 20.0f);

    void SetUp() override {
        WorldSessionSupport::SetUp();
        m_session->simulation().setNaturalSpawning(false);
        m_session->setTimeOfDay(kNight);
        // A stone floor around the bed: beds and respawn spots need normal cubes under them.
        for (int32_t z = kZ - 3; z <= kZ + 3; ++z) {
            for (int32_t x = kX - 2; x <= kX + 4; ++x) {
                ASSERT_TRUE(world().setBlock(x, kFloorY, z, BlockId::Stone));
            }
        }
    }

    [[nodiscard]] static physics::RaycastHit topOf(const core::BlockPos& pos, BlockId id) {
        physics::RaycastHit hit{};
        hit.hit = true;
        hit.blockPos = pos;
        hit.adjacentPos = pos.offset(0, 1, 0);
        hit.face = core::FaceDir::PosY;
        hit.blockId = id;
        hit.distance = 2.5f;
        return hit;
    }

    // Looking along +X: foot at (5, kY, 2), head at (6, kY, 2).
    void placeBed() {
        hold({items::Bed, 1});
        const std::optional<game::ItemUseResult> used =
            game::useHeldItemOnBlock(world(), player(), topOf({kX, kFloorY, kZ}, BlockId::Stone));
        ASSERT_TRUE(used.has_value());
        ASSERT_EQ(used->kind, game::ItemUseKind::BedPlaced);
    }

    // Counts the dropped items of `id` (whole stacks) in the session.
    [[nodiscard]] int32_t droppedItems(gameplay::ItemId id) {
        int32_t n = 0;
        m_session->simulation().entities().view<ecs::ItemDropComp>().each(
            [&](ecs::EntityId, const ecs::ItemDropComp& item) { n += item.id == id ? item.count : 0; });
        return n;
    }

    const core::BlockPos m_foot{kX, kY, kZ};
    const core::BlockPos m_head{kX + 1, kY, kZ};
};

TEST_F(WorldSessionSleep, RefusedByDayAndWithAZombieNearby) {
    placeBed();
    m_session->setTimeOfDay(0.25f);
    EXPECT_EQ(m_session->trySleep(m_foot), game::SleepResult::NotNight);
    m_session->setTimeOfDay(0.0f);
    EXPECT_EQ(m_session->trySleep(m_foot), game::SleepResult::NotNight);
    EXPECT_FALSE(m_session->isSleeping());

    m_session->setTimeOfDay(kNight);
    ecs::Registry& entities = m_session->simulation().entities();
    const ecs::EntityId zombie = ecs::EntityFactory::spawnMob(
        entities, {m_head.x + 5.5f, static_cast<float>(kY), m_head.z + 0.5f}, gameplay::MobKind::Zombie);
    EXPECT_EQ(m_session->trySleep(m_head), game::SleepResult::MonstersNearby);
    // A pig does not count, nor a zombie 10 blocks away.
    ecs::EntityFactory::spawnMob(entities, {m_head.x + 2.5f, static_cast<float>(kY), m_head.z + 0.5f},
                                 gameplay::MobKind::Pig);
    entities.get<ecs::TransformComp>(zombie).pos.x += 5.0f;
    EXPECT_EQ(m_session->trySleep(m_head), game::SleepResult::Asleep);
    EXPECT_EQ(m_session->sleepingBed(), std::optional<core::BlockPos>{m_head});
}

TEST_F(WorldSessionSleep, TheNightPassesAndTheBedBecomesTheSpawn) {
    placeBed();
    ASSERT_EQ(m_session->trySleep(m_foot), game::SleepResult::Asleep);
    for (int32_t i = 1; i < game::kSleepTicks; ++i) {
        const game::WorldTickEvents ev = tickIdle();
        ASSERT_FALSE(ev.game.wokeInBed.has_value()) << i;
        ASSERT_TRUE(m_session->isSleeping());
    }
    EXPECT_NEAR(m_session->sleepFade(), 0.99f, 1e-4f) << "the screen has almost faded out";
    const game::WorldTickEvents ev = tickIdle();
    ASSERT_EQ(ev.game.wokeInBed, std::optional<core::BlockPos>{m_head}) << "after 100 ticks";
    EXPECT_FALSE(m_session->isSleeping());
    // The session skips to sunrise (time of day 0) on waking: day again. Beta World.tick then
    // adds the tick's worldTime + 1, as the sleep check comes before it.
    EXPECT_FLOAT_EQ(m_session->timeOfDay(), kOneTick);
    EXPECT_TRUE(game::isDaytime(m_session->timeOfDay()));
    EXPECT_EQ(m_session->bedSpawn(), std::optional<core::BlockPos>{m_head});

    // The spawn point is next to the bed: on the floor, out of the bed's cells.
    const std::optional<glm::vec3> nextToBed = game::bedRespawnPoint(world(), *ev.game.wokeInBed);
    ASSERT_TRUE(nextToBed.has_value());
    const glm::vec3 choice{*nextToBed};
    EXPECT_FLOAT_EQ(choice.y, static_cast<float>(kY));
    EXPECT_LE(std::abs(choice.x - (m_head.x + 0.5f)), 2.0f);
    EXPECT_LE(std::abs(choice.z - (m_head.z + 0.5f)), 1.0f);
    const core::BlockPos cell{static_cast<int32_t>(std::floor(choice.x)), kY,
                              static_cast<int32_t>(std::floor(choice.z))};
    EXPECT_EQ(world().getBlock(cell), BlockId::Air);
    EXPECT_EQ(m_session->spawnPoint(), choice) << "the session's spawn point is that spot";
}

TEST_F(WorldSessionSleep, AHitOrALostBedEndsTheSleepWithoutSkippingTheNight) {
    placeBed();
    // Lava in the player's cell: the hit of the next tick wakes the player up.
    ASSERT_EQ(m_session->trySleep(m_foot), game::SleepResult::Asleep);
    ASSERT_TRUE(world().setBlock(2, kY, 2, BlockId::Lava));
    (void)player().fixedUpdate(1.0f / 60.0f, {}, world());
    ASSERT_TRUE(player().isInLava());
    const game::WorldTickEvents hurt = tickIdle();
    EXPECT_FALSE(hurt.game.playerHurt.empty());
    EXPECT_FALSE(hurt.game.wokeInBed.has_value());
    EXPECT_FALSE(m_session->isSleeping());
    EXPECT_FLOAT_EQ(m_session->timeOfDay(), kNight + kOneTick) << "still night, one tick later";
    ASSERT_TRUE(world().setBlock(2, kY, 2, BlockId::Air));

    // The bed broken under the sleeper.
    ASSERT_EQ(m_session->trySleep(m_foot), game::SleepResult::Asleep);
    ASSERT_TRUE(world().setBlock(m_head, BlockId::Air));
    const game::WorldTickEvents lost = tickIdle();
    EXPECT_FALSE(lost.game.wokeInBed.has_value());
    EXPECT_FALSE(m_session->isSleeping());
    EXPECT_EQ(world().getBlock(m_foot), BlockId::Air) << "the foot went with the head";
}

TEST_F(WorldSessionSleep, ABrokenBedIsNoLongerARespawnPoint) {
    placeBed();
    ASSERT_TRUE(game::bedRespawnPoint(world(), m_head).has_value());

    // Breaking the foot takes the head along and drops a single bed.
    const MineResult r = mine(m_foot);
    ASSERT_TRUE(r.broken.has_value());
    EXPECT_EQ(world().getBlock(m_foot), BlockId::Air);
    EXPECT_EQ(world().getBlock(m_head), BlockId::Air);
    (void)tickIdle(2);
    EXPECT_EQ(droppedItems(items::Bed), 1);

    // WorldSession::respawn then falls back to the world spawn (test_world_session_respawn).
    EXPECT_FALSE(game::bedRespawnPoint(world(), m_head).has_value());
    EXPECT_EQ(m_session->trySleep(m_foot), game::SleepResult::NotABed);
}

} // namespace
} // namespace betacraft::test::worldsession
