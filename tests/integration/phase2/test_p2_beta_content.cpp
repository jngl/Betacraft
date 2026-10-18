// Sprint 6 integration (Beta content) through the GL-free game::EntitySimulation and a real World:
// S6.4 buckets (Raycaster LiquidSources, game::useHeldBucket), S6.6 explosions and TNT
// (EntitySimulation::explode, primed TNT, flint and steel, chain reactions, the 4-per-tick queue),
// S6.2 arrows and S6.3 skeletons, spiders, creepers and cows in a running session, S6.7 armor
// (every hit but the void), S6.10 dungeon chests and spawners.
#include "P2GameplaySupport.hpp"

#include "crafting/BetaRecipes.hpp"
#include "ecs/Systems.hpp"
#include "game/ItemBridge.hpp"
#include "game/ItemUse.hpp"
#include "game/MobBehaviour.hpp"
#include "game/Projectiles.hpp"
#include "gameplay/Armor.hpp"
#include "gameplay/Explosion.hpp"
#include "gameplay/ItemIds.hpp"
#include "physics/Raycaster.hpp"
#include "worldgen/Dungeons.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <map>
#include <variant>

using namespace betacraft;
using namespace betacraft::test::p2gameplay;
using game::DamageCause;
namespace items = gameplay::items;

namespace {

// The player stands at (2.5, kY, 2.5) looking along +X; the eyes are in cell row kY + 1.
constexpr int32_t kEyeY = kY + 1;

class S6BetaContent : public GameplayWorld {
protected:
    // One full game tick as Application runs it: the session, then the world.
    game::GameTickEvents gameTick() {
        game::GameTickEvents ev = m_session.tick(*m_world, m_player, {});
        (void)m_world->tick();
        return ev;
    }

    // A game tick at midnight (SkyRenderer time of day 0.75): hostiles hunt, nothing burns.
    game::GameTickEvents nightTick() {
        game::GameTickEvents ev = m_session.tick(*m_world, m_player, {}, 0.75f);
        (void)m_world->tick();
        return ev;
    }

    ecs::EntityId spawnHunting(gameplay::MobKind kind, const glm::vec3& feet) {
        m_session.setNaturalSpawning(false);
        const ecs::EntityId e = ecs::EntityFactory::spawnMob(m_session.entities(), feet, kind);
        (void)tickIdle(); // Creates the player's proxy
        auto& ai = m_session.entities().get<ecs::MobAIComp>(e);
        ai.state = ecs::MobAIState::Chase;
        ai.target = m_session.playerProxy();
        return e;
    }

    [[nodiscard]] size_t primedCount() const { return m_session.entities().count<ecs::PrimedTntComp>(); }

    [[nodiscard]] int32_t droppedCount(gameplay::ItemId id) {
        int32_t n = 0;
        m_session.entities().view<ecs::ItemDropComp>().each([&](ecs::EntityId, const ecs::ItemDropComp& item) {
            n += item.id == id ? item.count : 0;
        });
        return n;
    }

    void clearItems() {
        std::vector<ecs::EntityId> items;
        m_session.entities().view<ecs::ItemDropComp>().each(
            [&](ecs::EntityId e, const ecs::ItemDropComp&) { items.push_back(e); });
        for (const ecs::EntityId e : items) (void)m_session.entities().destroy(e);
    }

    void fill(const core::BlockPos& lo, const core::BlockPos& hi, BlockId id) {
        for (int32_t x = lo.x; x <= hi.x; ++x)
            for (int32_t y = lo.y; y <= hi.y; ++y)
                for (int32_t z = lo.z; z <= hi.z; ++z) ASSERT_TRUE(m_world->setBlock(x, y, z, id) || m_world->getBlock(x, y, z) == id);
    }

    [[nodiscard]] int32_t countBlocks(const core::BlockPos& lo, const core::BlockPos& hi, BlockId id) const {
        int32_t n = 0;
        for (int32_t x = lo.x; x <= hi.x; ++x)
            for (int32_t y = lo.y; y <= hi.y; ++y)
                for (int32_t z = lo.z; z <= hi.z; ++z) n += m_world->getBlock(x, y, z) == id ? 1 : 0;
        return n;
    }
};

} // namespace

// --- S6.4 buckets --------------------------------------------------------------------------

TEST_F(S6BetaContent, LiquidSourceRaycastModeHitsSourcesOnly) {
    ASSERT_TRUE(m_world->setBlockWithMetadata(5, kEyeY, 2, BlockId::Water, 0));
    ASSERT_TRUE(m_world->setBlock(8, kEyeY, 2, BlockId::Stone));
    const glm::vec3 eye = m_player.getEyePosition();
    const glm::vec3 look(1.0f, 0.0f, 0.0f);
    // The crosshair ray goes through the water.
    const physics::RaycastHit solid = physics::Raycaster::cast(*m_world, eye, look, 8.0f);
    ASSERT_TRUE(solid.hit);
    EXPECT_EQ(solid.blockPos, (core::BlockPos{8, kEyeY, 2}));
    EXPECT_EQ(physics::Raycaster::cast(*m_world, eye, look, 8.0f, physics::RaycastMode::Solid).blockPos,
              solid.blockPos);
    const physics::RaycastHit source = physics::Raycaster::cast(*m_world, eye, look, 8.0f, physics::RaycastMode::LiquidSources);
    ASSERT_TRUE(source.hit);
    EXPECT_EQ(source.blockPos, (core::BlockPos{5, kEyeY, 2}));
    EXPECT_EQ(source.adjacentPos, (core::BlockPos{4, kEyeY, 2}));
    EXPECT_EQ(source.face, core::FaceDir::NegX);
    // Flowing water (metadata 1..7) is not a source.
    ASSERT_TRUE(m_world->setMetadata(5, kEyeY, 2, 3));
    EXPECT_EQ(physics::Raycaster::cast(*m_world, eye, look, 8.0f, physics::RaycastMode::LiquidSources).blockPos,
              solid.blockPos);
}

TEST_F(S6BetaContent, EmptyBucketTakesWaterAndLavaSources) {
    hold({items::Bucket, 1, 0});
    ASSERT_TRUE(m_world->setBlockWithMetadata(4, kEyeY, 2, BlockId::StillWater, 0));
    const std::optional<game::ItemUseResult> water = game::useHeldBucket(*m_world, m_player);
    ASSERT_TRUE(water.has_value());
    EXPECT_EQ(water->kind, game::ItemUseKind::BucketFilled);
    EXPECT_EQ(water->block, BlockId::Water);
    EXPECT_EQ(m_world->getBlock(4, kEyeY, 2), BlockId::Air);
    EXPECT_EQ(m_player.getHeldItem().id, items::WaterBucket);
    EXPECT_EQ(m_player.getHeldItem().count, 1);

    hold({items::Bucket, 1, 0});
    ASSERT_TRUE(m_world->setBlockWithMetadata(4, kEyeY, 2, BlockId::Lava, 0));
    // Step 3 (a block under the crosshair) forwards to the bucket's own ray.
    const std::optional<game::ItemUseResult> lava =
        game::useHeldItemOnBlock(*m_world, m_player, targetAt({4, kY - 1, 2}, BlockId::Glass));
    ASSERT_TRUE(lava.has_value());
    EXPECT_EQ(lava->block, BlockId::Lava);
    EXPECT_EQ(m_world->getBlock(4, kEyeY, 2), BlockId::Air);
    EXPECT_EQ(m_player.getHeldItem().id, items::LavaBucket);
}

TEST_F(S6BetaContent, EmptyBucketIgnoresFlowingLiquidAndBlocks) {
    hold({items::Bucket, 1, 0});
    ASSERT_TRUE(m_world->setBlockWithMetadata(4, kEyeY, 2, BlockId::Water, 2));
    EXPECT_FALSE(game::useHeldBucket(*m_world, m_player).has_value());
    EXPECT_EQ(m_world->getBlock(4, kEyeY, 2), BlockId::Water);
    ASSERT_TRUE(m_world->setBlock(4, kEyeY, 2, BlockId::Stone));
    EXPECT_FALSE(game::useHeldBucket(*m_world, m_player).has_value());
    EXPECT_EQ(m_player.getHeldItem().id, items::Bucket);
    // Out of reach (5 blocks from the eyes).
    ASSERT_TRUE(m_world->setBlock(4, kEyeY, 2, BlockId::Air));
    ASSERT_TRUE(m_world->setBlockWithMetadata(9, kEyeY, 2, BlockId::Water, 0));
    EXPECT_FALSE(game::useHeldBucket(*m_world, m_player).has_value());
}

TEST_F(S6BetaContent, FullBucketPoursASourceThatFlows) {
    hold({items::WaterBucket, 1, 0});
    ASSERT_TRUE(m_world->setBlock(5, kEyeY, 2, BlockId::Stone));
    const std::optional<game::ItemUseResult> poured = game::useHeldBucket(*m_world, m_player);
    ASSERT_TRUE(poured.has_value());
    EXPECT_EQ(poured->kind, game::ItemUseKind::BucketEmptied);
    EXPECT_EQ(poured->pos, (core::BlockPos{4, kEyeY, 2}));
    EXPECT_EQ(m_world->getBlock(4, kEyeY, 2), BlockId::Water);
    EXPECT_EQ(m_world->getMetadata(4, kEyeY, 2), 0);
    EXPECT_EQ(m_player.getHeldItem().id, items::Bucket);
    // The simulation woke up: the source spills down onto the floor.
    for (int i = 0; i < 40; ++i) (void)m_world->tick();
    EXPECT_TRUE(world::isWaterBlock(m_world->getBlock(4, kY, 2)));
    EXPECT_TRUE(world::isWaterBlock(m_world->getBlock(4, kEyeY, 2)));
}

TEST_F(S6BetaContent, FullBucketSeesThroughLiquidsAndNeedsAReplaceableCell) {
    hold({items::LavaBucket, 1, 0});
    // Water in front of the wall: the full bucket's ray ignores it and pours into it.
    ASSERT_TRUE(m_world->setBlock(6, kEyeY, 2, BlockId::Stone));
    ASSERT_TRUE(m_world->setBlockWithMetadata(5, kEyeY, 2, BlockId::Water, 0));
    const std::optional<game::ItemUseResult> poured = game::useHeldBucket(*m_world, m_player);
    ASSERT_TRUE(poured.has_value());
    EXPECT_EQ(poured->pos, (core::BlockPos{5, kEyeY, 2}));
    EXPECT_TRUE(world::isLavaBlock(m_world->getBlock(5, kEyeY, 2)));

    // Pouring into a source of the same liquid changes nothing and keeps the bucket full.
    hold({items::LavaBucket, 1, 0});
    EXPECT_FALSE(game::useHeldBucket(*m_world, m_player).has_value());
    EXPECT_EQ(m_player.getHeldItem().id, items::LavaBucket);
    // Nothing within reach: nothing happens either.
    ASSERT_TRUE(m_world->setBlock(5, kEyeY, 2, BlockId::Air));
    ASSERT_TRUE(m_world->setBlock(6, kEyeY, 2, BlockId::Air));
    EXPECT_FALSE(game::useHeldBucket(*m_world, m_player).has_value());
}

TEST_F(S6BetaContent, BucketRecipeAndStackOfOne) {
    const crafting::RecipeRegistry book = crafting::makeBetaRecipeBook();
    crafting::CraftingGrid grid{crafting::GridSize::Workbench3x3};
    ASSERT_TRUE(grid.set(0, 0, crafting::SlotStack{items::IronIngot, 1, 0}));
    ASSERT_TRUE(grid.set(2, 0, crafting::SlotStack{items::IronIngot, 1, 0}));
    ASSERT_TRUE(grid.set(1, 1, crafting::SlotStack{items::IronIngot, 1, 0}));
    const auto match = book.match(grid);
    ASSERT_TRUE(match.has_value());
    EXPECT_EQ(match->result.id, items::Bucket);
    EXPECT_EQ(match->result.count, 1);
    for (const gameplay::ItemId id : {items::Bucket, items::WaterBucket, items::LavaBucket, items::MilkBucket}) {
        EXPECT_EQ(gameplay::maxStackSize(id), 1) << id;
    }
}

// --- S6.6 explosions and TNT ---------------------------------------------------------------

TEST_F(S6BetaContent, FlintAndSteelRecipeAndTntRecipe) {
    const crafting::RecipeRegistry book = crafting::makeBetaRecipeBook();
    crafting::CraftingGrid small{crafting::GridSize::Inventory2x2};
    ASSERT_TRUE(small.set(1, 1, crafting::SlotStack{items::IronIngot, 1, 0}));
    ASSERT_TRUE(small.set(0, 0, crafting::SlotStack{items::Flint, 1, 0}));
    const auto flintAndSteel = book.match(small);
    ASSERT_TRUE(flintAndSteel.has_value());
    EXPECT_EQ(flintAndSteel->result.id, items::FlintAndSteel);

    crafting::CraftingGrid grid{crafting::GridSize::Workbench3x3};
    for (int y = 0; y < 3; ++y)
        for (int x = 0; x < 3; ++x)
            ASSERT_TRUE(grid.set(x, y, crafting::SlotStack{(x + y) % 2 == 0 ? items::Gunpowder : gameplay::toItemId(BlockId::Sand), 1, 0}));
    const auto tnt = book.match(grid);
    ASSERT_TRUE(tnt.has_value());
    EXPECT_EQ(tnt->result.id, gameplay::toItemId(BlockId::TNT));
}

TEST_F(S6BetaContent, BreakingTntInSurvivalLightsAFourSecondFuse) {
    ASSERT_TRUE(m_world->setBlock(9, kY, 9, BlockId::TNT));
    placePlayer({-4.5f, static_cast<float>(kY), -4.5f});
    const MineResult r = mine({9, kY, 9});
    ASSERT_TRUE(r.broken.has_value());
    EXPECT_EQ(r.ticks, 1) << "TNT breaks on the click";
    EXPECT_FALSE(r.broken->dropped);
    EXPECT_EQ(m_world->getBlock(9, kY, 9), BlockId::Air);
    EXPECT_EQ(droppedCount(gameplay::toItemId(BlockId::TNT)), 0);
    ASSERT_EQ(primedCount(), 1U);
    // mine() ran the click tick and one idle tick.
    int32_t fuse = 0;
    m_session.entities().view<ecs::PrimedTntComp>().each(
        [&](ecs::EntityId, const ecs::PrimedTntComp& tnt) { fuse = tnt.fuse; });
    EXPECT_EQ(fuse, ecs::PrimedTntComp::DEFAULT_FUSE_TICKS - 2);
    // Thrown up at 4 b/s: it hops off the floor before settling.
    int32_t ticks = 2;
    size_t explosions = 0;
    while (primedCount() > 0 && ticks < 200) {
        explosions += gameTick().explosions.size();
        ++ticks;
    }
    EXPECT_EQ(ticks, ecs::PrimedTntComp::DEFAULT_FUSE_TICKS);
    EXPECT_EQ(explosions, 1U);
}

TEST_F(S6BetaContent, CreativeBreaksTntWithoutLightingIt) {
    m_player.setGameMode(core::GameMode::Creative);
    ASSERT_TRUE(m_world->setBlock(4, kY, 2, BlockId::TNT));
    const MineResult r = mine({4, kY, 2});
    ASSERT_TRUE(r.broken.has_value());
    EXPECT_EQ(primedCount(), 0U);
}

TEST_F(S6BetaContent, FlintAndSteelLightsTntAndWears) {
    ASSERT_TRUE(m_world->setBlock(4, kY, 2, BlockId::TNT));
    hold({items::FlintAndSteel, 1, 0});
    // Not TNT: nothing.
    EXPECT_FALSE(m_session.igniteTnt(*m_world, m_player, targetAt({4, kY - 1, 2}, BlockId::Glass)).has_value());
    const auto lit = m_session.igniteTnt(*m_world, m_player, targetAt({4, kY, 2}, BlockId::TNT));
    ASSERT_TRUE(lit.has_value());
    EXPECT_EQ(m_world->getBlock(4, kY, 2), BlockId::Air);
    EXPECT_EQ(m_session.entities().get<ecs::PrimedTntComp>(*lit).fuse, ecs::PrimedTntComp::DEFAULT_FUSE_TICKS);
    EXPECT_FLOAT_EQ(m_session.entities().get<ecs::VelocityComp>(*lit).linear.y, game::kPrimedTntLaunchSpeed);
    EXPECT_EQ(m_player.getHeldItem().damage, 1);
    // The 65th use breaks it.
    ASSERT_TRUE(m_world->setBlock(5, kY, 2, BlockId::TNT));
    hold({items::FlintAndSteel, 1, game::kFlintAndSteelMaxDamage});
    ASSERT_TRUE(m_session.igniteTnt(*m_world, m_player, targetAt({5, kY, 2}, BlockId::TNT)).has_value());
    EXPECT_TRUE(m_player.getHeldItem().isEmpty());
    // Other items do not light it.
    ASSERT_TRUE(m_world->setBlock(6, kY, 2, BlockId::TNT));
    hold({items::Stick, 1, 0});
    EXPECT_FALSE(m_session.igniteTnt(*m_world, m_player, targetAt({6, kY, 2}, BlockId::TNT)).has_value());
}

TEST_F(S6BetaContent, FlintAndSteelKeepsItsWearWhenUsedForOtherThings) {
    // Digging or hitting with it does not wear it (it is no tool), nor does it repair it.
    hold({items::FlintAndSteel, 1, 10});
    ASSERT_TRUE(m_world->setBlock(4, kY, 2, BlockId::Dirt));
    ASSERT_TRUE(mine({4, kY, 2}).broken.has_value());
    EXPECT_EQ(m_player.getHeldItem(), (physics::InventorySlot{items::FlintAndSteel, 1, 10}));
    EXPECT_EQ(game::toItemStack(m_player.getHeldItem()).maxDamage, game::kFlintAndSteelMaxDamage);
    // Thrown and picked up again, it is the same worn item.
    const ecs::EntityId thrown = m_session.dropHeldItem(m_player, true);
    ASSERT_NE(thrown, ecs::NULL_ENTITY);
    EXPECT_EQ(m_session.entities().get<ecs::ItemDropComp>(thrown).damage, 10);
}

TEST_F(S6BetaContent, ExplosionCarvesACraterAndDropsSomeBlocks) {
    // A 9 x 9 x 9 dirt cube centred on (10, kY - 2, 10), an obsidian pillar through it.
    const core::BlockPos lo{6, kY - 6, 6};
    const core::BlockPos hi{14, kY + 2, 14};
    fill(lo, hi, BlockId::Dirt);
    fill({12, kY - 6, 10}, {12, kY + 2, 10}, BlockId::Obsidian);
    ASSERT_TRUE(m_world->setBlock(10, kY - 2, 10, BlockId::Air));
    placePlayer({-4.5f, static_cast<float>(kY), -4.5f});
    const int32_t before = countBlocks(lo, hi, BlockId::Dirt);
    game::GameTickEvents events{};
    m_session.explode(*m_world, m_player, {10.5f, kY - 1.5f, 10.5f}, game::kTntPower, ecs::NULL_ENTITY, events);
    const int32_t destroyed = before - countBlocks(lo, hi, BlockId::Dirt);
    ASSERT_EQ(events.explosions.size(), 1U);
    EXPECT_EQ(events.explosions[0].blocksDestroyed, static_cast<size_t>(destroyed));
    EXPECT_GT(destroyed, 60);
    EXPECT_EQ(countBlocks({12, kY - 6, 10}, {12, kY + 2, 10}, BlockId::Obsidian), 9);
    // 3 drops in 10 (dirt drops dirt).
    const int32_t drops = droppedCount(gameplay::toItemId(BlockId::Dirt));
    EXPECT_GT(drops, destroyed * 15 / 100);
    EXPECT_LT(drops, destroyed * 45 / 100);
    EXPECT_TRUE(events.playerHurt.empty()) << "the player is far away";
}

TEST_F(S6BetaContent, ExplosionDropsADoorAtMostOnce) {
    // Explode next to a door many times: never two door items.
    int32_t droppedOnce = 0;
    for (int trial = 0; trial < 30; ++trial) {
        clearItems();
        ASSERT_TRUE(m_world->setBlock(8, kY - 1, 8, BlockId::Stone));
        ASSERT_TRUE(m_world->setBlockWithMetadata(8, kY, 8, BlockId::WoodDoor, 0));
        ASSERT_TRUE(m_world->setBlockWithMetadata(8, kY + 1, 8, BlockId::WoodDoor, world::kDoorUpperBit));
        m_session.explode(*m_world, m_player, {9.5f, kY + 0.5f, 8.5f}, game::kTntPower, ecs::NULL_ENTITY);
        // The stone under it may go first: the door then breaks for lack of support and drops
        // on the next tick, while the blast skips its now empty cells.
        (void)tickIdle();
        EXPECT_EQ(m_world->getBlock(8, kY, 8), BlockId::Air);
        EXPECT_EQ(m_world->getBlock(8, kY + 1, 8), BlockId::Air);
        const int32_t doors = droppedCount(items::WoodenDoor);
        EXPECT_LE(doors, 1) << trial;
        droppedOnce += doors;
        // Repair the floor for the next trial.
        for (int32_t z = 5; z <= 12; ++z)
            for (int32_t x = 5; x <= 13; ++x)
                for (int32_t y = kFloorY - 2; y <= kFloorY; ++y) (void)m_world->setBlock(x, y, z, y == kFloorY ? BlockId::Glass : BlockId::Air);
    }
    EXPECT_GT(droppedOnce, 0);
}

TEST_F(S6BetaContent, ExplosionHurtsAndPushesThePlayerAndMobsAndDestroysItems) {
    const glm::vec3 feet = m_player.getPosition();
    const glm::vec3 centre = feet + glm::vec3(-5.0f, 0.5f, 0.0f);
    const ecs::EntityId zombie =
        ecs::EntityFactory::spawnMob(m_session.entities(), feet + glm::vec3(0.0f, 0.0f, 3.0f), gameplay::MobKind::Zombie);
    const ecs::EntityId item = m_session.spawnItem(gameplay::makeStack(items::Stick, 1), centre + glm::vec3(0.0f, 0.0f, -2.0f));
    const ecs::EntityId farItem = m_session.spawnItem(gameplay::makeStack(items::Stick, 1), centre + glm::vec3(-9.0f, 0.0f, 0.0f));
    const int32_t health = m_player.getHealth();
    const float vx = m_player.getVelocity().x;

    m_session.explode(*m_world, m_player, centre, game::kTntPower, ecs::NULL_ENTITY);
    // Outside tick(), the hits reach the next tick's events.
    const game::GameTickEvents ev = tickIdle();
    ASSERT_EQ(ev.explosions.size(), 1U);
    ASSERT_FALSE(ev.playerHurt.empty());
    EXPECT_EQ(ev.playerHurt[0].cause, DamageCause::Explosion);
    const int32_t expected =
        gameplay::explosionDamage(glm::length(feet - centre), game::kTntPower, 1.0f);
    EXPECT_EQ(health - m_player.getHealth(), std::min(expected, health));
    EXPECT_GT(m_player.getVelocity().x, vx) << "pushed away from the blast (+X)";

    ASSERT_TRUE(m_session.entities().valid(zombie));
    EXPECT_LT(m_session.entities().get<ecs::HealthComp>(zombie).current, gameplay::mobDef(gameplay::MobKind::Zombie).maxHealth);
    EXPECT_FALSE(m_session.entities().valid(item));
    EXPECT_TRUE(m_session.entities().valid(farItem));
}

TEST_F(S6BetaContent, CreativePlayerIsNotHurtByExplosions) {
    m_player.setGameMode(core::GameMode::Creative);
    game::GameTickEvents events{};
    m_session.explode(*m_world, m_player, m_player.getPosition() + glm::vec3(1.0f, 0.5f, 0.0f), game::kTntPower,
                      ecs::NULL_ENTITY, events);
    EXPECT_TRUE(events.playerHurt.empty());
    EXPECT_EQ(m_player.getHealth(), m_player.health().max);
}

TEST_F(S6BetaContent, TntCaughtInABlastGetsAShortFuse) {
    ASSERT_TRUE(m_world->setBlock(9, kY, 9, BlockId::TNT));
    ASSERT_TRUE(m_world->setBlock(9, kY, 11, BlockId::TNT));
    placePlayer({-4.5f, static_cast<float>(kY), -4.5f});
    game::GameTickEvents events{};
    m_session.explode(*m_world, m_player, {9.5f, kY + 0.5f, 10.5f}, game::kTntPower, ecs::NULL_ENTITY, events);
    EXPECT_EQ(m_world->getBlock(9, kY, 9), BlockId::Air);
    EXPECT_EQ(m_world->getBlock(9, kY, 11), BlockId::Air);
    EXPECT_EQ(droppedCount(gameplay::toItemId(BlockId::TNT)), 0);
    ASSERT_EQ(primedCount(), 2U);
    m_session.entities().view<ecs::PrimedTntComp>().each([&](ecs::EntityId, const ecs::PrimedTntComp& tnt) {
        EXPECT_GE(tnt.fuse, game::kChainFuseMin);
        EXPECT_LT(tnt.fuse, game::kChainFuseMin + game::kChainFuseRandom);
    });
}

TEST_F(S6BetaContent, ALineOfTenTntGoesOffInAChain) {
    for (int32_t x = 0; x < 10; ++x) {
        ASSERT_TRUE(m_world->setBlock(x, kY, 10, BlockId::TNT));
    }
    placePlayer({-4.5f, static_cast<float>(kY), -4.5f});
    hold({items::FlintAndSteel, 1, 0});
    ASSERT_TRUE(m_session.igniteTnt(*m_world, m_player, targetAt({0, kY, 10}, BlockId::TNT)).has_value());
    size_t explosions = 0;
    int32_t ticks = 0;
    for (; ticks < 400 && (primedCount() > 0 || m_session.pendingExplosions() > 0); ++ticks) {
        const game::GameTickEvents ev = gameTick();
        EXPECT_LE(ev.explosions.size(), game::kMaxExplosionsPerTick);
        explosions += ev.explosions.size();
    }
    EXPECT_EQ(explosions, 10U);
    EXPECT_EQ(countBlocks({0, kY, 10}, {9, kY, 10}, BlockId::TNT), 0);
    EXPECT_LT(ticks, 400);
}

TEST_F(S6BetaContent, AtMostFourExplosionsRunPerTick) {
    placePlayer({-4.5f, static_cast<float>(kY), -4.5f});
    for (int i = 0; i < 6; ++i) {
        m_session.queueExplosion({8.5f + static_cast<float>(i) * 2.0f, kY + 30.5f, 8.5f}, 1.0f);
    }
    EXPECT_EQ(m_session.pendingExplosions(), 6U);
    EXPECT_EQ(tickIdle().explosions.size(), game::kMaxExplosionsPerTick);
    EXPECT_EQ(m_session.pendingExplosions(), 2U);
    EXPECT_EQ(tickIdle().explosions.size(), 2U);
    EXPECT_EQ(m_session.pendingExplosions(), 0U);
}

// Wall-clock budget (roadmap S6 risks): one TNT blast in solid ground under 15 ms in Release.
// ctest label "benchmark" (Integration_beta_content_perf) sets BETACRAFT_PERF_TESTS.
TEST_F(S6BetaContent, ExplosionBudget) {
    if (std::getenv("BETACRAFT_PERF_TESTS") == nullptr) {
        GTEST_SKIP() << "set BETACRAFT_PERF_TESTS=1 (ctest -L benchmark) to run the timing budget";
    }
    placePlayer({-4.5f, static_cast<float>(kY), -4.5f});
    double worst = 0.0;
    for (int i = 0; i < 5; ++i) {
        fill({4, kY - 10, 4}, {16, kY + 2, 16}, BlockId::Dirt);
        ASSERT_TRUE(m_world->setBlock(10, kY - 4, 10, BlockId::Air));
        const auto t0 = std::chrono::steady_clock::now();
        m_session.explode(*m_world, m_player, {10.5f, kY - 3.5f, 10.5f}, game::kTntPower, ecs::NULL_ENTITY);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        worst = std::max(worst, ms);
    }
    EXPECT_LT(worst, 15.0);
}

// --- S6.2 arrows and S6.3 mobs ----------------------------------------------------------------

TEST_F(S6BetaContent, SkeletonStopsAndShootsEveryThirtyTicks) {
    const ecs::EntityId skeleton = spawnHunting(gameplay::MobKind::Skeleton, {8.5f, static_cast<float>(kY), 2.5f});
    ecs::Registry& reg = m_session.entities();
    std::vector<int32_t> shotTicks;
    std::vector<ecs::EntityId> seen;
    int32_t projectileHits = 0;
    for (int32_t t = 0; t < 95; ++t) {
        const game::GameTickEvents ev = nightTick();
        for (const game::PlayerHurtEvent& hurt : ev.playerHurt) projectileHits += hurt.cause == DamageCause::Projectile;
        reg.view<ecs::ArrowComp>().each([&](ecs::EntityId e, const ecs::ArrowComp& arrow) {
            if (arrow.shooter == skeleton && std::find(seen.begin(), seen.end(), e) == seen.end()) {
                seen.push_back(e);
                shotTicks.push_back(t);
            }
        });
        // It stands still to shoot (6 blocks away, in plain sight).
        ASSERT_NEAR(reg.get<ecs::TransformComp>(skeleton).pos.x, 8.5f, 0.05f) << t;
    }
    ASSERT_EQ(shotTicks.size(), 4U);
    for (size_t i = 1; i < shotTicks.size(); ++i) EXPECT_EQ(shotTicks[i] - shotTicks[i - 1], 30);
    // Facing the player (-X) and at least one arrow found its mark.
    EXPECT_NEAR(std::abs(reg.get<ecs::TransformComp>(skeleton).yaw), 180.0f, 5.0f);
    EXPECT_GE(projectileHits, 1);
    EXPECT_LT(m_player.getHealth(), m_player.health().max);
}

TEST_F(S6BetaContent, SkeletonWalksCloserBeyondTenBlocksAndHoldsFireBehindAWall) {
    const ecs::EntityId skeleton = spawnHunting(gameplay::MobKind::Skeleton, {14.0f, static_cast<float>(kY), 2.5f});
    ecs::Registry& reg = m_session.entities();
    for (int32_t t = 0; t < 5; ++t) (void)nightTick();
    EXPECT_EQ(reg.count<ecs::ArrowComp>(), 0U); // 11.5 blocks: out of range
    EXPECT_LT(reg.get<ecs::TransformComp>(skeleton).pos.x, 14.0f);

    // A wall between them: no line of sight, no arrow.
    fill({5, kY, -2}, {5, kY + 3, 7}, BlockId::Stone);
    const ecs::EntityId walled = spawnHunting(gameplay::MobKind::Skeleton, {8.5f, static_cast<float>(kY), 2.5f});
    (void)reg.destroy(skeleton);
    for (int32_t t = 0; t < 40; ++t) (void)nightTick();
    reg.view<ecs::ArrowComp>().each([&](ecs::EntityId, const ecs::ArrowComp& arrow) { EXPECT_NE(arrow.shooter, walled); });
}

TEST_F(S6BetaContent, ArrowsStickInTheGroundAndAreNotPickedUp) {
    m_session.setNaturalSpawning(false);
    (void)tickIdle();
    // Shot down into the floor right next to the player.
    const ecs::EntityId arrow =
        game::spawnArrow(m_session.entities(), m_player.getPosition() + glm::vec3(0.8f, 1.0f, 0.0f), {0.0f, -12.0f, 0.0f},
                         ecs::NULL_ENTITY);
    int32_t picked = 0;
    for (int32_t t = 0; t < 20; ++t) picked += tickIdle().itemsPickedUp;
    ASSERT_TRUE(m_session.entities().valid(arrow));
    EXPECT_TRUE(m_session.entities().get<ecs::ArrowComp>(arrow).inGround);
    EXPECT_EQ(picked, 0);
    EXPECT_EQ(m_player.getInventorySlot(0).count, 0);
}

TEST_F(S6BetaContent, SpiderLeapsAtThePlayer) {
    const ecs::EntityId spider = spawnHunting(gameplay::MobKind::Spider, {6.5f, static_cast<float>(kY), 2.5f});
    ecs::Registry& reg = m_session.entities();
    float highest = 0.0f;
    bool leapt = false;
    for (int32_t t = 0; t < 200 && !leapt; ++t) {
        // Keep it 4 blocks away so every grounded tick is a leap chance.
        if (reg.get<ecs::VelocityComp>(spider).onGround) {
            ecs::teleport(reg.get<ecs::TransformComp>(spider), {6.5f, static_cast<float>(kY), 2.5f});
        }
        (void)nightTick();
        leapt = reg.get<ecs::MobAIComp>(spider).leaping;
        highest = std::max(highest, reg.get<ecs::TransformComp>(spider).pos.y - static_cast<float>(kY));
    }
    ASSERT_TRUE(leapt);
    const ecs::VelocityComp& v = reg.get<ecs::VelocityComp>(spider);
    EXPECT_LT(v.linear.x, -5.0f); // Towards the player at about 8 b/s
    EXPECT_GT(v.linear.y, 5.0f);
    for (int32_t t = 0; t < 4; ++t) {
        (void)nightTick();
        highest = std::max(highest, reg.get<ecs::TransformComp>(spider).pos.y - static_cast<float>(kY));
    }
    EXPECT_GT(highest, 0.7f);
}

TEST_F(S6BetaContent, SpiderClimbsAWall) {
    fill({5, kY, -2}, {5, kY + 3, 7}, BlockId::Stone);
    placePlayer({2.5f, static_cast<float>(kY), 2.5f});
    const ecs::EntityId spider = spawnHunting(gameplay::MobKind::Spider, {7.5f, static_cast<float>(kY), 2.5f});
    ecs::Registry& reg = m_session.entities();
    float highest = 0.0f;
    for (int32_t t = 0; t < 60; ++t) {
        auto& ai = reg.get<ecs::MobAIComp>(spider);
        ai.state = ecs::MobAIState::Chase; // Behind the wall it cannot see the player
        ai.target = m_session.playerProxy();
        (void)nightTick();
        highest = std::max(highest, reg.get<ecs::TransformComp>(spider).pos.y - static_cast<float>(kY));
    }
    EXPECT_GT(highest, 2.5f); // Above a 4 block wall's half, no zombie could do that
}

TEST_F(S6BetaContent, SpidersAreNeutralInDaylight) {
    const ecs::EntityId spider = spawnHunting(gameplay::MobKind::Spider, {6.5f, static_cast<float>(kY), 2.5f});
    ecs::Registry& reg = m_session.entities();
    // At noon it forgets the player (1 tick in 100) and never looks for it again.
    bool forgot = false;
    for (int32_t t = 0; t < 1500 && !forgot; ++t) {
        (void)gameTick();
        forgot = reg.get<ecs::MobAIComp>(spider).state != ecs::MobAIState::Chase;
    }
    ASSERT_TRUE(forgot);
    for (int32_t t = 0; t < 100; ++t) {
        (void)gameTick();
        EXPECT_NE(reg.get<ecs::MobAIComp>(spider).state, ecs::MobAIState::Chase);
    }
    // At night it hunts again.
    bool hunting = false;
    for (int32_t t = 0; t < 40 && !hunting; ++t) {
        (void)nightTick();
        hunting = reg.get<ecs::MobAIComp>(spider).state == ecs::MobAIState::Chase;
    }
    EXPECT_TRUE(hunting);
}

TEST_F(S6BetaContent, CreeperExplodesAfterAThirtyTickFuseLeavingACrater) {
    const ecs::EntityId creeper = spawnHunting(gameplay::MobKind::Creeper, {4.5f, static_cast<float>(kY), 2.5f});
    ecs::Registry& reg = m_session.entities();
    int32_t litTicks = 0;
    std::vector<game::ExplosionEvent> blasts;
    std::vector<game::PlayerHurtEvent> hurts;
    float maxSwell = 0.0f;
    for (int32_t t = 0; t < 60 && blasts.empty(); ++t) {
        const game::GameTickEvents ev = nightTick();
        blasts = ev.explosions;
        hurts.insert(hurts.end(), ev.playerHurt.begin(), ev.playerHurt.end());
        if (blasts.empty()) {
            const ecs::FuseComp& fuse = reg.get<ecs::FuseComp>(creeper);
            litTicks += fuse.dir > 0;
            maxSwell = std::max(maxSwell, static_cast<float>(fuse.ticks));
            // It stands still while lit.
            EXPECT_NEAR(reg.get<ecs::TransformComp>(creeper).pos.x, 4.5f, 0.05f);
        }
    }
    ASSERT_EQ(blasts.size(), 1U);
    EXPECT_FLOAT_EQ(blasts[0].power, gameplay::mobDef(gameplay::MobKind::Creeper).explode.power);
    EXPECT_EQ(litTicks, 29); // The 30th lit tick is the blast
    EXPECT_EQ(maxSwell, 29.0f);
    EXPECT_FALSE(reg.valid(creeper));
    EXPECT_GT(blasts[0].blocksDestroyed, 0U); // A hole in the glass floor
    EXPECT_LT(countBlocks({2, kFloorY, 0}, {6, kFloorY, 4}, BlockId::Glass), 25);
    EXPECT_EQ(droppedCount(items::Gunpowder), 0); // No drops
    ASSERT_FALSE(hurts.empty());
    EXPECT_EQ(hurts.back().cause, DamageCause::Explosion);
}

TEST_F(S6BetaContent, CreeperFuseGoesOutWhenThePlayerRunsAway) {
    const ecs::EntityId creeper = spawnHunting(gameplay::MobKind::Creeper, {4.5f, static_cast<float>(kY), 2.5f});
    ecs::Registry& reg = m_session.entities();
    for (int32_t t = 0; t < 10; ++t) (void)nightTick();
    const int32_t lit = reg.get<ecs::FuseComp>(creeper).ticks;
    ASSERT_GT(lit, 5);
    placePlayer({13.5f, static_cast<float>(kY), 2.5f}); // 9 blocks: beyond the 7-block cancel range
    (void)nightTick();
    EXPECT_EQ(reg.get<ecs::FuseComp>(creeper).dir, -1);
    for (int32_t t = 0; t < 3; ++t) (void)nightTick();
    EXPECT_EQ(reg.get<ecs::FuseComp>(creeper).ticks, lit - 4);
    EXPECT_TRUE(reg.valid(creeper));
}

TEST_F(S6BetaContent, CreeperFuseStartsUnderThreeBlocksOnly) {
    const ecs::EntityId creeper = spawnHunting(gameplay::MobKind::Creeper, {7.0f, static_cast<float>(kY), 2.5f});
    ecs::Registry& reg = m_session.entities();
    // Pinned 4.5 blocks away it chases but never lights.
    for (int32_t t = 0; t < 20; ++t) {
        ecs::teleport(reg.get<ecs::TransformComp>(creeper), {7.0f, static_cast<float>(kY), 2.5f});
        (void)nightTick();
        EXPECT_EQ(reg.get<ecs::FuseComp>(creeper).ticks, 0);
    }
}

TEST_F(S6BetaContent, EmptyBucketOnACowGivesMilk) {
    m_session.setNaturalSpawning(false);
    const ecs::EntityId cow =
        ecs::EntityFactory::spawnMob(m_session.entities(), {4.0f, static_cast<float>(kY), 2.5f}, gameplay::MobKind::Cow);
    m_player.setRotation(0.0f, -25.0f); // Looking down at it
    const auto target = m_session.meleeTarget(m_player, physics::RaycastHit{});
    ASSERT_TRUE(target.has_value());
    EXPECT_EQ(*target, cow);

    hold({items::Stick, 1, 0});
    EXPECT_FALSE(m_session.useHeldItemOnEntity(m_player, cow));
    hold({items::Bucket, 1, 0});
    ASSERT_TRUE(m_session.useHeldItemOnEntity(m_player, cow));
    EXPECT_EQ(m_player.getHeldItem().id, items::MilkBucket);
    EXPECT_EQ(m_player.getHeldItem().count, 1);
    EXPECT_TRUE(m_session.entities().valid(cow)); // Milking is free

    // Only cows give milk.
    const ecs::EntityId pig =
        ecs::EntityFactory::spawnMob(m_session.entities(), {4.0f, static_cast<float>(kY), 6.5f}, gameplay::MobKind::Pig);
    hold({items::Bucket, 1, 0});
    EXPECT_FALSE(m_session.useHeldItemOnEntity(m_player, pig));
    EXPECT_EQ(m_player.getHeldItem().id, items::Bucket);
}

TEST_F(S6BetaContent, CowsArePassiveAndDropLeather) {
    const gameplay::MobDef& cow = gameplay::mobDef(gameplay::MobKind::Cow);
    EXPECT_EQ(cow.category, gameplay::MobCategory::Passive);
    m_session.setNaturalSpawning(false);
    int32_t leather = 0;
    for (int i = 0; i < 20; ++i) {
        (void)m_session.spawnMobDrops(gameplay::MobKind::Cow, {2.5f, kY + 0.5f, 6.5f}, false);
    }
    leather = droppedCount(items::Leather);
    EXPECT_GT(leather, 0);
    EXPECT_LE(leather, 40);
}

// --- S6.7 armor ----------------------------------------------------------------------------

namespace {

void wearFullSet(physics::Player& player, gameplay::ArmorMaterial material) {
    for (size_t i = 0; i < physics::Player::ARMOR_SLOT_COUNT; ++i) {
        player.setArmorSlot(i, {gameplay::armorItemId(material, static_cast<gameplay::ArmorSlot>(i)), 1});
    }
}

} // namespace

TEST_F(S6BetaContent, FullArmorReducesHitsAndWearsEveryPiece) {
    wearFullSet(m_player, gameplay::ArmorMaterial::Diamond);
    ASSERT_EQ(m_player.armorValue(), 20);
    game::GameTickEvents events{};
    const gameplay::HitResult hit = m_session.damagePlayer(
        m_player, {.amount = 10, .cause = DamageCause::Mob, .sourcePos = m_player.getPosition() + glm::vec3(1.0f, 0.0f, 0.0f)},
        events);
    EXPECT_EQ(hit.outcome, gameplay::HitOutcome::Full);
    EXPECT_EQ(hit.applied, 2);
    EXPECT_EQ(m_player.getHealth(), 18);
    ASSERT_EQ(events.playerHurt.size(), 1U);
    EXPECT_EQ(events.playerHurt[0].amount, 2);
    for (size_t i = 0; i < physics::Player::ARMOR_SLOT_COUNT; ++i) {
        EXPECT_EQ(m_player.getArmorSlot(i).damage, 10) << "every piece takes the whole hit as wear";
    }
}

TEST_F(S6BetaContent, ArmorBreaksAndTheProtectionGoesWithIt) {
    const gameplay::ItemId cap = gameplay::armorItemId(gameplay::ArmorMaterial::Leather, gameplay::ArmorSlot::Helmet);
    m_player.setArmorSlot(0, {cap, 1, gameplay::armorInfo(cap)->maxDamage}); // One use left
    game::GameTickEvents events{};
    (void)m_session.damagePlayer(m_player, {.amount = 4, .cause = DamageCause::Mob}, events);
    EXPECT_TRUE(m_player.getArmorSlot(0).isEmpty()) << "the cap broke";
    EXPECT_EQ(m_player.armorValue(), 0);
    EXPECT_EQ(m_player.getHealth(), 20 - 3) << "it still took its share of that hit: 4 x 24 / 25";
}

TEST_F(S6BetaContent, TheVoidIgnoresArmorButFallsDoNot) {
    wearFullSet(m_player, gameplay::ArmorMaterial::Iron);
    game::GameTickEvents events{};
    (void)m_session.damagePlayer(m_player, {.amount = 10, .cause = DamageCause::Void, .bypassArmor = true}, events);
    EXPECT_EQ(m_player.getHealth(), 10);
    EXPECT_EQ(m_player.getArmorSlot(1).damage, 0) << "no wear either";

    // Falls (and drowning) go through the armor, as in Beta.
    m_player.health() = gameplay::makeHealth(20);
    (void)m_session.damagePlayer(m_player, {.amount = 10, .cause = DamageCause::Fall}, events);
    EXPECT_EQ(m_player.getHealth(), 18);

    // The real void path of the survival tick bypasses it.
    m_player.health() = gameplay::makeHealth(20);
    m_session.setNaturalSpawning(false);
    m_player.setPosition({2.5f, game::EntitySimulation::OUT_OF_WORLD_Y - 10.0f, 2.5f});
    const game::GameTickEvents ev = tickIdle();
    ASSERT_FALSE(ev.playerHurt.empty());
    EXPECT_EQ(ev.playerHurt[0].cause, DamageCause::Void);
    EXPECT_EQ(ev.playerHurt[0].amount, gameplay::kVoidDamage);
}

TEST_F(S6BetaContent, ArmorSoftensExplosions) {
    wearFullSet(m_player, gameplay::ArmorMaterial::Diamond);
    const glm::vec3 feet = m_player.getPosition();
    const glm::vec3 centre = feet + glm::vec3(-3.0f, 0.5f, 0.0f);
    game::GameTickEvents events{};
    m_session.explode(*m_world, m_player, centre, game::kTntPower, ecs::NULL_ENTITY, events);
    ASSERT_FALSE(events.playerHurt.empty());
    EXPECT_EQ(events.playerHurt[0].cause, DamageCause::Explosion);
    const int32_t raw = gameplay::explosionDamage(glm::length(feet - centre), game::kTntPower, 1.0f);
    int32_t remainder = 0;
    EXPECT_EQ(events.playerHurt[0].amount, gameplay::absorbDamage(raw, 20, remainder));
    EXPECT_LT(events.playerHurt[0].amount, raw);
    EXPECT_EQ(m_player.getArmorSlot(0).damage, raw);
}

TEST_F(S6BetaContent, ArmorIsDroppedOnDeath) {
    m_session.setNaturalSpawning(false);
    wearFullSet(m_player, gameplay::ArmorMaterial::Gold);
    m_player.setInventorySlot(4, {items::Stick, 3});
    EXPECT_EQ(m_session.dropInventoryOnDeath(m_player), 5U);
    for (size_t i = 0; i < physics::Player::ARMOR_SLOT_COUNT; ++i) EXPECT_TRUE(m_player.getArmorSlot(i).isEmpty());
    EXPECT_EQ(droppedCount(gameplay::armorItemId(gameplay::ArmorMaterial::Gold, gameplay::ArmorSlot::Boots)), 1);
}

TEST_F(S6BetaContent, ArmorRecipes) {
    const crafting::RecipeRegistry book = crafting::makeBetaRecipeBook(crafting::BetaRecipeOptions::vanilla());
    crafting::CraftingGrid grid{crafting::GridSize::Workbench3x3};
    const auto put = [&](std::initializer_list<std::pair<int, int>> cells, gameplay::ItemId id) {
        grid.clear();
        for (const auto& [x, y] : cells) ASSERT_TRUE(grid.set(x, y, crafting::SlotStack{id, 1, 0}));
    };
    put({{0, 0}, {1, 0}, {2, 0}, {0, 1}, {2, 1}}, items::Leather);
    ASSERT_TRUE(book.match(grid).has_value());
    EXPECT_EQ(book.match(grid)->result.id, gameplay::armorItemId(gameplay::ArmorMaterial::Leather, gameplay::ArmorSlot::Helmet));
    put({{0, 0}, {2, 0}, {0, 1}, {1, 1}, {2, 1}, {0, 2}, {1, 2}, {2, 2}}, items::Diamond);
    EXPECT_EQ(book.match(grid)->result.id,
              gameplay::armorItemId(gameplay::ArmorMaterial::Diamond, gameplay::ArmorSlot::Chestplate));
    put({{0, 0}, {1, 0}, {2, 0}, {0, 1}, {2, 1}, {0, 2}, {2, 2}}, items::IronIngot);
    EXPECT_EQ(book.match(grid)->result.id, gameplay::armorItemId(gameplay::ArmorMaterial::Iron, gameplay::ArmorSlot::Leggings));
    put({{0, 1}, {2, 1}, {0, 2}, {2, 2}}, items::GoldIngot);
    EXPECT_EQ(book.match(grid)->result.id, gameplay::armorItemId(gameplay::ArmorMaterial::Gold, gameplay::ArmorSlot::Boots));
    // No chain armor recipe exists.
    for (size_t i = 0; i < book.size(); ++i) {
        const gameplay::ItemId out = book.result(i).id;
        if (const auto m = gameplay::armorMaterial(out)) {
            EXPECT_NE(*m, gameplay::ArmorMaterial::Chain) << out;
        }
    }
}

// --- S6.10 dungeons ------------------------------------------------------------------------

TEST_F(S6BetaContent, DungeonChestDrawsTheSameLootOnFirstAccess) {
    m_session.setNaturalSpawning(false);
    const core::BlockPos pos{6, kY, 6};
    ASSERT_TRUE(m_world->setBlockWithMetadata(pos.x, pos.y, pos.z, BlockId::Chest, worldgen::kChestLootPendingMeta));
    const auto expected = worldgen::rollDungeonChestLoot(m_world->getSeed(), pos);
    ASSERT_TRUE(std::any_of(expected.begin(), expected.end(), [](const world::StoredStack& s) { return !s.empty(); }))
        << "pick a position whose chest is not empty";

    ASSERT_TRUE(game::fillDungeonChest(*m_world, pos));
    EXPECT_EQ(m_world->getMetadata(pos), 0);
    const world::BlockEntity* entity = m_world->blockEntities().find(pos);
    ASSERT_NE(entity, nullptr);
    const auto* chest = std::get_if<world::ChestData>(entity);
    ASSERT_NE(chest, nullptr);
    EXPECT_EQ(chest->slots, expected);

    // Second access: nothing is drawn again (the player may have emptied it).
    std::get<world::ChestData>(m_world->blockEntities().getOrCreate(pos, BlockId::Chest)).slots = {};
    EXPECT_FALSE(game::fillDungeonChest(*m_world, pos));
    EXPECT_EQ(std::get<world::ChestData>(*m_world->blockEntities().find(pos)).slots, world::ChestData{}.slots);
    // An ordinary chest is left alone.
    const core::BlockPos plain{8, kY, 6};
    ASSERT_TRUE(m_world->setBlock(plain, BlockId::Chest));
    EXPECT_FALSE(game::fillDungeonChest(*m_world, plain));
    EXPECT_EQ(m_world->blockEntities().find(plain), nullptr);
}

TEST_F(S6BetaContent, BreakingAnUnopenedDungeonChestSpillsItsLoot) {
    m_session.setNaturalSpawning(false);
    const core::BlockPos pos{4, kY, 2};
    ASSERT_TRUE(m_world->setBlockWithMetadata(pos.x, pos.y, pos.z, BlockId::Chest, worldgen::kChestLootPendingMeta));
    const auto loot = worldgen::rollDungeonChestLoot(m_world->getSeed(), pos);
    int32_t total = 0;
    for (const world::StoredStack& s : loot) total += s.count;
    ASSERT_GT(total, 0);
    hold({items::IronAxe, 1, 0});
    const MineResult r = mine(pos);
    ASSERT_TRUE(r.broken.has_value());
    (void)tickIdle();
    int32_t dropped = 0;
    m_session.entities().view<ecs::ItemDropComp>().each([&](ecs::EntityId, const ecs::ItemDropComp& item) {
        dropped += item.id == items::Chest ? 0 : item.count;
    });
    // Picked-up items count too: the player stands right next to it.
    int32_t picked = 0;
    for (size_t i = 0; i < physics::Player::TOTAL_INVENTORY_SIZE; ++i) {
        const physics::InventorySlot slot = m_player.getInventorySlot(i);
        if (!slot.isEmpty() && slot.id != items::IronAxe && slot.id != items::Chest) picked += slot.count;
    }
    EXPECT_EQ(dropped + picked, total);
}

TEST_F(S6BetaContent, DungeonSpawnerFillsADarkRoomUpToSixMobs) {
    // A closed stone room next to the player, a spawner in the middle.
    const core::BlockPos lo{7, kY, 7};
    const core::BlockPos hi{15, kY + 6, 15};
    fill(lo, hi, BlockId::Stone);
    fill({8, kY + 1, 8}, {14, kY + 5, 14}, BlockId::Air);
    const core::BlockPos spawner{11, kY + 2, 11};
    ASSERT_TRUE(m_world->setBlock(spawner, BlockId::MobSpawner));
    ASSERT_EQ(m_world->getSkyLight(9, kY + 1, 9), 0);
    const gameplay::MobKind kind = game::MobSpawner::spawnerKind(spawner);

    game::MobSpawner spawners;
    ecs::Registry reg;
    ecs::CommandBuffer commands;
    gameplay::SplitMix64 rng(42);
    const glm::vec3 feet = m_player.getPosition();
    spawners.tickSpawners(*m_world, reg, commands, feet, 0.25f, rng);
    ASSERT_TRUE(spawners.spawnerDelays().contains(spawner));
    const int32_t delay = spawners.spawnerDelays().at(spawner);
    EXPECT_GE(delay, game::MobSpawner::SPAWNER_MIN_DELAY);
    EXPECT_LT(delay, game::MobSpawner::SPAWNER_MIN_DELAY + game::MobSpawner::SPAWNER_DELAY_RANGE);

    // Out of range (16 blocks), the delay does not move.
    const glm::vec3 far = feet + glm::vec3(-30.0f, 0.0f, 0.0f);
    for (int i = 0; i < 50; ++i) spawners.tickSpawners(*m_world, reg, commands, far, 0.25f, rng);
    EXPECT_EQ(spawners.spawnerDelays().at(spawner), delay);

    int32_t firstSpawnTick = -1;
    for (int32_t t = 0; t < 4000; ++t) {
        spawners.tickSpawners(*m_world, reg, commands, feet, 0.25f, rng);
        (void)commands.flush(reg);
        if (firstSpawnTick < 0 && reg.count<ecs::MobKindComp>() > 0) firstSpawnTick = t;
    }
    ASSERT_GE(firstSpawnTick, delay - 1) << "nothing before the delay ran out";
    int32_t mobs = 0;
    reg.view<ecs::MobKindComp, ecs::TransformComp>().each(
        [&](ecs::EntityId, const ecs::MobKindComp& mob, const ecs::TransformComp& t) {
            EXPECT_EQ(mob.kind, kind);
            EXPECT_LE(std::abs(t.pos.x - static_cast<float>(spawner.x)), 4.0f);
            EXPECT_LE(std::abs(t.pos.z - static_cast<float>(spawner.z)), 4.0f);
            EXPECT_GE(t.pos.y, static_cast<float>(spawner.y - 1));
            EXPECT_LE(t.pos.y, static_cast<float>(spawner.y + 1));
            ++mobs;
        });
    EXPECT_EQ(mobs, game::MobSpawner::SPAWNER_MAX_NEARBY);

    // Breaking the spawner forgets it.
    ASSERT_TRUE(m_world->setBlock(spawner, BlockId::Air));
    spawners.tickSpawners(*m_world, reg, commands, feet, 0.25f, rng);
    EXPECT_FALSE(spawners.spawnerDelays().contains(spawner));
}

TEST_F(S6BetaContent, SpawnerKindsFollowBeta) {
    std::map<gameplay::MobKind, int32_t> kinds;
    for (int32_t i = 0; i < 4000; ++i) ++kinds[game::MobSpawner::spawnerKind({i * 13 - 900, 10 + i % 50, 77 - i * 5})];
    EXPECT_EQ(kinds.size(), 3U);
    EXPECT_NEAR(kinds[gameplay::MobKind::Zombie], 2000, 200);
    EXPECT_NEAR(kinds[gameplay::MobKind::Skeleton], 1000, 150);
    EXPECT_NEAR(kinds[gameplay::MobKind::Spider], 1000, 150);
    EXPECT_EQ(game::MobSpawner::spawnerKind({3, 4, 5}), game::MobSpawner::spawnerKind({3, 4, 5}));
}

TEST_F(S6BetaContent, MobSpawnerBlockIsBetas) {
    const world::BlockDef& def = world::BlockRegistry::get(BlockId::MobSpawner);
    EXPECT_EQ(static_cast<int>(BlockId::MobSpawner), 52);
    EXPECT_FLOAT_EQ(def.hardness, 5.0f);
    EXPECT_TRUE(def.solid);
    EXPECT_FALSE(def.opaque);
    EXPECT_EQ(def.dropId, BlockId::Air);
    // Needs a pickaxe, drops nothing even then.
    hold({items::WoodPickaxe, 1, 0});
    const core::BlockPos pos{4, kY, 2};
    ASSERT_TRUE(m_world->setBlock(pos, BlockId::MobSpawner));
    m_session.setNaturalSpawning(false);
    const MineResult r = mine(pos);
    ASSERT_TRUE(r.broken.has_value());
    EXPECT_FALSE(r.broken->dropped);
}
