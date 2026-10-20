// S4.3: the pure Beta 1.7.3 spawning rules (gameplay/MobSpawnRules): sky darkening over the
// day, effective light, the hostile and passive light tests, mob caps, the sun curve and
// despawning.
#include "gameplay/MobSpawnRules.hpp"
#include "render/SkyRenderer.hpp"

#include <gtest/gtest.h>

using namespace betacraft;
using gameplay::MobCategory;
using gameplay::SplitMix64;
using voxel::BlockId;

TEST(MobSpawnRules, SkylightSubtractedIsZeroAtNoonElevenAtMidnightAndMonotonic) {
    EXPECT_EQ(gameplay::skylightSubtracted(0.25f), 0);
    EXPECT_EQ(gameplay::skylightSubtracted(0.75f), 11);
    int32_t previous = 0;
    for (int i = 0; i <= 100; ++i) {
        const float t = 0.25f + 0.5f * static_cast<float>(i) / 100.0f;
        const int32_t s = gameplay::skylightSubtracted(t);
        EXPECT_GE(s, previous) << "t = " << t;
        EXPECT_GE(s, 0);
        EXPECT_LE(s, 11);
        previous = s;
    }
    EXPECT_EQ(previous, 11);
    // Rain and thunder darken the noon sky (S8.10).
    EXPECT_GT(gameplay::skylightSubtracted(0.25f, 1.0f, 1.0f), 0);
}

TEST(MobSpawnRules, EffectiveLightTakesBlockLightUnderABlackSky) {
    EXPECT_EQ(gameplay::effectiveLight(15, 0, 11), 4);
    EXPECT_EQ(gameplay::effectiveLight(15, 0, 0), 15);
    EXPECT_EQ(gameplay::effectiveLight(4, 12, 11), 12);
    EXPECT_EQ(gameplay::effectiveLight(0, 7, 0), 7);
    EXPECT_EQ(gameplay::effectiveLight(3, 0, 11), 0); // never below zero
}

TEST(MobSpawnRules, NoHostileAboveLightSeven) {
    SplitMix64 rng(1234);
    for (uint8_t effective = 8; effective <= 15; ++effective) {
        for (int i = 0; i < 2000; ++i) {
            EXPECT_FALSE(gameplay::hostileLightOk(0, effective, rng));
        }
    }
    // Pitch dark caves spawn often; open sky at full raw light only half the time at best.
    int32_t dark = 0;
    int32_t outdoors = 0;
    for (int i = 0; i < 4000; ++i) {
        dark += gameplay::hostileLightOk(0, 0, rng) ? 1 : 0;
        outdoors += gameplay::hostileLightOk(15, 4, rng) ? 1 : 0;
    }
    EXPECT_EQ(dark, 4000);
    EXPECT_GT(outdoors, 0);
    EXPECT_LT(outdoors, dark / 2);
}

TEST(MobSpawnRules, PassivesNeedGrassAndLight) {
    EXPECT_TRUE(gameplay::passiveSpawnOk(BlockId::Grass, 9));
    EXPECT_TRUE(gameplay::passiveSpawnOk(BlockId::Grass, 15));
    EXPECT_FALSE(gameplay::passiveSpawnOk(BlockId::Grass, 8));
    EXPECT_FALSE(gameplay::passiveSpawnOk(BlockId::Dirt, 15));
    EXPECT_FALSE(gameplay::passiveSpawnOk(BlockId::Stone, 15));
}

TEST(MobSpawnRules, MobCapIsProportionalAndHardCapped) {
    EXPECT_EQ(gameplay::mobCap(MobCategory::Hostile, 0), 0);
    EXPECT_EQ(gameplay::mobCap(MobCategory::Passive, 0), 0);
    EXPECT_EQ(gameplay::mobCap(MobCategory::Hostile, 256), 24); // 70, hard-capped
    EXPECT_EQ(gameplay::mobCap(MobCategory::Passive, 256), 12); // 15, hard-capped
    EXPECT_EQ(gameplay::mobCap(MobCategory::Hostile, 64), 17);  // 70 * 64 / 256
    EXPECT_EQ(gameplay::mobCap(MobCategory::Hostile, 32), 8);
    EXPECT_EQ(gameplay::mobCap(MobCategory::Passive, 128), 7);  // 15 * 128 / 256
    EXPECT_EQ(gameplay::mobCap(MobCategory::Passive, 64), 3);
    int32_t previous = 0;
    for (size_t chunks = 0; chunks <= 400; ++chunks) {
        const int32_t cap = gameplay::mobCap(MobCategory::Hostile, chunks);
        EXPECT_GE(cap, previous);
        EXPECT_LE(cap, gameplay::kHostileHardCap);
        EXPECT_LE(gameplay::mobCap(MobCategory::Passive, chunks), gameplay::kPassiveHardCap);
        previous = cap;
    }
    EXPECT_EQ(gameplay::mobCap(MobCategory::Hostile, static_cast<size_t>(-1)), 24);
}

TEST(MobSpawnRules, SunBrightnessMatchesTheSkyRenderer) {
    for (int i = 0; i <= 200; ++i) {
        const float t = static_cast<float>(i) / 200.0f;
        EXPECT_NEAR(gameplay::sunBrightness(t), render::SkyRenderer::evaluateAtmosphere(t, false).sunBrightness, 1e-5f)
            << "t = " << t;
    }
    EXPECT_FLOAT_EQ(gameplay::sunBrightness(0.25f), 1.0f);
    EXPECT_FLOAT_EQ(gameplay::sunBrightness(0.75f), 0.16f);
}

TEST(MobSpawnRules, DespawnBeyond128NeverWithin32) {
    SplitMix64 rng(99);
    for (int i = 0; i < 5000; ++i) {
        EXPECT_TRUE(gameplay::shouldDespawn(128.5f, 0, rng));
        EXPECT_FALSE(gameplay::shouldDespawn(31.9f, 100000, rng));
        EXPECT_FALSE(gameplay::shouldDespawn(64.0f, 600, rng)); // too young
    }
    int32_t despawned = 0;
    for (int i = 0; i < 80000; ++i) {
        despawned += gameplay::shouldDespawn(64.0f, 601, rng) ? 1 : 0;
    }
    EXPECT_GT(despawned, 50); // about 1 in 800
    EXPECT_LT(despawned, 200);
}
