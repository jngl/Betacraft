// Deterministic seeded Simplex noise (2D / 3D) contract tests.
#include "worldgen/SimplexNoise.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <limits>

using betacraft::worldgen::SimplexNoise;

namespace {
// Reference outputs of seed 42 (see GoldenValuesArePinned); regenerate only on purpose.
constexpr double GOLDEN_2D_A = -0.79485156231510212;
constexpr double GOLDEN_2D_B = -0.57624756412167233;
constexpr double GOLDEN_3D_A = -0.27566346992053115;
constexpr double GOLDEN_3D_B = 0.067221713119392235;
} // namespace

TEST(SimplexNoise, SameSeedProducesIdenticalValues3D) {
    const SimplexNoise a(12345ULL);
    const SimplexNoise b(12345ULL);
    for (int i = 0; i < 500; ++i) {
        const double x = i * 0.731 - 100.0;
        const double y = i * 0.173 + 3.0;
        const double z = i * -0.519 + 41.0;
        ASSERT_EQ(a.noise3D(x, y, z), b.noise3D(x, y, z)) << "i=" << i;
    }
}

TEST(SimplexNoise, SameSeedProducesIdenticalValues2D) {
    const SimplexNoise a(987ULL);
    const SimplexNoise b(987ULL);
    for (int i = 0; i < 500; ++i) {
        const double x = i * 1.37 - 250.0;
        const double y = i * -0.61 + 17.0;
        ASSERT_EQ(a.noise2D(x, y), b.noise2D(x, y)) << "i=" << i;
    }
}

TEST(SimplexNoise, ReportsItsSeed) {
    EXPECT_EQ(SimplexNoise(42ULL).getSeed(), 42ULL);
    EXPECT_EQ(SimplexNoise(0xFFFFFFFFFFFFFFFFULL).getSeed(), 0xFFFFFFFFFFFFFFFFULL);
}

TEST(SimplexNoise, CopiesAreBitIdentical) {
    const SimplexNoise a(5150ULL);
    const SimplexNoise copy = a; // NOLINT(performance-unnecessary-copy-initialization)
    for (int i = 0; i < 100; ++i) {
        ASSERT_EQ(a.noise3D(i * 0.3, i * 0.7, i * 1.1), copy.noise3D(i * 0.3, i * 0.7, i * 1.1));
    }
}

// Range: values must stay inside [-1, 1] and actually use a meaningful part of it.
TEST(SimplexNoise, Noise3DStaysWithinUnitRangeAndIsNotDegenerate) {
    const SimplexNoise n(777ULL);
    double lo = 1e9;
    double hi = -1e9;
    double sum = 0.0;
    int count = 0;
    for (int x = 0; x < 48; ++x) {
        for (int y = 0; y < 48; ++y) {
            for (int z = 0; z < 48; ++z) {
                const double v = n.noise3D(x * 0.173 - 3.1, y * 0.211 + 7.7, z * 0.157 - 11.3);
                ASSERT_GE(v, -1.0);
                ASSERT_LE(v, 1.0);
                lo = std::min(lo, v);
                hi = std::max(hi, v);
                sum += v;
                ++count;
            }
        }
    }
    EXPECT_LT(lo, -0.6);
    EXPECT_GT(hi, 0.6);
    EXPECT_NEAR(sum / count, 0.0, 0.08); // zero-centred
}

TEST(SimplexNoise, Noise2DStaysWithinUnitRangeAndIsNotDegenerate) {
    const SimplexNoise n(31337ULL);
    double lo = 1e9;
    double hi = -1e9;
    double sum = 0.0;
    int count = 0;
    for (int x = 0; x < 300; ++x) {
        for (int y = 0; y < 300; ++y) {
            const double v = n.noise2D(x * 0.0931 - 50.0, y * 0.1077 + 20.0);
            ASSERT_GE(v, -1.0);
            ASSERT_LE(v, 1.0);
            lo = std::min(lo, v);
            hi = std::max(hi, v);
            sum += v;
            ++count;
        }
    }
    EXPECT_LT(lo, -0.6);
    EXPECT_GT(hi, 0.6);
    EXPECT_NEAR(sum / count, 0.0, 0.08);
}

TEST(SimplexNoise, HandlesLargeAndNegativeCoordinates) {
    const SimplexNoise n(1ULL);
    const double coords[] = {-1.0e6, -30000000.5, -0.0, 0.0, 1.0e-12, 29999999.25, 1.0e6};
    for (double c : coords) {
        const double v3 = n.noise3D(c, -c * 0.5, c * 0.25);
        const double v2 = n.noise2D(c, -c * 0.75);
        EXPECT_TRUE(std::isfinite(v3)) << c;
        EXPECT_TRUE(std::isfinite(v2)) << c;
        EXPECT_GE(v3, -1.0);
        EXPECT_LE(v3, 1.0);
        EXPECT_GE(v2, -1.0);
        EXPECT_LE(v2, 1.0);
    }
}

// Continuity: simplex noise is C1 with a bounded gradient; a tiny step must yield a tiny change.
TEST(SimplexNoise, Noise3DIsLipschitzContinuous) {
    const SimplexNoise n(2024ULL);
    constexpr double eps = 1.0e-4;
    double maxDelta = 0.0;
    for (int i = 0; i < 4000; ++i) {
        const double x = i * 0.0517 - 40.0;
        const double y = i * 0.0291 + 5.0;
        const double z = i * -0.0433 + 9.0;
        const double v = n.noise3D(x, y, z);
        maxDelta = std::max(maxDelta, std::abs(n.noise3D(x + eps, y, z) - v));
        maxDelta = std::max(maxDelta, std::abs(n.noise3D(x, y + eps, z) - v));
        maxDelta = std::max(maxDelta, std::abs(n.noise3D(x, y, z + eps) - v));
    }
    EXPECT_LT(maxDelta, 0.01); // gradient magnitude < 100 by a wide margin
}

TEST(SimplexNoise, Noise2DIsLipschitzContinuous) {
    const SimplexNoise n(2025ULL);
    constexpr double eps = 1.0e-4;
    double maxDelta = 0.0;
    for (int i = 0; i < 4000; ++i) {
        const double x = i * 0.0517 - 40.0;
        const double y = i * 0.0291 + 5.0;
        const double v = n.noise2D(x, y);
        maxDelta = std::max(maxDelta, std::abs(n.noise2D(x + eps, y) - v));
        maxDelta = std::max(maxDelta, std::abs(n.noise2D(x, y + eps) - v));
    }
    EXPECT_LT(maxDelta, 0.01);
}

// Continuity across simplex cell boundaries: dense 1D walk must never jump.
TEST(SimplexNoise, NoDiscontinuitiesAlongDenseWalk) {
    const SimplexNoise n(99ULL);
    double prev3 = n.noise3D(0.0, 0.3, 0.7);
    double prev2 = n.noise2D(0.0, 0.3);
    for (int i = 1; i < 20000; ++i) {
        const double t = i * 0.001;
        const double v3 = n.noise3D(t, 0.3 + t * 0.5, 0.7 - t * 0.25);
        const double v2 = n.noise2D(t, 0.3 + t * 0.5);
        ASSERT_LT(std::abs(v3 - prev3), 0.05) << "t=" << t;
        ASSERT_LT(std::abs(v2 - prev2), 0.05) << "t=" << t;
        prev3 = v3;
        prev2 = v2;
    }
}

TEST(SimplexNoise, DifferentSeedsProduceDifferentFields) {
    const SimplexNoise a(1ULL);
    const SimplexNoise b(2ULL);
    int differing = 0;
    double sqDiff = 0.0;
    for (int i = 0; i < 1000; ++i) {
        const double x = i * 0.37;
        const double y = i * 0.11 - 4.0;
        const double z = i * 0.23 + 8.0;
        const double d = a.noise3D(x, y, z) - b.noise3D(x, y, z);
        if (std::abs(d) > 1e-9) ++differing;
        sqDiff += d * d;
    }
    EXPECT_GT(differing, 950);
    EXPECT_GT(sqDiff / 1000.0, 0.01);
}

TEST(SimplexNoise, HighSeedBitsAlsoChangeTheField) {
    // Seeds differing only in the upper 32 bits must not collide (64-bit world seeds).
    const SimplexNoise a(0x0000000100000007ULL);
    const SimplexNoise b(0x0000000200000007ULL);
    int differing = 0;
    for (int i = 0; i < 200; ++i) {
        if (a.noise2D(i * 0.71, i * 0.13) != b.noise2D(i * 0.71, i * 0.13)) ++differing;
    }
    EXPECT_GT(differing, 180);
}

TEST(SimplexNoise, FbmIsNormalisedAndDeterministic) {
    const SimplexNoise n(4242ULL);
    for (int i = 0; i < 2000; ++i) {
        const double x = i * 0.093;
        const double y = i * -0.051;
        const double z = i * 0.077;
        const double f3 = n.fbm3D(x, y, z, 4);
        const double f2 = n.fbm2D(x, y, 5, 2.0, 0.5);
        ASSERT_GE(f3, -1.0);
        ASSERT_LE(f3, 1.0);
        ASSERT_GE(f2, -1.0);
        ASSERT_LE(f2, 1.0);
        ASSERT_EQ(f3, n.fbm3D(x, y, z, 4));
    }
}

TEST(SimplexNoise, FbmWithOneOctaveEqualsBaseNoise) {
    const SimplexNoise n(8ULL);
    for (int i = 0; i < 100; ++i) {
        EXPECT_DOUBLE_EQ(n.fbm3D(i * 0.3, i * 0.2, i * 0.1, 1), n.noise3D(i * 0.3, i * 0.2, i * 0.1));
        EXPECT_DOUBLE_EQ(n.fbm2D(i * 0.3, i * 0.2, 1), n.noise2D(i * 0.3, i * 0.2));
    }
}

TEST(SimplexNoise, FbmWithNonPositiveOctavesReturnsZero) {
    const SimplexNoise n(8ULL);
    EXPECT_EQ(n.fbm3D(1.5, 2.5, 3.5, 0), 0.0);
    EXPECT_EQ(n.fbm2D(1.5, 2.5, -3), 0.0);
}

// ---- Review fixes: the [-1, 1] contract must be met by the scale factor, not by the clamp ----

namespace {

struct RangeScan {
    double lo{1e9};
    double hi{-1e9};
    int saturated{0}; // samples sitting exactly on the clamp bounds
    void add(double v) {
        lo = std::min(lo, v);
        hi = std::max(hi, v);
        if (std::abs(v) >= 1.0) ++saturated;
    }
};

} // namespace

TEST(SimplexNoise, Noise2DUsesTheWholeUnitRangeWithoutSaturating) {
    // A scale factor that is too small (unit gradients with Gustavson's factor 70 peak at
    // +-0.706) or too large (clamp engages) must both fail: |extremum| > 0.9, no saturation.
    for (uint64_t seed : {1ULL, 42ULL, 99ULL}) {
        const SimplexNoise n(seed);
        RangeScan r;
        for (int x = 0; x < 700; ++x)
            for (int y = 0; y < 700; ++y) r.add(n.noise2D(x * 0.0371 - 13.0, y * 0.0413 + 5.0));
        EXPECT_LT(r.lo, -0.9) << seed;
        EXPECT_GT(r.hi, 0.9) << seed;
        EXPECT_EQ(r.saturated, 0) << seed;
    }
}

TEST(SimplexNoise, Noise3DUsesTheWholeUnitRangeWithoutSaturating) {
    for (uint64_t seed : {1ULL, 42ULL}) {
        const SimplexNoise n(seed);
        RangeScan r;
        for (int x = 0; x < 80; ++x)
            for (int y = 0; y < 80; ++y)
                for (int z = 0; z < 80; ++z) r.add(n.noise3D(x * 0.0731, y * 0.0613 - 2.0, z * 0.0571 + 9.0));
        EXPECT_LT(r.lo, -0.9) << seed;
        EXPECT_GT(r.hi, 0.9) << seed;
        EXPECT_EQ(r.saturated, 0) << seed;
    }
}

// Golden values: any change to the permutation, the RNG, the gradient sets or the scale
// silently changes every generated world. These pin the exact field (seed 42).
TEST(SimplexNoise, GoldenValuesArePinned) {
    const SimplexNoise n(42ULL);
    EXPECT_NEAR(n.noise2D(0.5, 0.25), GOLDEN_2D_A, 1e-12);
    EXPECT_NEAR(n.noise2D(-123.456, 789.012), GOLDEN_2D_B, 1e-12);
    EXPECT_NEAR(n.noise3D(0.5, 0.25, 0.125), GOLDEN_3D_A, 1e-12);
    EXPECT_NEAR(n.noise3D(-31.7, 12.9, 1024.3), GOLDEN_3D_B, 1e-12);
}

// Non-finite or astronomically large inputs used to reach static_cast<int64_t>(floor(v)),
// which is undefined behaviour; they are now outside the supported domain and yield 0.
TEST(SimplexNoise, NonFiniteInputsYieldZero) {
    const SimplexNoise n(7ULL);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    for (double bad : {nan, inf, -inf, 1.0e300, -1.0e300}) {
        EXPECT_EQ(n.noise2D(bad, 0.5), 0.0) << bad;
        EXPECT_EQ(n.noise2D(0.5, bad), 0.0) << bad;
        EXPECT_EQ(n.noise3D(bad, 0.5, 0.5), 0.0) << bad;
        EXPECT_EQ(n.noise3D(0.5, bad, 0.5), 0.0) << bad;
        EXPECT_EQ(n.noise3D(0.5, 0.5, bad), 0.0) << bad;
    }
}

TEST(SimplexNoise, FbmStaysFiniteForExtremeOctavesAndLacunarity) {
    const SimplexNoise n(11ULL);
    struct Case { int octaves; double lacunarity; double gain; };
    for (const Case c : {Case{64, 2.0, 0.5}, Case{200, 2.0, 0.5}, Case{40, 4.0, 0.5},
                         Case{1000, 1.0e10, 0.5}, Case{32, 2.0, 1.5}, Case{50, 2.0, 1.0e20},
                         Case{8, 2.0, -0.5}}) {
        for (int i = 0; i < 50; ++i) {
            const double x = i * 1.37 + 0.11;
            const double f3 = n.fbm3D(x, -x * 0.5, x * 0.25, c.octaves, c.lacunarity, c.gain);
            const double f2 = n.fbm2D(x, -x * 0.75, c.octaves, c.lacunarity, c.gain);
            ASSERT_TRUE(std::isfinite(f3)) << c.octaves << " " << c.lacunarity << " " << c.gain;
            ASSERT_TRUE(std::isfinite(f2)) << c.octaves << " " << c.lacunarity << " " << c.gain;
            ASSERT_GE(f3, -1.0);
            ASSERT_LE(f3, 1.0);
            ASSERT_GE(f2, -1.0);
            ASSERT_LE(f2, 1.0);
        }
    }
    EXPECT_EQ(n.fbm3D(std::numeric_limits<double>::quiet_NaN(), 0.0, 0.0, 4), 0.0);
    EXPECT_EQ(n.fbm2D(0.0, std::numeric_limits<double>::infinity(), 4), 0.0);
}

TEST(SimplexNoise, FbmOctavesAreCappedSoTheLoopIsBounded) {
    const SimplexNoise n(12ULL);
    // Beyond MAX_FBM_OCTAVES extra octaves contribute nothing (and cost nothing).
    EXPECT_EQ(n.fbm3D(1.3, 2.7, -0.4, SimplexNoise::MAX_FBM_OCTAVES),
              n.fbm3D(1.3, 2.7, -0.4, SimplexNoise::MAX_FBM_OCTAVES + 500));
    EXPECT_EQ(n.fbm2D(1.3, 2.7, SimplexNoise::MAX_FBM_OCTAVES),
              n.fbm2D(1.3, 2.7, std::numeric_limits<int>::max()));
}
