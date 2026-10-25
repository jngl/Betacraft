// Pins the carver to the specification itself rather than to its own implementation.
//
// SpecOracle below is an independent transliteration of the original cave design spec's reference
// code (VolumetricCaveCarver::computeVerticalEnvelope / shouldCarve /
// getExcavationFillBlock) with every constant written as a literal from the spec. The only
// project-specific parts are the noise primitive (SimplexNoise instead of PerlinNoise, as
// Requires) and the per-field seed derivation, which is itself pinned because changing
// it changes every world.
#include "worldgen/CaveCarver.hpp"
#include "worldgen/SeededRandom.hpp"
#include "worldgen/SimplexNoise.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>

using namespace betacraft::worldgen;

namespace {

class SpecOracle {
public:
    explicit SpecOracle(uint64_t seed)
        : m_wormA(mix64(seed ^ 0x9E3779B97F4A7C15ULL)),
          m_wormB(mix64(seed ^ 0x85EBCA6BC2B2AE35ULL)),
          m_cheese(mix64(seed ^ 0xC2B2AE3D27D4EB4FULL)),
          m_aquifer(mix64(seed ^ 0x165667B19E3779F9ULL)) {}

    static float envelope(int32_t wy, int32_t surfaceHeight, int32_t waterFloorHeight, bool isWaterBody) {
        const float y = static_cast<float>(wy);
        if (y <= 5.0f) return 0.0f;
        const float bottomFactor = std::clamp((y - 5.0f) / 4.0f, 0.0f, 1.0f);
        const float effectiveCeiling = isWaterBody ? (static_cast<float>(waterFloorHeight) - 6.0f)
                                                   : (static_cast<float>(surfaceHeight) - 3.0f);
        if (y >= effectiveCeiling) return 0.0f;
        const float topFactor = std::clamp((effectiveCeiling - y) / 4.0f, 0.0f, 1.0f);
        auto smoothstep = [](float t) { return t * t * (3.0f - 2.0f * t); };
        return smoothstep(bottomFactor) * smoothstep(topFactor);
    }

    [[nodiscard]] CaveKind classify(int32_t wx, int32_t wy, int32_t wz, const ColumnContext& c) const {
        if (wy <= 5 || wy >= c.surfaceHeight - 3) return CaveKind::None;
        const float a = envelope(wy, c.surfaceHeight, c.waterFloorHeight, c.isWaterBody);
        if (a <= 0.0001f) return CaveKind::None;
        const double fx = static_cast<double>(wx) * 0.038;
        const double fy = static_cast<double>(wy) * 0.048;
        const double fz = static_cast<double>(wz) * 0.038;
        const double wa = m_wormA.noise3D(fx, fy, fz);
        const double wb = m_wormB.noise3D(fx + 128.5, fy + 43.2, fz - 89.7);
        if ((wa * wa + wb * wb) < 0.0165 * static_cast<double>(a)) return CaveKind::Spaghetti;
        if (wy < 50) {
            const double cheeseVal = m_cheese.noise3D(fx * 0.5, fy * 0.6, fz * 0.5);
            if (cheeseVal * static_cast<double>(a) > 0.58) return CaveKind::Cheese;
        }
        return CaveKind::None;
    }

    [[nodiscard]] CaveFill fill(int32_t wx, int32_t wy, int32_t wz) const {
        if (wy <= 10) return CaveFill::Lava;
        if (wy <= 32 && m_aquifer.noise2D(static_cast<double>(wx) * 0.015, static_cast<double>(wz) * 0.015) > 0.42) {
            return CaveFill::Water;
        }
        return CaveFill::Air;
    }

private:
    SimplexNoise m_wormA;
    SimplexNoise m_wormB;
    SimplexNoise m_cheese;
    SimplexNoise m_aquifer;
};

// Contexts chosen so that A(y) sweeps the whole (0, 1) range in many columns: shallow land,
// deep land, a sea whose aquitard ceiling sits mid-height, and a land column guarded by a sea.
constexpr std::array<ColumnContext, 5> CONTEXTS{{
    {20, 20, false}, {70, 70, false}, {120, 120, false}, {44, 44, true}, {90, 58, true},
}};

// Recorded from the reviewed implementation; regenerate only on purpose.
constexpr uint64_t GOLDEN_CLASSIFY_HASH = 10911151433420512430ULL;

uint64_t fnv(uint64_t h, uint64_t v) {
    return (h ^ v) * 0x100000001B3ULL;
}

} // namespace

TEST(CaveSpecPinning, ConstantsMatchTheSpecification) {
    EXPECT_EQ(CaveCarver::BEDROCK_GUARD_MAX_Y, 5);
    EXPECT_FLOAT_EQ(CaveCarver::BEDROCK_FADE_IN, 4.0f);
    EXPECT_FLOAT_EQ(CaveCarver::MIN_AQUITARD_THICKNESS, 6.0f);
    EXPECT_FLOAT_EQ(CaveCarver::LAND_CEILING_OFFSET, 3.0f);
    EXPECT_FLOAT_EQ(CaveCarver::SURFACE_FADE_OUT, 4.0f);
    EXPECT_DOUBLE_EQ(CaveCarver::HORIZONTAL_FREQ, 0.038);
    EXPECT_DOUBLE_EQ(CaveCarver::VERTICAL_FREQ, 0.048);
    EXPECT_DOUBLE_EQ(CaveCarver::WORM_THRESHOLD, 0.0165);
    EXPECT_DOUBLE_EQ(CaveCarver::CHEESE_THRESHOLD, 0.58);
    EXPECT_EQ(CaveCarver::CHEESE_MAX_Y, 50);
    EXPECT_EQ(CaveCarver::LAVA_MAX_Y, 10);
    EXPECT_EQ(CaveCarver::AQUIFER_MIN_Y, 11);
    EXPECT_EQ(CaveCarver::AQUIFER_MAX_Y, 32);
    EXPECT_DOUBLE_EQ(CaveCarver::AQUIFER_FREQ, 0.015);
    EXPECT_DOUBLE_EQ(CaveCarver::AQUIFER_THRESHOLD, 0.42);
}

TEST(CaveSpecPinning, EnvelopeEqualsTheSpecReferenceEverywhere) {
    for (const ColumnContext& c : CONTEXTS)
        for (int32_t y = -4; y < 140; ++y)
            ASSERT_EQ(CaveCarver::computeVerticalEnvelope(y, c),
                      SpecOracle::envelope(y, c.surfaceHeight, c.waterFloorHeight, c.isWaterBody))
                << "y=" << y << " surface=" << c.surfaceHeight;
}

// The decisive test: any change to the A(y) modulation (e.g. dropping "* envelope" from the
// worm test), a frequency, the wb offsets or the cheese scaling breaks voxel equality.
TEST(CaveSpecPinning, ClassifyEqualsTheSpecReferenceFormula) {
    for (uint64_t seed : {173173173ULL, 5ULL}) {
        const CaveCarver carver(seed);
        const SpecOracle oracle(seed);
        int partialEnvelopeCarves = 0; // carved voxels where 0 < A(y) < 1
        for (const ColumnContext& c : CONTEXTS)
            for (int32_t x = -40; x < 40; x += 3)
                for (int32_t z = -40; z < 40; z += 3)
                    for (int32_t y = 0; y < 128; ++y) {
                        const CaveKind want = oracle.classify(x, y, z, c);
                        ASSERT_EQ(carver.classify(x, y, z, c), want)
                            << "seed=" << seed << " at " << x << "," << y << "," << z << " surface=" << c.surfaceHeight;
                        const float a = CaveCarver::computeVerticalEnvelope(y, c);
                        if (want != CaveKind::None && a < 1.0f) ++partialEnvelopeCarves;
                    }
        // Sanity: the sample really exercises the modulated band, not only A = 1.
        EXPECT_GT(partialEnvelopeCarves, 50) << seed;
    }
}

TEST(CaveSpecPinning, ExcavationFillEqualsTheSpecReference) {
    const CaveCarver carver(173173173ULL);
    const SpecOracle oracle(173173173ULL);
    int water = 0;
    for (int32_t x = -600; x < 600; x += 13)
        for (int32_t z = -600; z < 600; z += 13)
            for (int32_t y = 0; y < 128; y += 3) {
                const CaveFill want = oracle.fill(x, y, z);
                ASSERT_EQ(carver.getExcavationFill(x, y, z), want) << x << "," << y << "," << z;
                if (want == CaveFill::Water) ++water;
            }
    EXPECT_GT(water, 0);
}

// With the corrected noise2D amplitude (+-0.998), aq > 0.42 is 42 % of the real range.
// On Simplex noise that floods about 27 % of columns (the +-0.706 field gave ~18 %; the
// spec's own PerlinNoise reference ~4.5 %), so this band is a sanity pin of the calibrated
// literal threshold, not a tuning target.
TEST(CaveSpecPinning, AquiferColumnShareMatchesACalibratedThreshold) {
    const CaveCarver carver(173173173ULL);
    int wet = 0;
    int total = 0;
    for (int32_t x = -2000; x < 2000; x += 11)
        for (int32_t z = -2000; z < 2000; z += 11) {
            ++total;
            if (carver.getExcavationFill(x, 20, z) == CaveFill::Water) ++wet;
        }
    const double share = static_cast<double>(wet) / total;
    EXPECT_GT(share, 0.20);
    EXPECT_LT(share, 0.35);
}

// Golden snapshot of the whole decision field: guards against silent world changes caused by
// the permutation, the RNG, the seed derivation or the noise scale (determinism tests only
// compare two instances of the same build).
TEST(CaveSpecPinning, GoldenClassificationSnapshot) {
    const CaveCarver carver(173173173ULL);
    uint64_t h = 0xCBF29CE484222325ULL;
    std::array<int, 3> kinds{};
    for (const ColumnContext& c : CONTEXTS)
        for (int32_t x = -24; x < 24; x += 3)
            for (int32_t z = -24; z < 24; z += 3)
                for (int32_t y = 0; y < 128; y += 2) {
                    const CaveKind k = carver.classify(x, y, z, c);
                    ++kinds[static_cast<size_t>(k)];
                    h = fnv(h, static_cast<uint64_t>(k));
                    h = fnv(h, static_cast<uint64_t>(carver.getExcavationFill(x * 37, y, z * 37)));
                }
    EXPECT_EQ(h, GOLDEN_CLASSIFY_HASH) << "kinds: " << kinds[0] << " " << kinds[1] << " " << kinds[2];
}
