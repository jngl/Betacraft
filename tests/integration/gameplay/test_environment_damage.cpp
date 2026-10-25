// Integration: environmental damage: lava hits for 4 and sets the player on
// fire for 600 ticks, fire costs 1 health every 20 ticks until water puts it out, a cactus
// hurts 1 per contact. Every hit goes through EntitySimulation::damagePlayer, so the 10-tick
// hurt-resistance window paces them; Creative ignores all of it. The fire is saved in
// level.dat under `player.fire`.
#include "tests/integration/support/GameplaySupport.hpp"

#include "game/PlayerPersistence.hpp"
#include "gameplay/Health.hpp"
#include "gameplay/Survival.hpp"
#include "storage/WorldStorage.hpp"

#include <string>

using namespace betacraft;
using namespace betacraft::test::gameplay_support;
using game::DamageCause;
using game::GameTickEvents;

namespace {

class EnvironmentDamage : public GameplayWorld {
protected:
    // Sets the block at the player's feet and refreshes the physics flags.
    void setFeetBlock(BlockId id) {
        ASSERT_TRUE(m_world->setBlock(2, kY, 2, id));
        (void)m_player.fixedUpdate(1.0f / 60.0f, {}, *m_world);
    }

    [[nodiscard]] static int32_t hurtTotal(const GameTickEvents& ev, std::optional<DamageCause> cause = {}) {
        int32_t total = 0;
        for (const game::PlayerHurtEvent& h : ev.playerHurt) {
            if (!cause || h.cause == *cause) total += h.amount;
        }
        return total;
    }

    // Ticks `n` times and returns the damage of every hit that landed.
    int32_t tickAndSumDamage(int32_t n) {
        int32_t total = 0;
        for (int32_t i = 0; i < n; ++i) total += hurtTotal(tickIdle());
        return total;
    }

    // A cactus east of the player, with the player's box reaching 0.03 into its cell but
    // short of the inset collision box (no movement needed to keep the contact).
    void standAgainstCactus() {
        ASSERT_TRUE(m_world->setBlock(3, kY, 2, BlockId::Cactus));
        m_player.setPosition({2.73f, static_cast<float>(kY), 2.5f});
        (void)m_player.fixedUpdate(1.0f / 60.0f, {}, *m_world);
        ASSERT_TRUE(m_player.isTouchingCactus());
    }
};

} // namespace

TEST_F(EnvironmentDamage, LavaHitsFourThenRespectsInvulnerability) {
    setFeetBlock(BlockId::Lava);
    ASSERT_TRUE(m_player.isInLava());

    const GameTickEvents first = tickIdle();
    ASSERT_EQ(first.playerHurt.size(), 1U);
    EXPECT_EQ(first.playerHurt[0].cause, DamageCause::Lava);
    EXPECT_EQ(first.playerHurt[0].amount, gameplay::kLavaDamage);
    EXPECT_EQ(m_player.getHealth(), 16) << "2 hearts";
    EXPECT_EQ(m_player.burning().fireTicks, gameplay::kLavaFireTicks);
    EXPECT_EQ(m_player.hurtTime(), physics::Player::HURT_TIME_TICKS) << "a full hit tilts the camera";

    // The rest of the window absorbs both the lava and the fire hits.
    EXPECT_EQ(tickAndSumDamage(gameplay::kHurtResistanceTicks - 1), 0);
    EXPECT_EQ(m_player.getHealth(), 16);

    // Then 4 more per window (fire 1 lands first, lava adds the remaining 3, as in Beta).
    EXPECT_EQ(tickAndSumDamage(gameplay::kHurtResistanceTicks), 4);
    EXPECT_EQ(m_player.getHealth(), 12);
    EXPECT_EQ(tickAndSumDamage(gameplay::kHurtResistanceTicks), 4);
    EXPECT_EQ(m_player.getHealth(), 8);

    // Lava kills in 5 windows (41 ticks from 20 health), never in 5 ticks.
    int32_t ticks = 0;
    while (!m_player.isDead() && ticks < 200) {
        (void)tickIdle();
        ++ticks;
    }
    EXPECT_TRUE(m_player.isDead());
    EXPECT_EQ(ticks, gameplay::kHurtResistanceTicks + 1) << "8 health = two more windows";
}

TEST_F(EnvironmentDamage, BurningContinuesAfterLeavingLavaUntilWater) {
    setFeetBlock(BlockId::Lava);
    (void)tickIdle(); // 4 from lava, on fire
    ASSERT_EQ(m_player.getHealth(), 16);
    setFeetBlock(BlockId::Air);
    ASSERT_FALSE(m_player.isInLava());
    ASSERT_TRUE(m_player.burning().isBurning());

    // Out of the lava the fire keeps burning: 1 per second (fire = 580, 560, 540, 520 in
    // the next 100 ticks; the 600 one fell inside the lava hit's window).
    int32_t fire = 0;
    for (int i = 0; i < 100; ++i) {
        const GameTickEvents ev = tickIdle();
        fire += hurtTotal(ev, DamageCause::Fire);
        EXPECT_EQ(hurtTotal(ev, DamageCause::Lava), 0);
    }
    EXPECT_EQ(fire, 4);
    EXPECT_EQ(m_player.getHealth(), 12);
    EXPECT_TRUE(m_player.burning().isBurning());

    // Water puts it out at once.
    setFeetBlock(BlockId::Water);
    ASSERT_TRUE(m_player.isInWaterMaterial());
    EXPECT_EQ(tickAndSumDamage(1), 0);
    EXPECT_FALSE(m_player.burning().isBurning());
    setFeetBlock(BlockId::Air);
    EXPECT_EQ(tickAndSumDamage(200), 0);
    EXPECT_EQ(m_player.getHealth(), 12);
}

TEST_F(EnvironmentDamage, CactusContactHurtsOncePerWindow) {
    standAgainstCactus();
    const GameTickEvents first = tickIdle();
    ASSERT_EQ(first.playerHurt.size(), 1U);
    EXPECT_EQ(first.playerHurt[0].cause, DamageCause::Cactus);
    EXPECT_EQ(first.playerHurt[0].amount, gameplay::kCactusDamage);
    EXPECT_EQ(m_player.getHealth(), 19);

    EXPECT_EQ(tickAndSumDamage(gameplay::kHurtResistanceTicks - 1), 0) << "resisted inside the window";
    EXPECT_EQ(tickAndSumDamage(gameplay::kHurtResistanceTicks), 1);
    EXPECT_EQ(tickAndSumDamage(5 * gameplay::kHurtResistanceTicks), 5);
    EXPECT_EQ(m_player.getHealth(), 13);

    // Stepping away ends it.
    m_player.setPosition({2.5f, static_cast<float>(kY), 2.5f});
    (void)m_player.fixedUpdate(1.0f / 60.0f, {}, *m_world);
    EXPECT_FALSE(m_player.isTouchingCactus());
    EXPECT_EQ(tickAndSumDamage(40), 0);
}

TEST_F(EnvironmentDamage, CreativeIsImmuneToLavaAndCactus) {
    m_player.setGameMode(core::GameMode::Creative);
    standAgainstCactus();
    ASSERT_TRUE(m_world->setBlock(2, kY, 2, BlockId::Lava));
    (void)m_player.fixedUpdate(1.0f / 60.0f, {}, *m_world);
    ASSERT_TRUE(m_player.isInLava());
    ASSERT_TRUE(m_player.isTouchingCactus());

    for (int i = 0; i < 60; ++i) {
        const GameTickEvents ev = tickIdle();
        EXPECT_TRUE(ev.playerHurt.empty());
    }
    EXPECT_EQ(m_player.getHealth(), 20);
    EXPECT_FALSE(m_player.burning().isBurning()) << "Creative does not burn";
    EXPECT_EQ(m_player.hurtTime(), 0);
}

TEST_F(EnvironmentDamage, FireTicksSurviveSaveReload) {
    setFeetBlock(BlockId::Lava);
    (void)tickIdle();
    setFeetBlock(BlockId::Air);
    tickIdle(37);
    const int32_t fire = m_player.burning().fireTicks;
    ASSERT_EQ(fire, gameplay::kLavaFireTicks - 37);

    storage::WorldLevelData level{};
    level.itemsScheme = storage::kCurrentItemsScheme;
    level.player = game::capturePlayer(m_player);
    EXPECT_EQ(level.player->fireTicks, fire);
    const std::string text = storage::encodeLevelData(level);
    EXPECT_NE(text.find("player.fire " + std::to_string(fire)), std::string::npos);
    EXPECT_EQ(text.rfind("BETACRAFT_LEVEL_V2", 0), 0U) << "a new key, no version change";

    storage::WorldLevelData loaded{};
    ASSERT_EQ(storage::decodeLevelData(text, loaded), storage::LevelLoadStatus::Ok);
    ASSERT_TRUE(loaded.player.has_value());
    physics::Player after;
    game::restorePlayer(after, *loaded.player, loaded.itemsScheme);
    EXPECT_EQ(after.burning().fireTicks, fire);

    // The reloaded fire keeps burning on the same schedule.
    tickIdle(1);
    EXPECT_EQ(m_player.burning().fireTicks, fire - 1);

    // Not burning: no key, and an old file without it reads as not burning.
    physics::Player cold;
    level.player = game::capturePlayer(cold);
    const std::string coldText = storage::encodeLevelData(level);
    EXPECT_EQ(coldText.find("player.fire"), std::string::npos);
    storage::WorldLevelData coldLoaded{};
    ASSERT_EQ(storage::decodeLevelData(coldText, coldLoaded), storage::LevelLoadStatus::Ok);
    physics::Player restored;
    restored.burning().fireTicks = 123;
    game::restorePlayer(restored, *coldLoaded.player);
    EXPECT_EQ(restored.burning().fireTicks, 0);

    // A corrupted value is clamped to the lava maximum.
    storage::PlayerSaveData bad = *loaded.player;
    bad.fireTicks = 1 << 30;
    game::restorePlayer(restored, bad);
    EXPECT_EQ(restored.burning().fireTicks, gameplay::kLavaFireTicks);
    bad.fireTicks = -5;
    game::restorePlayer(restored, bad);
    EXPECT_EQ(restored.burning().fireTicks, 0);
}
