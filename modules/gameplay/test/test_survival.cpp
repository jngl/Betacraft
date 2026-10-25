#include "gameplay/Health.hpp"
#include "gameplay/Survival.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <random>

using namespace betacraft::gameplay;

TEST(Breath, FullLungsByDefault) {
    const Breath b{};
    EXPECT_EQ(b.air, kMaxAirTicks);
    EXPECT_EQ(kMaxAirTicks, 300);
    EXPECT_EQ(airBubbles(b), kAirBubbleCount);
}

TEST(Breath, FirstDrowningHitAfterFifteenPlusOneSeconds) {
    Breath b{};
    // Beta: air reaches 0 after 300 ticks, then counts to -20 before the first hit.
    for (int32_t t = 1; t < kMaxAirTicks + kDrowningIntervalTicks; ++t) {
        ASSERT_EQ(tickBreath(b, true), 0) << t;
    }
    EXPECT_EQ(tickBreath(b, true), kDrowningDamage);
    EXPECT_EQ(b.air, 0);
}

TEST(Breath, DrowningRepeatsEveryTwentyTicks) {
    Breath b{.air = 0};
    int32_t hits = 0;
    for (int32_t t = 1; t <= 10 * kDrowningIntervalTicks; ++t) {
        const int32_t dmg = tickBreath(b, true);
        if (dmg > 0) {
            ++hits;
            EXPECT_EQ(t % kDrowningIntervalTicks, 0) << t;
        }
    }
    EXPECT_EQ(hits, 10);
}

TEST(Breath, SurfacingRefillsInstantly) {
    Breath b{.air = 5};
    EXPECT_EQ(tickBreath(b, false), 0);
    EXPECT_EQ(b.air, kMaxAirTicks) << "Beta resets air to max as soon as the head is dry";
}

TEST(Breath, BubblesDrainProgressively) {
    Breath b{};
    EXPECT_EQ(airBubbles(b), 10);
    b.air = 150;
    EXPECT_EQ(airBubbles(b), 5);
    b.air = 31;
    EXPECT_EQ(airBubbles(b), 1);
    b.air = 30;
    EXPECT_EQ(airBubbles(b), 1);
    b.air = 0;
    EXPECT_EQ(airBubbles(b), 0);
    b.air = -15;
    EXPECT_EQ(airBubbles(b), 0) << "negative air never shows negative bubbles";
}

TEST(Breath, PropertyBubblesMonotonicAndBounded) {
    int32_t previous = airBubbles(Breath{.air = -kDrowningIntervalTicks});
    for (int32_t air = -kDrowningIntervalTicks; air <= kMaxAirTicks; ++air) {
        const int32_t bubbles = airBubbles(Breath{.air = air});
        ASSERT_GE(bubbles, 0);
        ASSERT_LE(bubbles, kAirBubbleCount);
        ASSERT_GE(bubbles, previous) << air;
        previous = bubbles;
    }
}

TEST(Breath, DrowningKillsThePlayerThroughHealth) {
    // End-to-end with the hurt-resistance rules: 2 damage every second -> 10 hits for 20 HP.
    Breath b{};
    Health h{};
    int32_t ticks = 0;
    while (!h.isDead()) {
        const int32_t dmg = tickBreath(b, true);
        if (dmg > 0) {
            (void)applyDamage(h, dmg);
        }
        tickHealth(h);
        ASSERT_LT(++ticks, 10'000);
    }
    EXPECT_EQ(ticks, kMaxAirTicks + kDrowningIntervalTicks * 10);
}

TEST(OutOfWorld, SafeAboveTheVoid) {
    EXPECT_EQ(outOfWorldDamage(64.0f), 0);
    EXPECT_EQ(outOfWorldDamage(-15.0f), 0);
    EXPECT_EQ(outOfWorldDamage(kVoidDamageY), 0) << "Beta tests posY < -64 strictly";
}

TEST(OutOfWorld, BelowTheVoidHurtsEveryTick) {
    EXPECT_EQ(outOfWorldDamage(-64.01f), kVoidDamage);
    EXPECT_EQ(outOfWorldDamage(-1.0e9f), kVoidDamage);
    EXPECT_EQ(outOfWorldDamage(-std::numeric_limits<float>::infinity()), kVoidDamage);
}

TEST(OutOfWorld, CustomThresholdForShallowWorlds) {
    // The current engine teleports at Y = -15; the integration can keep that floor.
    EXPECT_EQ(outOfWorldDamage(-15.5f, -15.0f), kVoidDamage);
    EXPECT_EQ(outOfWorldDamage(-14.5f, -15.0f), 0);
}

TEST(OutOfWorld, NaNIsHarmless) {
    EXPECT_EQ(outOfWorldDamage(std::numeric_limits<float>::quiet_NaN()), 0);
}

TEST(OutOfWorld, VoidKillsWithinTwoAndAHalfSeconds) {
    // 4 damage per tick, but equal hits are absorbed for 10 ticks: 4 HP per 0.5 s.
    Health h{};
    int32_t ticks = 0;
    while (!h.isDead()) {
        (void)applyDamage(h, outOfWorldDamage(-100.0f));
        tickHealth(h);
        ASSERT_LT(++ticks, 1000);
    }
    EXPECT_EQ(ticks, 4 * kHurtResistanceTicks + 1);
}

TEST(Breath, ExtremeValuesAreSanitised) {
    constexpr int32_t kMax = std::numeric_limits<int32_t>::max();
    constexpr int32_t kMin = std::numeric_limits<int32_t>::min();
    EXPECT_EQ(airBubbles(Breath{.air = kMax}), kAirBubbleCount);
    EXPECT_EQ(airBubbles(Breath{.air = kMin}), 0);
    Breath low{.air = kMin};
    EXPECT_EQ(tickBreath(low, true), kDrowningDamage) << "already past the trigger";
    EXPECT_EQ(low.air, 0);
    Breath high{.air = kMax};
    EXPECT_EQ(tickBreath(high, true), 0);
    EXPECT_EQ(high.air, kMaxAirTicks - 1);
}

// Beta Entity.fire. Lava sets 600 ticks, a burning entity loses 1 health whenever
// fireTicks % 20 == 0, water puts it out, and the fire burns out on its own after 30 s.
TEST(Fire, LavaSetsSixHundredFireTicks) {
    Burning b{};
    EXPECT_FALSE(b.isBurning());
    EXPECT_EQ(tickFire(b, true, false), 0) << "the lava hit itself is the caller's kLavaDamage";
    EXPECT_EQ(b.fireTicks, kLavaFireTicks);
    EXPECT_EQ(kLavaFireTicks, 600);
    EXPECT_EQ(kLavaDamage, 4);
    EXPECT_EQ(kCactusDamage, 1);
    // Staying in lava keeps re-setting it, whatever burned meanwhile.
    for (int i = 0; i < 50; ++i) {
        (void)tickFire(b, true, false);
        EXPECT_EQ(b.fireTicks, 600);
    }
}

TEST(Fire, FireHurtsOncePerSecond) {
    Burning b{};
    (void)tickFire(b, true, false); // 600
    int32_t damage = 0;
    int32_t hits = 0;
    for (int t = 0; t < 100; ++t) {
        const int32_t d = tickFire(b, false, false);
        EXPECT_TRUE(d == 0 || d == 1);
        damage += d;
        hits += d > 0 ? 1 : 0;
    }
    // 600, 580, 560, 540, 520 are the hurting values within the first 100 ticks.
    EXPECT_EQ(hits, 5);
    EXPECT_EQ(damage, 5);
    EXPECT_EQ(b.fireTicks, 500);
}

TEST(Fire, WaterExtinguishes) {
    Burning b{};
    (void)tickFire(b, true, false);
    (void)tickFire(b, false, false);
    ASSERT_TRUE(b.isBurning());
    EXPECT_EQ(tickFire(b, false, true), 0);
    EXPECT_EQ(b.fireTicks, 0);
    EXPECT_FALSE(b.isBurning());
    // Lava and water together (a flowing mix): lava wins after the water check, as in Beta.
    (void)tickFire(b, true, true);
    EXPECT_EQ(b.fireTicks, 600);
}

TEST(Fire, FireRunsOut) {
    Burning b{};
    (void)tickFire(b, true, false);
    int32_t damage = 0;
    int ticks = 0;
    while (b.isBurning() && ticks < 10000) {
        damage += tickFire(b, false, false);
        ++ticks;
    }
    EXPECT_EQ(ticks, 600);
    EXPECT_EQ(damage, 30) << "600 / 20 one-point hits";
    EXPECT_EQ(tickFire(b, false, false), 0);
    EXPECT_EQ(b.fireTicks, 0);
    // A corrupted counter is clamped instead of burning forever or going negative.
    Burning bad{-50};
    EXPECT_EQ(tickFire(bad, false, false), 0);
    EXPECT_EQ(bad.fireTicks, 0);
    Burning huge{1 << 30};
    (void)tickFire(huge, false, false);
    EXPECT_LE(huge.fireTicks, kLavaFireTicks);
}
