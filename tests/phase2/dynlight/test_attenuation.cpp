// P4.6 — attenuation curve shared by CPU and GLSL: L = C * (1 - d/R)^2.
#include "lighting/DynamicLight.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

using namespace betacraft::lighting;

TEST(DynLightAttenuation, FullIntensityAtSource) {
    EXPECT_FLOAT_EQ(attenuation(0.0f, 14.0f), 1.0f);
}

TEST(DynLightAttenuation, ZeroAtRadius) {
    EXPECT_FLOAT_EQ(attenuation(14.0f, 14.0f), 0.0f);
}

TEST(DynLightAttenuation, ZeroBeyondRadius) {
    EXPECT_FLOAT_EQ(attenuation(20.0f, 14.0f), 0.0f);
    EXPECT_FLOAT_EQ(attenuation(1e9f, 14.0f), 0.0f);
}

TEST(DynLightAttenuation, QuadraticFalloffMatchesSpecFormula) {
    // Spec: (1 - d/R)^2 -> at half radius the light is 0.25.
    EXPECT_FLOAT_EQ(attenuation(7.0f, 14.0f), 0.25f);
    EXPECT_NEAR(attenuation(3.5f, 14.0f), 0.5625f, 1e-6f);
}

TEST(DynLightAttenuation, NegativeDistanceTreatedAsZero) {
    EXPECT_FLOAT_EQ(attenuation(-3.0f, 10.0f), 1.0f);
}

TEST(DynLightAttenuation, NonPositiveRadiusEmitsNothing) {
    EXPECT_FLOAT_EQ(attenuation(0.0f, 0.0f), 0.0f);
    EXPECT_FLOAT_EQ(attenuation(0.0f, -5.0f), 0.0f);
}

TEST(DynLightAttenuation, NonFiniteInputsAreSafe) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_FLOAT_EQ(attenuation(nan, 10.0f), 0.0f);
    EXPECT_FLOAT_EQ(attenuation(1.0f, nan), 0.0f);
    EXPECT_FLOAT_EQ(attenuation(inf, 10.0f), 0.0f);
}

TEST(DynLightAttenuation, PropertyMonotonicNonIncreasingAndBounded) {
    std::mt19937 rng(0xD1A1u);
    std::uniform_real_distribution<float> radiusDist(0.5f, 32.0f);
    std::uniform_real_distribution<float> unit(0.0f, 1.5f);
    for (int i = 0; i < 2000; ++i) {
        const float r = radiusDist(rng);
        const float d0 = unit(rng) * r;
        const float d1 = d0 + unit(rng) * r;
        const float a0 = attenuation(d0, r);
        const float a1 = attenuation(d1, r);
        ASSERT_GE(a0, 0.0f);
        ASSERT_LE(a0, 1.0f);
        ASSERT_GE(a0, a1) << "r=" << r << " d0=" << d0 << " d1=" << d1;
    }
}

TEST(DynLightAttenuation, IsConstexprUsable) {
    static_assert(attenuation(0.0f, 4.0f) == 1.0f);
    static_assert(attenuation(4.0f, 4.0f) == 0.0f);
    SUCCEED();
}

// --- Block-light-like 15 level mapping -------------------------------------

TEST(DynLightBlockLevels, RadiusEqualsEmissionLevelLikeBlockLight) {
    // Block light loses one level per block: a level-L emitter reaches zero after L blocks.
    EXPECT_FLOAT_EQ(radiusForEmission(14), 14.0f);
    EXPECT_FLOAT_EQ(radiusForEmission(15), 15.0f);
    EXPECT_FLOAT_EQ(radiusForEmission(0), 0.0f);
}

TEST(DynLightBlockLevels, EmissionClampedToMaxBlockLight) {
    EXPECT_FLOAT_EQ(radiusForEmission(200), static_cast<float>(kMaxBlockLight));
}

TEST(DynLightBlockLevels, LevelAtSourceIsEmission) {
    EXPECT_EQ(blockLightLevelAt(0.0f, 14), 14);
    EXPECT_EQ(blockLightLevelAt(0.0f, 15), 15);
}

TEST(DynLightBlockLevels, LevelAtOrBeyondRadiusIsZero) {
    EXPECT_EQ(blockLightLevelAt(14.0f, 14), 0);
    EXPECT_EQ(blockLightLevelAt(100.0f, 14), 0);
}

TEST(DynLightBlockLevels, MatchesBetaBlockLightAtIntegerDistances) {
    // Beta 1.7.3 block light loses exactly one level per block: a level-L emitter gives
    // max(0, L - d). A held torch must read like a placed one (14 -> 13 at 1 block, 7 at 7,
    // 4 at 10), not like the quadratic visual falloff.
    EXPECT_EQ(blockLightLevelAt(1.0f, 14), 13);
    EXPECT_EQ(blockLightLevelAt(7.0f, 14), 7);
    EXPECT_EQ(blockLightLevelAt(10.0f, 14), 4);
    EXPECT_EQ(blockLightLevelAt(13.0f, 14), 1);
    for (int e = 0; e <= 15; ++e) {
        for (int d = 0; d <= 20; ++d) {
            ASSERT_EQ(blockLightLevelAt(static_cast<float>(d), static_cast<uint8_t>(e)), std::max(0, e - d))
                << "emission " << e << " distance " << d;
        }
    }
}

TEST(DynLightBlockLevels, LevelIsPositiveExactlyWhereTheGpuLightIsNonZero) {
    // A CPU consumer (hand shading) must never call "dark" a point the shader lights, nor the
    // reverse.
    std::mt19937 rng(0x1E7E1u);
    std::uniform_real_distribution<float> dist(-1.0f, 17.0f);
    std::uniform_int_distribution<int> em(0, 15);
    for (int i = 0; i < 5000; ++i) {
        const float d = dist(rng);
        const auto e = static_cast<uint8_t>(em(rng));
        ASSERT_EQ(blockLightLevelAt(d, e) > 0, attenuation(d, radiusForEmission(e)) > 0.0f) << "d=" << d << " e=" << +e;
    }
}

TEST(DynLightBlockLevels, LevelsMonotonicAndWithinRange) {
    for (uint8_t e = 0; e <= 15; ++e) {
        uint8_t prev = 15;
        for (float d = 0.0f; d <= 16.0f; d += 0.25f) {
            const uint8_t lvl = blockLightLevelAt(d, e);
            ASSERT_LE(lvl, e);
            ASSERT_LE(lvl, prev);
            prev = lvl;
        }
    }
}
