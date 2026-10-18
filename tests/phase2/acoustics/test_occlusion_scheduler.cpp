// P4.2 — 10 Hz occlusion scheduler: per-source rate limiting with an injected clock.
#include "AcousticsTestUtils.hpp"
#include "acoustics/OcclusionScheduler.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <map>
#include <random>
#include <type_traits>
#include <vector>

namespace {

using namespace betacraft::acoustics;
using betacraft::acoustics::test::FakeVoxelGrid;

const glm::vec3 LISTENER{0.5f, 0.5f, 0.5f};

bool contains(std::span<const OcclusionUpdate> ups, uint32_t handle) {
    for (const OcclusionUpdate& u : ups) {
        if (u.voiceHandle == handle) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST(OcclusionScheduler, DefaultsToTenHertz) {
    EXPECT_DOUBLE_EQ(OCCLUSION_UPDATE_INTERVAL_S, 0.1);
    const OcclusionScheduler s;
    EXPECT_DOUBLE_EQ(s.interval(), 0.1);
    EXPECT_EQ(OcclusionScheduler::CAPACITY, 32u);
}

TEST(OcclusionScheduler, UpdatePayloadIsTriviallyCopyableForMpscQueue) {
    EXPECT_TRUE(std::is_trivially_copyable_v<OcclusionUpdate>);
}

TEST(OcclusionScheduler, NewSourceIsComputedOnFirstUpdate) {
    FakeVoxelGrid grid;
    grid.fillBox({2, -3, -3}, {2, 3, 3});
    OcclusionScheduler s;
    ASSERT_TRUE(s.addSource(7u, {5.5f, 0.5f, 0.5f}));
    const auto ups = s.update(10.0, LISTENER, grid);
    ASSERT_EQ(ups.size(), 1u);
    EXPECT_EQ(ups[0].voiceHandle, 7u);
    EXPECT_EQ(ups[0].solidVoxels, 1u);
    EXPECT_NEAR(ups[0].cutoffHz, 13000.0f, 0.01f);
    EXPECT_NEAR(ups[0].gain, 0.8f, 1e-5f);
    const auto t = s.target(7u);
    ASSERT_TRUE(t.has_value());
    EXPECT_NEAR(t->cutoffHz, 13000.0f, 0.01f);
}

TEST(OcclusionScheduler, RecomputesAtMostEveryHundredMilliseconds) {
    FakeVoxelGrid grid;
    OcclusionScheduler s;
    ASSERT_TRUE(s.addSource(1u, {8.5f, 0.5f, 0.5f}));
    EXPECT_EQ(s.update(0.0, LISTENER, grid).size(), 1u);
    EXPECT_EQ(s.update(0.05, LISTENER, grid).size(), 0u);
    EXPECT_EQ(s.update(0.0999, LISTENER, grid).size(), 0u);
    EXPECT_EQ(s.update(0.1, LISTENER, grid).size(), 1u);
    EXPECT_EQ(s.update(0.15, LISTENER, grid).size(), 0u);
    EXPECT_EQ(s.update(0.2, LISTENER, grid).size(), 1u);
}

TEST(OcclusionScheduler, NoRaysAreCastBetweenDueTimes) {
    FakeVoxelGrid grid;
    OcclusionScheduler s;
    ASSERT_TRUE(s.addSource(1u, {20.5f, 0.5f, 0.5f}));
    (void)s.update(0.0, LISTENER, grid);
    grid.resetQueries();
    for (int i = 1; i < 6; ++i) {
        (void)s.update(0.016 * i, LISTENER, grid);
    }
    EXPECT_EQ(grid.queries(), 0u);
}

TEST(OcclusionScheduler, RealFrameRatesYieldAboutTenHertzNeverMore) {
    for (const double fps : {30.0, 60.0, 144.0, 240.0}) {
        FakeVoxelGrid grid;
        OcclusionScheduler s;
        ASSERT_TRUE(s.addSource(3u, {4.5f, 1.5f, 0.5f}));
        int computes = 0;
        double lastCompute = -1.0;
        const int frames = static_cast<int>(fps * 10.0);
        for (int f = 0; f < frames; ++f) {
            const double now = static_cast<double>(f) / fps;
            if (!s.update(now, LISTENER, grid).empty()) {
                if (lastCompute >= 0.0) {
                    EXPECT_GE(now - lastCompute, 0.1 - 1e-9) << "fps " << fps;
                }
                lastCompute = now;
                ++computes;
            }
        }
        EXPECT_LE(computes, 101) << "fps " << fps;
        EXPECT_GE(computes, 90) << "fps " << fps; // 144 fps quantises to every 15th frame (9.6 Hz)
    }
}

TEST(OcclusionScheduler, MovingSourceDoesNotBypassRateLimit) {
    FakeVoxelGrid grid;
    grid.fillBox({2, -3, -3}, {2, 3, 3});
    OcclusionScheduler s;
    ASSERT_TRUE(s.addSource(1u, {1.5f, 0.5f, 0.5f})); // In front of the wall: unoccluded
    (void)s.update(0.0, LISTENER, grid);
    EXPECT_FLOAT_EQ(s.target(1u)->gain, 1.0f);
    ASSERT_TRUE(s.setSourcePosition(1u, {5.5f, 0.5f, 0.5f})); // Behind the wall
    EXPECT_TRUE(s.update(0.05, LISTENER, grid).empty());
    EXPECT_FLOAT_EQ(s.target(1u)->gain, 1.0f);
    EXPECT_FALSE(s.update(0.1, LISTENER, grid).empty());
    EXPECT_LT(s.target(1u)->gain, 1.0f);
}

TEST(OcclusionScheduler, SourcesAreScheduledIndependently) {
    FakeVoxelGrid grid;
    OcclusionScheduler s;
    ASSERT_TRUE(s.addSource(1u, {3.5f, 0.5f, 0.5f}));
    (void)s.update(0.0, LISTENER, grid);
    ASSERT_TRUE(s.addSource(2u, {-3.5f, 0.5f, 0.5f}));
    auto ups = s.update(0.04, LISTENER, grid);
    ASSERT_EQ(ups.size(), 1u);
    EXPECT_EQ(ups[0].voiceHandle, 2u);
    ups = s.update(0.1, LISTENER, grid);
    ASSERT_EQ(ups.size(), 1u);
    EXPECT_EQ(ups[0].voiceHandle, 1u);
    ups = s.update(0.14, LISTENER, grid);
    ASSERT_EQ(ups.size(), 1u);
    EXPECT_EQ(ups[0].voiceHandle, 2u);
}

TEST(OcclusionScheduler, CapacityAndDuplicateHandles) {
    OcclusionScheduler s;
    for (uint32_t h = 0; h < OcclusionScheduler::CAPACITY; ++h) {
        ASSERT_TRUE(s.addSource(100u + h, glm::vec3{0.0f}));
    }
    EXPECT_EQ(s.sourceCount(), OcclusionScheduler::CAPACITY);
    EXPECT_FALSE(s.addSource(999u, glm::vec3{0.0f}));
    EXPECT_FALSE(s.addSource(100u, glm::vec3{0.0f})) << "duplicate handle must be rejected";
    EXPECT_TRUE(s.removeSource(100u));
    EXPECT_FALSE(s.removeSource(100u));
    EXPECT_TRUE(s.addSource(999u, glm::vec3{0.0f}));
}

TEST(OcclusionScheduler, UnknownHandlesAreRejected) {
    OcclusionScheduler s;
    EXPECT_FALSE(s.setSourcePosition(42u, glm::vec3{1.0f}));
    EXPECT_FALSE(s.target(42u).has_value());
    EXPECT_FALSE(s.removeSource(42u));
}

TEST(OcclusionScheduler, TargetIsEmptyUntilFirstComputation) {
    OcclusionScheduler s;
    ASSERT_TRUE(s.addSource(5u, glm::vec3{2.0f}));
    EXPECT_FALSE(s.target(5u).has_value());
}

TEST(OcclusionScheduler, ReAddedHandleIsComputedImmediately) {
    FakeVoxelGrid grid;
    OcclusionScheduler s;
    ASSERT_TRUE(s.addSource(1u, glm::vec3{4.0f}));
    (void)s.update(0.0, LISTENER, grid);
    ASSERT_TRUE(s.removeSource(1u));
    ASSERT_TRUE(s.addSource(1u, glm::vec3{4.0f}));
    EXPECT_EQ(s.update(0.01, LISTENER, grid).size(), 1u);
}

TEST(OcclusionScheduler, ClockGoingBackwardsResynchronisesWithoutBurst) {
    FakeVoxelGrid grid;
    OcclusionScheduler s;
    ASSERT_TRUE(s.addSource(1u, glm::vec3{4.0f}));
    (void)s.update(100.0, LISTENER, grid);
    EXPECT_TRUE(s.update(5.0, LISTENER, grid).empty());  // jump back: resync only
    EXPECT_TRUE(s.update(5.05, LISTENER, grid).empty());
    EXPECT_EQ(s.update(5.1, LISTENER, grid).size(), 1u); // resumes on the new timeline
}

TEST(OcclusionScheduler, NonFiniteTimeIsIgnored) {
    FakeVoxelGrid grid;
    OcclusionScheduler s;
    ASSERT_TRUE(s.addSource(1u, glm::vec3{4.0f}));
    EXPECT_TRUE(s.update(std::numeric_limits<double>::quiet_NaN(), LISTENER, grid).empty());
    EXPECT_TRUE(s.update(std::numeric_limits<double>::infinity(), LISTENER, grid).empty());
    EXPECT_EQ(s.update(0.0, LISTENER, grid).size(), 1u);
}

TEST(OcclusionScheduler, CustomIntervalIsHonouredAndInvalidFallsBackToDefault) {
    FakeVoxelGrid grid;
    OcclusionScheduler fast(0.05);
    ASSERT_TRUE(fast.addSource(1u, glm::vec3{4.0f}));
    (void)fast.update(0.0, LISTENER, grid);
    EXPECT_EQ(fast.update(0.05, LISTENER, grid).size(), 1u);
    const OcclusionScheduler bad(-1.0);
    EXPECT_DOUBLE_EQ(bad.interval(), OCCLUSION_UPDATE_INTERVAL_S);
    const OcclusionScheduler nan(std::numeric_limits<double>::quiet_NaN());
    EXPECT_DOUBLE_EQ(nan.interval(), OCCLUSION_UPDATE_INTERVAL_S);
}

TEST(OcclusionScheduler, ListenerMovementIsUsedAtRecomputeTime) {
    FakeVoxelGrid grid;
    grid.fillBox({2, -3, -3}, {2, 3, 3});
    OcclusionScheduler s;
    ASSERT_TRUE(s.addSource(1u, {5.5f, 0.5f, 0.5f}));
    (void)s.update(0.0, LISTENER, grid);
    EXPECT_LT(s.target(1u)->gain, 1.0f);
    (void)s.update(0.1, {4.5f, 0.5f, 0.5f}, grid); // Listener walked through to the other side
    EXPECT_FLOAT_EQ(s.target(1u)->gain, 1.0f);
}

TEST(OcclusionScheduler, PropertyDeterministicReplay) {
    FakeVoxelGrid grid;
    std::mt19937 rng(2024u);
    std::uniform_int_distribution<int> cell(-10, 10);
    for (int i = 0; i < 800; ++i) {
        grid.set({cell(rng), cell(rng), cell(rng)});
    }
    auto run = [&](uint32_t seed) {
        std::mt19937 r(seed);
        std::uniform_real_distribution<float> pos(-10.0f, 10.0f);
        std::uniform_real_distribution<double> dt(0.0, 0.05);
        OcclusionScheduler s;
        for (uint32_t h = 0; h < 16u; ++h) {
            (void)s.addSource(h, {pos(r), pos(r), pos(r)});
        }
        std::vector<OcclusionUpdate> log;
        double now = 0.0;
        for (int f = 0; f < 300; ++f) {
            now += dt(r);
            (void)s.setSourcePosition(static_cast<uint32_t>(f % 16), {pos(r), pos(r), pos(r)});
            for (const OcclusionUpdate& u : s.update(now, {pos(r), pos(r), pos(r)}, grid)) {
                log.push_back(u);
            }
        }
        return log;
    };
    const auto a = run(9u);
    const auto b = run(9u);
    ASSERT_EQ(a.size(), b.size());
    ASSERT_FALSE(a.empty());
    for (size_t i = 0; i < a.size(); ++i) {
        ASSERT_EQ(a[i].voiceHandle, b[i].voiceHandle);
        ASSERT_EQ(a[i].solidVoxels, b[i].solidVoxels);
        ASSERT_FLOAT_EQ(a[i].gain, b[i].gain);
        ASSERT_FLOAT_EQ(a[i].cutoffHz, b[i].cutoffHz);
    }
}

TEST(OcclusionScheduler, PropertyPerSourceRateLimitUnderRandomFrameTimes) {
    FakeVoxelGrid grid;
    OcclusionScheduler s;
    for (uint32_t h = 0; h < OcclusionScheduler::CAPACITY; ++h) {
        ASSERT_TRUE(s.addSource(h, {static_cast<float>(h), 1.0f, 2.0f}));
    }
    std::mt19937 rng(31u);
    std::uniform_real_distribution<double> dt(0.0, 0.03);
    std::map<uint32_t, double> last;
    double now = 0.0;
    for (int f = 0; f < 2000; ++f) {
        now += dt(rng);
        for (const OcclusionUpdate& u : s.update(now, LISTENER, grid)) {
            const auto it = last.find(u.voiceHandle);
            if (it != last.end()) {
                ASSERT_GE(now - it->second, 0.1 - 1e-9);
            }
            last[u.voiceHandle] = now;
        }
    }
    EXPECT_EQ(last.size(), OcclusionScheduler::CAPACITY);
    EXPECT_TRUE(contains(s.update(now + 1.0, LISTENER, grid), 0u));
}

// --- Game-thread interpolation between 10 Hz samples (anti-zipper for block-rate consumers) -------

namespace {

struct WallFixture {
    FakeVoxelGrid grid;
    WallFixture() { grid.fillBox({2, -3, -3}, {2, 3, 3}); }
};

const glm::vec3 IN_FRONT{1.5f, 0.5f, 0.5f}; // Same side as LISTENER: unoccluded
const glm::vec3 BEHIND{5.5f, 0.5f, 0.5f};   // Behind the 1-block wall at x = 2

} // namespace

TEST(OcclusionSchedulerInterpolation, UnknownOrNotYetEvaluatedIsEmpty) {
    OcclusionScheduler s;
    EXPECT_FALSE(s.interpolatedTarget(1u, 0.0).has_value());
    ASSERT_TRUE(s.addSource(1u, IN_FRONT));
    EXPECT_FALSE(s.interpolatedTarget(1u, 0.0).has_value());
}

TEST(OcclusionSchedulerInterpolation, FirstEvaluationSnapsWithoutRamp) {
    // A fresh sound behind a wall must start muffled, not fade in from "unoccluded".
    WallFixture w;
    OcclusionScheduler s;
    ASSERT_TRUE(s.addSource(1u, BEHIND));
    (void)s.update(3.0, LISTENER, w.grid);
    const auto t = s.interpolatedTarget(1u, 3.0);
    ASSERT_TRUE(t.has_value());
    EXPECT_FLOAT_EQ(t->gain, s.target(1u)->gain);
    EXPECT_FLOAT_EQ(t->cutoffHz, s.target(1u)->cutoffHz);
}

TEST(OcclusionSchedulerInterpolation, RampsLinearlyInGainOverOneInterval) {
    WallFixture w;
    OcclusionScheduler s;
    ASSERT_TRUE(s.addSource(1u, IN_FRONT));
    (void)s.update(0.0, LISTENER, w.grid);
    ASSERT_TRUE(s.setSourcePosition(1u, BEHIND));
    (void)s.update(0.1, LISTENER, w.grid);
    const float g1 = s.target(1u)->gain;
    ASSERT_LT(g1, 1.0f);

    EXPECT_FLOAT_EQ(s.interpolatedTarget(1u, 0.1)->gain, 1.0f);              // Ramp starts at old value
    EXPECT_NEAR(s.interpolatedTarget(1u, 0.125)->gain, 1.0f + 0.25f * (g1 - 1.0f), 1e-5f);
    EXPECT_NEAR(s.interpolatedTarget(1u, 0.15)->gain, 0.5f * (1.0f + g1), 1e-5f);
    EXPECT_FLOAT_EQ(s.interpolatedTarget(1u, 0.2)->gain, g1);                // ... and lands on the new one
    EXPECT_FLOAT_EQ(s.interpolatedTarget(1u, 5.0)->gain, g1);                // Holds afterwards
}

TEST(OcclusionSchedulerInterpolation, CutoffRampsGeometrically) {
    // Pitch perception is logarithmic: halfway through the ramp the cutoff is the geometric mean.
    WallFixture w;
    OcclusionScheduler s;
    ASSERT_TRUE(s.addSource(1u, IN_FRONT));
    (void)s.update(0.0, LISTENER, w.grid);
    ASSERT_TRUE(s.setSourcePosition(1u, BEHIND));
    (void)s.update(0.1, LISTENER, w.grid);
    const float c1 = s.target(1u)->cutoffHz; // 13 kHz
    EXPECT_NEAR(s.interpolatedTarget(1u, 0.15)->cutoffHz, std::sqrt(20000.0f * c1), 0.5f);
    EXPECT_NEAR(s.interpolatedTarget(1u, 0.2)->cutoffHz, c1, 0.01f);
}

TEST(OcclusionSchedulerInterpolation, TimeBeforeRampStartHoldsStartValue) {
    WallFixture w;
    OcclusionScheduler s;
    ASSERT_TRUE(s.addSource(1u, IN_FRONT));
    (void)s.update(0.0, LISTENER, w.grid);
    ASSERT_TRUE(s.setSourcePosition(1u, BEHIND));
    (void)s.update(0.1, LISTENER, w.grid);
    EXPECT_FLOAT_EQ(s.interpolatedTarget(1u, 0.05)->gain, 1.0f);
    EXPECT_FLOAT_EQ(s.interpolatedTarget(1u, std::numeric_limits<double>::quiet_NaN())->gain, 1.0f);
}

TEST(OcclusionSchedulerInterpolation, PropertyContinuousAndRateBoundedUnderRandomMotion) {
    // Sampled every millisecond while the source teleports around randomly, the interpolated gain never
    // jumps by more than (full range / interval) * dt, i.e. there is no zipper step at update times.
    FakeVoxelGrid grid;
    std::mt19937 rng(606u);
    std::uniform_int_distribution<int> cell(-12, 12);
    for (int i = 0; i < 1500; ++i) {
        grid.set({cell(rng), cell(rng), cell(rng)});
    }
    std::uniform_real_distribution<float> pos(-12.0f, 12.0f);
    OcclusionScheduler s;
    ASSERT_TRUE(s.addSource(9u, {pos(rng), pos(rng), pos(rng)}));
    (void)s.update(0.0, LISTENER, grid);
    float prevGain = s.interpolatedTarget(9u, 0.0)->gain;
    float prevLogCut = std::log2(s.interpolatedTarget(9u, 0.0)->cutoffHz);
    const OcclusionParams p{};
    const double dt = 0.001;
    const float maxGainStep = (1.0f - p.minGain) * static_cast<float>(dt / s.interval()) + 1e-5f;
    const float maxLogStep = std::log2(p.maxCutoffHz / p.minCutoffHz) * static_cast<float>(dt / s.interval()) + 1e-4f;
    int changes = 0;
    for (int ms = 1; ms <= 5000; ++ms) {
        const double now = ms * dt;
        if (ms % 37 == 0) {
            (void)s.setSourcePosition(9u, {pos(rng), pos(rng), pos(rng)});
        }
        changes += static_cast<int>(s.update(now, LISTENER, grid).size());
        const OcclusionTarget t = *s.interpolatedTarget(9u, now);
        ASSERT_LE(std::fabs(t.gain - prevGain), maxGainStep) << "t=" << now;
        ASSERT_LE(std::fabs(std::log2(t.cutoffHz) - prevLogCut), maxLogStep) << "t=" << now;
        ASSERT_GE(t.gain, p.minGain);
        ASSERT_LE(t.gain, 1.0f);
        prevGain = t.gain;
        prevLogCut = std::log2(t.cutoffHz);
    }
    EXPECT_GT(changes, 40);
}

TEST(OcclusionSchedulerInterpolation, ClockJumpingBackwardsMidRampSettlesOnLatestTarget) {
    // After a backwards jump (e.g. a world reload resetting the game clock) the old ramp's timeline is
    // meaningless: hold the latest sample rather than snapping back to the ramp's stale start value.
    WallFixture w;
    OcclusionScheduler s;
    ASSERT_TRUE(s.addSource(1u, IN_FRONT));
    (void)s.update(10.0, LISTENER, w.grid);
    ASSERT_TRUE(s.setSourcePosition(1u, BEHIND));
    (void)s.update(10.1, LISTENER, w.grid);
    const float g1 = s.target(1u)->gain;
    ASSERT_NEAR(s.interpolatedTarget(1u, 10.15)->gain, 0.5f * (1.0f + g1), 1e-5f);
    EXPECT_TRUE(s.update(2.0, LISTENER, w.grid).empty());
    EXPECT_FLOAT_EQ(s.interpolatedTarget(1u, 2.0)->gain, g1);
    EXPECT_FLOAT_EQ(s.interpolatedTarget(1u, 2.05)->gain, g1);
}

TEST(OcclusionScheduler, UpdatesFlagSourcesBeyondAudibleRange) {
    FakeVoxelGrid grid;
    OcclusionScheduler s;
    ASSERT_TRUE(s.addSource(1u, {10.5f, 0.5f, 0.5f}));
    ASSERT_TRUE(s.addSource(2u, {90.5f, 0.5f, 0.5f}));
    const auto ups = s.update(0.0, LISTENER, grid);
    ASSERT_EQ(ups.size(), 2u);
    for (const OcclusionUpdate& u : ups) {
        EXPECT_EQ(u.beyondAudibleRange, u.voiceHandle == 2u) << u.voiceHandle;
    }
}

// --- Load spreading: sources registered together must not stay phase-locked -------------------

namespace {

struct LoadStats {
    size_t maxPerFrameAfterFirst{0};
    std::map<uint32_t, int> evaluations;
    double minGap{1e9};
};

LoadStats runFrames(OcclusionScheduler& s, double fps, double seconds, const FakeVoxelGrid& grid) {
    LoadStats st;
    std::map<uint32_t, double> last;
    const int frames = static_cast<int>(fps * seconds);
    for (int f = 0; f < frames; ++f) {
        const double now = static_cast<double>(f) / fps;
        const auto ups = s.update(now, LISTENER, grid);
        if (f > 0) {
            st.maxPerFrameAfterFirst = std::max(st.maxPerFrameAfterFirst, ups.size());
        }
        for (const OcclusionUpdate& u : ups) {
            if (const auto it = last.find(u.voiceHandle); it != last.end()) {
                st.minGap = std::min(st.minGap, now - it->second);
            }
            last[u.voiceHandle] = now;
            ++st.evaluations[u.voiceHandle];
        }
    }
    return st;
}

} // namespace

TEST(OcclusionSchedulerLoad, SourcesStartedTogetherAreSpreadOverSeveralFrames) {
    FakeVoxelGrid grid;
    OcclusionScheduler s;
    for (uint32_t h = 1; h <= OcclusionScheduler::CAPACITY; ++h) {
        ASSERT_TRUE(s.addSource(h, {static_cast<float>(h) + 0.5f, 3.5f, 0.5f})); // e.g. a 32-TNT burst
    }
    const LoadStats st = runFrames(s, 60.0, 3.0, grid);
    // 32 rays every 100 ms at 60 fps is ~5.3 per frame on average: never the whole burst at once.
    EXPECT_LE(st.maxPerFrameAfterFirst, 8u);
    EXPECT_GE(st.minGap, 0.1 - 1e-9); // Still never faster than 10 Hz
    for (const auto& [handle, count] : st.evaluations) {
        EXPECT_GE(count, 27) << handle; // ~10 Hz over 3 s (a few frames of slip at most)
        EXPECT_LE(count, 31) << handle;
    }
    EXPECT_EQ(st.evaluations.size(), OcclusionScheduler::CAPACITY);
}

TEST(OcclusionSchedulerLoad, FullSchedulerKeepsNearlyTenHertzAtLowFrameRates) {
    for (const double fps : {20.0, 30.0, 144.0}) {
        FakeVoxelGrid grid;
        OcclusionScheduler s;
        for (uint32_t h = 1; h <= OcclusionScheduler::CAPACITY; ++h) {
            ASSERT_TRUE(s.addSource(h, {0.5f, 0.5f, static_cast<float>(h) + 0.5f}));
        }
        const LoadStats st = runFrames(s, fps, 5.0, grid);
        for (const auto& [handle, count] : st.evaluations) {
            EXPECT_GE(count, 40) << "fps " << fps << " handle " << handle; // >= 8 Hz
        }
        EXPECT_GE(st.minGap, 0.1 - 1e-9);
    }
}

TEST(OcclusionSchedulerLoad, NewSourcesAreNeverDelayedByTheBudget) {
    FakeVoxelGrid grid;
    OcclusionScheduler s;
    for (uint32_t h = 1; h < OcclusionScheduler::CAPACITY; ++h) {
        ASSERT_TRUE(s.addSource(h, {static_cast<float>(h) + 0.5f, 0.5f, 0.5f}));
    }
    for (int f = 0; f < 6; ++f) {
        (void)s.update(static_cast<double>(f) / 60.0, LISTENER, grid);
    }
    // Frame 6 (t = 0.1 s): all 31 old sources fall due together and exceed the per-frame budget;
    // a brand-new sound must still be measured in this very frame.
    ASSERT_TRUE(s.addSource(999u, {2.5f, 0.5f, 0.5f}));
    const auto ups = s.update(6.0 / 60.0, LISTENER, grid);
    EXPECT_TRUE(contains(ups, 999u));
    EXPECT_LE(ups.size(), 9u); // Budget (ceil(31 / 6) + 1 = 7) + the new source
}

TEST(OcclusionSchedulerLoad, LongFrameEvaluatesEveryDueSource) {
    FakeVoxelGrid grid;
    OcclusionScheduler s;
    for (uint32_t h = 1; h <= OcclusionScheduler::CAPACITY; ++h) {
        ASSERT_TRUE(s.addSource(h, glm::vec3{static_cast<float>(h)}));
    }
    (void)s.update(0.0, LISTENER, grid);
    EXPECT_EQ(s.update(0.25, LISTENER, grid).size(), OcclusionScheduler::CAPACITY); // 4 fps hitch
}
