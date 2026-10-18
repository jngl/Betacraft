// P4.2 — cave / enclosure detection from ray probes around the listener -> reverb parameters.
#include "AcousticsTestUtils.hpp"
#include "acoustics/AuxReverbBus.hpp"
#include "acoustics/EnclosureDetector.hpp"
#include "acoustics/FdnReverb.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <random>

namespace {

using namespace betacraft::acoustics;
using betacraft::acoustics::test::FakeVoxelGrid;
using betacraft::core::BlockPos;

const glm::vec3 LISTENER{0.5f, 0.5f, 0.5f};

} // namespace

TEST(EnclosureDetector, ProbeDirectionsAreTwentySixUnitVectorsSymmetric) {
    const auto& dirs = EnclosureDetector::probeDirections();
    ASSERT_EQ(dirs.size(), 26u);
    glm::vec3 sum{0.0f};
    for (const glm::vec3& d : dirs) {
        EXPECT_NEAR(glm::length(d), 1.0f, 1e-6f);
        sum += d;
    }
    EXPECT_NEAR(glm::length(sum), 0.0f, 1e-5f);
}

TEST(EnclosureDetector, OpenAirIsNotEnclosed) {
    FakeVoxelGrid grid;
    const EnclosureDetector det;
    const EnclosureSample s = det.probe(LISTENER, grid);
    EXPECT_TRUE(s.valid);
    EXPECT_EQ(s.hits, 0u);
    EXPECT_FLOAT_EQ(s.enclosure, 0.0f);
    EXPECT_FLOAT_EQ(s.meanFreePath, det.params().probeRange);
    EXPECT_TRUE(s.skyOpen);
    const ReverbParams r = det.reverbFor(s);
    EXPECT_FLOAT_EQ(r.sendLevel, det.params().minSend);
    EXPECT_FLOAT_EQ(r.decaySeconds, det.params().minDecaySeconds);
}

TEST(EnclosureDetector, FlatGroundOnlyHitsDownwardProbes) {
    FakeVoxelGrid grid;
    grid.fillBox({-20, -1, -20}, {20, -1, 20});
    const EnclosureSample s = EnclosureDetector{}.probe(LISTENER, grid);
    EXPECT_EQ(s.hits, 9u);
    EXPECT_NEAR(s.enclosure, 9.0f / 26.0f, 1e-6f);
    EXPECT_TRUE(s.skyOpen);
}

TEST(EnclosureDetector, SmallSealedRoomIsFullyEnclosedWithShortPaths) {
    FakeVoxelGrid grid;
    grid.hollowBox({-2, -2, -2}, {2, 2, 2}); // 3x3x3 air pocket around the listener
    const EnclosureSample s = EnclosureDetector{}.probe(LISTENER, grid);
    EXPECT_EQ(s.hits, 26u);
    EXPECT_FLOAT_EQ(s.enclosure, 1.0f);
    EXPECT_FALSE(s.skyOpen);
    EXPECT_LT(s.meanFreePath, 3.0f);
    EXPECT_GT(s.meanFreePath, 1.0f);
}

TEST(EnclosureDetector, LargeCaveHasLongerDecayThanSmallRoom) {
    FakeVoxelGrid small;
    small.hollowBox({-2, -2, -2}, {2, 2, 2});
    FakeVoxelGrid cave;
    cave.hollowBox({-12, -12, -12}, {12, 12, 12});
    const EnclosureDetector det;
    const EnclosureSample ss = det.probe(LISTENER, small);
    const EnclosureSample cs = det.probe(LISTENER, cave);
    EXPECT_FLOAT_EQ(cs.enclosure, 1.0f);
    EXPECT_GT(cs.meanFreePath, 8.0f);
    const ReverbParams rs = det.reverbFor(ss);
    const ReverbParams rc = det.reverbFor(cs);
    EXPECT_GT(rc.decaySeconds, rs.decaySeconds + 1.0f);
    EXPECT_GT(rc.feedback, rs.feedback);
    EXPECT_FLOAT_EQ(rc.sendLevel, det.params().maxSend);
    EXPECT_FLOAT_EQ(rs.sendLevel, det.params().maxSend);
    EXPECT_LE(rc.damping, rs.damping); // Big caves keep more highs in their tail
}

TEST(EnclosureDetector, CaveIsMuchWetterThanOutdoors) {
    FakeVoxelGrid cave;
    cave.hollowBox({-10, -6, -10}, {10, 6, 10});
    FakeVoxelGrid outdoors;
    outdoors.fillBox({-20, -1, -20}, {20, -1, 20});
    const EnclosureDetector det;
    const ReverbParams rc = det.analyze(LISTENER, cave);
    const ReverbParams ro = det.analyze(LISTENER, outdoors);
    EXPECT_GT(rc.sendLevel, 2.0f * ro.sendLevel);
    EXPECT_GT(rc.decaySeconds, ro.decaySeconds);
}

TEST(EnclosureDetector, WallsBeyondProbeRangeCountAsOpen) {
    FakeVoxelGrid grid;
    grid.hollowBox({-40, -40, -40}, {40, 40, 40});
    const EnclosureSample s = EnclosureDetector{}.probe(LISTENER, grid);
    EXPECT_EQ(s.hits, 0u);
}

TEST(EnclosureDetector, ListenerVoxelItselfIsIgnored) {
    FakeVoxelGrid grid;
    grid.set({0, 0, 0});
    const EnclosureSample s = EnclosureDetector{}.probe(LISTENER, grid);
    EXPECT_EQ(s.hits, 0u);
}

TEST(EnclosureDetector, HitDistanceIsDistanceToFirstSolidFace) {
    FakeVoxelGrid grid;
    grid.fillBox({4, -30, -30}, {4, 30, 30}); // Wall at x in [4, 5)
    const EnclosureSample s = EnclosureDetector{}.probe(LISTENER, grid);
    // Only the 9 +x probes can reach it within range; the axis-aligned +x probe hits at 3.5.
    EXPECT_EQ(s.hits, 9u);
    EXPECT_NEAR(s.minHitDistance, 3.5f, 1e-4f);
}

TEST(EnclosureDetector, InvalidListenerYieldsOutdoorDefaults) {
    FakeVoxelGrid grid;
    grid.hollowBox({-2, -2, -2}, {2, 2, 2});
    const EnclosureDetector det;
    const EnclosureSample s = det.probe({std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f}, grid);
    EXPECT_FALSE(s.valid);
    EXPECT_FLOAT_EQ(s.enclosure, 0.0f);
    const ReverbParams r = det.reverbFor(s);
    EXPECT_FLOAT_EQ(r.sendLevel, det.params().minSend);
}

TEST(EnclosureDetector, QueryCountIsBounded) {
    FakeVoxelGrid grid;
    const EnclosureDetector det;
    (void)det.probe({0.37f, 0.81f, 0.12f}, grid);
    const auto range = static_cast<uint64_t>(std::ceil(det.params().probeRange));
    EXPECT_LE(grid.queries(), 26u * 3u * (range + 1u));
}

TEST(EnclosureDetector, PropertyAddingSolidsNeverReducesEnclosure) {
    std::mt19937 rng(4242u);
    std::uniform_int_distribution<int> cell(-15, 15);
    const EnclosureDetector det;
    FakeVoxelGrid grid;
    float prev = det.probe(LISTENER, grid).enclosure;
    for (int i = 0; i < 300; ++i) {
        const BlockPos p{cell(rng), cell(rng), cell(rng)};
        if (p == BlockPos{0, 0, 0}) {
            continue;
        }
        grid.set(p);
        const EnclosureSample s = det.probe(LISTENER, grid);
        ASSERT_GE(s.enclosure, prev);
        ASSERT_GE(s.enclosure, 0.0f);
        ASSERT_LE(s.enclosure, 1.0f);
        ASSERT_GT(s.meanFreePath, 0.0f);
        ASSERT_LE(s.meanFreePath, det.params().probeRange);
        const ReverbParams r = det.reverbFor(s);
        ASSERT_GE(r.feedback, 0.0f);
        ASSERT_LE(r.feedback, FdnReverbProcessor::MAX_FEEDBACK);
        ASSERT_GE(r.damping, 0.0f);
        ASSERT_LE(r.damping, FdnReverbProcessor::MAX_DAMPING);
        ASSERT_GE(r.sendLevel, det.params().minSend);
        ASSERT_LE(r.sendLevel, det.params().maxSend);
        prev = s.enclosure;
    }
}

// --- Decay <-> FDN loop gain ----------------------------------------------------------------------

TEST(ReverbMapping, FeedbackForDecayMatchesRt60Formula) {
    // g = 10^(-3 * meanDelay / (RT60 * fs)): each pass through a mean-length line loses 60 dB / passes.
    const float meanDelay = static_cast<float>(FdnReverbProcessor::TOTAL_DELAY_SAMPLES) / 8.0f;
    EXPECT_NEAR(feedbackForDecay(1.0f), std::pow(10.0f, -3.0f * meanDelay / 44100.0f), 1e-6f);
    EXPECT_NEAR(feedbackForDecay(2.0f), std::pow(10.0f, -3.0f * meanDelay / (2.0f * 44100.0f)), 1e-6f);
}

TEST(ReverbMapping, FeedbackForDecayIsMonotoneAndClamped) {
    float prev = feedbackForDecay(0.05f);
    for (float t = 0.1f; t < 60.0f; t *= 1.25f) {
        const float g = feedbackForDecay(t);
        EXPECT_GE(g, prev);
        EXPECT_LE(g, FdnReverbProcessor::MAX_FEEDBACK);
        prev = g;
    }
    EXPECT_FLOAT_EQ(feedbackForDecay(1000.0f), FdnReverbProcessor::MAX_FEEDBACK);
    EXPECT_FLOAT_EQ(feedbackForDecay(0.0f), 0.0f);
    EXPECT_FLOAT_EQ(feedbackForDecay(-1.0f), 0.0f);
    EXPECT_FLOAT_EQ(feedbackForDecay(std::numeric_limits<float>::quiet_NaN()), 0.0f);
}

TEST(ReverbMapping, ReverbFeedbackIsDerivedFromDecay) {
    FakeVoxelGrid cave;
    cave.hollowBox({-8, -8, -8}, {8, 8, 8});
    const EnclosureDetector det;
    const ReverbParams r = det.analyze(LISTENER, cave);
    EXPECT_FLOAT_EQ(r.feedback, feedbackForDecay(r.decaySeconds));
    EXPECT_GE(r.decaySeconds, det.params().minDecaySeconds);
    EXPECT_LE(r.decaySeconds, det.params().maxDecaySeconds);
}

TEST(ReverbMapping, MeasuredFdnDecayRoughlyMatchesRequestedRt60) {
    // Sanity check that the mapping really produces the requested tail length on the actual FDN.
    FdnReverbProcessor fdn;
    const float rt60 = 1.5f;
    fdn.setParameters(feedbackForDecay(rt60), 0.0f);
    std::vector<float> in(512, 0.0f);
    std::vector<float> l(512);
    std::vector<float> r(512);
    in[0] = 1.0f;
    (void)fdn.processBlock(in, l, r);
    in[0] = 0.0f;
    // Let the first reflections build up, then measure energy over rt60 seconds.
    for (int i = 0; i < 20; ++i) {
        (void)fdn.processBlock(in, l, r);
    }
    const double e0 = fdn.energy();
    const auto blocks = static_cast<int>(rt60 * 44100.0f / 512.0f);
    for (int i = 0; i < blocks; ++i) {
        (void)fdn.processBlock(in, l, r);
    }
    const double dropDb = 10.0 * std::log10(e0 / fdn.energy());
    EXPECT_NEAR(dropDb, 60.0, 8.0);
}

TEST(ReverbMapping, BusSendIsAMultiplierAroundTheSpecNeutralLevel) {
    // The bus send scales each voice's own send (spec default 0.15): 1 is neutral, open terrain is
    // drier than the spec default and a sealed cave wetter, without the gain collapsing to ~0.
    const EnclosureParams p{};
    EXPECT_LT(p.minSend, 1.0f);
    EXPECT_GT(p.minSend, 0.1f);
    EXPECT_GT(p.maxSend, 1.0f);
    EXPECT_LE(p.maxSend, AuxReverbBus::MAX_SEND_LEVEL);
}

TEST(ReverbMapping, OpenSkyMakesTheSameEnclosureDrierAndShorter) {
    // A cave with a skylight straight above the listener leaks its energy upwards: same hit fraction
    // and free path as a sealed one, but less send and a shorter tail.
    const EnclosureDetector det;
    EnclosureSample sealed;
    sealed.valid = true;
    sealed.enclosure = 0.9f;
    sealed.meanFreePath = 12.0f;
    sealed.hits = 23;
    sealed.skyOpen = false;
    EnclosureSample skylight = sealed;
    skylight.skyOpen = true;
    const ReverbParams rs = det.reverbFor(sealed);
    const ReverbParams ro = det.reverbFor(skylight);
    EXPECT_LT(ro.sendLevel, rs.sendLevel);
    EXPECT_LT(ro.decaySeconds, rs.decaySeconds);
    EXPECT_LT(ro.feedback, rs.feedback);
    EXPECT_GE(ro.sendLevel, det.params().minSend);
    EXPECT_GE(ro.decaySeconds, det.params().minDecaySeconds);
}

TEST(ReverbMapping, SkyOpenFlagIsIrrelevantWhenNothingIsHit) {
    const EnclosureDetector det;
    EnclosureSample open;
    open.valid = true;
    open.meanFreePath = det.params().probeRange;
    open.skyOpen = true;
    EnclosureSample openNoSky = open;
    openNoSky.skyOpen = false;
    EXPECT_FLOAT_EQ(det.reverbFor(open).sendLevel, det.params().minSend);
    EXPECT_FLOAT_EQ(det.reverbFor(openNoSky).sendLevel, det.params().minSend);
}
