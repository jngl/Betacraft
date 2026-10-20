#include "gameplay/Health.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <random>

using namespace betacraft::gameplay;

namespace {
void tickN(Health& h, int n) {
    for (int i = 0; i < n; ++i) {
        tickHealth(h);
    }
}
} // namespace

TEST(Health, DefaultsToFullPlayerHealth) {
    const Health h{};
    EXPECT_EQ(h.current, kPlayerMaxHealth);
    EXPECT_EQ(h.max, 20);
    EXPECT_FALSE(h.isDead());
    EXPECT_FALSE(h.isInvulnerable());
    EXPECT_FALSE(h.isHurtFlashing());
}

TEST(Health, MakeHealthForMobs) {
    const Health zombie = makeHealth(20);
    EXPECT_EQ(zombie.current, 20);
    const Health chicken = makeHealth(4);
    EXPECT_EQ(chicken.current, 4);
    EXPECT_EQ(chicken.max, 4);
    EXPECT_EQ(makeHealth(0).max, 1) << "max health is at least 1";
    EXPECT_EQ(makeHealth(-7).max, 1);
}

TEST(Health, FullHitAppliesDamageAndStartsTimers) {
    Health h{};
    const HitResult r = applyDamage(h, 4);
    EXPECT_EQ(r.outcome, HitOutcome::Full);
    EXPECT_EQ(r.applied, 4);
    EXPECT_FALSE(r.killed);
    EXPECT_TRUE(r.appliesKnockback());
    EXPECT_EQ(h.current, 16);
    EXPECT_EQ(h.invulnerableTicks, kHurtResistanceTicks);
    EXPECT_EQ(h.hurtFlashTicks, kHurtFlashTicks);
    EXPECT_TRUE(h.isInvulnerable());
    EXPECT_TRUE(h.isHurtFlashing());
}

TEST(Health, HurtTimingConstants) {
    EXPECT_EQ(kHurtResistanceTicks, 10); // Beta: heartsLife > heartsHalvesLife / 2
    EXPECT_EQ(kHurtFlashTicks, 5);       // Roadmap: 0.25 s red flash at 20 TPS
}

TEST(Health, WeakerOrEqualHitDuringResistanceIsAbsorbed) {
    Health h{};
    (void)applyDamage(h, 6);
    const HitResult same = applyDamage(h, 6);
    EXPECT_EQ(same.outcome, HitOutcome::Absorbed);
    EXPECT_EQ(same.applied, 0);
    EXPECT_FALSE(same.appliesKnockback());
    const HitResult weaker = applyDamage(h, 2);
    EXPECT_EQ(weaker.outcome, HitOutcome::Absorbed);
    EXPECT_EQ(h.current, 14);
}

TEST(Health, StrongerHitDuringResistanceDealsOnlyTheDifference) {
    Health h{};
    (void)applyDamage(h, 4);
    tickN(h, 3);
    const HitResult r = applyDamage(h, 10);
    EXPECT_EQ(r.outcome, HitOutcome::Partial);
    EXPECT_EQ(r.applied, 6);
    EXPECT_FALSE(r.appliesKnockback()) << "Beta: no knockback inside the resistance window";
    EXPECT_EQ(h.current, 10);
    EXPECT_EQ(h.lastDamage, 10);
    EXPECT_EQ(h.invulnerableTicks, kHurtResistanceTicks - 3) << "partial hits do not reset the window";
    // A further 10-damage hit is now fully absorbed.
    EXPECT_EQ(applyDamage(h, 10).outcome, HitOutcome::Absorbed);
}

TEST(Health, ResistanceExpiresAfterExactlyTenTicks) {
    Health h{};
    (void)applyDamage(h, 1);
    tickN(h, kHurtResistanceTicks - 1);
    EXPECT_TRUE(h.isInvulnerable());
    EXPECT_EQ(applyDamage(h, 1).outcome, HitOutcome::Absorbed);
    tickHealth(h);
    EXPECT_FALSE(h.isInvulnerable());
    EXPECT_EQ(applyDamage(h, 1).outcome, HitOutcome::Full);
    EXPECT_EQ(h.current, 18);
}

TEST(Health, HurtFlashLastsQuarterSecond) {
    Health h{};
    (void)applyDamage(h, 1);
    tickN(h, kHurtFlashTicks - 1);
    EXPECT_TRUE(h.isHurtFlashing());
    EXPECT_GT(hurtFlashIntensity(h), 0.0f);
    tickHealth(h);
    EXPECT_FALSE(h.isHurtFlashing());
    EXPECT_FLOAT_EQ(hurtFlashIntensity(h), 0.0f);
}

TEST(Health, HurtFlashIntensityFadesLinearly) {
    Health h{};
    (void)applyDamage(h, 1);
    EXPECT_FLOAT_EQ(hurtFlashIntensity(h), 1.0f);
    tickHealth(h);
    EXPECT_FLOAT_EQ(hurtFlashIntensity(h), 4.0f / 5.0f);
}

TEST(Health, NonPositiveDamageIsIgnored) {
    Health h{};
    EXPECT_EQ(applyDamage(h, 0).outcome, HitOutcome::Ignored);
    EXPECT_EQ(applyDamage(h, -5).outcome, HitOutcome::Ignored);
    EXPECT_EQ(h.current, 20);
    EXPECT_FALSE(h.isInvulnerable());
}

TEST(Health, DeathAtZeroAndClampsOverkill) {
    Health h{};
    const HitResult r = applyDamage(h, 25);
    EXPECT_EQ(r.outcome, HitOutcome::Full);
    EXPECT_EQ(r.applied, 20) << "applied damage is clamped to remaining health";
    EXPECT_TRUE(r.killed);
    EXPECT_EQ(h.current, 0);
    EXPECT_TRUE(h.isDead());
}

TEST(Health, ExactLethalDamageKills) {
    Health h = makeHealth(4);
    EXPECT_TRUE(applyDamage(h, 4).killed);
    EXPECT_TRUE(h.isDead());
}

TEST(Health, PartialHitCanKill) {
    Health h = makeHealth(5);
    (void)applyDamage(h, 2);
    const HitResult r = applyDamage(h, 9);
    EXPECT_EQ(r.outcome, HitOutcome::Partial);
    EXPECT_TRUE(r.killed);
    EXPECT_EQ(h.current, 0);
}

TEST(Health, DeadEntitiesIgnoreFurtherDamage) {
    Health h{};
    (void)applyDamage(h, 20);
    tickN(h, 50);
    const HitResult r = applyDamage(h, 5);
    EXPECT_EQ(r.outcome, HitOutcome::Ignored);
    EXPECT_FALSE(r.killed) << "killed is only reported on the transition";
}

TEST(Health, DeathTimerForCorpseRemoval) {
    Health h{};
    (void)applyDamage(h, 20);
    EXPECT_FALSE(isCorpseExpired(h));
    tickN(h, kDeathAnimationTicks - 1);
    EXPECT_FALSE(isCorpseExpired(h));
    tickHealth(h);
    EXPECT_TRUE(isCorpseExpired(h));
    EXPECT_EQ(kDeathAnimationTicks, 20);
}

TEST(Health, HealClampsAndRefusesDead) {
    Health h{};
    (void)applyDamage(h, 7);
    EXPECT_EQ(heal(h, 3), 3);
    EXPECT_EQ(h.current, 16);
    EXPECT_EQ(heal(h, 100), 4);
    EXPECT_EQ(h.current, 20);
    EXPECT_EQ(heal(h, -3), 0);
    (void)applyDamage(h, 99);
    tickN(h, 20);
    (void)applyDamage(h, 99);
    EXPECT_EQ(heal(h, 5), 0);
    EXPECT_TRUE(h.isDead());
}

TEST(Health, HealNeverLowersHealthAboveMax) {
    // current > max can happen after max is lowered or from a deserialised save: healing
    // must neither report a negative amount nor take health away.
    Health h{};
    h.current = 30;
    h.max = 20;
    EXPECT_EQ(heal(h, 5), 0);
    EXPECT_EQ(h.current, 30);
    h.current = 20;
    EXPECT_EQ(heal(h, 5), 0) << "already full";
    EXPECT_EQ(h.current, 20);
}

TEST(Health, HealSaturatesOnHugeAmounts) {
    Health h = makeHealth(20);
    (void)applyDamage(h, 7);
    EXPECT_EQ(heal(h, std::numeric_limits<int32_t>::max()), 7);
    EXPECT_EQ(h.current, 20);
}

TEST(Health, RespawnRestoresFreshState) {
    Health h = makeHealth(20);
    (void)applyDamage(h, 20);
    tickN(h, 7);
    respawn(h);
    EXPECT_EQ(h, makeHealth(20));
    EXPECT_FALSE(h.isDead());
}

TEST(Health, PropertyHealthStaysInRangeUnderRandomAbuse) {
    std::mt19937 rng(1234u);
    std::uniform_int_distribution<int32_t> dmg(-5, 12);
    std::uniform_int_distribution<int32_t> ticks(0, 12);
    for (int trial = 0; trial < 500; ++trial) {
        Health h = makeHealth(20);
        int32_t totalApplied = 0;
        bool sawKill = false;
        for (int step = 0; step < 40; ++step) {
            const int32_t before = h.current;
            const HitResult r = applyDamage(h, dmg(rng));
            ASSERT_GE(r.applied, 0);
            ASSERT_EQ(before - h.current, r.applied);
            totalApplied += r.applied;
            if (r.killed) {
                ASSERT_FALSE(sawKill) << "a death is reported once";
                sawKill = true;
            }
            ASSERT_GE(h.current, 0);
            ASSERT_LE(h.current, h.max);
            ASSERT_EQ(h.isDead(), h.current == 0);
            ASSERT_GE(h.invulnerableTicks, 0);
            ASSERT_LE(h.invulnerableTicks, kHurtResistanceTicks);
            tickN(h, ticks(rng));
        }
        EXPECT_EQ(totalApplied, 20 - h.current);
        EXPECT_EQ(sawKill, h.isDead());
    }
}

TEST(Health, PropertyResistanceCapsDamagePerWindow) {
    // Within one 10-tick window the total damage never exceeds the strongest single hit.
    std::mt19937 rng(99u);
    std::uniform_int_distribution<int32_t> dmg(1, 9);
    for (int trial = 0; trial < 300; ++trial) {
        Health h = makeHealth(1000);
        int32_t strongest = 0;
        for (int t = 0; t < kHurtResistanceTicks; ++t) {
            const int32_t d = dmg(rng);
            strongest = std::max(strongest, d);
            (void)applyDamage(h, d);
            tickHealth(h);
        }
        EXPECT_EQ(1000 - h.current, strongest);
    }
}

// ---------------------------------------------------------------------------
// Fall damage (Beta EntityLiving.fall: ceil(distance - 3))
// ---------------------------------------------------------------------------

TEST(FallDamage, SafeUpToThreeBlocks) {
    EXPECT_EQ(fallDamage(0.0f), 0);
    EXPECT_EQ(fallDamage(3.0f), 0);
    EXPECT_EQ(fallDamage(-10.0f), 0);
}

TEST(FallDamage, CeilAboveThree) {
    EXPECT_EQ(fallDamage(3.01f), 1);
    EXPECT_EQ(fallDamage(4.0f), 1);
    EXPECT_EQ(fallDamage(4.5f), 2);
    EXPECT_EQ(fallDamage(23.0f), 20);
}

TEST(FallDamage, HugeOrInvalidDistancesAreSafe) {
    EXPECT_EQ(fallDamage(1e30f), kMaxFallDamage);
    EXPECT_EQ(fallDamage(std::numeric_limits<float>::infinity()), kMaxFallDamage);
    EXPECT_EQ(fallDamage(std::numeric_limits<float>::quiet_NaN()), 0);
}

TEST(FallDamage, LongFallKillsPlayer) {
    // Diagnostic 4: the player used to be clamped at 1 HP; a 30-block fall must now kill.
    Health h{};
    EXPECT_TRUE(applyDamage(h, fallDamage(30.0f)).killed);
}
