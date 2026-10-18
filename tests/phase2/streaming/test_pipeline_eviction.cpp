// Bounded-queue behaviour when eviction / rejection interacts with jobs already in
// flight. An evicted section must always be reported to the streamer unless a CURRENT
// submission of it is still going to produce a mesh.

#include "streaming/AsyncChunkPipeline.hpp"

#include "PipelineTestDoubles.hpp"
#include "StreamingTestHelpers.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <future>
#include <map>
#include <random>
#include <set>
#include <tuple>
#include <vector>

using namespace std::chrono_literals;
using betacraft::streaming::AsyncChunkPipeline;
using betacraft::streaming::PipelineConfig;
using betacraft::streaming::SectionCoord;
using betacraft::streaming::SubmitResult;
using betacraft::streaming::test::drainAll;
using betacraft::streaming::test::GateMesher;
using betacraft::streaming::test::inlineConfig;
using betacraft::streaming::test::kGenerousTimeout;
using betacraft::streaming::test::stoneCube;
using betacraft::streaming::test::threadedConfig;

namespace {

constexpr SectionCoord kFar{4, 0, 0};
constexpr SectionCoord kNear{0, 0, 0};

} // namespace

// Reviewer scenario: S v1 in flight, S v2 pending, S v2 evicted by a closer job. The v1
// result is stale (v2 superseded it), so nothing will ever refresh S: it must be reported.
TEST(PipelineEviction, EvictionWhileAnOlderJobIsInFlightIsStillReported) {
    GateMesher gate;
    gate.ignoreStop = true; // v1 keeps running after it is stopped as stale
    PipelineConfig cfg = threadedConfig(1, gate.fn());
    cfg.maxQueuedJobs = 1;
    AsyncChunkPipeline p(cfg);
    p.setViewerPosition(glm::vec3(8.0f));

    ASSERT_EQ(p.requestSectionMesh(kFar, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(gate.started.wait_for(kGenerousTimeout), std::future_status::ready); // v1 in flight
    ASSERT_EQ(p.requestSectionMesh(kFar, stoneCube(), 2), SubmitResult::Accepted);  // v2 pending
    ASSERT_EQ(p.requestSectionMesh(kNear, stoneCube(), 1), SubmitResult::AcceptedEvictedOther);

    EXPECT_FALSE(p.isScheduled(kFar)) << "only a stale job of S remains: S is not going to be refreshed";
    EXPECT_EQ(p.takeEvictedSections(), (std::vector<SectionCoord>{kFar}));

    gate.release();
    ASSERT_TRUE(p.waitIdle(kGenerousTimeout));
    for (const auto& r : drainAll(p)) {
        EXPECT_NE(r.coord, kFar) << "the stale v1 mesh must not be uploaded";
    }
    EXPECT_TRUE(p.takeEvictedSections().empty()) << "reported exactly once";
}

// Control: when the in-flight job IS the current submission, the section will be
// refreshed by it, so there is nothing to report.
TEST(PipelineEviction, CurrentInFlightJobSuppressesTheReport) {
    GateMesher gate;
    PipelineConfig cfg = threadedConfig(1, gate.fn());
    cfg.maxQueuedJobs = 1;
    AsyncChunkPipeline p(cfg);
    p.setViewerPosition(glm::vec3(8.0f));

    ASSERT_EQ(p.requestSectionMesh(kFar, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(gate.started.wait_for(kGenerousTimeout), std::future_status::ready); // S v1 in flight
    ASSERT_EQ(p.requestSectionMesh({9, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh(kNear, stoneCube(), 1), SubmitResult::AcceptedEvictedOther);
    // (9,0,0) was evicted and has nothing in flight; S v1 is current and untouched.
    EXPECT_EQ(p.takeEvictedSections(), (std::vector<SectionCoord>{{9, 0, 0}}));
    EXPECT_TRUE(p.isScheduled(kFar));
    gate.release();
    ASSERT_TRUE(p.waitIdle(kGenerousTimeout));
}

// A request turned away by a full queue still carries the caller's newest version: an
// older mesh, staged or in flight, must not be uploaded afterwards.
TEST(PipelineEviction, RejectedNewerVersionMakesTheStagedMeshStale) {
    PipelineConfig cfg = inlineConfig();
    cfg.maxQueuedJobs = 2;
    AsyncChunkPipeline p(cfg);
    p.setViewerPosition(glm::vec3(8.0f));

    ASSERT_EQ(p.requestSectionMesh(kFar, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.runPendingInline(1), 1u); // S v1 staged
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh({1, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh(kFar, stoneCube(), 2), SubmitResult::RejectedQueueFull);

    EXPECT_TRUE(p.isCurrent(kFar, 2));
    EXPECT_FALSE(p.isCurrent(kFar, 1));
    const auto s = p.drainUploads(1h, [&](betacraft::streaming::ChunkMeshResult& r) {
        EXPECT_NE(r.coord, kFar) << "v1 is older than the rejected v2";
    });
    EXPECT_EQ(s.discarded, 1u);
    EXPECT_EQ(p.stats().rejectedFull, 1u);
}

TEST(PipelineEviction, RejectedEqualVersionKeepsTheStagedMeshDisplayable) {
    PipelineConfig cfg = inlineConfig();
    cfg.maxQueuedJobs = 2;
    AsyncChunkPipeline p(cfg);
    p.setViewerPosition(glm::vec3(8.0f));

    ASSERT_EQ(p.requestSectionMesh(kFar, stoneCube(), 3), SubmitResult::Accepted);
    ASSERT_EQ(p.runPendingInline(1), 1u);
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh({1, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh(kFar, stoneCube(), 3), SubmitResult::RejectedQueueFull);

    int farUploads = 0;
    (void)p.drainUploads(1h, [&](betacraft::streaming::ChunkMeshResult& r) { farUploads += r.coord == kFar; });
    EXPECT_EQ(farUploads, 1);
}

// A rejected submission for a never-seen section must leave no bookkeeping behind.
TEST(PipelineEviction, RejectedUnknownSectionLeavesNoState) {
    PipelineConfig cfg = inlineConfig();
    cfg.maxQueuedJobs = 1;
    AsyncChunkPipeline p(cfg);
    p.setViewerPosition(glm::vec3(8.0f));
    ASSERT_EQ(p.requestSectionMesh(kNear, stoneCube(), 1), SubmitResult::Accepted);
    for (int x = 10; x < 60; ++x) {
        ASSERT_EQ(p.requestSectionMesh({x, 0, 0}, stoneCube(), 7), SubmitResult::RejectedQueueFull);
    }
    EXPECT_FALSE(p.isCurrent({10, 0, 0}, 7)) << "never accepted: unknown to the pipeline";
    EXPECT_EQ(p.trackedSections(), 1u);
    // A rejected section can still be accepted later from scratch, with any version.
    ASSERT_EQ(p.runPendingInline(1), 1u);
    EXPECT_EQ(p.requestSectionMesh({10, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
}

TEST(PipelineEviction, NoteVersionOnUnknownSectionLeavesNoState) {
    AsyncChunkPipeline p(inlineConfig());
    for (int x = 0; x < 20; ++x) p.noteSectionVersion({x, 0, 0}, 5);
    EXPECT_EQ(p.trackedSections(), 0u);
}

TEST(PipelineEviction, RepeatedEvictionsOfOneSectionAreReportedOnce) {
    PipelineConfig cfg = inlineConfig();
    cfg.maxQueuedJobs = 1;
    AsyncChunkPipeline p(cfg);
    p.setViewerPosition(glm::vec3(8.0f));
    for (int round = 0; round < 5; ++round) {
        ASSERT_EQ(p.requestSectionMesh(kFar, stoneCube(), 1), SubmitResult::Accepted);
        ASSERT_EQ(p.requestSectionMesh(kNear, stoneCube(), 1), SubmitResult::AcceptedEvictedOther);
        ASSERT_EQ(p.runPendingInline(1), 1u);
    }
    EXPECT_EQ(p.pendingEvictionReports(), 1u) << "the report list is bounded by distinct sections";
    EXPECT_EQ(p.takeEvictedSections(), (std::vector<SectionCoord>{kFar}));
    EXPECT_EQ(p.pendingEvictionReports(), 0u);
}

// Concurrency + tiny queue: evictions and rejections happen constantly while 4 workers
// mesh. The streamer model re-requests whatever was turned away or reported evicted.
// Invariants: no upload is older than the section's newest version at upload time, and
// once everything settles every live section shows exactly its newest version.
TEST(PipelineEviction, RandomizedStressWithTinyQueueConverges) {
    using betacraft::streaming::ChunkMeshResult;
    using betacraft::streaming::TaskPriority;
    using betacraft::streaming::test::emptySnap;
    using Key = std::tuple<int, int, int>;

    PipelineConfig cfg = threadedConfig(4);
    cfg.maxQueuedJobs = 6;
    AsyncChunkPipeline p(cfg);
    std::mt19937 rng(424242u);
    std::uniform_int_distribution<int> coordDist(-3, 3);
    std::uniform_int_distribution<int> yDist(0, 7);
    std::uniform_int_distribution<int> action(0, 99);
    const auto snapA = stoneCube();
    const auto snapB = emptySnap();

    std::map<Key, uint64_t> worldVersion; // truth, as the World would hold it
    std::map<Key, uint64_t> lastUploaded;
    std::set<Key> needsRequest; // turned away or evicted: the streamer owes a request
    bool violation = false;
    const auto keyOf = [](SectionCoord c) { return std::make_tuple(c.x, c.y, c.z); };
    const auto coordOf = [](const Key& k) { return SectionCoord{std::get<0>(k), std::get<1>(k), std::get<2>(k)}; };
    const auto uploader = [&](ChunkMeshResult& r) {
        const auto it = worldVersion.find(keyOf(r.coord));
        if (it == worldVersion.end() || it->second != r.version) violation = true;
        lastUploaded[keyOf(r.coord)] = r.version;
    };
    const auto submit = [&](SectionCoord c, uint64_t v, TaskPriority prio) {
        const SubmitResult res = p.requestSectionMesh(c, (v & 1u) ? snapA : snapB, v, prio);
        if (res == SubmitResult::RejectedQueueFull) {
            needsRequest.insert(keyOf(c));
        } else {
            ASSERT_TRUE(res == SubmitResult::Accepted || res == SubmitResult::Replaced ||
                        res == SubmitResult::AcceptedEvictedOther)
                << static_cast<int>(res);
            needsRequest.erase(keyOf(c));
        }
    };
    const auto collectEvicted = [&] {
        for (const SectionCoord& c : p.takeEvictedSections()) {
            ASSERT_TRUE(worldVersion.contains(keyOf(c))) << "cancelled sections are never reported";
            needsRequest.insert(keyOf(c));
        }
    };

    for (int step = 0; step < 3000; ++step) {
        // Alternate phases: while paused the 6-slot queue saturates, so eviction and
        // rejection happen whatever the machine speed (under TSAN the workers would
        // otherwise drain it faster than this thread fills it); while running, workers race
        // the main thread on exactly those evicted/rejected sections.
        if (step % 250 == 0) {
            if ((step / 250) % 2 == 0) {
                p.pause();
            } else {
                p.resume();
            }
        }
        const SectionCoord c{coordDist(rng), yDist(rng), coordDist(rng)};
        const Key key = keyOf(c);
        const int a = action(rng);
        if (a < 65) {
            const uint64_t v = ++worldVersion[key]; // block edit + remesh request
            submit(c, v, static_cast<TaskPriority>(a % 4));
        } else if (a < 72) {
            if (worldVersion.contains(key)) {
                p.noteSectionVersion(c, ++worldVersion[key]); // edited, request comes later
                needsRequest.insert(key);
            }
        } else if (a < 77) {
            p.cancelSection(c); // unloaded
            worldVersion.erase(key);
            lastUploaded.erase(key);
            needsRequest.erase(key);
        } else if (a < 90) {
            (void)p.drainUploads(200us, uploader);
        } else if (a < 95) {
            collectEvicted();
        } else {
            p.setViewerPosition(glm::vec3(coordDist(rng) * 16, 64, coordDist(rng) * 16));
        }
    }

    p.resume();
    // Settle like a streamer would: never more requests per round than the queue holds,
    // so the settle phase itself cannot evict.
    bool settled = false;
    for (int round = 0; round < 10000 && !settled; ++round) {
        ASSERT_TRUE(p.waitIdle(kGenerousTimeout));
        while (p.drainUploads(1h, uploader).uploaded > 0) {
        }
        collectEvicted();
        if (needsRequest.empty()) {
            settled = true;
            break;
        }
        std::vector<Key> batch(needsRequest.begin(), needsRequest.end());
        batch.resize(std::min(batch.size(), p.maxQueuedJobs()));
        for (const Key& k : batch) submit(coordOf(k), worldVersion.at(k), TaskPriority::HighNear);
    }
    ASSERT_TRUE(settled);
    EXPECT_FALSE(violation) << "a stale mesh reached the uploader";
    for (const auto& [key, v] : worldVersion) {
        const auto it = lastUploaded.find(key);
        ASSERT_NE(it, lastUploaded.end()) << "section never uploaded";
        EXPECT_EQ(it->second, v) << "section stuck on an old mesh";
    }
    const auto st = p.stats();
    EXPECT_GT(st.evicted, 0u) << "the scenario must exercise eviction";
    EXPECT_GT(st.rejectedFull, 0u) << "the scenario must exercise rejection";
    EXPECT_EQ(st.failed, 0u);
}
