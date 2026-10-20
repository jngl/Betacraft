#include "streaming/AsyncChunkPipeline.hpp"

#include "PipelineTestDoubles.hpp"
#include "StreamingTestHelpers.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <latch>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;
using betacraft::streaming::AsyncChunkPipeline;
using betacraft::streaming::ChunkMeshResult;
using betacraft::streaming::MeshFunction;
using betacraft::streaming::MeshOptions;
using betacraft::streaming::PipelineConfig;
using betacraft::streaming::RawMeshBuffer;
using betacraft::streaming::SectionNeighborhoodSnapshot18;
using betacraft::streaming::SubmitResult;
using betacraft::streaming::TaskPriority;
using betacraft::streaming::test::FakeClock;
using betacraft::streaming::test::kGenerousTimeout;
using betacraft::streaming::VoxelSample;
using betacraft::voxel::SectionCoord;
using betacraft::voxel::BlockId;
using betacraft::voxel::BlockStateId;
using Snap = SectionNeighborhoodSnapshot18;
using SnapPtr = std::shared_ptr<const Snap>;

using betacraft::streaming::test::drainAll;
using betacraft::streaming::test::emptySnap;
using betacraft::streaming::test::GateMesher;
using betacraft::streaming::test::inlineConfig;
using betacraft::streaming::test::RecordingMesher;
using betacraft::streaming::test::stoneCube;
using betacraft::streaming::test::threadedConfig;

// ------------------------------------------------------------- basics ----

TEST(AsyncChunkPipeline, DefaultWorkerCountLeavesOneCoreForTheMainThread) {
    const size_t hw = std::max(1u, std::thread::hardware_concurrency());
    EXPECT_EQ(AsyncChunkPipeline::defaultWorkerCount(), hw > 1 ? hw - 1 : 1u);
}

TEST(AsyncChunkPipeline, InlineModeRunsNothingUntilAsked) {
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    EXPECT_EQ(p.workerCount(), 0u);
    EXPECT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    EXPECT_EQ(p.pendingJobs(), 1u);
    EXPECT_EQ(rec.calls.load(), 0);
    EXPECT_EQ(p.runPendingInline(10), 1u);
    EXPECT_EQ(p.pendingJobs(), 0u);
    EXPECT_EQ(p.stagedResults(), 1u);
}

TEST(AsyncChunkPipeline, EndToEndWithRealMesherProducesGeometry) {
    AsyncChunkPipeline p(inlineConfig());
    ASSERT_EQ(p.requestSectionMesh({2, 5, -3}, stoneCube(), 7), SubmitResult::Accepted);
    (void)p.runPendingInline(1);
    const auto results = drainAll(p);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_EQ(results[0].coord, (SectionCoord{2, 5, -3}));
    EXPECT_EQ(results[0].version, 7u);
    EXPECT_EQ(results[0].buffers.opaqueQuadCount, 6u);
    EXPECT_EQ(results[0].buffers.opaqueVertices[0].getReserved(), 5u);
}

TEST(AsyncChunkPipeline, EmptySectionsStillPublishAnEmptyResult) {
    // An emptied section must reach the renderer so it can drop the stale GPU mesh.
    AsyncChunkPipeline p(inlineConfig());
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, emptySnap(), 1), SubmitResult::Accepted);
    (void)p.runPendingInline(1);
    const auto results = drainAll(p);
    ASSERT_EQ(results.size(), 1u);
    EXPECT_TRUE(results[0].buffers.empty());
}

TEST(AsyncChunkPipeline, NullSnapshotIsRejected) {
    AsyncChunkPipeline p(inlineConfig());
    EXPECT_EQ(p.requestSectionMesh({0, 0, 0}, nullptr, 1), SubmitResult::RejectedInvalid);
    EXPECT_EQ(p.pendingJobs(), 0u);
}

TEST(AsyncChunkPipeline, InvalidSectionYIsRejected) {
    AsyncChunkPipeline p(inlineConfig());
    EXPECT_EQ(p.requestSectionMesh({0, -1, 0}, stoneCube(), 1), SubmitResult::RejectedInvalid);
    EXPECT_EQ(p.requestSectionMesh({0, 8, 0}, stoneCube(), 1), SubmitResult::RejectedInvalid);
}

// ----------------------------------------------------------- priority ----

TEST(AsyncChunkPipeline, NearestSectionsAreMeshedFirst) {
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    p.setViewerPosition(glm::vec3(8.0f, 8.0f, 8.0f)); // centre of section (0,0,0)
    for (int x : {5, -3, 1, 0, 2, -1}) {
        ASSERT_EQ(p.requestSectionMesh({x, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    }
    (void)p.runPendingInline(100);
    const auto order = rec.snapshotOrder();
    std::vector<int> xs;
    for (const auto& c : order) xs.push_back(c.x);
    EXPECT_EQ(xs, (std::vector<int>{0, 1, -1, 2, -3, 5}));
}

TEST(AsyncChunkPipeline, PriorityClassDominatesDistance) {
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    p.setViewerPosition(glm::vec3(8.0f));
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1, TaskPriority::LowBackground), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh({9, 0, 0}, stoneCube(), 1, TaskPriority::ImmediateCamera), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh({1, 0, 0}, stoneCube(), 1, TaskPriority::MediumFar), SubmitResult::Accepted);
    (void)p.runPendingInline(10);
    const auto order = rec.snapshotOrder();
    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0].x, 9);
    EXPECT_EQ(order[1].x, 1);
    EXPECT_EQ(order[2].x, 0);
}

TEST(AsyncChunkPipeline, EqualKeysAreFifo) {
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    p.setViewerPosition(glm::vec3(8.0f, 8.0f, 8.0f));
    // Four sections at identical distance from the viewer.
    const std::vector<SectionCoord> ring{{1, 0, 0}, {0, 0, 1}, {-1, 0, 0}, {0, 0, -1}};
    for (const auto& c : ring) ASSERT_EQ(p.requestSectionMesh(c, stoneCube(), 1), SubmitResult::Accepted);
    (void)p.runPendingInline(10);
    EXPECT_EQ(rec.snapshotOrder(), ring);
}

TEST(AsyncChunkPipeline, MovingTheViewerReprioritisesPendingJobs) {
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    p.setViewerPosition(glm::vec3(8.0f));
    for (int x = 0; x < 5; ++x) ASSERT_EQ(p.requestSectionMesh({x, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    p.setViewerPosition(glm::vec3(4 * 16 + 8, 8, 8)); // now next to section x = 4
    (void)p.runPendingInline(10);
    std::vector<int> xs;
    for (const auto& c : rec.snapshotOrder()) xs.push_back(c.x);
    EXPECT_EQ(xs, (std::vector<int>{4, 3, 2, 1, 0}));
}

TEST(AsyncChunkPipeline, ThreadedSingleWorkerHonoursPriorityAfterResume) {
    RecordingMesher rec;
    AsyncChunkPipeline p(threadedConfig(1, rec.fn()));
    p.pause();
    p.setViewerPosition(glm::vec3(8.0f));
    for (int x : {3, 0, 2, 1}) ASSERT_EQ(p.requestSectionMesh({x, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    p.resume();
    ASSERT_TRUE(p.waitIdle(kGenerousTimeout));
    std::vector<int> xs;
    for (const auto& c : rec.snapshotOrder()) xs.push_back(c.x);
    EXPECT_EQ(xs, (std::vector<int>{0, 1, 2, 3}));
    std::vector<int> uploaded;
    for (const auto& r : drainAll(p)) uploaded.push_back(r.coord.x);
    EXPECT_EQ(uploaded, xs) << "staging preserves completion order";
}

// ------------------------------------------------- de-duplication ----

TEST(AsyncChunkPipeline, ResubmittingAPendingSectionReplacesIt) {
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    EXPECT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    EXPECT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 2), SubmitResult::Replaced);
    EXPECT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 3), SubmitResult::Replaced);
    EXPECT_EQ(p.pendingJobs(), 1u);
    (void)p.runPendingInline(10);
    EXPECT_EQ(rec.calls.load(), 1);
    const auto r = drainAll(p);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].version, 3u);
    EXPECT_EQ(p.stats().replaced, 2u);
}

TEST(AsyncChunkPipeline, OlderVersionThanKnownIsRejectedAsStale) {
    AsyncChunkPipeline p(inlineConfig());
    EXPECT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 5), SubmitResult::Accepted);
    EXPECT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 4), SubmitResult::RejectedStale);
    EXPECT_EQ(p.pendingJobs(), 1u);
    EXPECT_EQ(p.stats().rejectedStale, 1u);
}

TEST(AsyncChunkPipeline, ReplacementCanRaisePriority) {
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    p.setViewerPosition(glm::vec3(8.0f));
    ASSERT_EQ(p.requestSectionMesh({1, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh({6, 0, 0}, stoneCube(), 1, TaskPriority::LowBackground), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh({6, 0, 0}, stoneCube(), 2, TaskPriority::ImmediateCamera), SubmitResult::Replaced);
    (void)p.runPendingInline(10);
    ASSERT_EQ(rec.snapshotOrder().size(), 2u);
    EXPECT_EQ(rec.snapshotOrder()[0].x, 6);
}

// --------------------------------------------------- bounded queue ----

TEST(AsyncChunkPipeline, FullQueueRejectsLowerPriorityWork) {
    PipelineConfig cfg = inlineConfig();
    cfg.maxQueuedJobs = 3;
    AsyncChunkPipeline p(cfg);
    p.setViewerPosition(glm::vec3(8.0f));
    for (int x = 0; x < 3; ++x) ASSERT_EQ(p.requestSectionMesh({x, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    EXPECT_EQ(p.requestSectionMesh({10, 0, 0}, stoneCube(), 1), SubmitResult::RejectedQueueFull);
    EXPECT_EQ(p.pendingJobs(), 3u);
    EXPECT_FALSE(p.isScheduled({10, 0, 0}));
}

TEST(AsyncChunkPipeline, FullQueueEvictsFarthestForCloserWork) {
    RecordingMesher rec;
    PipelineConfig cfg = inlineConfig(rec.fn());
    cfg.maxQueuedJobs = 3;
    AsyncChunkPipeline p(cfg);
    p.setViewerPosition(glm::vec3(8.0f));
    for (int x = 1; x <= 3; ++x) ASSERT_EQ(p.requestSectionMesh({x, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    EXPECT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::AcceptedEvictedOther);
    EXPECT_EQ(p.pendingJobs(), 3u);
    EXPECT_FALSE(p.isScheduled({3, 0, 0}));
    EXPECT_TRUE(p.isScheduled({0, 0, 0}));
    EXPECT_EQ(p.stats().evicted, 1u);
    (void)p.runPendingInline(10);
    std::vector<int> xs;
    for (const auto& c : rec.snapshotOrder()) xs.push_back(c.x);
    EXPECT_EQ(xs, (std::vector<int>{0, 1, 2}));
}

TEST(AsyncChunkPipeline, ReplacingInAFullQueueNeedsNoEviction) {
    PipelineConfig cfg = inlineConfig();
    cfg.maxQueuedJobs = 2;
    AsyncChunkPipeline p(cfg);
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh({1, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    EXPECT_EQ(p.requestSectionMesh({1, 0, 0}, stoneCube(), 2), SubmitResult::Replaced);
    EXPECT_EQ(p.stats().evicted, 0u);
}

TEST(AsyncChunkPipeline, CapacityZeroIsClampedToOne) {
    PipelineConfig cfg = inlineConfig();
    cfg.maxQueuedJobs = 0;
    AsyncChunkPipeline p(cfg);
    EXPECT_EQ(p.maxQueuedJobs(), 1u);
    EXPECT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
}

// ------------------------------------------- versions / staleness ----

TEST(AsyncChunkPipeline, EditBeforeRunSkipsMeshingEntirely) {
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    p.noteSectionVersion({0, 0, 0}, 2); // block edited, remesh not requested yet
    (void)p.runPendingInline(10);
    EXPECT_EQ(rec.calls.load(), 0) << "stale jobs must not waste worker CPU";
    EXPECT_TRUE(drainAll(p).empty());
    EXPECT_EQ(p.stats().skippedStale, 1u);
}

TEST(AsyncChunkPipeline, EditAfterMeshingDiscardsResultAtDrain) {
    AsyncChunkPipeline p(inlineConfig());
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    (void)p.runPendingInline(10);
    ASSERT_EQ(p.stagedResults(), 1u);
    p.noteSectionVersion({0, 0, 0}, 2);
    int uploads = 0;
    const auto s = p.drainUploads(4ms, [&](ChunkMeshResult&) { ++uploads; });
    EXPECT_EQ(uploads, 0);
    EXPECT_EQ(s.discarded, 1u);
}

TEST(AsyncChunkPipeline, NoteVersionNeverLowersTheKnownVersion) {
    AsyncChunkPipeline p(inlineConfig());
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 5), SubmitResult::Accepted);
    p.noteSectionVersion({0, 0, 0}, 3);
    EXPECT_TRUE(p.isCurrent({0, 0, 0}, 5));
    EXPECT_FALSE(p.isCurrent({0, 0, 0}, 3));
}

TEST(AsyncChunkPipeline, UnknownSectionsAreNeverCurrent) {
    AsyncChunkPipeline p(inlineConfig());
    EXPECT_FALSE(p.isCurrent({9, 0, 9}, 0));
}

TEST(AsyncChunkPipeline, InFlightJobSupersededByNewerVersionIsDiscarded) {
    GateMesher gate;
    AsyncChunkPipeline p(threadedConfig(1, gate.fn()));
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(gate.started.wait_for(kGenerousTimeout), std::future_status::ready);
    EXPECT_TRUE(p.isScheduled({0, 0, 0}));
    EXPECT_EQ(p.inFlightJobs(), 1u);
    // Re-edit while v1 is being meshed: v2 is queued, v1's result must never be uploaded.
    EXPECT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 2), SubmitResult::Accepted);
    gate.release();
    ASSERT_TRUE(p.waitIdle(kGenerousTimeout));
    const auto r = drainAll(p);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].version, 2u);
    EXPECT_EQ(gate.calls.load(), 2);
    // v1 was stopped cooperatively (or, had it already finished, discarded): never uploaded.
    EXPECT_EQ(p.stats().discardedStale + p.stats().abortedStale, 1u);
}

// ------------------------------------------------------ cancellation ----

TEST(AsyncChunkPipeline, CancelSectionRemovesPendingJob) {
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh({1, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    p.cancelSection({0, 0, 0});
    EXPECT_FALSE(p.isScheduled({0, 0, 0}));
    (void)p.runPendingInline(10);
    ASSERT_EQ(rec.snapshotOrder().size(), 1u);
    EXPECT_EQ(rec.snapshotOrder()[0].x, 1);
    EXPECT_EQ(p.stats().cancelled, 1u);
}

TEST(AsyncChunkPipeline, CancelChunkDropsAllEightSectionsIncludingStagedResults) {
    AsyncChunkPipeline p(inlineConfig());
    for (int y = 0; y < 8; ++y) ASSERT_EQ(p.requestSectionMesh({3, y, 3}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh({4, 0, 3}, stoneCube(), 1), SubmitResult::Accepted);
    (void)p.runPendingInline(4); // half are meshed and staged already
    p.cancelChunk({3, 3});        // chunk unloaded
    (void)p.runPendingInline(100);
    const auto r = drainAll(p);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].coord, (SectionCoord{4, 0, 3}));
}

TEST(AsyncChunkPipeline, CancelledSectionCanBeRequestedAgainFromScratch) {
    AsyncChunkPipeline p(inlineConfig());
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 9), SubmitResult::Accepted);
    p.cancelSection({0, 0, 0});
    // A reloaded chunk restarts its version counter: must not be considered stale.
    EXPECT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
}

TEST(AsyncChunkPipeline, CancelInFlightJobDiscardsItsResult) {
    GateMesher gate;
    AsyncChunkPipeline p(threadedConfig(1, gate.fn()));
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(gate.started.wait_for(kGenerousTimeout), std::future_status::ready);
    p.cancelSection({0, 0, 0});
    gate.release();
    ASSERT_TRUE(p.waitIdle(kGenerousTimeout));
    EXPECT_TRUE(drainAll(p).empty());
}

// ------------------------------------------------ failure handling ----

TEST(AsyncChunkPipeline, ThrowingMesherDoesNotKillTheWorker) {
    std::atomic<int> calls{0};
    MeshFunction flaky = [&](const Snap&, SectionCoord c, const MeshOptions&, std::stop_token) -> std::optional<RawMeshBuffer> {
        calls.fetch_add(1);
        if (c.x == 0) throw std::runtime_error("boom");
        return RawMeshBuffer{};
    };
    AsyncChunkPipeline p(threadedConfig(1, flaky));
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_TRUE(p.waitIdle(kGenerousTimeout));
    ASSERT_EQ(p.requestSectionMesh({1, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_TRUE(p.waitIdle(kGenerousTimeout));
    EXPECT_EQ(calls.load(), 2);
    EXPECT_EQ(p.stats().failed, 1u);
    const auto r = drainAll(p);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].coord.x, 1);
}

// --------------------------------------------------------- shutdown ----

TEST(AsyncChunkPipeline, WorkersAreAllRunningAfterConstruction) {
    AsyncChunkPipeline p(threadedConfig(3));
    EXPECT_EQ(p.workerCount(), 3u);
    EXPECT_EQ(p.liveWorkers(), 3u);
}

TEST(AsyncChunkPipeline, ShutdownJoinsEveryWorkerAndIsIdempotent) {
    AsyncChunkPipeline p(threadedConfig(4));
    p.shutdown();
    EXPECT_EQ(p.liveWorkers(), 0u);
    EXPECT_EQ(p.workerCount(), 0u);
    p.shutdown();
    EXPECT_EQ(p.liveWorkers(), 0u);
}

TEST(AsyncChunkPipeline, SubmitAfterShutdownIsRejected) {
    AsyncChunkPipeline p(threadedConfig(1));
    p.shutdown();
    EXPECT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::RejectedShutdown);
    EXPECT_EQ(p.runPendingInline(1), 0u);
}

TEST(AsyncChunkPipeline, ShutdownCooperativelyStopsABusyMesherAndDropsPendingWork) {
    GateMesher gate; // never released: only the stop_token can unblock it
    auto p = std::make_unique<AsyncChunkPipeline>(threadedConfig(1, gate.fn()));
    ASSERT_EQ(p->requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(gate.started.wait_for(kGenerousTimeout), std::future_status::ready);
    for (int x = 1; x < 20; ++x) ASSERT_EQ(p->requestSectionMesh({x, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);

    auto destroyed = std::async(std::launch::async, [&] { p.reset(); });
    ASSERT_EQ(destroyed.wait_for(kGenerousTimeout), std::future_status::ready) << "destructor hung";
    EXPECT_EQ(gate.sawStop.load(), 1);
    EXPECT_EQ(gate.calls.load(), 1) << "pending jobs must not start after stop";
}

TEST(AsyncChunkPipeline, PausedPipelineIsNotIdleWhileWorkIsQueued) {
    AsyncChunkPipeline p(threadedConfig(1));
    p.pause();
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    EXPECT_FALSE(p.waitIdle(20ms));
    p.resume();
    EXPECT_TRUE(p.waitIdle(kGenerousTimeout));
    EXPECT_EQ(p.stagedResults(), 1u);
}

// ------------------------------------------------------ upload drain ----

TEST(AsyncChunkPipeline, DrainUploadsRespectsBudgetWithInjectedClock) {
    auto fake = std::make_shared<FakeClock>();
    PipelineConfig cfg = inlineConfig();
    cfg.clock = [fake] { return fake->now(); };
    AsyncChunkPipeline p(cfg);
    for (int x = 0; x < 10; ++x) ASSERT_EQ(p.requestSectionMesh({x, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    (void)p.runPendingInline(100);

    size_t total = 0;
    std::vector<size_t> perFrame;
    for (int frame = 0; frame < 4; ++frame) {
        const auto s = p.drainUploads(4ms, [&](ChunkMeshResult&) { fake->advance(1500us); });
        perFrame.push_back(s.uploaded);
        total += s.uploaded;
    }
    // 1.5 ms per upload, 4 ms budget: uploads at t=0, 1.5, 3.0 -> 3 per frame.
    EXPECT_EQ(perFrame, (std::vector<size_t>{3, 3, 3, 1}));
    EXPECT_EQ(total, 10u);
}

TEST(AsyncChunkPipeline, UploaderCanMoveVertexDataOut) {
    AsyncChunkPipeline p(inlineConfig());
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    (void)p.runPendingInline(1);
    std::vector<betacraft::render::PackedChunkVertex> taken;
    (void)p.drainUploads(4ms, [&](ChunkMeshResult& r) { taken = std::move(r.buffers.opaqueVertices); });
    EXPECT_EQ(taken.size(), 24u);
}

// -------------------------------------------------- World integration ----

TEST(AsyncChunkPipeline, RequestFromWorldCapturesOnTheCallingThread) {
    betacraft::streaming::test::ScopedTempDir dir;
    betacraft::world::World world(99ULL, dir.path());
    auto& chunk = world.ensureChunkLoaded({0, 0});
    for (int y = 16; y < 32; ++y)
        for (int z = 0; z < 16; ++z)
            for (int x = 0; x < 16; ++x) chunk.setBlock(x, y, z, BlockId::Air, false);
    chunk.setBlock(5, 20, 5, BlockId::Stone, false);

    AsyncChunkPipeline p(threadedConfig(2));
    ASSERT_EQ(p.requestSectionMeshFromWorld(world, {0, 1, 0}, 1), SubmitResult::Accepted);
    // Mutating the world right after submission is safe: the worker only sees the snapshot.
    chunk.setBlock(6, 20, 5, BlockId::Stone, false);
    ASSERT_TRUE(p.waitIdle(kGenerousTimeout));
    const auto r = drainAll(p);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].buffers.opaqueQuadCount, 6u);
}

// ---------------------------------------------------- randomized stress ----

// Many workers, the main thread interleaving submissions, edits, cancels and drains.
// Invariants: every upload is current at upload time, and after quiescence each live
// section's last uploaded version equals its final version. Primary TSAN target.
TEST(AsyncChunkPipeline, RandomizedStressKeepsVersionInvariants) {
    PipelineConfig cfg = threadedConfig(4);
    cfg.maxQueuedJobs = 1u << 16;
    AsyncChunkPipeline p(cfg);
    std::mt19937 rng(20261017u);
    std::uniform_int_distribution<int> coordDist(-3, 3);
    std::uniform_int_distribution<int> yDist(0, 7);
    std::uniform_int_distribution<int> action(0, 99);

    const auto snapA = stoneCube();
    const auto snapB = emptySnap();
    std::map<std::tuple<int, int, int>, uint64_t> latest; // expected live versions
    std::map<std::tuple<int, int, int>, uint64_t> lastUploaded;
    bool violation = false;

    const auto uploader = [&](ChunkMeshResult& r) {
        const auto key = std::make_tuple(r.coord.x, r.coord.y, r.coord.z);
        const auto it = latest.find(key);
        if (it == latest.end() || it->second != r.version) violation = true;
        lastUploaded[key] = r.version;
    };

    for (int step = 0; step < 4000; ++step) {
        const SectionCoord c{coordDist(rng), yDist(rng), coordDist(rng)};
        const auto key = std::make_tuple(c.x, c.y, c.z);
        const int a = action(rng);
        if (a < 70) {
            const uint64_t v = latest.contains(key) ? latest[key] + 1 : 1;
            const auto res = p.requestSectionMesh(c, (a & 1) ? snapA : snapB, v,
                                                  static_cast<TaskPriority>(a % 4));
            ASSERT_TRUE(res == SubmitResult::Accepted || res == SubmitResult::Replaced);
            latest[key] = v;
        } else if (a < 80) {
            if (latest.contains(key)) {
                p.noteSectionVersion(c, ++latest[key]); // edited, remesh requested later
                ASSERT_EQ(p.requestSectionMesh(c, snapA, latest[key]) == SubmitResult::RejectedStale, false);
            }
        } else if (a < 85) {
            p.cancelSection(c);
            latest.erase(key);
            lastUploaded.erase(key);
        } else if (a < 95) {
            (void)p.drainUploads(200us, uploader);
        } else {
            p.setViewerPosition(glm::vec3(coordDist(rng) * 16, 64, coordDist(rng) * 16));
        }
    }
    ASSERT_TRUE(p.waitIdle(kGenerousTimeout));
    while (p.drainUploads(1h, uploader).uploaded > 0) {
    }
    EXPECT_FALSE(violation) << "a stale mesh reached the uploader";
    for (const auto& [key, v] : latest) {
        const auto it = lastUploaded.find(key);
        ASSERT_NE(it, lastUploaded.end()) << "section never uploaded";
        EXPECT_EQ(it->second, v);
    }
    const auto st = p.stats();
    EXPECT_EQ(st.failed, 0u);
    EXPECT_EQ(p.pendingJobs(), 0u);
    EXPECT_EQ(p.inFlightJobs(), 0u);
}

// -------------------------------------------------- eviction reporting ----

TEST(AsyncChunkPipeline, EvictedSectionsAreReportedOnceForReRequest) {
    PipelineConfig cfg = inlineConfig();
    cfg.maxQueuedJobs = 2;
    AsyncChunkPipeline p(cfg);
    p.setViewerPosition(glm::vec3(8.0f));
    ASSERT_EQ(p.requestSectionMesh({2, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh({3, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    EXPECT_TRUE(p.takeEvictedSections().empty());
    ASSERT_EQ(p.requestSectionMesh({1, 0, 0}, stoneCube(), 1), SubmitResult::AcceptedEvictedOther);
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::AcceptedEvictedOther);
    EXPECT_EQ(p.takeEvictedSections(), (std::vector<SectionCoord>{{3, 0, 0}, {2, 0, 0}}));
    EXPECT_TRUE(p.takeEvictedSections().empty()) << "reported exactly once";
}

TEST(AsyncChunkPipeline, EvictedThenReRequestedSectionIsNotReported) {
    PipelineConfig cfg = inlineConfig();
    cfg.maxQueuedJobs = 1;
    AsyncChunkPipeline p(cfg);
    p.setViewerPosition(glm::vec3(8.0f));
    ASSERT_EQ(p.requestSectionMesh({5, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::AcceptedEvictedOther);
    ASSERT_EQ(p.runPendingInline(1), 1u);
    ASSERT_EQ(p.requestSectionMesh({5, 0, 0}, stoneCube(), 1), SubmitResult::Accepted); // caller retried
    p.cancelSection({0, 0, 0});
    EXPECT_TRUE(p.takeEvictedSections().empty()) << "already queued again: nothing to report";
}

TEST(AsyncChunkPipeline, CancelledEvictedSectionIsNotReported) {
    PipelineConfig cfg = inlineConfig();
    cfg.maxQueuedJobs = 1;
    AsyncChunkPipeline p(cfg);
    p.setViewerPosition(glm::vec3(8.0f));
    ASSERT_EQ(p.requestSectionMesh({5, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::AcceptedEvictedOther);
    p.cancelChunk({5, 0}); // unloaded meanwhile
    EXPECT_TRUE(p.takeEvictedSections().empty());
}
