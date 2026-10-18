// P4.2 — 10 Hz cave/enclosure monitor feeding the shared reverb bus (rate limit, dedup, snap rules).
#include "AcousticsTestUtils.hpp"
#include "acoustics/AuxReverbBus.hpp"
#include "acoustics/EnclosureMonitor.hpp"
#include "acoustics/FdnReverb.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <random>

namespace {

using namespace betacraft::acoustics;
using betacraft::acoustics::test::FakeVoxelGrid;

const glm::vec3 LISTENER{0.5f, 0.5f, 0.5f};

FakeVoxelGrid makeCave() {
    FakeVoxelGrid g;
    g.hollowBox({-10, -6, -10}, {10, 6, 10});
    return g;
}

} // namespace

TEST(EnclosureMonitor, DefaultsToTenHertz) {
    const EnclosureMonitor m;
    EXPECT_DOUBLE_EQ(m.interval(), 0.1);
}

TEST(EnclosureMonitor, FirstSampleIsEmittedAndSnaps) {
    const FakeVoxelGrid cave = makeCave();
    EnclosureMonitor m;
    const auto u = m.update(0.0, LISTENER, cave);
    ASSERT_TRUE(u.has_value());
    EXPECT_TRUE(u->snap);
    const ReverbParams expected = EnclosureDetector{}.analyze(LISTENER, cave);
    EXPECT_FLOAT_EQ(u->params.sendLevel, expected.sendLevel);
    EXPECT_FLOAT_EQ(u->params.feedback, expected.feedback);
    EXPECT_FLOAT_EQ(u->params.damping, expected.damping);
}

TEST(EnclosureMonitor, NoProbesBetweenDueTimes) {
    FakeVoxelGrid cave = makeCave();
    EnclosureMonitor m;
    (void)m.update(0.0, LISTENER, cave);
    cave.resetQueries();
    EXPECT_FALSE(m.update(0.05, LISTENER, cave).has_value());
    EXPECT_FALSE(m.update(0.0999, LISTENER, cave).has_value());
    EXPECT_EQ(cave.queries(), 0u);
}

TEST(EnclosureMonitor, UnchangedSurroundingsEmitNothing) {
    // The probes still run at 10 Hz, but an identical result is not re-sent through the queue.
    FakeVoxelGrid cave = makeCave();
    EnclosureMonitor m;
    (void)m.update(0.0, LISTENER, cave);
    cave.resetQueries();
    EXPECT_FALSE(m.update(0.1, LISTENER, cave).has_value());
    EXPECT_GT(cave.queries(), 0u);
}

TEST(EnclosureMonitor, ChangedSurroundingsEmitASmoothedUpdate) {
    FakeVoxelGrid open;
    EnclosureMonitor m;
    ASSERT_TRUE(m.update(0.0, LISTENER, open).has_value());
    const FakeVoxelGrid cave = makeCave(); // Same position, the world around changed (e.g. built walls)
    const auto u = m.update(0.1, LISTENER, cave);
    ASSERT_TRUE(u.has_value());
    EXPECT_FALSE(u->snap);
    EXPECT_GT(u->params.sendLevel, EnclosureParams{}.minSend);
}

TEST(EnclosureMonitor, TeleportSnaps) {
    const FakeVoxelGrid cave = makeCave();
    EnclosureMonitor m;
    (void)m.update(0.0, LISTENER, cave);
    const glm::vec3 far = LISTENER + glm::vec3{EnclosureMonitor::TELEPORT_DISTANCE + 1.0f, 0.0f, 0.0f};
    const auto u = m.update(0.1, far, cave);
    ASSERT_TRUE(u.has_value());
    EXPECT_TRUE(u->snap);
}

TEST(EnclosureMonitor, WalkingDoesNotSnap) {
    FakeVoxelGrid cave = makeCave();
    EnclosureMonitor m;
    (void)m.update(0.0, LISTENER, cave);
    // 0.4 blocks in 100 ms is walking speed; the geometry change still yields a smoothed update.
    const auto u = m.update(0.1, LISTENER + glm::vec3{0.4f, 0.0f, 0.0f}, FakeVoxelGrid{});
    ASSERT_TRUE(u.has_value());
    EXPECT_FALSE(u->snap);
}

TEST(EnclosureMonitor, ResetForcesASnappedResample) {
    const FakeVoxelGrid cave = makeCave();
    EnclosureMonitor m;
    (void)m.update(0.0, LISTENER, cave);
    m.reset();
    const auto u = m.update(0.01, LISTENER, cave);
    ASSERT_TRUE(u.has_value());
    EXPECT_TRUE(u->snap);
}

TEST(EnclosureMonitor, ClockGoingBackwardsResynchronisesWithoutBurst) {
    const FakeVoxelGrid open;
    const FakeVoxelGrid cave = makeCave();
    EnclosureMonitor m;
    (void)m.update(50.0, LISTENER, open);
    EXPECT_FALSE(m.update(1.0, LISTENER, cave).has_value());
    EXPECT_FALSE(m.update(1.05, LISTENER, cave).has_value());
    EXPECT_TRUE(m.update(1.1, LISTENER, cave).has_value());
}

TEST(EnclosureMonitor, NonFiniteTimeOrListenerIsIgnored) {
    const FakeVoxelGrid cave = makeCave();
    EnclosureMonitor m;
    EXPECT_FALSE(m.update(std::numeric_limits<double>::quiet_NaN(), LISTENER, cave).has_value());
    EXPECT_FALSE(m.update(0.0, {std::numeric_limits<float>::infinity(), 0.0f, 0.0f}, cave).has_value());
    EXPECT_TRUE(m.update(0.0, LISTENER, cave).has_value());
}

TEST(EnclosureMonitor, InvalidIntervalFallsBackToDefault) {
    EXPECT_DOUBLE_EQ(EnclosureMonitor(0.0).interval(), 0.1);
    EXPECT_DOUBLE_EQ(EnclosureMonitor(std::numeric_limits<double>::quiet_NaN()).interval(), 0.1);
    EXPECT_DOUBLE_EQ(EnclosureMonitor(0.25).interval(), 0.25);
}

TEST(EnclosureMonitor, PropertyEmissionsAreRateLimitedAndInRange) {
    FakeVoxelGrid grid;
    std::mt19937 rng(515u);
    std::uniform_int_distribution<int> cell(-14, 14);
    for (int i = 0; i < 2500; ++i) {
        grid.set({cell(rng), cell(rng), cell(rng)});
    }
    std::uniform_real_distribution<double> dt(0.0, 0.04);
    std::uniform_real_distribution<float> step(-0.3f, 0.3f);
    EnclosureMonitor m;
    glm::vec3 pos = LISTENER;
    double now = 0.0;
    double last = -1.0;
    int emitted = 0;
    for (int f = 0; f < 3000; ++f) {
        now += dt(rng);
        pos += glm::vec3{step(rng), 0.0f, step(rng)};
        pos = glm::clamp(pos, glm::vec3{-12.0f}, glm::vec3{12.0f});
        const auto u = m.update(now, pos, grid);
        if (!u) {
            continue;
        }
        if (last >= 0.0) {
            ASSERT_GE(now - last, m.interval() - 1e-9);
        }
        ASSERT_GE(u->params.sendLevel, 0.0f);
        ASSERT_LE(u->params.sendLevel, AuxReverbBus::MAX_SEND_LEVEL);
        ASSERT_LE(u->params.feedback, FdnReverbProcessor::MAX_FEEDBACK);
        ASSERT_LE(u->params.damping, FdnReverbProcessor::MAX_DAMPING);
        last = now;
        ++emitted;
    }
    EXPECT_GT(emitted, 10);
}
