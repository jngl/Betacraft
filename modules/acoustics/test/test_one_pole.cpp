// One-pole low-pass (occlusion LPF) and EMA parameter smoothing (anti-zipper).
#include "acoustics/OnePoleFilter.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <random>
#include <vector>

namespace {

using namespace betacraft::acoustics;

constexpr float FS = 44100.0f;

// Steady-state peak amplitude of the filter's response to a unit sine at `freqHz`.
float measureSineGain(float cutoffHz, float freqHz) {
    OnePoleLowPass lpf(FS);
    lpf.setCutoff(cutoffHz);
    float peak = 0.0f;
    const int n = 44100;
    for (int i = 0; i < n; ++i) {
        const float x = std::sin(2.0f * std::numbers::pi_v<float> * freqHz * static_cast<float>(i) / FS);
        const float y = lpf.process(x);
        if (i > n / 2) {
            peak = std::max(peak, std::fabs(y));
        }
    }
    return peak;
}

float measureNyquistGain(float cutoffHz) {
    OnePoleLowPass lpf(FS);
    lpf.setCutoff(cutoffHz);
    float y = 0.0f;
    for (int i = 0; i < 4000; ++i) {
        y = lpf.process((i % 2 == 0) ? 1.0f : -1.0f);
    }
    return std::fabs(y);
}

} // namespace

TEST(OnePoleCoefficient, MatchesExactMinusThreeDbDesign) {
    // a = sqrt(k (k + 2)) - k, k = 1 - cos(2 pi fc / fs); ~ 1 - exp(-w) at low cutoffs.
    const double k = 1.0 - std::cos(2.0 * std::numbers::pi * 1000.0 / FS);
    EXPECT_NEAR(onePoleCoefficient(1000.0f, FS), std::sqrt(k * (k + 2.0)) - k, 1e-6);
    EXPECT_NEAR(onePoleCoefficient(100.0f, FS), 1.0f - std::exp(-2.0f * std::numbers::pi_v<float> * 100.0f / FS), 1e-5f);
}

TEST(OnePoleCoefficient, ClampsInvalidAndExtremeCutoffs) {
    EXPECT_LE(onePoleCoefficient(1.0e9f, FS), 1.0f);
    EXPECT_GT(onePoleCoefficient(1.0e9f, FS), 0.9f);
    EXPECT_GT(onePoleCoefficient(0.0f, FS), 0.0f);   // Never fully frozen
    EXPECT_GT(onePoleCoefficient(-50.0f, FS), 0.0f);
    EXPECT_FLOAT_EQ(onePoleCoefficient(std::numeric_limits<float>::quiet_NaN(), FS),
                    onePoleCoefficient(FS * 0.5f, FS)); // NaN -> pass-through-ish
    EXPECT_FLOAT_EQ(onePoleCoefficient(1000.0f, 0.0f), 1.0f); // Bad sample rate -> bypass
}

TEST(OnePoleCoefficient, MonotoneInCutoff) {
    float prev = 0.0f;
    for (float fc = 20.0f; fc <= 22050.0f; fc *= 1.3f) {
        const float a = onePoleCoefficient(fc, FS);
        EXPECT_GT(a, prev);
        prev = a;
    }
}

TEST(OnePoleLowPass, UnityDcGain) {
    OnePoleLowPass lpf(FS);
    lpf.setCutoff(400.0f);
    float y = 0.0f;
    for (int i = 0; i < 20000; ++i) {
        y = lpf.process(1.0f);
    }
    EXPECT_NEAR(y, 1.0f, 1e-4f);
}

TEST(OnePoleLowPass, MinusThreeDbAtCutoff) {
    EXPECT_NEAR(measureSineGain(1000.0f, 1000.0f), 0.7077f, 0.01f);
}

TEST(OnePoleLowPass, OccludedCutoffStronglyAttenuatesHighs) {
    EXPECT_LT(measureNyquistGain(400.0f), 0.03f);
    EXPECT_GT(measureNyquistGain(20000.0f), 0.85f);
    EXPECT_LT(measureSineGain(400.0f, 8000.0f), 0.1f);
}

TEST(OnePoleLowPass, LowerCutoffNeverLetsMoreHighsThrough) {
    float prev = 0.0f;
    for (float fc = 400.0f; fc <= 20000.0f; fc *= 1.5f) {
        const float g = measureNyquistGain(fc);
        EXPECT_GE(g, prev);
        prev = g;
    }
}

TEST(OnePoleLowPass, BlockProcessingEqualsPerSample) {
    std::mt19937 rng(5u);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<float> in(1000);
    for (float& v : in) {
        v = dist(rng);
    }
    OnePoleLowPass a(FS);
    OnePoleLowPass b(FS);
    a.setCutoff(2500.0f);
    b.setCutoff(2500.0f);
    std::vector<float> block = in;
    b.processBlock(block);
    for (size_t i = 0; i < in.size(); ++i) {
        ASSERT_FLOAT_EQ(a.process(in[i]), block[i]);
    }
}

TEST(OnePoleLowPass, OutputBoundedByInputPeak) {
    std::mt19937 rng(99u);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    OnePoleLowPass lpf(FS);
    lpf.setCutoff(3000.0f);
    for (int i = 0; i < 200000; ++i) {
        ASSERT_LE(std::fabs(lpf.process(dist(rng))), 1.0f + 1e-6f);
    }
}

TEST(OnePoleLowPass, NonFiniteInputDoesNotPoisonState) {
    OnePoleLowPass lpf(FS);
    lpf.setCutoff(1000.0f);
    (void)lpf.process(std::numeric_limits<float>::quiet_NaN());
    (void)lpf.process(std::numeric_limits<float>::infinity());
    EXPECT_TRUE(std::isfinite(lpf.state()));
    EXPECT_TRUE(std::isfinite(lpf.process(0.5f)));
}

TEST(OnePoleLowPass, DecaysToExactZeroWithoutDenormals) {
    OnePoleLowPass lpf(FS);
    lpf.setCutoff(50.0f);
    (void)lpf.process(1.0f);
    for (int i = 0; i < 44100 * 4; ++i) {
        const float y = lpf.process(0.0f);
        ASSERT_NE(std::fpclassify(y), FP_SUBNORMAL);
    }
    EXPECT_EQ(lpf.state(), 0.0f);
}

TEST(OnePoleLowPass, ResetClearsState) {
    OnePoleLowPass lpf(FS);
    (void)lpf.process(1.0f);
    lpf.reset();
    EXPECT_EQ(lpf.state(), 0.0f);
}

// --- ParameterSmoother ------------------------------------------------------------------------

TEST(ParameterSmoother, StartsAtInitialValue) {
    ParameterSmoother s(0.25f, 0.02f, FS);
    EXPECT_FLOAT_EQ(s.current(), 0.25f);
    EXPECT_FLOAT_EQ(s.target(), 0.25f);
}

TEST(ParameterSmoother, ReachesOneTimeConstantAtSixtyThreePercent) {
    ParameterSmoother s(0.0f, 0.01f, FS);
    s.setTarget(1.0f);
    for (int i = 0; i < 441; ++i) {
        (void)s.next();
    }
    EXPECT_NEAR(s.current(), 1.0f - std::exp(-1.0f), 0.01f);
}

TEST(ParameterSmoother, MonotoneConvergenceWithoutOvershoot) {
    ParameterSmoother s(1.0f, 0.02f, FS);
    s.setTarget(0.12f);
    float prev = s.current();
    for (int i = 0; i < 44100; ++i) {
        const float v = s.next();
        ASSERT_LE(v, prev);
        ASSERT_GE(v, 0.12f);
        prev = v;
    }
    EXPECT_NEAR(s.current(), 0.12f, 1e-4f);
}

TEST(ParameterSmoother, PerSampleStepIsBoundedSoNoZipperNoise) {
    // A 10 Hz occlusion update jumping from unoccluded to max-occluded must ramp, not step.
    ParameterSmoother s(1.0f, 0.02f, FS);
    s.setTarget(0.12f);
    float prev = s.current();
    float maxStep = 0.0f;
    for (int i = 0; i < 4410; ++i) {
        const float v = s.next();
        maxStep = std::max(maxStep, std::fabs(v - prev));
        prev = v;
    }
    EXPECT_LT(maxStep, 0.0015f);
}

TEST(ParameterSmoother, AdvanceMatchesRepeatedNext) {
    ParameterSmoother a(0.0f, 0.015f, FS);
    ParameterSmoother b(0.0f, 0.015f, FS);
    a.setTarget(1.0f);
    b.setTarget(1.0f);
    for (int i = 0; i < 512; ++i) {
        (void)a.next();
    }
    b.advance(512);
    EXPECT_NEAR(a.current(), b.current(), 1e-5f);
}

TEST(ParameterSmoother, ZeroTimeConstantJumpsImmediately) {
    ParameterSmoother s(0.0f, 0.0f, FS);
    s.setTarget(0.7f);
    EXPECT_FLOAT_EQ(s.next(), 0.7f);
}

TEST(ParameterSmoother, SnapSetsBothCurrentAndTarget) {
    ParameterSmoother s(0.0f, 0.05f, FS);
    s.setTarget(1.0f);
    s.snap(0.3f);
    EXPECT_FLOAT_EQ(s.current(), 0.3f);
    EXPECT_FLOAT_EQ(s.target(), 0.3f);
    EXPECT_FLOAT_EQ(s.next(), 0.3f);
}

TEST(ParameterSmoother, IgnoresNonFiniteTargets) {
    ParameterSmoother s(0.5f, 0.01f, FS);
    s.setTarget(std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(s.target(), 0.5f);
    s.setTarget(std::numeric_limits<float>::infinity());
    EXPECT_FLOAT_EQ(s.target(), 0.5f);
}

TEST(ParameterSmoother, SettlesExactlyOnTarget) {
    ParameterSmoother s(0.0f, 0.001f, FS);
    s.setTarget(1.0f);
    for (int i = 0; i < 44100; ++i) {
        (void)s.next();
    }
    EXPECT_EQ(s.current(), 1.0f);
}

// --- Spec cutoffs (20000 * 0.65^N) must really be the -3 dB points ------------------------

namespace {

// |H(e^jw)| of y += a (x - y), evaluated exactly.
double emaMagnitude(double a, double freqHz, double fs) {
    const double w = 2.0 * std::numbers::pi * freqHz / fs;
    const double b = 1.0 - a;
    return a / std::sqrt(1.0 - 2.0 * b * std::cos(w) + b * b);
}

} // namespace

TEST(OnePoleCoefficient, SpecOcclusionCutoffsAreMinusThreeDbPoints) {
    for (int n = 1; n <= 9; ++n) {
        const float fc = 20000.0f * std::pow(0.65f, static_cast<float>(n)); // 13 kHz, 8.45 kHz, ...
        const double mag = emaMagnitude(onePoleCoefficient(fc, FS), fc, FS);
        EXPECT_NEAR(20.0 * std::log10(mag), -3.0103, 0.02) << "N=" << n << " fc=" << fc;
    }
    EXPECT_NEAR(20.0 * std::log10(emaMagnitude(onePoleCoefficient(400.0f, FS), 400.0, FS)), -3.0103, 0.02);
}

TEST(OnePoleLowPass, MeasuredMinusThreeDbAtThirteenAndEightKilohertz) {
    EXPECT_NEAR(measureSineGain(13000.0f, 13000.0f), 0.7071f, 0.015f);
    EXPECT_NEAR(measureSineGain(8450.0f, 8450.0f), 0.7071f, 0.015f);
}

TEST(OnePoleCoefficient, UnoccludedCutoffIsAnExactBypass) {
    EXPECT_FLOAT_EQ(onePoleCoefficient(BYPASS_CUTOFF_HZ, FS), 1.0f);
    EXPECT_FLOAT_EQ(onePoleCoefficient(20000.0f, FS), 1.0f);
    EXPECT_FLOAT_EQ(onePoleCoefficient(21000.0f, FS), 1.0f);
    EXPECT_LT(onePoleCoefficient(19999.0f, FS), 1.0f);
    EXPECT_NEAR(measureSineGain(20000.0f, 15000.0f), 1.0f, 1e-3f); // No loss near the top octave
    OnePoleLowPass lpf(FS);
    EXPECT_FLOAT_EQ(lpf.coefficient(), 1.0f); // Default-constructed: transparent
}

TEST(OnePoleCoefficient, MonotoneNonDecreasingAcrossTheWholeRange) {
    float prev = 0.0f;
    for (float fc = 1.0f; fc <= 30000.0f; fc *= 1.05f) {
        const float a = onePoleCoefficient(fc, FS);
        ASSERT_GE(a, prev) << fc;
        ASSERT_GT(a, 0.0f);
        ASSERT_LE(a, 1.0f);
        prev = a;
    }
}
