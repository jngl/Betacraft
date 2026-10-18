// Phase 2 integration, P2.1 ECS entities in the game tick: game::EntitySimulation owns an
// ecs::Registry of non-player entities and runs its systems every 20 TPS tick. Dropped items
// are ItemDropComp entities that fall, rest on the floor and are picked up by the player
// after Beta's delay; melee swings hit the first entity on the look ray with Beta damage,
// knockback and hurt frames, wear the weapon, and corpses are removed after the death
// animation.
#include "P2GameplaySupport.hpp"

#include "ecs/Systems.hpp"
#include "fluids/FluidSimulator.hpp"
#include "game/DynamicLightCollector.hpp"
#include "lighting/DynamicLightManager.hpp"
#include "gameplay/Health.hpp"
#include "gameplay/ItemIds.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <vector>

using namespace betacraft;
using namespace betacraft::test::p2gameplay;
using ecs::EntityId;
using gameplay::HitOutcome;
using gameplay::toItemId;
using gameplay::WearResult;
using physics::InventorySlot;
namespace items = gameplay::items;

namespace {

class P2GameplayEntities : public GameplayWorld {
protected:
    // A mob standing idle on the floor `distance` blocks in front of the player (who looks
    // along +X). The AI is parked in Idle so it does not wander off the look ray.
    EntityId spawnMobAhead(float distance, int16_t health = 10) {
        const glm::vec3 feet = m_player.getPosition() + glm::vec3(distance, 0.0f, 0.0f);
        const EntityId mob = ecs::EntityFactory::spawnMob(m_session.entities(), feet, health, 0.6f, 1.8f);
        m_session.entities().get<ecs::MobAIComp>(mob).stateTimer = 1.0e6f;
        return mob;
    }

    game::GameTickEvents swing(const physics::RaycastHit& target = {}) {
        return m_session.tick(*m_world, m_player, {.attackPressed = true, .attackHeld = true, .target = target});
    }

    [[nodiscard]] const ecs::HealthComp& health(EntityId e) { return m_session.entities().get<ecs::HealthComp>(e); }
    [[nodiscard]] ecs::TransformComp& transform(EntityId e) { return m_session.entities().get<ecs::TransformComp>(e); }
    [[nodiscard]] ecs::VelocityComp& velocity(EntityId e) { return m_session.entities().get<ecs::VelocityComp>(e); }
};

} // namespace

TEST_F(P2GameplayEntities, BrokenBlocksSpawnItemEntitiesThatFallAndRest) {
    const core::BlockPos cell{6, kY + 3, 2}; // Floating, so the drop has to fall 3 blocks
    ASSERT_TRUE(m_world->setBlock(cell, BlockId::Dirt));
    const MineResult r = mine(cell);
    ASSERT_TRUE(r.broken.has_value());
    ASSERT_TRUE(r.broken->dropped);
    const EntityId drop = r.broken->drop;
    ASSERT_TRUE(m_session.entities().valid(drop));
    EXPECT_EQ(m_session.entities().get<ecs::ItemDropComp>(drop).id, BlockId::Dirt);

    tickIdle(60);
    ASSERT_TRUE(m_session.entities().valid(drop));
    const auto& t = transform(drop);
    EXPECT_NEAR(t.pos.y, static_cast<float>(kY), 0.01f); // Resting on the glass floor
    EXPECT_TRUE(velocity(drop).onGround);
    EXPECT_EQ(m_player.countItem(BlockId::Dirt), 0); // Out of the player's reach
}

TEST_F(P2GameplayEntities, ItemsArePickedUpAfterTheBetaDelayWhenThePlayerIsClose) {
    const glm::vec3 feet = m_player.getPosition();
    const EntityId e = m_session.spawnItem(gameplay::makeStack(toItemId(BlockId::Cobblestone), 5), feet);
    ASSERT_NE(e, ecs::NULL_ENTITY);

    // Beta EntityItem: 10 ticks before a fresh drop can be collected.
    int32_t picked = 0;
    int32_t ticks = 0;
    while (m_session.entities().valid(e) && ticks < 40) {
        picked += tickIdle().itemsPickedUp;
        ++ticks;
    }
    EXPECT_FALSE(m_session.entities().valid(e));
    EXPECT_GE(ticks, 10);
    EXPECT_LE(ticks, 11);
    EXPECT_EQ(picked, 5);
    EXPECT_EQ(m_player.countItem(BlockId::Cobblestone), 5);
}

TEST_F(P2GameplayEntities, DistantItemsStayOnTheGround) {
    const EntityId e = m_session.spawnItem(gameplay::makeStack(toItemId(BlockId::Sand), 1),
                                           m_player.getPosition() + glm::vec3(4.0f, 0.0f, 0.0f));
    tickIdle(40);
    EXPECT_TRUE(m_session.entities().valid(e));
    EXPECT_EQ(m_player.countItem(BlockId::Sand), 0);
}

TEST_F(P2GameplayEntities, APartialPickupLeavesTheRestOnTheGround) {
    for (size_t i = 1; i < physics::Player::TOTAL_INVENTORY_SIZE; ++i) {
        m_player.setInventorySlot(i, {BlockId::Glass, 64});
    }
    m_player.setInventorySlot(0, {BlockId::Gravel, 54});
    const EntityId e = m_session.spawnItem(gameplay::makeStack(toItemId(BlockId::Gravel), 30), m_player.getPosition());
    tickIdle(20);
    ASSERT_TRUE(m_session.entities().valid(e));
    EXPECT_EQ(m_session.entities().get<ecs::ItemDropComp>(e).count, 20);
    EXPECT_EQ(m_player.countItem(BlockId::Gravel), 64);
}

TEST_F(P2GameplayEntities, DroppedToolsKeepTheirWear) {
    gameplay::ItemStack worn = gameplay::makeStack(items::StonePickaxe, 1);
    worn.damage = 40;
    const EntityId e = m_session.spawnItem(worn, m_player.getPosition());
    ASSERT_NE(e, ecs::NULL_ENTITY);
    EXPECT_EQ(m_session.entities().get<ecs::ItemDropComp>(e).damage, 40);
    tickIdle(20);
    EXPECT_EQ(m_player.getInventorySlot(0), InventorySlot(items::StonePickaxe, 1, 40));
}

TEST_F(P2GameplayEntities, AFistHitDamagesKnocksBackAndOpensTheHurtWindow) {
    const EntityId mob = spawnMobAhead(2.0f);
    tickIdle(2);
    ASSERT_EQ(m_session.meleeTarget(m_player, {}), std::optional<EntityId>(mob));
    const float startX = transform(mob).pos.x;

    const game::GameTickEvents first = swing();
    ASSERT_TRUE(first.melee.has_value());
    EXPECT_EQ(first.melee->target, mob);
    EXPECT_EQ(first.melee->result.hit.outcome, HitOutcome::Full);
    EXPECT_EQ(health(mob).current, 9); // Fist: 1 half-heart
    // Knocked back along the look (+X) and lifted; the same tick already moved it.
    EXPECT_GT(first.melee->result.impulse.x, 8.0f);
    EXPECT_GT(first.melee->result.impulse.y, 4.0f);
    EXPECT_NEAR(first.melee->result.impulse.z, 0.0f, 1e-4f);
    EXPECT_GT(transform(mob).pos.x, startX);
    EXPECT_FALSE(velocity(mob).onGround);

    // Inside the 10-tick hurt window an equal hit is absorbed: no damage, no knockback.
    ecs::teleport(transform(mob), m_player.getPosition() + glm::vec3(2.0f, 0.0f, 0.0f));
    velocity(mob).linear = glm::vec3(0.0f);
    const game::GameTickEvents second = swing();
    ASSERT_TRUE(second.melee.has_value());
    EXPECT_EQ(second.melee->result.hit.outcome, HitOutcome::Absorbed);
    EXPECT_EQ(health(mob).current, 9);
    EXPECT_EQ(second.melee->result.impulse, glm::vec3(0.0f));

    // The mob flies, lands and stops; once the window is over the next hit lands again.
    tickIdle(12);
    EXPECT_TRUE(velocity(mob).onGround);
    EXPECT_NEAR(velocity(mob).linear.x, 0.0f, 1e-3f);
    ecs::teleport(transform(mob), m_player.getPosition() + glm::vec3(2.0f, 0.0f, 0.0f));
    const game::GameTickEvents third = swing();
    ASSERT_TRUE(third.melee.has_value());
    EXPECT_EQ(third.melee->result.hit.outcome, HitOutcome::Full);
    EXPECT_EQ(health(mob).current, 8);
}

TEST_F(P2GameplayEntities, SwordHitsDealBetaDamageAndWearTheSword) {
    hold({items::WoodSword, 1});
    const EntityId mob = spawnMobAhead(2.0f, 20);
    tickIdle(2);
    const game::GameTickEvents ev = swing();
    ASSERT_TRUE(ev.melee.has_value());
    EXPECT_EQ(health(mob).current, 16); // Beta wooden sword: 4
    EXPECT_EQ(ev.heldWear, WearResult::Worn);
    EXPECT_EQ(m_player.getHeldItem(), InventorySlot(items::WoodSword, 1, 1));
}

TEST_F(P2GameplayEntities, ASwingAtAnEntityDoesNotMineTheBlockBehindIt) {
    const core::BlockPos wall{6, kY + 1, 2};
    ASSERT_TRUE(m_world->setBlock(wall, BlockId::Dirt));
    const EntityId mob = spawnMobAhead(2.0f);
    tickIdle(2);
    const game::GameTickEvents ev = swing(targetAt(wall, BlockId::Dirt, 3.4f));
    ASSERT_TRUE(ev.melee.has_value());
    EXPECT_EQ(ev.melee->target, mob);
    EXPECT_FALSE(ev.blockBroken.has_value());
    EXPECT_EQ(m_world->getBlock(wall), BlockId::Dirt);
}

TEST_F(P2GameplayEntities, BlocksInFrontOfAnEntityShieldIt) {
    const EntityId mob = spawnMobAhead(3.0f);
    tickIdle(2);
    const core::BlockPos shield{4, kY + 1, 2};
    ASSERT_TRUE(m_world->setBlock(shield, BlockId::Dirt));
    EXPECT_FALSE(m_session.meleeTarget(m_player, targetAt(shield, BlockId::Dirt, 1.38f)).has_value());
    const game::GameTickEvents ev = swing(targetAt(shield, BlockId::Dirt, 1.38f));
    EXPECT_FALSE(ev.melee.has_value());
    EXPECT_EQ(health(mob).current, 10);
}

TEST_F(P2GameplayEntities, KilledMobsStayAsCorpsesForTheDeathAnimationThenVanish) {
    hold({items::StoneSword, 1});
    const EntityId mob = spawnMobAhead(2.0f, 4);
    tickIdle(2);
    const game::GameTickEvents kill = swing();
    ASSERT_TRUE(kill.melee.has_value());
    EXPECT_TRUE(kill.melee->result.hit.killed);
    EXPECT_TRUE(health(mob).isDead());

    // Still targetable while dying: the swing deals nothing but wears the sword (Beta).
    ecs::teleport(transform(mob), m_player.getPosition() + glm::vec3(2.0f, 0.0f, 0.0f));
    velocity(mob).linear = glm::vec3(0.0f);
    const game::GameTickEvents again = swing();
    ASSERT_TRUE(again.melee.has_value());
    EXPECT_EQ(again.melee->result.hit.outcome, HitOutcome::Ignored);
    EXPECT_EQ(m_player.getHeldItem().damage, 2);

    int32_t ticks = 2; // The kill tick and the swing above
    while (m_session.entities().valid(mob) && ticks < 60) {
        (void)tickIdle();
        ++ticks;
    }
    EXPECT_FALSE(m_session.entities().valid(mob));
    EXPECT_EQ(ticks, gameplay::kDeathAnimationTicks);
}

TEST_F(P2GameplayEntities, ItemsThePlayerDropsFlyAheadAndAreNotPickedUpAtOnce) {
    const EntityId e = m_session.dropFromPlayer(m_player, gameplay::makeStack(toItemId(BlockId::Cobblestone), 3));
    ASSERT_NE(e, ecs::NULL_ENTITY);
    EXPECT_GT(velocity(e).linear.x, 0.0f); // Thrown along the look (+X)
    EXPECT_NEAR(transform(e).pos.y, m_player.getEyePosition().y - 0.3f, 1e-4f);
    // Beta: 40 ticks before a thrown item can be collected, so it is not re-picked at once.
    tickIdle(30);
    EXPECT_TRUE(m_session.entities().valid(e));
    EXPECT_EQ(m_player.countItem(BlockId::Cobblestone), 0);
    EXPECT_GT(transform(e).pos.x, m_player.getPosition().x + 0.5f);
}

TEST_F(P2GameplayEntities, FlowersWashedAwayByWaterDropAsItemEntities) {
    const core::BlockPos flower{9, kY, 8};
    ASSERT_TRUE(m_world->setBlock(flower, BlockId::Dandelion));
    (void)m_world->takeFluidEvents();
    ASSERT_TRUE(m_world->setBlock({8, kY, 8}, BlockId::Water));

    size_t spawned = 0;
    for (int i = 0; i < 20 && spawned == 0; ++i) {
        (void)m_world->tick();
        spawned += m_session.spawnFluidDrops(m_world->takeFluidEvents());
    }
    ASSERT_EQ(spawned, 1U);
    EXPECT_NE(m_world->getBlock(flower), BlockId::Dandelion);
    ASSERT_EQ(itemEntityCount(), 1U);
    bool found = false;
    m_session.entities().view<ecs::TransformComp, ecs::ItemDropComp>().each(
        [&](const ecs::TransformComp& t, const ecs::ItemDropComp& item) {
            found = item.id == BlockId::Dandelion && std::floor(t.pos.x) == 9.0f && std::floor(t.pos.z) == 8.0f;
        });
    EXPECT_TRUE(found);
}

TEST_F(P2GameplayEntities, FizzEventsAndDroplessWashesSpawnNothing) {
    const std::vector<fluids::FluidEvent> events{
        {fluids::FluidEventKind::Fizz, {1, kY, 1}, BlockId::Obsidian},
        {fluids::FluidEventKind::BlockWashedAway, {2, kY, 1}, BlockId::TallGrass}, // Seeds 1 time in 8 (S5.7): not on this seeded roll
        {fluids::FluidEventKind::BlockWashedAway, {3, kY, 1}, BlockId::Torch},
    };
    EXPECT_EQ(m_session.spawnFluidDrops(events), 1U);
    EXPECT_EQ(itemEntityCount(), 1U);
}

TEST_F(P2GameplayEntities, SessionItemsLightTheWorldOnTheGameTickClock) {
    lighting::DynamicLightManager lights;
    const EntityId glow = m_session.spawnItem(gameplay::makeStack(toItemId(BlockId::Glowstone), 1), {0.0f, 64.0f, 0.0f});
    (void)m_session.spawnItem(gameplay::makeStack(items::GlowstoneDust, 4), {3.0f, 64.0f, 0.0f}); // Items never glow
    auto& t = transform(glow);
    t.prevPos = {0.0f, 64.0f, 0.0f};
    t.pos = {2.0f, 64.0f, 0.0f};
    m_player.setRenderAlpha(0.9f); // 60 Hz physics alpha: not the clock items move on

    game::collectDynamicLights(lights, m_player, &m_session.entities(), 0.25f);
    ASSERT_EQ(lights.size(), 1U);
    EXPECT_FLOAT_EQ(lights.lights()[0].position.x, 0.5f);
}

TEST_F(P2GameplayEntities, EntitiesThatFallOutOfTheWorldAreRemoved) {
    // Over an unloaded chunk the world reads as air, so nothing stops the fall (Beta: killed below y -64).
    const EntityId e = m_session.spawnItem(gameplay::makeStack(toItemId(BlockId::Sand), 1), {5000.5f, -60.0f, 5000.5f});
    ASSERT_NE(e, ecs::NULL_ENTITY);
    tickIdle(40);
    EXPECT_FALSE(m_session.entities().valid(e));
}

// S3.4: Q throws one item of the held stack, Ctrl+Q the whole stack, wear kept, through
// dropFromPlayer (thrown along the look, collectable after Beta's 40 ticks only).
TEST_F(P2GameplayEntities, DropHeldItemThrowsOneOrWholeStack) {
    hold({BlockId::Cobblestone, 10});
    const EntityId one = m_session.dropHeldItem(m_player, false);
    ASSERT_NE(one, ecs::NULL_ENTITY);
    EXPECT_EQ(m_session.entities().get<ecs::ItemDropComp>(one).count, 1);
    EXPECT_EQ(m_player.getHeldItem(), (InventorySlot{BlockId::Cobblestone, 9}));
    EXPECT_GT(velocity(one).linear.x, 0.0f) << "thrown along the look (+X)";

    const EntityId rest = m_session.dropHeldItem(m_player, true);
    ASSERT_NE(rest, ecs::NULL_ENTITY);
    EXPECT_EQ(m_session.entities().get<ecs::ItemDropComp>(rest).count, 9);
    EXPECT_TRUE(m_player.getHeldItem().isEmpty());
    EXPECT_EQ(m_session.dropHeldItem(m_player, false), ecs::NULL_ENTITY) << "empty hand";
    EXPECT_EQ(m_session.dropHeldItem(m_player, true), ecs::NULL_ENTITY);

    // A worn tool keeps its wear.
    m_player.setSelectedSlot(3);
    hold({items::IronPickaxe, 1, 77});
    const EntityId tool = m_session.dropHeldItem(m_player, false);
    ASSERT_NE(tool, ecs::NULL_ENTITY);
    EXPECT_EQ(m_session.entities().get<ecs::ItemDropComp>(tool).damage, 77);
    EXPECT_TRUE(m_player.getInventorySlot(3).isEmpty());

    // Not picked up again at once; after the 40-tick delay the items come back.
    tickIdle(30);
    EXPECT_EQ(m_player.countItem(BlockId::Cobblestone), 0);
    EXPECT_EQ(itemEntityCount(), 3U);

    // The dead throw nothing.
    hold({BlockId::Dirt, 5});
    game::GameTickEvents ev{};
    (void)m_session.damagePlayer(m_player, {.amount = 100, .cause = game::DamageCause::Void}, ev);
    ASSERT_TRUE(m_player.isDead());
    EXPECT_EQ(m_session.dropHeldItem(m_player, true), ecs::NULL_ENTITY);
    EXPECT_EQ(m_player.getHeldItem().count, 5);
}
