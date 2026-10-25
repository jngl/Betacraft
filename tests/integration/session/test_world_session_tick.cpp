// GL-free WorldSession game tick (spec #9, ticket #14): one WorldSession::tick runs the input
// stage, the entity simulation and the world (fluids, random ticks, weather, sleep) in today's
// order and returns what happened as WorldTickEvents. Equivalents of the Application e2e tests
// that check drops.

#include "tests/integration/support/WorldSessionSupport.hpp"

#include "crafting/CraftingGrid.hpp"
#include "gameplay/ItemIds.hpp"
#include "gameplay/MobDefs.hpp"
#include "voxel/Block.hpp"
#include "world/Weather.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <optional>
#include <vector>

namespace betacraft::test::worldsession {
namespace {

class WorldSessionTick : public WorldSessionSupport {};

// Press and release inside one 50 ms tick: the tick sees attackPressed without attackHeld and
// still breaks an instant block, which drops as an item entity.
TEST_F(WorldSessionTick, ATapOnAFlowerReleasedWithinOneTickBreaksItAndDropsIt) {
    const core::BlockPos flower{4, kY, 2};
    ASSERT_TRUE(world().setBlock(flower, BlockId::Dandelion));
    const size_t before = itemEntityCount();

    const game::WorldTickEvents ev =
        tick({.attackPressed = true, .attackHeld = false, .target = targetAt(flower, BlockId::Dandelion)});

    ASSERT_TRUE(ev.game.blockBroken.has_value());
    EXPECT_EQ(world().getBlock(flower), BlockId::Air);
    EXPECT_EQ(itemEntityCount(), before + 1);
}

// The world phase runs inside the tick: water spreads (World::tick) and the flower it washes
// away drops as an item entity, reported among the tick's fluid events.
TEST_F(WorldSessionTick, WaterPlacedNextToAFlowerFlowsOverItAndDropsIt) {
    // Away from the player so the drop is not picked up at once, and more than Beta's 4-block
    // flow search from every floor edge so the source spreads in all four directions.
    const core::BlockPos source{6, kY, 6};
    const core::BlockPos flower{source.x + 1, source.y, source.z};
    ASSERT_TRUE(world().setBlock(flower, BlockId::Rose));
    ASSERT_TRUE(world().setBlock(source, BlockId::Water));
    const size_t before = itemEntityCount();

    bool washedAway = false;
    for (int32_t t = 0; t < 600 && !voxel::isWaterBlock(world().getBlock(flower)); ++t) {
        for (const fluids::FluidEvent& ev : tickIdle().fluids) {
            washedAway = washedAway || (ev.kind == fluids::FluidEventKind::BlockWashedAway && ev.pos == flower);
        }
    }

    EXPECT_TRUE(voxel::isWaterBlock(world().getBlock(flower)));
    EXPECT_TRUE(washedAway);
    EXPECT_EQ(itemEntityCount(), before + 1);
}

// Beta onDeath, in the tick the player dies: the grid and the cursor stack go back to the
// inventory, then every stack scatters, even with the inventory screen open.
TEST_F(WorldSessionTick, TheTickThePlayerDiesScattersTheGridTheCursorAndTheInventory) {
    player().setInventorySlot(0, {BlockId::OakLog, 3});
    player().setInventorySlot(4, {BlockId::Cobblestone, 64});
    player().setInventorySlot(9, {BlockId::Torch, 17});
    (void)m_session->crafting().open(crafting::GridSize::Inventory2x2, player());
    ASSERT_TRUE(m_session->crafting().clickInventorySlot(player(), 0)); // 3 logs on the cursor
    ASSERT_TRUE(m_session->crafting().clickGridSlot(0, game::SlotButton::Right)); // 1 into the grid
    ASSERT_EQ(m_session->crafting().pendingStacks().size(), 2U);
    const size_t before = itemEntityCount();

    player().setPosition({2.5f, -70.0f, 2.5f});
    game::WorldTickEvents death{};
    for (int32_t i = 0; i < 200 && !death.game.playerDied; ++i) {
        death = tickIdle();
    }

    ASSERT_TRUE(death.game.playerDied);
    EXPECT_TRUE(m_session->crafting().pendingStacks().empty()) << "grid and cursor emptied";
    for (size_t i = 0; i < physics::Player::TOTAL_INVENTORY_SIZE; ++i) {
        EXPECT_TRUE(player().getInventorySlot(i).isEmpty()) << "slot " << i;
    }
    int32_t logs = 0;
    int32_t total = 0;
    m_session->simulation().entities().view<ecs::ItemDropComp>().each([&](const ecs::ItemDropComp& item) {
        logs += item.id == gameplay::toItemId(BlockId::OakLog) ? item.count : 0;
        total += item.count;
    });
    EXPECT_EQ(logs, 3) << "the grid and cursor logs scatter too";
    EXPECT_EQ(total, 3 + 64 + 17);
    EXPECT_GE(itemEntityCount(), before + 3);
    EXPECT_FALSE(m_session->simulation().miningTarget().has_value());
}

// The lightning roll is part of the tick: in a lasting thunderstorm a strike comes back
// in the events, within 64 blocks of the player, on top of a rainy column.
TEST_F(WorldSessionTick, AThunderstormReturnsLightningStrikes) {
    m_session->setWeather(storage::WeatherState{.raining = true,
                                                .thundering = true,
                                                .rainTime = 100000,
                                                .thunderTime = 100000,
                                                .rainStrength = 1.0f,
                                                .prevRainStrength = 1.0f,
                                                .thunderStrength = 1.0f,
                                                .prevThunderStrength = 1.0f});
    std::optional<glm::vec3> strike;
    for (int32_t t = 0; t < 4000 && !strike; ++t) {
        strike = tickIdle().lightningStrike;
    }

    ASSERT_TRUE(strike.has_value());
    EXPECT_TRUE(m_session->weather().thundering) << "the storm lasts";
    const glm::vec3 feet = player().getPosition();
    EXPECT_LE(std::abs(strike->x - feet.x), 66.0f);
    EXPECT_LE(std::abs(strike->z - feet.z), 66.0f);
    const auto x = static_cast<int32_t>(std::floor(strike->x));
    const auto z = static_cast<int32_t>(std::floor(strike->z));
    EXPECT_FLOAT_EQ(strike->y, static_cast<float>(world::precipitationHeight(world(), x, z)));
    EXPECT_EQ(world::precipitationAt(world().getBiomeAt(x, z)), world::Precipitation::Rain);
}

// Spec #9 user story 10: natural spawning runs in the tick with the session's weather. At
// noon a clear sky keeps hostiles off the sunlit surface; a thunderstorm lets them spawn there
// (Beta reads their light with skylightSubtracted 10). Hostiles in caves do not count.
TEST_F(WorldSessionTick, ThunderLetsHostilesSpawnOnTheSunlitSurfaceAtNoon) {
    const auto sunlitHostilesWithin = [&](const storage::WeatherState& weather, int32_t ticks) {
        ecs::Registry& entities = m_session->simulation().entities();
        std::vector<ecs::EntityId> mobs;
        entities.view<ecs::MobKindComp>().each([&](ecs::EntityId e, const ecs::MobKindComp&) {
            if (!entities.has<ecs::PlayerTagComp>(e)) {
                mobs.push_back(e);
            }
        });
        for (const ecs::EntityId e : mobs) {
            (void)entities.destroy(e);
        }
        m_session->setWeather(weather);
        m_session->setTimeOfDay(0.25f); // Noon
        int32_t sunlit = 0;
        for (int32_t t = 0; t < ticks && sunlit == 0; ++t) {
            (void)tickIdle();
            entities.view<ecs::TransformComp, ecs::MobKindComp>().each(
                [&](ecs::EntityId e, const ecs::TransformComp& transform, const ecs::MobKindComp& mob) {
                    if (entities.has<ecs::PlayerTagComp>(e) ||
                        gameplay::mobDef(mob.kind).category != gameplay::MobCategory::Hostile) {
                        return;
                    }
                    const core::BlockPos cell{static_cast<int32_t>(std::floor(transform.pos.x)),
                                              static_cast<int32_t>(std::floor(transform.pos.y)),
                                              static_cast<int32_t>(std::floor(transform.pos.z))};
                    sunlit += world().getSkyLight(cell.x, cell.y, cell.z) == 15 ? 1 : 0;
                });
        }
        return sunlit;
    };

    // Long timers: neither weather changes during the run.
    EXPECT_EQ(sunlitHostilesWithin(storage::WeatherState{.rainTime = 100000, .thunderTime = 100000}, 1500), 0);
    EXPECT_GT(sunlitHostilesWithin(storage::WeatherState{.raining = true,
                                                         .thundering = true,
                                                         .rainTime = 100000,
                                                         .thunderTime = 100000,
                                                         .rainStrength = 1.0f,
                                                         .prevRainStrength = 1.0f,
                                                         .thunderStrength = 1.0f,
                                                         .prevThunderStrength = 1.0f},
                                   1500),
              0);
}

// Clear weather never strikes, and the weather advances once per tick.
TEST_F(WorldSessionTick, ClearWeatherNeverStrikesAndTheWeatherAdvancesEachTick) {
    m_session->setWeather(storage::WeatherState{.rainTime = 1000});
    for (int32_t t = 0; t < 500; ++t) {
        ASSERT_FALSE(tickIdle().lightningStrike.has_value()) << t;
    }
    EXPECT_EQ(m_session->weather().rainTime, 500);
    EXPECT_FALSE(m_session->weather().raining);
}

} // namespace
} // namespace betacraft::test::worldsession
