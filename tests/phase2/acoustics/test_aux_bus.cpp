// P4.2 — shared auxiliary reverb bus (one FDN for all voices) and the per-voice occlusion filter.
#include "acoustics/AuxReverbBus.hpp"
#include "acoustics/VoiceAcousticFilter.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>
#include <vector>

namespace {

using namespace betacraft::acoustics;

constexpr size_t N = AuxReverbBus::MAX_BLOCK_FRAMES;

ReverbParams caveParams() {
    ReverbParams p;
    p.sendLevel = 0.4f;
    p.feedback = 0.9f;
    p.damping = 0.3f;
    p.decaySeconds = 0.0f; // Explicit-feedback mode: the bus uses 0.9 as given
    return p;
}

// Runs `blocks` blocks of a single voice through the bus and returns the interleaved output.
std::vector<float> renderVoices(const std::vector<std::vector<float>>& voices, const std::vector<float>& sends,
                                int blocks) {
    AuxReverbBus bus;
    bus.setReverbParams(caveParams(), true);
    std::vector<float> out;
    std::vector<float> block(2 * N);
    for (int b = 0; b < blocks; ++b) {
        const size_t frames = bus.beginBlock(N);
        for (size_t v = 0; v < voices.size(); ++v) {
            const std::vector<float>& src = voices[v];
            std::vector<float> chunk(frames, 0.0f);
            for (size_t i = 0; i < frames; ++i) {
                const size_t idx = static_cast<size_t>(b) * N + i;
                chunk[i] = idx < src.size() ? src[idx] : 0.0f;
            }
            bus.accumulate(chunk, sends[v]);
        }
        std::fill(block.begin(), block.end(), 0.0f);
        bus.processAndMix(block, frames);
        out.insert(out.end(), block.begin(), block.end());
    }
    return out;
}

} // namespace

TEST(AuxReverbBus, OwnsExactlyOneSharedFdn) {
    const AuxReverbBus bus;
    EXPECT_EQ(bus.reverb().memoryBytes(), FdnReverbProcessor::TOTAL_DELAY_SAMPLES * sizeof(float));
    // FDN (65 240 B) + mono send + stereo wet scratch (3 x 512 floats) — far below 32 per-voice FDNs.
    EXPECT_EQ(bus.memoryBytes(), bus.reverb().memoryBytes() + 3u * N * sizeof(float));
    EXPECT_LT(bus.memoryBytes(), 72u * 1024u);
}

TEST(AuxReverbBus, MemoryDoesNotGrowWithVoiceCount) {
    AuxReverbBus bus;
    const size_t before = bus.memoryBytes();
    std::vector<float> voice(N, 0.1f);
    const size_t frames = bus.beginBlock(N);
    for (int v = 0; v < 32; ++v) {
        bus.accumulate(voice, 0.2f);
    }
    std::vector<float> out(2 * N, 0.0f);
    bus.processAndMix(out, frames);
    EXPECT_EQ(bus.memoryBytes(), before);
}

TEST(AuxReverbBus, BeginBlockClampsToCapacity) {
    AuxReverbBus bus;
    EXPECT_EQ(bus.beginBlock(100), 100u);
    EXPECT_EQ(bus.beginBlock(N + 1000), N);
    EXPECT_EQ(bus.beginBlock(0), 0u);
}

TEST(AuxReverbBus, SilenceInSilenceOut) {
    const auto out = renderVoices({std::vector<float>(10 * N, 0.0f)}, {1.0f}, 10);
    EXPECT_TRUE(std::all_of(out.begin(), out.end(), [](float v) { return v == 0.0f; }));
}

TEST(AuxReverbBus, WetIsAddedOnTopOfDryMix) {
    AuxReverbBus bus;
    bus.setReverbParams(caveParams(), true);
    std::vector<float> voice(N, 0.0f);
    voice[0] = 1.0f;
    std::vector<float> dry(2 * N, 0.0f);
    std::vector<float> withDry(2 * N, 0.5f);
    AuxReverbBus bus2;
    bus2.setReverbParams(caveParams(), true);
    for (int b = 0; b < 4; ++b) {
        std::fill(dry.begin(), dry.end(), 0.0f);
        std::fill(withDry.begin(), withDry.end(), 0.5f);
        (void)bus.beginBlock(N);
        (void)bus2.beginBlock(N);
        bus.accumulate(voice, 1.0f);
        bus2.accumulate(voice, 1.0f);
        voice[0] = 0.0f;
        bus.processAndMix(dry, N);
        bus2.processAndMix(withDry, N);
        for (size_t i = 0; i < dry.size(); ++i) {
            ASSERT_FLOAT_EQ(withDry[i], dry[i] + 0.5f);
        }
    }
}

TEST(AuxReverbBus, ImpulseProducesDelayedStereoTail) {
    std::vector<float> voice(N, 0.0f);
    voice[0] = 1.0f;
    const auto out = renderVoices({voice}, {1.0f}, 20);
    const size_t firstL = FdnReverbProcessor::DELAY_LENGTHS[0];
    const size_t firstR = FdnReverbProcessor::DELAY_LENGTHS[1];
    for (size_t f = 0; f < firstL; ++f) {
        ASSERT_EQ(out[2 * f], 0.0f);
    }
    EXPECT_NE(out[2 * firstL], 0.0f);
    EXPECT_NE(out[2 * firstR + 1], 0.0f);
}

TEST(AuxReverbBus, SendsFromManyVoicesSumLinearlyIntoOneNetwork) {
    std::mt19937 rng(17u);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::vector<float> a(4 * N);
    std::vector<float> b(4 * N);
    std::vector<float> mixed(4 * N);
    for (size_t i = 0; i < a.size(); ++i) {
        a[i] = dist(rng);
        b[i] = dist(rng);
        mixed[i] = a[i] * 0.3f + b[i] * 0.7f;
    }
    const auto two = renderVoices({a, b}, {0.3f, 0.7f}, 12);
    const auto one = renderVoices({mixed}, {1.0f}, 12);
    ASSERT_EQ(two.size(), one.size());
    for (size_t i = 0; i < two.size(); ++i) {
        ASSERT_NEAR(two[i], one[i], 1e-4f) << i;
    }
}

TEST(AuxReverbBus, BusSendLevelScalesWet) {
    std::vector<float> voice(N, 0.0f);
    voice[0] = 1.0f;
    AuxReverbBus loud;
    AuxReverbBus quiet;
    ReverbParams p = caveParams();
    loud.setReverbParams(p, true);
    p.sendLevel = 0.1f;
    quiet.setReverbParams(p, true);
    std::vector<float> ol(2 * N);
    std::vector<float> oq(2 * N);
    double el = 0.0;
    double eq = 0.0;
    for (int b = 0; b < 8; ++b) {
        std::fill(ol.begin(), ol.end(), 0.0f);
        std::fill(oq.begin(), oq.end(), 0.0f);
        (void)loud.beginBlock(N);
        (void)quiet.beginBlock(N);
        loud.accumulate(voice, 1.0f);
        quiet.accumulate(voice, 1.0f);
        voice[0] = 0.0f;
        loud.processAndMix(ol, N);
        quiet.processAndMix(oq, N);
        for (size_t i = 0; i < ol.size(); ++i) {
            el += static_cast<double>(ol[i]) * ol[i];
            eq += static_cast<double>(oq[i]) * oq[i];
        }
    }
    EXPECT_NEAR(std::sqrt(el / eq), 4.0, 0.01); // 0.4 / 0.1
}

TEST(AuxReverbBus, ReverbParamsAreForwardedToFdn) {
    AuxReverbBus bus;
    bus.setReverbParams(caveParams(), true);
    EXPECT_FLOAT_EQ(bus.reverb().feedbackGain(), 0.9f);
    EXPECT_FLOAT_EQ(bus.reverb().damping(), 0.3f);
}

TEST(AuxReverbBus, ParameterChangesAreSmoothedAcrossBlocks) {
    AuxReverbBus bus;
    ReverbParams p = caveParams();
    p.feedback = 0.2f;
    bus.setReverbParams(p, true);
    p.feedback = 0.95f;
    bus.setReverbParams(p); // smoothed
    std::vector<float> out(2 * N, 0.0f);
    (void)bus.beginBlock(N);
    bus.processAndMix(out, N);
    const float afterOne = bus.reverb().feedbackGain();
    EXPECT_GT(afterOne, 0.2f);
    EXPECT_LT(afterOne, 0.95f);
    for (int b = 0; b < 200; ++b) {
        (void)bus.beginBlock(N);
        bus.processAndMix(out, N);
    }
    EXPECT_NEAR(bus.reverb().feedbackGain(), 0.95f, 1e-3f);
}

TEST(AuxReverbBus, AccumulateBeyondBlockAndShortOutputAreSafe) {
    AuxReverbBus bus;
    bus.setReverbParams(caveParams(), true);
    const size_t frames = bus.beginBlock(64);
    std::vector<float> longVoice(4 * N, 1.0f);
    bus.accumulate(longVoice, 1.0f); // Only the first 64 frames are taken
    std::vector<float> tinyOut(10, 0.0f); // Room for only 5 stereo frames
    bus.processAndMix(tinyOut, frames);
    for (const float v : tinyOut) {
        EXPECT_TRUE(std::isfinite(v));
    }
}

TEST(AuxReverbBus, NonFiniteSendGainIsIgnored) {
    AuxReverbBus bus;
    bus.setReverbParams(caveParams(), true);
    std::vector<float> voice(N, 1.0f);
    std::vector<float> out(2 * N, 0.0f);
    for (int b = 0; b < 10; ++b) {
        (void)bus.beginBlock(N);
        bus.accumulate(voice, std::numeric_limits<float>::quiet_NaN());
        bus.processAndMix(out, N);
    }
    EXPECT_TRUE(std::all_of(out.begin(), out.end(), [](float v) { return v == 0.0f; }));
}

// --- VoiceAcousticFilter --------------------------------------------------------------------------

TEST(VoiceAcousticFilter, UnoccludedSettlesToUnityAtDc) {
    VoiceAcousticFilter f;
    f.setOcclusionTarget({1.0f, 20000.0f});
    f.snapToTargets();
    VoiceSample s{};
    for (int i = 0; i < 2000; ++i) {
        s = f.process(1.0f);
    }
    EXPECT_NEAR(s.dry, 1.0f, 1e-4f);
}

TEST(VoiceAcousticFilter, OccludedAttenuatesAndMuffles) {
    VoiceAcousticFilter f;
    f.setOcclusionTarget({0.5f, 400.0f});
    f.snapToTargets();
    VoiceSample s{};
    for (int i = 0; i < 20000; ++i) {
        s = f.process(1.0f);
    }
    EXPECT_NEAR(s.dry, 0.5f, 1e-3f);
    float y = 0.0f;
    for (int i = 0; i < 4000; ++i) {
        y = f.process((i % 2 == 0) ? 1.0f : -1.0f).dry;
    }
    EXPECT_LT(std::fabs(y), 0.02f);
}

TEST(VoiceAcousticFilter, TargetChangesRampWithoutZipperSteps) {
    VoiceAcousticFilter f;
    f.setOcclusionTarget({1.0f, 20000.0f});
    f.snapToTargets();
    for (int i = 0; i < 1000; ++i) {
        (void)f.process(1.0f);
    }
    f.setOcclusionTarget({0.12f, 400.0f}); // A 10 Hz update lands: wall appeared
    float prev = f.process(1.0f).dry;
    float maxStep = 0.0f;
    for (int i = 0; i < 4410; ++i) {
        const float v = f.process(1.0f).dry;
        maxStep = std::max(maxStep, std::fabs(v - prev));
        prev = v;
    }
    EXPECT_LT(maxStep, 0.003f);
    EXPECT_LT(prev, 0.2f); // Mostly converged within 100 ms, ready for the next update
}

TEST(VoiceAcousticFilter, SnapAppliesTargetsImmediately) {
    VoiceAcousticFilter f;
    f.setOcclusionTarget({0.25f, 1000.0f});
    f.snapToTargets();
    EXPECT_FLOAT_EQ(f.currentGain(), 0.25f);
    EXPECT_FLOAT_EQ(f.currentCoefficient(), onePoleCoefficient(1000.0f, DEFAULT_SAMPLE_RATE));
}

TEST(VoiceAcousticFilter, SendIsPostOcclusion) {
    VoiceAcousticFilter f;
    f.setOcclusionTarget({0.5f, 20000.0f});
    f.setReverbSend(0.3f);
    f.snapToTargets();
    VoiceSample s{};
    for (int i = 0; i < 2000; ++i) {
        s = f.process(1.0f);
    }
    EXPECT_NEAR(s.send, s.dry * 0.3f, 1e-5f);
}

TEST(VoiceAcousticFilter, InvalidTargetsAreIgnored) {
    VoiceAcousticFilter f;
    f.setOcclusionTarget({0.5f, 2000.0f});
    f.snapToTargets();
    f.setOcclusionTarget({std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()});
    f.setReverbSend(std::numeric_limits<float>::infinity());
    f.snapToTargets();
    EXPECT_FLOAT_EQ(f.currentGain(), 0.5f);
    EXPECT_FLOAT_EQ(f.currentCoefficient(), onePoleCoefficient(2000.0f, DEFAULT_SAMPLE_RATE));
    EXPECT_TRUE(std::isfinite(f.process(1.0f).send));
}

TEST(VoiceAcousticFilter, GainIsClampedToUnitRange) {
    VoiceAcousticFilter f;
    f.setOcclusionTarget({7.0f, 20000.0f});
    f.snapToTargets();
    EXPECT_FLOAT_EQ(f.currentGain(), 1.0f);
    f.setOcclusionTarget({-1.0f, 20000.0f});
    f.snapToTargets();
    EXPECT_FLOAT_EQ(f.currentGain(), 0.0f);
}

TEST(VoiceAcousticFilter, ResetClearsFilterMemory) {
    VoiceAcousticFilter f;
    f.setOcclusionTarget({1.0f, 300.0f});
    f.snapToTargets();
    for (int i = 0; i < 100; ++i) {
        (void)f.process(1.0f);
    }
    f.reset();
    EXPECT_FLOAT_EQ(f.process(0.0f).dry, 0.0f);
}

// --- Unconfigured bus: the spec's per-voice send (0.15) must reach the FDN as is ----------------

namespace {

double wetEnergyOfImpulse(AuxReverbBus& bus, float voiceSend, int blocks) {
    std::vector<float> voice(N, 0.0f);
    voice[0] = 1.0f;
    std::vector<float> out(2 * N);
    double e = 0.0;
    for (int b = 0; b < blocks; ++b) {
        std::fill(out.begin(), out.end(), 0.0f);
        const size_t n = bus.beginBlock(N);
        bus.accumulate(voice, voiceSend);
        voice[0] = 0.0f;
        bus.processAndMix(out, n);
        for (const float v : out) {
            e += static_cast<double>(v) * v;
        }
    }
    return e;
}

} // namespace

TEST(AuxReverbBus, DefaultBusIsAudibleBeforeAnyEnvironmentUpdate) {
    AuxReverbBus bus;
    EXPECT_FLOAT_EQ(AuxReverbBus::DEFAULT_SEND_LEVEL, 1.0f);
    EXPECT_GT(wetEnergyOfImpulse(bus, VoiceAcousticFilter::DEFAULT_REVERB_SEND, 12), 1e-4);
}

TEST(AuxReverbBus, DefaultBusIsTheNeutralUnitySendOfTheSpec) {
    // No extra bus gain on top of the voice send (Fiche 5: reverbSendGain 0.15 straight into the FDN).
    AuxReverbBus unconfigured;
    AuxReverbBus explicitUnity;
    ReverbParams unity;
    unity.sendLevel = 1.0f;
    unity.feedback = FdnReverbProcessor::DEFAULT_FEEDBACK;
    unity.damping = FdnReverbProcessor::DEFAULT_DAMPING;
    explicitUnity.setReverbParams(unity, true);
    EXPECT_DOUBLE_EQ(wetEnergyOfImpulse(unconfigured, 0.15f, 12), wetEnergyOfImpulse(explicitUnity, 0.15f, 12));
}

TEST(AuxReverbBus, BusSendLevelAboveUnityIsAllowedUpToItsCap) {
    AuxReverbBus bus;
    ReverbParams p = caveParams();
    p.sendLevel = 100.0f;
    bus.setReverbParams(p, true);
    AuxReverbBus capped;
    p.sendLevel = AuxReverbBus::MAX_SEND_LEVEL;
    capped.setReverbParams(p, true);
    AuxReverbBus unity;
    p.sendLevel = 1.0f;
    unity.setReverbParams(p, true);
    const double eBus = wetEnergyOfImpulse(bus, 0.15f, 8);
    EXPECT_DOUBLE_EQ(eBus, wetEnergyOfImpulse(capped, 0.15f, 8));
    EXPECT_GT(eBus, wetEnergyOfImpulse(unity, 0.15f, 8));
}

// --- Sample-rate awareness: RT60 is honoured whatever the device rate --------------------------

TEST(AuxReverbBus, DecayIsConvertedToFeedbackAtTheBusSampleRate) {
    ReverbParams p;
    p.sendLevel = 1.0f;
    p.feedback = 0.5f; // Ignored: decaySeconds > 0 takes precedence
    p.damping = 0.3f;
    p.decaySeconds = 2.0f;
    for (const float fs : {44100.0f, 48000.0f, 96000.0f}) {
        AuxReverbBus bus(fs);
        bus.setReverbParams(p, true);
        EXPECT_FLOAT_EQ(bus.reverb().feedbackGain(), feedbackForDecay(2.0f, fs)) << fs;
    }
    EXPECT_GT(feedbackForDecay(2.0f, 48000.0f), feedbackForDecay(2.0f, 44100.0f)); // Same RT60 needs more g
}

TEST(AuxReverbBus, ZeroDecayMeansExplicitFeedback) {
    ReverbParams p;
    p.sendLevel = 1.0f;
    p.feedback = 0.7f;
    p.damping = 0.3f;
    p.decaySeconds = 0.0f;
    AuxReverbBus bus(48000.0f);
    bus.setReverbParams(p, true);
    EXPECT_FLOAT_EQ(bus.reverb().feedbackGain(), 0.7f);
}
