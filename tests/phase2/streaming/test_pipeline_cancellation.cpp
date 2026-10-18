// Cooperative cancellation of RUNNING jobs. Discarding a stale result is not enough: a
// job made stale by an unload, an edit or a newer submission must have its stop_token
// signalled so the worker stops burning CPU on a mesh nobody will upload.
//
// The GateMesher blocks until released OR until its stop_token fires, so a test that
// never releases the gate can only finish if the pipeline actually requested the stop.

#include "streaming/AsyncChunkPipeline.hpp"

#include "PipelineTestDoubles.hpp"
#include "StreamingTestHelpers.hpp"

#include <gtest/gtest.h>

#include <future>
#include <memory>
#include <vector>

using namespace std::chrono_literals;
using betacraft::streaming::AsyncChunkPipeline;
using betacraft::streaming::PipelineConfig;
using betacraft::streaming::SectionCoord;
using betacraft::streaming::SubmitResult;
using betacraft::streaming::test::drainAll;
using betacraft::streaming::test::GateMesher;
using betacraft::streaming::test::kGenerousTimeout;
using betacraft::streaming::test::stoneCube;
using betacraft::streaming::test::threadedConfig;

namespace {

constexpr SectionCoord kS{0, 0, 0};

// One worker, first job of `kS` held in flight by a gate that is never released.
struct HeldJob {
    GateMesher gate;
    AsyncChunkPipeline pipeline;

    explicit HeldJob(size_t maxQueued = 4096) : pipeline(config(gate, maxQueued)) {
        EXPECT_EQ(pipeline.requestSectionMesh(kS, stoneCube(), 1), SubmitResult::Accepted);
        EXPECT_EQ(gate.started.wait_for(kGenerousTimeout), std::future_status::ready);
    }

    static PipelineConfig config(GateMesher& g, size_t maxQueued) {
        PipelineConfig cfg = threadedConfig(1, g.fn());
        cfg.maxQueuedJobs = maxQueued;
        return cfg;
    }
};

} // namespace

TEST(PipelineCancellation, CancelSectionStopsTheRunningMesher) {
    HeldJob h;
    h.pipeline.cancelSection(kS);
    ASSERT_TRUE(h.pipeline.waitIdle(kGenerousTimeout)) << "running job was never told to stop";
    EXPECT_EQ(h.gate.sawStop.load(), 1);
    EXPECT_EQ(h.pipeline.stats().abortedStale, 1u);
    EXPECT_EQ(h.pipeline.stats().aborted, 0u) << "not a shutdown abort";
    EXPECT_TRUE(drainAll(h.pipeline).empty());
}

TEST(PipelineCancellation, CancelChunkStopsRunningJobsOfThatColumn) {
    HeldJob h;
    h.pipeline.cancelChunk(kS.chunk());
    ASSERT_TRUE(h.pipeline.waitIdle(kGenerousTimeout));
    EXPECT_EQ(h.gate.sawStop.load(), 1);
}

TEST(PipelineCancellation, EditNoticeStopsTheRunningMesher) {
    HeldJob h;
    h.pipeline.noteSectionVersion(kS, 2);
    ASSERT_TRUE(h.pipeline.waitIdle(kGenerousTimeout));
    EXPECT_EQ(h.gate.sawStop.load(), 1);
    EXPECT_EQ(h.pipeline.stats().abortedStale, 1u);
}

TEST(PipelineCancellation, NewerSubmissionStopsTheSupersededRunningJob) {
    HeldJob h;
    ASSERT_EQ(h.pipeline.requestSectionMesh(kS, stoneCube(), 2), SubmitResult::Accepted);
    // The stop is requested synchronously inside requestSectionMesh, i.e. before the gate
    // opens; GateMesher checks its token first, so a stopped v1 never completes. Without
    // the stop, releasing the gate would let v1 finish (and only be discarded).
    h.gate.release();
    ASSERT_TRUE(h.pipeline.waitIdle(kGenerousTimeout));
    const auto r = drainAll(h.pipeline);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].version, 2u);
    EXPECT_EQ(h.gate.sawStop.load(), 1) << "v1 saw its stop_token, it did not run to completion";
    EXPECT_EQ(h.pipeline.stats().abortedStale, 1u);
    EXPECT_EQ(h.pipeline.stats().discardedStale, 0u);
}

TEST(PipelineCancellation, RejectedNewerVersionStopsTheRunningMesher) {
    HeldJob h(/*maxQueued=*/1);
    h.pipeline.setViewerPosition(glm::vec3(8.0f));
    ASSERT_EQ(h.pipeline.requestSectionMesh({1, 0, 0}, stoneCube(), 1), SubmitResult::Accepted); // fills the queue
    ASSERT_EQ(h.pipeline.requestSectionMesh(kS, stoneCube(), 2, betacraft::streaming::TaskPriority::LowBackground),
              SubmitResult::RejectedQueueFull);
    // v1 is stale (the caller says v2 exists): its job is stopped even though v2 was refused.
    h.gate.release();
    ASSERT_TRUE(h.pipeline.waitIdle(kGenerousTimeout));
    EXPECT_EQ(h.gate.sawStop.load(), 1);
    for (const auto& r : drainAll(h.pipeline)) EXPECT_NE(r.coord, kS);
}

TEST(PipelineCancellation, UnrelatedActivityDoesNotStopACurrentJob) {
    HeldJob h;
    h.pipeline.cancelSection({5, 0, 5});
    h.pipeline.noteSectionVersion({5, 0, 5}, 9);
    h.pipeline.noteSectionVersion(kS, 1); // same version: nothing became stale
    ASSERT_EQ(h.pipeline.requestSectionMesh({1, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    EXPECT_FALSE(h.pipeline.waitIdle(20ms)) << "the current job keeps running";
    EXPECT_EQ(h.gate.sawStop.load(), 0);
    h.gate.release();
    ASSERT_TRUE(h.pipeline.waitIdle(kGenerousTimeout));
    EXPECT_EQ(drainAll(h.pipeline).size(), 2u);
    EXPECT_EQ(h.pipeline.stats().abortedStale, 0u);
}

TEST(PipelineCancellation, ShutdownStillAbortsACurrentJob) {
    auto h = std::make_unique<HeldJob>();
    h->pipeline.shutdown();
    EXPECT_EQ(h->gate.sawStop.load(), 1);
    EXPECT_EQ(h->pipeline.stats().aborted, 1u);
    EXPECT_EQ(h->pipeline.stats().abortedStale, 0u);
}
