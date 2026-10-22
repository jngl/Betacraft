#include "streaming/UploadBudget.hpp"

#include "test_support/StreamingTestHelpers.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <random>
#include <stdexcept>
#include <vector>

using namespace std::chrono_literals;
using betacraft::streaming::DrainStats;
using betacraft::streaming::MonotonicClock;
using betacraft::streaming::PingPongStaging;
using betacraft::streaming::drainWithBudget;
using betacraft::streaming::test::FakeClock;

namespace {

struct Fixture {
    PingPongStaging<int> staging;
    FakeClock fake;
    MonotonicClock clock{[this] { return fake.now(); }};
    std::vector<int> uploaded;
    std::chrono::nanoseconds costPerUpload{1ms};

    DrainStats drain(std::chrono::nanoseconds budget, auto isCurrent) {
        return drainWithBudget(staging, budget, clock, isCurrent, [this](int& v) {
            uploaded.push_back(v);
            fake.advance(costPerUpload);
        });
    }
    DrainStats drain(std::chrono::nanoseconds budget) {
        return drain(budget, [](const int&) { return true; });
    }
    void publishRange(int from, int to) {
        for (int i = from; i < to; ++i) staging.publish(i);
    }
};

} // namespace

TEST(UploadBudget, NothingToDoReturnsZeroStats) {
    Fixture f;
    const DrainStats s = f.drain(4ms);
    EXPECT_EQ(s.uploaded, 0u);
    EXPECT_EQ(s.discarded, 0u);
    EXPECT_EQ(s.remaining, 0u);
    EXPECT_FALSE(s.budgetExhausted);
}

TEST(UploadBudget, StopsWhenBudgetIsExhaustedAndKeepsTheRest) {
    Fixture f;
    f.publishRange(0, 10);
    const DrainStats s = f.drain(4ms);
    EXPECT_EQ(s.uploaded, 4u);
    EXPECT_TRUE(s.budgetExhausted);
    EXPECT_EQ(s.remaining, 6u);
    EXPECT_EQ(s.elapsed, 4ms);
    EXPECT_EQ(f.uploaded, (std::vector<int>{0, 1, 2, 3}));
}

TEST(UploadBudget, NextFrameResumesInOrder) {
    Fixture f;
    f.publishRange(0, 10);
    (void)f.drain(4ms);
    f.publishRange(10, 12); // new results arriving between frames go after the backlog
    (void)f.drain(4ms);
    (void)f.drain(4ms);
    EXPECT_EQ(f.uploaded, (std::vector<int>{0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}));
    EXPECT_EQ(f.drain(4ms).uploaded, 0u);
}

TEST(UploadBudget, ZeroBudgetStillUploadsExactlyOne) {
    Fixture f;
    f.publishRange(0, 3);
    const DrainStats s = f.drain(0ns);
    EXPECT_EQ(s.uploaded, 1u);
    EXPECT_EQ(s.remaining, 2u);
    EXPECT_TRUE(s.budgetExhausted);
}

TEST(UploadBudget, SingleUploadSlowerThanBudgetStillMakesProgress) {
    Fixture f;
    f.costPerUpload = 50ms;
    f.publishRange(0, 3);
    for (int frame = 0; frame < 3; ++frame) {
        EXPECT_EQ(f.drain(4ms).uploaded, 1u) << "frame " << frame;
    }
    EXPECT_EQ(f.uploaded.size(), 3u);
}

TEST(UploadBudget, FreeUploadsDrainEverything) {
    Fixture f;
    f.costPerUpload = 0ns;
    f.publishRange(0, 100);
    const DrainStats s = f.drain(4ms);
    EXPECT_EQ(s.uploaded, 100u);
    EXPECT_EQ(s.remaining, 0u);
    EXPECT_FALSE(s.budgetExhausted);
}

TEST(UploadBudget, BudgetBoundaryJustBelowAndAbove) {
    for (const auto& [budget, expected] : std::vector<std::pair<std::chrono::nanoseconds, size_t>>{
             {3999us, 4u}, {4000us, 4u}, {4001us, 5u}}) {
        Fixture f;
        f.publishRange(0, 10);
        EXPECT_EQ(f.drain(budget).uploaded, expected) << budget.count();
    }
}

TEST(UploadBudget, StaleItemsAreDiscardedWithoutUploading) {
    Fixture f;
    f.publishRange(0, 10);
    const DrainStats s = f.drain(100ms, [](const int& v) { return v % 2 == 0; });
    EXPECT_EQ(f.uploaded, (std::vector<int>{0, 2, 4, 6, 8}));
    EXPECT_EQ(s.uploaded, 5u);
    EXPECT_EQ(s.discarded, 5u);
    EXPECT_EQ(s.remaining, 0u);
}

TEST(UploadBudget, StaleItemsDoNotSatisfyTheProgressGuarantee) {
    Fixture f;
    f.publishRange(0, 5);
    // Only item 4 is current: it must be reached even with a zero budget.
    const DrainStats s = f.drain(0ns, [](const int& v) { return v == 4; });
    EXPECT_EQ(f.uploaded, (std::vector<int>{4}));
    EXPECT_EQ(s.discarded, 4u);
}

TEST(UploadBudget, ThrowingUploaderConsumesItemAndPropagates) {
    PingPongStaging<int> staging;
    staging.publish(1);
    staging.publish(2);
    FakeClock fake;
    const MonotonicClock clock = [&] { return fake.now(); };
    EXPECT_THROW((void)drainWithBudget(staging, 4ms, clock, [](const int&) { return true; },
                                       [](int&) { throw std::runtime_error("GL out of memory"); }),
                 std::runtime_error);
    std::vector<int> rest;
    (void)drainWithBudget(staging, 4ms, clock, [](const int&) { return true; },
                          [&](int& v) { rest.push_back(v); });
    EXPECT_EQ(rest, (std::vector<int>{2}));
}

TEST(UploadBudget, PropertyEveryItemUploadedExactlyOnceAcrossFrames) {
    std::mt19937 rng(7u);
    for (int trial = 0; trial < 50; ++trial) {
        Fixture f;
        std::uniform_int_distribution<int> cost(0, 3000);
        std::uniform_int_distribution<int> budget(0, 8000);
        std::uniform_int_distribution<int> burst(0, 20);
        int next = 0;
        for (int frame = 0; frame < 40; ++frame) {
            const int n = burst(rng);
            f.publishRange(next, next + n);
            next += n;
            f.costPerUpload = std::chrono::microseconds(cost(rng));
            const size_t before = f.uploaded.size();
            const DrainStats s = f.drain(std::chrono::microseconds(budget(rng)));
            ASSERT_EQ(f.uploaded.size() - before, s.uploaded);
            if (s.uploaded + s.remaining > 0) {
                ASSERT_GE(s.uploaded, 1u) << "a frame with pending work must make progress";
            }
        }
        while (f.drain(1s).uploaded > 0) {
        }
        ASSERT_EQ(static_cast<int>(f.uploaded.size()), next);
        for (int i = 0; i < next; ++i) ASSERT_EQ(f.uploaded[static_cast<size_t>(i)], i);
    }
}
