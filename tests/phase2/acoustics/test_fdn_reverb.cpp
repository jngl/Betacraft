// P4.2 — shared 8-line Householder FDN reverb (spec Fiche 5 §2.2 / §3).
#include "acoustics/FdnReverb.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <random>
#include <vector>

namespace {

using betacraft::acoustics::FdnReverbProcessor;
using Fdn = FdnReverbProcessor;

constexpr size_t BLOCK = 512;

// Feeds `input` (zero-padded to `frames`) and returns L/R outputs.
void run(Fdn& fdn, const std::vector<float>& input, size_t frames, std::vector<float>& outL, std::vector<float>& outR) {
    outL.assign(frames, 0.0f);
    outR.assign(frames, 0.0f);
    std::vector<float> in(BLOCK);
    for (size_t pos = 0; pos < frames; pos += BLOCK) {
        const size_t n = std::min(BLOCK, frames - pos);
        for (size_t i = 0; i < n; ++i) {
            in[i] = (pos + i < input.size()) ? input[pos + i] : 0.0f;
        }
        const size_t done = fdn.processBlock(std::span<const float>(in.data(), n), std::span<float>(outL.data() + pos, n),
                                             std::span<float>(outR.data() + pos, n));
        ASSERT_EQ(done, n);
    }
}

void runSilence(Fdn& fdn, size_t frames) {
    std::vector<float> in(BLOCK, 0.0f);
    std::vector<float> l(BLOCK);
    std::vector<float> r(BLOCK);
    for (size_t pos = 0; pos < frames; pos += BLOCK) {
        (void)fdn.processBlock(in, l, r);
    }
}

} // namespace

TEST(FdnReverb, UsesEightLinesWithSpecDelayLengths) {
    EXPECT_EQ(Fdn::NUM_DELAY_LINES, 8u);
    const std::array<size_t, 8> expected{1087, 1283, 1523, 1823, 2137, 2467, 2803, 3187};
    EXPECT_EQ(Fdn::DELAY_LENGTHS, expected);
}

TEST(FdnReverb, DelayLengthsAreMutuallyPrime) {
    for (size_t i = 0; i < Fdn::NUM_DELAY_LINES; ++i) {
        for (size_t j = i + 1; j < Fdn::NUM_DELAY_LINES; ++j) {
            EXPECT_EQ(std::gcd(Fdn::DELAY_LENGTHS[i], Fdn::DELAY_LENGTHS[j]), 1u) << i << "," << j;
        }
    }
}

TEST(FdnReverb, MemoryBudgetIsAbout65KilobytesAndShared) {
    const Fdn fdn;
    EXPECT_EQ(Fdn::TOTAL_DELAY_SAMPLES, 16310u);
    EXPECT_EQ(fdn.allocatedSampleCount(), 16310u);
    EXPECT_EQ(fdn.memoryBytes(), 16310u * sizeof(float)); // 65 240 bytes
    EXPECT_GE(fdn.memoryBytes(), 64u * 1024u - 2000u);
    EXPECT_LE(fdn.memoryBytes(), 66000u);
    // Fits comfortably in a 512 KB L2, unlike 32 per-voice instances (~2.1 MB).
    EXPECT_LT(fdn.memoryBytes(), 512u * 1024u / 4u);
    EXPECT_GT(32u * fdn.memoryBytes(), 2u * 1000u * 1000u);
}

TEST(FdnReverb, HouseholderMatrixIsOrthogonalAndInvolutive) {
    std::mt19937 rng(11u);
    std::normal_distribution<float> dist(0.0f, 1.0f);
    for (int iter = 0; iter < 200; ++iter) {
        std::array<float, 8> v{};
        for (float& x : v) {
            x = dist(rng);
        }
        const std::array<float, 8> original = v;
        Fdn::applyHouseholder(v);
        double n0 = 0.0;
        double n1 = 0.0;
        for (size_t i = 0; i < 8; ++i) {
            n0 += static_cast<double>(original[i]) * original[i];
            n1 += static_cast<double>(v[i]) * v[i];
        }
        ASSERT_NEAR(n1, n0, 1e-4 * n0);
        Fdn::applyHouseholder(v);
        for (size_t i = 0; i < 8; ++i) {
            ASSERT_NEAR(v[i], original[i], 1e-5f);
        }
    }
}

TEST(FdnReverb, HouseholderMatchesExplicitMatrix) {
    // H = I - (2/N) * 1 * 1^T
    std::array<float, 8> e{};
    e[3] = 1.0f;
    Fdn::applyHouseholder(e);
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_FLOAT_EQ(e[i], (i == 3 ? 1.0f : 0.0f) - 0.25f);
    }
}

TEST(FdnReverb, DefaultParametersMatchSpec) {
    const Fdn fdn;
    EXPECT_FLOAT_EQ(fdn.feedbackGain(), 0.84f);
    EXPECT_FLOAT_EQ(fdn.damping(), 0.30f);
}

TEST(FdnReverb, ParametersAreClampedToStableRange) {
    Fdn fdn;
    fdn.setParameters(5.0f, 2.0f);
    EXPECT_FLOAT_EQ(fdn.feedbackGain(), Fdn::MAX_FEEDBACK);
    EXPECT_FLOAT_EQ(fdn.damping(), Fdn::MAX_DAMPING);
    EXPECT_FLOAT_EQ(Fdn::MAX_FEEDBACK, 0.98f);
    EXPECT_FLOAT_EQ(Fdn::MAX_DAMPING, 0.85f);
    fdn.setParameters(-1.0f, -1.0f);
    EXPECT_FLOAT_EQ(fdn.feedbackGain(), 0.0f);
    EXPECT_FLOAT_EQ(fdn.damping(), 0.0f);
    fdn.setParameters(0.5f, 0.2f);
    fdn.setParameters(std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
    EXPECT_FLOAT_EQ(fdn.feedbackGain(), 0.5f);
    EXPECT_FLOAT_EQ(fdn.damping(), 0.2f);
}

TEST(FdnReverb, ImpulseArrivesAfterShortestDelayOnLeftThenRight) {
    Fdn fdn;
    std::vector<float> l;
    std::vector<float> r;
    run(fdn, {1.0f}, 4096, l, r);
    for (size_t i = 0; i < Fdn::DELAY_LENGTHS[0]; ++i) {
        ASSERT_EQ(l[i], 0.0f) << i;
    }
    for (size_t i = 0; i < Fdn::DELAY_LENGTHS[1]; ++i) {
        ASSERT_EQ(r[i], 0.0f) << i;
    }
    EXPECT_FLOAT_EQ(l[Fdn::DELAY_LENGTHS[0]], 0.25f);
    EXPECT_FLOAT_EQ(r[Fdn::DELAY_LENGTHS[1]], 0.25f);
}

TEST(FdnReverb, ZeroFeedbackIsAPureMultiTapDelay) {
    Fdn fdn;
    fdn.setParameters(0.0f, 0.0f);
    std::vector<float> l;
    std::vector<float> r;
    run(fdn, {1.0f}, 8000, l, r);
    float sumL = 0.0f;
    float sumR = 0.0f;
    for (size_t i = 0; i < l.size(); ++i) {
        sumL += l[i];
        sumR += r[i];
    }
    EXPECT_FLOAT_EQ(sumL, 1.0f); // 4 taps of 0.25
    EXPECT_FLOAT_EQ(sumR, 1.0f);
    EXPECT_FLOAT_EQ(l[Fdn::DELAY_LENGTHS[6]], 0.25f);
    EXPECT_FLOAT_EQ(r[Fdn::DELAY_LENGTHS[7]], 0.25f);
}

TEST(FdnReverb, StereoOutputsAreDecorrelated) {
    Fdn fdn;
    std::vector<float> l;
    std::vector<float> r;
    run(fdn, {1.0f}, 44100, l, r);
    double dot = 0.0;
    double nl = 0.0;
    double nr = 0.0;
    for (size_t i = 0; i < l.size(); ++i) {
        dot += static_cast<double>(l[i]) * r[i];
        nl += static_cast<double>(l[i]) * l[i];
        nr += static_cast<double>(r[i]) * r[i];
    }
    ASSERT_GT(nl, 0.0);
    ASSERT_GT(nr, 0.0);
    EXPECT_LT(std::fabs(dot / std::sqrt(nl * nr)), 0.5);
}

TEST(FdnReverb, EnergyDecaysMonotonicallyPerSecondForGainBelowOne) {
    Fdn fdn;
    fdn.setParameters(Fdn::MAX_FEEDBACK, 0.0f); // Worst case: longest tail
    std::vector<float> in(BLOCK, 0.0f);
    std::vector<float> l(BLOCK);
    std::vector<float> r(BLOCK);
    in[0] = 1.0f;
    (void)fdn.processBlock(in, l, r);
    in[0] = 0.0f;
    double prev = fdn.energy();
    ASSERT_GT(prev, 0.0);
    for (int second = 0; second < 30; ++second) {
        runSilence(fdn, 44100);
        const double e = fdn.energy();
        ASSERT_LT(e, prev) << "second " << second;
        prev = e;
    }
    EXPECT_LT(prev, 1e-3);
}

TEST(FdnReverb, HigherFeedbackGivesLongerTail) {
    Fdn shortRoom;
    Fdn bigCave;
    shortRoom.setParameters(0.5f, 0.3f);
    bigCave.setParameters(0.95f, 0.3f);
    std::vector<float> l;
    std::vector<float> r;
    run(shortRoom, {1.0f}, 2 * 44100, l, r);
    run(bigCave, {1.0f}, 2 * 44100, l, r);
    EXPECT_GT(bigCave.energy(), 100.0 * shortRoom.energy());
}

TEST(FdnReverb, DampingReducesHighFrequencyContentOfTail) {
    auto zeroCrossings = [](float damping) {
        Fdn fdn;
        fdn.setParameters(0.9f, damping);
        std::vector<float> l;
        std::vector<float> r;
        run(fdn, {1.0f}, 44100, l, r);
        int crossings = 0;
        for (size_t i = 22050; i + 1 < l.size(); ++i) {
            if ((l[i] > 0.0f) != (l[i + 1] > 0.0f)) {
                ++crossings;
            }
        }
        return crossings;
    };
    EXPECT_LT(zeroCrossings(0.85f), zeroCrossings(0.0f));
}

TEST(FdnReverb, LongNoiseRunAtMaxFeedbackStaysFiniteAndBounded) {
    Fdn fdn;
    fdn.setParameters(Fdn::MAX_FEEDBACK, 0.0f);
    std::mt19937 rng(3u);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<float> in(BLOCK);
    std::vector<float> l(BLOCK);
    std::vector<float> r(BLOCK);
    float peak = 0.0f;
    for (size_t pos = 0; pos < 10u * 44100u; pos += BLOCK) {
        for (float& x : in) {
            x = dist(rng);
        }
        (void)fdn.processBlock(in, l, r);
        for (size_t i = 0; i < BLOCK; ++i) {
            ASSERT_TRUE(std::isfinite(l[i]) && std::isfinite(r[i]));
            peak = std::max({peak, std::fabs(l[i]), std::fabs(r[i])});
        }
    }
    EXPECT_LT(peak, 100.0f);
    // ... and then dies away once the input stops.
    runSilence(fdn, 60u * 44100u);
    EXPECT_LT(fdn.energy(), 1e-12);
}

TEST(FdnReverb, TailFlushesToExactZeroWithoutDenormals) {
    Fdn fdn;
    fdn.setParameters(0.5f, 0.85f);
    std::vector<float> in(BLOCK, 0.0f);
    std::vector<float> l(BLOCK);
    std::vector<float> r(BLOCK);
    in[0] = 1.0f;
    for (size_t pos = 0; pos < 20u * 44100u; pos += BLOCK) {
        (void)fdn.processBlock(in, l, r);
        in[0] = 0.0f;
        for (size_t i = 0; i < BLOCK; ++i) {
            ASSERT_NE(std::fpclassify(l[i]), FP_SUBNORMAL);
            ASSERT_NE(std::fpclassify(r[i]), FP_SUBNORMAL);
        }
    }
    EXPECT_EQ(fdn.energy(), 0.0);
    EXPECT_FALSE(fdn.hasSubnormalState());
}

TEST(FdnReverb, NonFiniteInputIsSanitised) {
    Fdn fdn;
    std::vector<float> in(BLOCK, 0.0f);
    std::vector<float> l(BLOCK);
    std::vector<float> r(BLOCK);
    in[0] = std::numeric_limits<float>::quiet_NaN();
    in[1] = std::numeric_limits<float>::infinity();
    in[2] = -std::numeric_limits<float>::infinity();
    in[3] = 0.5f;
    for (int b = 0; b < 40; ++b) {
        (void)fdn.processBlock(in, l, r);
        std::fill(in.begin(), in.end(), 0.0f);
        for (size_t i = 0; i < BLOCK; ++i) {
            ASSERT_TRUE(std::isfinite(l[i]) && std::isfinite(r[i]));
        }
    }
    EXPECT_TRUE(std::isfinite(fdn.energy()));
    EXPECT_GT(fdn.energy(), 0.0); // The finite 0.5 sample still reverberates
}

TEST(FdnReverb, MismatchedSpansProcessShortestLength) {
    Fdn fdn;
    std::vector<float> in(100, 0.0f);
    std::vector<float> l(64, -7.0f);
    std::vector<float> r(80, -7.0f);
    EXPECT_EQ(fdn.processBlock(in, l, r), 64u);
    EXPECT_EQ(l[63], 0.0f);
    EXPECT_EQ(r[63], 0.0f);
    EXPECT_EQ(r[64], -7.0f); // Untouched beyond the processed range
    EXPECT_EQ(fdn.processBlock({}, l, r), 0u);
}

TEST(FdnReverb, ResetSilencesNetwork) {
    Fdn fdn;
    std::vector<float> l;
    std::vector<float> r;
    run(fdn, {1.0f, 0.5f, -0.3f}, 5000, l, r);
    ASSERT_GT(fdn.energy(), 0.0);
    fdn.reset();
    EXPECT_EQ(fdn.energy(), 0.0);
    run(fdn, {}, 5000, l, r);
    EXPECT_TRUE(std::all_of(l.begin(), l.end(), [](float v) { return v == 0.0f; }));
}

TEST(FdnReverb, PropertyLinearity) {
    std::mt19937 rng(8u);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<float> a(3000);
    std::vector<float> b(3000);
    std::vector<float> sum(3000);
    for (size_t i = 0; i < a.size(); ++i) {
        a[i] = dist(rng);
        b[i] = dist(rng);
        sum[i] = a[i] + b[i];
    }
    Fdn fa;
    Fdn fb;
    Fdn fs;
    std::vector<float> la, ra, lb, rb, ls, rs;
    run(fa, a, 12000, la, ra);
    run(fb, b, 12000, lb, rb);
    run(fs, sum, 12000, ls, rs);
    for (size_t i = 0; i < ls.size(); ++i) {
        ASSERT_NEAR(ls[i], la[i] + lb[i], 1e-4f) << i;
        ASSERT_NEAR(rs[i], ra[i] + rb[i], 1e-4f) << i;
    }
}

TEST(FdnReverb, PropertyBlockSizeInvariance) {
    std::mt19937 rng(21u);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<float> input(9000);
    for (float& v : input) {
        v = dist(rng);
    }
    Fdn whole;
    std::vector<float> l1(input.size());
    std::vector<float> r1(input.size());
    (void)whole.processBlock(input, l1, r1);

    Fdn chunked;
    std::vector<float> l2(input.size());
    std::vector<float> r2(input.size());
    std::uniform_int_distribution<size_t> chunk(1, 700);
    for (size_t pos = 0; pos < input.size();) {
        const size_t n = std::min(chunk(rng), input.size() - pos);
        (void)chunked.processBlock(std::span<const float>(input.data() + pos, n), std::span<float>(l2.data() + pos, n),
                                   std::span<float>(r2.data() + pos, n));
        pos += n;
    }
    for (size_t i = 0; i < input.size(); ++i) {
        ASSERT_EQ(l1[i], l2[i]);
        ASSERT_EQ(r1[i], r2[i]);
    }
}
