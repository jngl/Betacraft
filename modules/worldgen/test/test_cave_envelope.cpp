// Vertical modulation envelope A(y).
//   * Bedrock guard: A(y) = 0 for every y <= 5.
//   * Bottom fade-in over 4 blocks (Y 5 -> 9) and top fade-out over 4 blocks, smoothed by
//     the Hermite polynomial S(t) = 3t^2 - 2t^3.
//   * Ceiling = surface - 3 on land, water_floor - 6 under oceans / rivers (aquitard guard).
#include "worldgen/CaveCarver.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>

using betacraft::worldgen::CaveCarver;
using betacraft::worldgen::ColumnContext;

namespace {

float hermite(float t) { return t * t * (3.0f - 2.0f * t); }

constexpr ColumnContext land(int32_t surface) { return ColumnContext{surface, surface, false}; }
constexpr ColumnContext water(int32_t floorY) { return ColumnContext{floorY, floorY, true}; }

} // namespace

TEST(CaveEnvelope, BedrockGuardIsZeroForEveryYAtOrBelowFive) {
    for (int32_t y = -64; y <= 5; ++y) {
        for (int32_t surface : {6, 40, 64, 100, 127}) {
            ASSERT_EQ(CaveCarver::computeVerticalEnvelope(y, surface, surface, false), 0.0f) << y;
            ASSERT_EQ(CaveCarver::computeVerticalEnvelope(y, surface, surface, true), 0.0f) << y;
        }
    }
}

TEST(CaveEnvelope, BedrockGuardIsAvailableAtCompileTime) {
    static_assert(CaveCarver::computeVerticalEnvelope(5, 100, 100, false) == 0.0f);
    static_assert(CaveCarver::computeVerticalEnvelope(0, 100, 100, false) == 0.0f);
    SUCCEED();
}

TEST(CaveEnvelope, BottomFadeInFollowsHermiteRampFromFiveToNine) {
    // Surface far above so the top factor is 1.
    EXPECT_FLOAT_EQ(CaveCarver::computeVerticalEnvelope(6, 120, 120, false), hermite(0.25f));
    EXPECT_FLOAT_EQ(CaveCarver::computeVerticalEnvelope(7, 120, 120, false), hermite(0.50f));
    EXPECT_FLOAT_EQ(CaveCarver::computeVerticalEnvelope(8, 120, 120, false), hermite(0.75f));
    EXPECT_FLOAT_EQ(CaveCarver::computeVerticalEnvelope(9, 120, 120, false), 1.0f);
    EXPECT_FLOAT_EQ(CaveCarver::computeVerticalEnvelope(40, 120, 120, false), 1.0f);
}

TEST(CaveEnvelope, LandCeilingIsSurfaceMinusThreeWithFourBlockFadeOut) {
    constexpr int32_t surface = 80; // ceiling = 77
    EXPECT_EQ(CaveCarver::computeVerticalEnvelope(77, surface, surface, false), 0.0f);
    EXPECT_EQ(CaveCarver::computeVerticalEnvelope(78, surface, surface, false), 0.0f);
    EXPECT_EQ(CaveCarver::computeVerticalEnvelope(127, surface, surface, false), 0.0f);
    EXPECT_FLOAT_EQ(CaveCarver::computeVerticalEnvelope(76, surface, surface, false), hermite(0.25f));
    EXPECT_FLOAT_EQ(CaveCarver::computeVerticalEnvelope(75, surface, surface, false), hermite(0.50f));
    EXPECT_FLOAT_EQ(CaveCarver::computeVerticalEnvelope(73, surface, surface, false), 1.0f);
}

TEST(CaveEnvelope, WaterBodyCeilingIsWaterFloorMinusSix) {
    constexpr int32_t floorY = 50; // ceiling = 44
    EXPECT_EQ(CaveCarver::computeVerticalEnvelope(44, floorY, floorY, true), 0.0f);
    EXPECT_EQ(CaveCarver::computeVerticalEnvelope(47, floorY, floorY, true), 0.0f);
    EXPECT_FLOAT_EQ(CaveCarver::computeVerticalEnvelope(43, floorY, floorY, true), hermite(0.25f));
    EXPECT_FLOAT_EQ(CaveCarver::computeVerticalEnvelope(40, floorY, floorY, true), 1.0f);
}

TEST(CaveEnvelope, WaterFloorIsUsedOnlyForWaterBodies) {
    // A land column ignores waterFloorHeight entirely, even if it is (bogusly) low.
    EXPECT_FLOAT_EQ(CaveCarver::computeVerticalEnvelope(50, 90, 10, false), 1.0f);
    // A water column ignores the surface height: the aquitard ceiling governs.
    EXPECT_EQ(CaveCarver::computeVerticalEnvelope(50, 90, 52, true), 0.0f);
}

TEST(CaveEnvelope, ShallowColumnsCollapseToZeroEverywhere) {
    // Surface at 8 => ceiling at 5 overlaps the bedrock guard: nothing may be carved.
    for (int32_t y = 0; y < 128; ++y) {
        ASSERT_EQ(CaveCarver::computeVerticalEnvelope(y, 8, 8, false), 0.0f) << y;
        ASSERT_EQ(CaveCarver::computeVerticalEnvelope(y, 11, 11, true), 0.0f) << y;
    }
}

TEST(CaveEnvelope, OverloadTakingColumnContextMatches) {
    for (int32_t y = 0; y < 128; ++y) {
        ASSERT_EQ(CaveCarver::computeVerticalEnvelope(y, land(70)),
                  CaveCarver::computeVerticalEnvelope(y, 70, 70, false));
        ASSERT_EQ(CaveCarver::computeVerticalEnvelope(y, water(55)),
                  CaveCarver::computeVerticalEnvelope(y, 55, 55, true));
    }
}

// Property sweep: A in [0, 1], C0-continuous with at most a Hermite step per block,
// and zero outside the open band (5, ceiling).
TEST(CaveEnvelope, PropertySweepRangeContinuityAndSupport) {
    for (int32_t surface = 0; surface < 128; ++surface) {
        for (int isWater = 0; isWater < 2; ++isWater) {
            const int32_t ceiling = isWater ? surface - 6 : surface - 3;
            float prev = CaveCarver::computeVerticalEnvelope(-1, surface, surface, isWater != 0);
            for (int32_t y = 0; y < 128; ++y) {
                const float a = CaveCarver::computeVerticalEnvelope(y, surface, surface, isWater != 0);
                ASSERT_GE(a, 0.0f);
                ASSERT_LE(a, 1.0f);
                ASSERT_LE(std::abs(a - prev), hermite(0.75f) - hermite(0.25f) + 1e-6f)
                    << "surface=" << surface << " y=" << y;
                if (y <= 5 || y >= ceiling) {
                    ASSERT_EQ(a, 0.0f) << "surface=" << surface << " y=" << y;
                } else {
                    ASSERT_GT(a, 0.0f) << "surface=" << surface << " y=" << y;
                }
                prev = a;
            }
        }
    }
}
