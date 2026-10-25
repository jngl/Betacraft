// Integration: honest survival: one player health (gameplay::Health), one damage
// entry point (EntitySimulation::damagePlayer) and the survival rules the game tick runs first
// (fall damage from the landing physics recorded, drowning, the void), all with Beta 1.7.3
// numbers. Creative ignores them.
#include "tests/integration/support/GameplaySupport.hpp"

#include "gameplay/DeathDrops.hpp"
#include "gameplay/Health.hpp"
#include "gameplay/Survival.hpp"

#include <algorithm>

using namespace betacraft;
using namespace betacraft::test::gameplay_support;
using game::DamageCause;
using game::GameTickEvents;
using game::PlayerDamage;

namespace {

class SurvivalRules : public GameplayWorld {
protected:
    // Drops the player from `height` blocks above the floor and lets physics land it.
    void fallFrom(float height) {
        m_player.setPosition({2.5f, static_cast<float>(kY) + height, 2.5f});
        for (int i = 0; i < 600; ++i) {
            (void)m_player.fixedUpdate(1.0f / 60.0f, {}, *m_world);
            if (m_player.isOnGround()) break;
        }
        ASSERT_TRUE(m_player.isOnGround());
        ASSERT_NEAR(m_player.getPosition().y, static_cast<float>(kY), 0.01f);
    }

    // Floods the player's column up past the head and refreshes the physics water flags.
    void submerge() {
        for (int32_t y = kY; y <= kY + 2; ++y) {
            ASSERT_TRUE(m_world->setBlock(2, y, 2, BlockId::Water));
        }
        (void)m_player.fixedUpdate(1.0f / 60.0f, {}, *m_world);
        ASSERT_TRUE(m_player.isHeadUnderWater());
    }

    // Ticks in the void until the killing hit; returns how many ticks reported a death.
    int32_t dieInTheVoid() {
        m_player.setPosition({2.5f, -70.0f, 2.5f});
        int32_t deaths = 0;
        for (int i = 0; i < 200 && !m_player.isDead(); ++i) {
            if (tickIdle().playerDied) ++deaths;
        }
        return deaths;
    }

    [[nodiscard]] int32_t droppedItemTotal() {
        int32_t total = 0;
        m_session.entities().view<ecs::ItemDropComp>().each(
            [&](ecs::EntityId, const ecs::ItemDropComp& item) { total += item.count; });
        return total;
    }

    [[nodiscard]] static int32_t hurtTotal(const GameTickEvents& ev, DamageCause cause) {
        int32_t total = 0;
        for (const game::PlayerHurtEvent& h : ev.playerHurt) {
            if (h.cause == cause) total += h.amount;
        }
        return total;
    }
};

} // namespace

TEST_F(SurvivalRules, TenBlockFallCostsSevenHalfHearts) {
    fallFrom(10.0f);
    EXPECT_EQ(m_player.getHealth(), 20) << "physics only records the fall";
    const GameTickEvents ev = tickIdle();
    ASSERT_EQ(ev.playerHurt.size(), 1U);
    EXPECT_EQ(ev.playerHurt[0].cause, DamageCause::Fall);
    EXPECT_EQ(ev.playerHurt[0].amount, 7); // Beta ceil(fall - 3): 3.5 hearts
    EXPECT_FALSE(ev.playerHurt[0].killed);
    EXPECT_EQ(m_player.getHealth(), 13);
    EXPECT_TRUE(tickIdle().playerHurt.empty()) << "a landing hurts once";
}

TEST_F(SurvivalRules, ThreeBlockFallIsSafe) {
    fallFrom(3.0f);
    EXPECT_TRUE(tickIdle().playerHurt.empty());
    EXPECT_EQ(m_player.getHealth(), 20);

    // Just past the threshold costs one half-heart (Beta, unlike the old floor(d - 3)).
    EXPECT_EQ(gameplay::fallDamage(3.2f), 1);
}

TEST_F(SurvivalRules, CreativeIgnoresFallAndDrowning) {
    m_player.setGameMode(core::GameMode::Creative);
    fallFrom(10.0f);
    EXPECT_TRUE(tickIdle().playerHurt.empty());
    EXPECT_FLOAT_EQ(m_player.takePendingFall(), 0.0f) << "the tick drained the pending fall";

    submerge();
    for (int i = 0; i < 400; ++i) {
        const GameTickEvents ev = tickIdle();
        ASSERT_TRUE(ev.playerHurt.empty());
    }
    EXPECT_EQ(m_player.getHealth(), 20);
    EXPECT_EQ(m_player.breath().air, gameplay::kMaxAirTicks);

    GameTickEvents ev{};
    EXPECT_EQ(m_session.damagePlayer(m_player, {.amount = 5, .cause = DamageCause::Lava}, ev).applied, 0);
    EXPECT_TRUE(ev.playerHurt.empty());
}

TEST_F(SurvivalRules, DrowningStartsAfter300TicksThenTwoEvery20) {
    submerge();
    // 300 ticks of air, then Beta waits until air reaches -20 before the first hit.
    for (int t = 1; t < 320; ++t) {
        ASSERT_TRUE(tickIdle().playerHurt.empty()) << "tick " << t;
    }
    EXPECT_FLOAT_EQ(m_player.getAirRatio(), 0.0f);
    GameTickEvents ev = tickIdle();
    EXPECT_EQ(hurtTotal(ev, DamageCause::Drown), 2);
    EXPECT_EQ(m_player.getHealth(), 18);
    for (int t = 1; t < 20; ++t) {
        ASSERT_TRUE(tickIdle().playerHurt.empty()) << "tick " << t;
    }
    ev = tickIdle();
    EXPECT_EQ(hurtTotal(ev, DamageCause::Drown), 2);
    EXPECT_EQ(m_player.getHealth(), 16);

    // Surfacing refills the lungs at once (Beta).
    for (int32_t y = kY; y <= kY + 2; ++y) {
        ASSERT_TRUE(m_world->setBlock(2, y, 2, BlockId::Air));
    }
    (void)m_player.fixedUpdate(1.0f / 60.0f, {}, *m_world);
    (void)tickIdle();
    EXPECT_FLOAT_EQ(m_player.getAirRatio(), 1.0f);
}

TEST_F(SurvivalRules, VoidKillsInSurvival) {
    m_player.setPosition({2.5f, -70.0f, 2.5f});
    int32_t deaths = 0;
    int32_t ticks = 0;
    for (; ticks < 200 && !m_player.isDead(); ++ticks) {
        const GameTickEvents ev = tickIdle();
        for (const game::PlayerHurtEvent& h : ev.playerHurt) {
            EXPECT_EQ(h.cause, DamageCause::Void);
        }
        if (ev.playerDied) ++deaths;
    }
    EXPECT_TRUE(m_player.isDead());
    EXPECT_EQ(m_player.getHealth(), 0);
    EXPECT_EQ(deaths, 1);
    EXPECT_LE(ticks, 60) << "4 damage per hit, a hit every resistance window";

    // The dead take no more damage and the death is reported once.
    for (int i = 0; i < 40; ++i) {
        const GameTickEvents ev = tickIdle();
        EXPECT_FALSE(ev.playerDied);
        EXPECT_TRUE(ev.playerHurt.empty());
    }

    // Creative keeps its safety net: the physics lifts the player back above the void.
    physics::Player creative;
    creative.setGameMode(core::GameMode::Creative);
    creative.setPosition({2.5f, -20.0f, 2.5f});
    (void)creative.fixedUpdate(1.0f / 60.0f, {}, *m_world);
    EXPECT_GT(creative.getPosition().y, 0.0f);
}

TEST_F(SurvivalRules, NoNaturalRegeneration) {
    GameTickEvents ev{};
    (void)m_session.damagePlayer(m_player, {.amount = 7, .cause = DamageCause::Fall}, ev);
    ASSERT_EQ(m_player.getHealth(), 13);
    for (int i = 0; i < 1200; ++i) {
        (void)m_player.fixedUpdate(1.0f / 60.0f, {}, *m_world);
        ASSERT_TRUE(tickIdle().playerHurt.empty());
    }
    EXPECT_EQ(m_player.getHealth(), 13);
}

TEST_F(SurvivalRules, DamagePlayerRespectsInvulnerabilityWindow) {
    GameTickEvents ev{};
    gameplay::HitResult hit = m_session.damagePlayer(m_player, {.amount = 4, .cause = DamageCause::Fall}, ev);
    EXPECT_EQ(hit.outcome, gameplay::HitOutcome::Full);
    EXPECT_EQ(hit.applied, 4);
    ASSERT_EQ(ev.playerHurt.size(), 1U);
    EXPECT_EQ(ev.playerHurt[0].amount, 4);

    // Inside the 10-tick window: an equal hit is absorbed, a stronger one lands the excess.
    hit = m_session.damagePlayer(m_player, {.amount = 4, .cause = DamageCause::Drown}, ev);
    EXPECT_EQ(hit.outcome, gameplay::HitOutcome::Absorbed);
    EXPECT_EQ(ev.playerHurt.size(), 1U) << "an absorbed hit hurts nobody";
    hit = m_session.damagePlayer(m_player, {.amount = 6, .cause = DamageCause::Fall}, ev);
    EXPECT_EQ(hit.outcome, gameplay::HitOutcome::Partial);
    EXPECT_EQ(hit.applied, 2);
    EXPECT_EQ(m_player.getHealth(), 14);

    tickIdle(gameplay::kHurtResistanceTicks);
    hit = m_session.damagePlayer(m_player, {.amount = 4, .cause = DamageCause::Fall}, ev);
    EXPECT_EQ(hit.outcome, gameplay::HitOutcome::Full);
    EXPECT_EQ(m_player.getHealth(), 10);

    // The killing hit sets playerDied; afterwards the dead ignore damage.
    GameTickEvents killTick{};
    tickIdle(gameplay::kHurtResistanceTicks);
    hit = m_session.damagePlayer(m_player, {.amount = 50, .cause = DamageCause::Void}, killTick);
    EXPECT_TRUE(hit.killed);
    EXPECT_EQ(hit.applied, 10);
    EXPECT_TRUE(killTick.playerDied);
    ASSERT_EQ(killTick.playerHurt.size(), 1U);
    EXPECT_TRUE(killTick.playerHurt[0].killed);
    GameTickEvents after{};
    EXPECT_EQ(m_session.damagePlayer(m_player, {.amount = 4, .cause = DamageCause::Fall}, after).outcome,
              gameplay::HitOutcome::Ignored);
    EXPECT_FALSE(after.playerDied);
}

TEST_F(SurvivalRules, DeathFiresOnceAndScattersEveryStack) {
    m_player.setInventorySlot(0, physics::InventorySlot{gameplay::items::StonePickaxe, 1, 40});
    m_player.setInventorySlot(4, {BlockId::Cobblestone, 64});
    m_player.setInventorySlot(9, {BlockId::Torch, 17});
    m_player.setInventorySlot(22, physics::InventorySlot{gameplay::items::IronIngot, 5});
    m_player.setInventorySlot(35, {BlockId::Dirt, 3});
    const int32_t carried = 1 + 64 + 17 + 5 + 3;
    const size_t before = itemEntityCount();

    EXPECT_EQ(dieInTheVoid(), 1);
    ASSERT_TRUE(m_player.isDead());
    for (int i = 0; i < 40; ++i) {
        ASSERT_FALSE(tickIdle().playerDied) << "the death is reported once";
    }
    EXPECT_EQ(m_player.countItem(BlockId::Cobblestone), 64) << "dying alone drops nothing";
    // Back on the floor, so the drops land within reach of the (dead) player.
    m_player.setPosition({2.5f, static_cast<float>(kY), 2.5f});

    // What the Application does on events.playerDied.
    EXPECT_EQ(m_session.dropInventoryOnDeath(m_player), 5U);
    EXPECT_EQ(itemEntityCount(), before + 5);
    EXPECT_EQ(droppedItemTotal(), carried) << "every item is scattered, none lost";
    for (size_t i = 0; i < physics::Player::TOTAL_INVENTORY_SIZE; ++i) {
        EXPECT_TRUE(m_player.getInventorySlot(i).isEmpty()) << "slot " << i;
    }
    bool sawWornPickaxe = false;
    m_session.entities().view<ecs::ItemDropComp>().each([&](ecs::EntityId, const ecs::ItemDropComp& item) {
        EXPECT_FLOAT_EQ(item.pickupDelaySeconds,
                        static_cast<float>(gameplay::kDeathDropPickupDelayTicks) * game::EntitySimulation::TICK_SECONDS);
        EXPECT_EQ(gameplay::kDeathDropPickupDelayTicks, 40);
        if (item.id == gameplay::items::StonePickaxe) sawWornPickaxe = item.damage == 40;
    });
    EXPECT_TRUE(sawWornPickaxe) << "tool wear travels with the drop";

    // Nothing to drop twice, and the dead collect nothing even once the delay is over.
    EXPECT_EQ(m_session.dropInventoryOnDeath(m_player), 0U);
    for (int i = 0; i < 60; ++i) {
        EXPECT_EQ(tickIdle().itemsPickedUp, 0);
    }
}

