// Beta 1.7.3 tick order inside WorldSession::tick (spec #9 "Tick order", ticket #15), after the
// single-player client's Minecraft.runTick: input -> World.updateEntities (entities, then tile
// entities) -> World.tick (weather, sleep, spawning, time, scheduled ticks, random ticks).
// The tests check when an entity exists and when it first moves, not which function ran first.

#include "WorldSessionSupport.hpp"

#include "crafting/SmeltingRecipes.hpp"
#include "gameplay/Crops.hpp"
#include "gameplay/ItemIds.hpp"
#include "voxel/BlockEntity.hpp"
#include "voxel/BlockEntityStore.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <memory>
#include <set>
#include <span>
#include <vector>

namespace betacraft::test::worldsession {
namespace {

class WorldSessionBetaOrder : public WorldSessionSupport {
protected:
    void SetUp() override {
        WorldSessionSupport::SetUp();
        // No mob to die and drop loot in the middle of what the tests count.
        m_session->simulation().setNaturalSpawning(false);
    }

    [[nodiscard]] std::set<ecs::EntityId> itemIds() {
        std::set<ecs::EntityId> ids;
        m_session->simulation().entities().view<ecs::ItemDropComp>().each(
            [&](ecs::EntityId e, const ecs::ItemDropComp&) { ids.insert(e); });
        return ids;
    }

    [[nodiscard]] const ecs::TransformComp& transform(ecs::EntityId e) {
        return m_session->simulation().entities().get<ecs::TransformComp>(e);
    }

    // Item entities not in `before` that stand in one of `cells` (Beta drops a broken block's
    // items inside its cell).
    [[nodiscard]] std::vector<ecs::EntityId> newDropsIn(const std::set<ecs::EntityId>& before,
                                                        std::span<const core::BlockPos> cells) {
        std::vector<ecs::EntityId> out;
        for (const ecs::EntityId e : itemIds()) {
            if (before.contains(e)) continue;
            const glm::vec3 p = transform(e).pos;
            const core::BlockPos cell{static_cast<int32_t>(std::floor(p.x)), static_cast<int32_t>(std::floor(p.y)),
                                      static_cast<int32_t>(std::floor(p.z))};
            for (const core::BlockPos& c : cells) {
                if (c == cell) {
                    out.push_back(e);
                    break;
                }
            }
        }
        return out;
    }

    // Idle ticks until `cells` lost `block` at one of them (at most `maxTicks`); returns the
    // drops that appeared there during that tick.
    std::vector<ecs::EntityId> tickUntilBroken(std::span<const core::BlockPos> cells, BlockId block,
                                               int32_t maxTicks) {
        for (int32_t t = 0; t < maxTicks; ++t) {
            const std::set<ecs::EntityId> before = itemIds();
            (void)tickIdle();
            for (const core::BlockPos& c : cells) {
                if (world().getBlock(c) != block) {
                    return newDropsIn(before, cells);
                }
            }
        }
        ADD_FAILURE() << "nothing broke within " << maxTicks << " ticks";
        return {};
    }

    // Beta: a block broken during World.tick drops an EntityItem at once, after the entities
    // were updated: it exists at the end of the tick, and its first update is the next tick.
    void expectExistUnmovedThenMoveNextTick(const std::vector<ecs::EntityId>& drops, const char* what) {
        ASSERT_FALSE(drops.empty()) << what << ": the drop exists at the end of the tick its block broke in";
        std::vector<glm::vec3> spawned;
        for (const ecs::EntityId e : drops) {
            EXPECT_EQ(transform(e).pos, transform(e).prevPos) << what << ": not moved in the tick it appeared";
            spawned.push_back(transform(e).pos);
        }
        (void)tickIdle();
        for (size_t i = 0; i < drops.size(); ++i) {
            ASSERT_TRUE(m_session->simulation().entities().valid(drops[i])) << what;
            EXPECT_NE(transform(drops[i]).pos, spawned[i]) << what << ": first moves in the next tick";
        }
    }
};

// Beta: a block mined in the input phase drops before updateEntities, which moves it at once.
TEST_F(WorldSessionBetaOrder, AMiningDropMovesInTheTickTheBlockBreaks) {
    const core::BlockPos flower{4, kY, 2};
    ASSERT_TRUE(world().setBlock(flower, BlockId::Dandelion));

    const game::WorldTickEvents ev =
        tick({.attackPressed = true, .attackHeld = true, .target = targetAt(flower, BlockId::Dandelion)});

    ASSERT_TRUE(ev.game.blockBroken.has_value());
    ASSERT_TRUE(ev.game.blockBroken->dropped);
    const ecs::TransformComp& t = transform(ev.game.blockBroken->drop);
    EXPECT_NE(t.pos, t.prevPos) << "the drop moved in the tick it appeared";
}

// Beta: drops from fluid flow, scheduled ticks and random ticks are created during World.tick,
// after the entities: they exist at the end of that tick and first move in the next one.
TEST_F(WorldSessionBetaOrder, AWorldPhaseDropExistsAtTheEndOfItsTickAndFirstMovesInTheNext) {
    // Random tick: ripe crops off farmland break (Beta BlockCrops.updateTick) and drop wheat.
    {
        std::vector<core::BlockPos> crops;
        for (int32_t z = 10; z <= 14; z += 4) {
            for (int32_t x = 0; x <= 14; x += 2) {
                ASSERT_TRUE(world().setBlock(x, kFloorY, z, BlockId::Dirt));
                ASSERT_TRUE(world().setBlockWithMetadata(x, kY, z, BlockId::Crops, gameplay::kCropsRipeStage));
                crops.push_back({x, kY, z});
            }
        }
        expectExistUnmovedThenMoveNextTick(tickUntilBroken(crops, BlockId::Crops, 5000), "random tick");
    }
    // Scheduled tick: sand falls 3 ticks after its support goes, and the torch on it breaks.
    {
        const core::BlockPos support{12, kY, 2};
        const std::array<core::BlockPos, 1> torch{core::BlockPos{support.x, support.y + 2, support.z}};
        ASSERT_TRUE(world().setBlock(support, BlockId::Stone));
        ASSERT_TRUE(world().setBlock(support.offset(0, 1, 0), BlockId::Sand));
        ASSERT_TRUE(world().setBlock(torch[0], BlockId::Torch)); // Standing (meta 0)
        (void)tickIdle(5);
        ASSERT_EQ(world().getBlock(torch[0]), BlockId::Torch);
        ASSERT_TRUE(world().setBlock(support, BlockId::Air));
        const std::vector<ecs::EntityId> drops = tickUntilBroken(torch, BlockId::Torch, 10);
        EXPECT_EQ(m_session->simulation().entities().count<ecs::FallingBlockComp>(), 1U)
            << "the falling sand exists in the tick its support went";
        expectExistUnmovedThenMoveNextTick(drops, "scheduled tick");
    }
    // Fluid: water washes a rose away (last: the water then spreads over the floor).
    {
        const core::BlockPos source{6, kY, 6};
        const std::array<core::BlockPos, 1> rose{core::BlockPos{source.x + 1, source.y, source.z}};
        ASSERT_TRUE(world().setBlock(rose[0], BlockId::Rose));
        ASSERT_TRUE(world().setBlock(source, BlockId::Water));
        expectExistUnmovedThenMoveNextTick(tickUntilBroken(rose, BlockId::Rose, 600), "fluid wash-away");
    }
}

// Beta World.updateEntities updates tile entities after every entity: TNT that goes off next
// to a furnace one tick from smelting blows it up before it smelts, so the ore drops unsmelted.
TEST_F(WorldSessionBetaOrder, EntitiesRunBeforeBlockEntities) {
    const core::BlockPos furnace{12, kY, 10};
    const core::BlockPos tnt{11, kY, 10};
    ASSERT_TRUE(world().setBlock(furnace, BlockId::LitFurnace));
    std::get<voxel::FurnaceData>(world().blockEntities().getOrCreate(furnace, BlockId::LitFurnace)) =
        voxel::FurnaceData{.input = {gameplay::toItemId(BlockId::IronOre), 2, 0},
                           .burnTicks = 100,
                           .burnTicksTotal = 1600,
                           .cookTicks = static_cast<int16_t>(crafting::kSmeltTicks - 1)};
    world().markBlockEntityChanged(furnace);
    ASSERT_TRUE(world().setBlock(tnt, BlockId::TNT));
    ASSERT_NE(m_session->simulation().primeTnt(world(), tnt, 1), ecs::NULL_ENTITY);

    const game::WorldTickEvents ev = tickIdle();
    ASSERT_EQ(ev.game.explosions.size(), 1U);
    ASSERT_EQ(world().getBlock(furnace), BlockId::Air) << "the blast takes the furnace";
    (void)tickIdle();

    int32_t ore = 0;
    int32_t ingots = 0;
    m_session->simulation().entities().view<ecs::ItemDropComp>().each([&](const ecs::ItemDropComp& item) {
        ore += item.id == gameplay::toItemId(BlockId::IronOre) ? item.count : 0;
        ingots += item.id == gameplay::items::IronIngot ? item.count : 0;
    });
    EXPECT_EQ(ore, 2) << "the furnace never got its last tick";
    EXPECT_EQ(ingots, 0);
}

// Beta World.tick: worldTime + 1 each game tick. A day of OpenOptions::dayLengthSeconds at
// 20 TPS; 1 s or less freezes the time.
TEST_F(WorldSessionBetaOrder, TimeOfDayAdvancesOncePerGameTick) {
    m_session->setTimeOfDay(0.3f);
    (void)tickIdle(240);
    EXPECT_NEAR(m_session->timeOfDay(), 0.3f + 240.0f / (1200.0f * 20.0f), 1e-5f);

    std::filesystem::create_directories(m_dir.path() / "frozen");
    game::WorldSession::OpenOptions options = openOptions(m_dir.path() / "frozen");
    options.dayLengthSeconds = 1.0f;
    std::unique_ptr<game::WorldSession> frozen = game::WorldSession::open(options);
    ASSERT_NE(frozen, nullptr);
    frozen->startNewWorld(core::GameMode::Survival);
    frozen->setTimeOfDay(0.3f);
    for (int32_t t = 0; t < 100; ++t) {
        (void)frozen->tick({});
    }
    EXPECT_FLOAT_EQ(frozen->timeOfDay(), 0.3f);
}

// The sky reads the time of day between the last two game ticks, across midnight too.
TEST_F(WorldSessionBetaOrder, TheSkyReadsTheTimeOfDayBetweenTwoTicks) {
    constexpr float kOneTick = 1.0f / (1200.0f * 20.0f);
    m_session->setTimeOfDay(0.3f);
    (void)tickIdle();
    EXPECT_FLOAT_EQ(m_session->timeOfDay(0.0f), 0.3f);
    EXPECT_FLOAT_EQ(m_session->timeOfDay(0.5f), 0.3f + 0.5f * kOneTick);
    EXPECT_FLOAT_EQ(m_session->timeOfDay(1.0f), m_session->timeOfDay());

    m_session->setTimeOfDay(1.0f - 0.5f * kOneTick);
    (void)tickIdle();
    EXPECT_LT(m_session->timeOfDay(), kOneTick) << "past midnight";
    const float midway = m_session->timeOfDay(0.5f);
    EXPECT_LT(std::min(midway, 1.0f - midway), 0.25f * kOneTick) << "midnight, not noon";
}

} // namespace
} // namespace betacraft::test::worldsession
