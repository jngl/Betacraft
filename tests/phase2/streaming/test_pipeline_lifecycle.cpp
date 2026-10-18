// Strict per-chunk state machine enforced by the pipeline (roadmap P1.1):
//   Unloaded -> TerrainGen -> Lit -> Decorated -> Meshed -> Uploaded
// The first three stages are produced outside the pipeline (World generation and
// lighting) and reported through advanceChunkStage; the last two are owned by the
// pipeline and follow the column's sections: Meshed once every requested section has a
// current mesh staged, Uploaded once all of them reached the uploader. An edit (new
// request or version notice) brings the column back to Decorated.

#include "streaming/AsyncChunkPipeline.hpp"
#include "streaming/ChunkLifecycle.hpp"

#include "PipelineTestDoubles.hpp"
#include "StreamingTestHelpers.hpp"

#include <gtest/gtest.h>

#include <vector>

using namespace std::chrono_literals;
using betacraft::core::ChunkCoord;
using betacraft::streaming::AsyncChunkPipeline;
using betacraft::streaming::ChunkMeshResult;
using betacraft::streaming::ChunkStage;
using betacraft::streaming::PipelineConfig;
using betacraft::streaming::SectionCoord;
using betacraft::streaming::SubmitResult;
using betacraft::streaming::test::drainAll;
using betacraft::streaming::test::kGenerousTimeout;
using betacraft::streaming::test::stoneCube;

namespace {

constexpr ChunkCoord kChunk{2, -3};

PipelineConfig strictInline() {
    PipelineConfig cfg;
    cfg.workerCount = 0;
    return cfg; // enforceChunkLifecycle is the default
}

SectionCoord sec(int y, ChunkCoord c = kChunk) { return {c.x, y, c.z}; }

void generate(AsyncChunkPipeline& p, ChunkCoord c = kChunk) {
    ASSERT_TRUE(p.advanceChunkStage(c, ChunkStage::TerrainGen));
    ASSERT_TRUE(p.advanceChunkStage(c, ChunkStage::Lit));
    ASSERT_TRUE(p.advanceChunkStage(c, ChunkStage::Decorated));
}

} // namespace

TEST(PipelineLifecycle, EnforcementIsTheDefault) { EXPECT_TRUE(PipelineConfig{}.enforceChunkLifecycle); }

TEST(PipelineLifecycle, MeshingBeforeDecorationIsRejected) {
    AsyncChunkPipeline p(strictInline());
    EXPECT_EQ(p.requestSectionMesh(sec(0), stoneCube(), 1), SubmitResult::RejectedNotReady);
    ASSERT_TRUE(p.advanceChunkStage(kChunk, ChunkStage::TerrainGen));
    EXPECT_EQ(p.requestSectionMesh(sec(0), stoneCube(), 1), SubmitResult::RejectedNotReady);
    ASSERT_TRUE(p.advanceChunkStage(kChunk, ChunkStage::Lit));
    EXPECT_EQ(p.requestSectionMesh(sec(0), stoneCube(), 1), SubmitResult::RejectedNotReady);
    ASSERT_TRUE(p.advanceChunkStage(kChunk, ChunkStage::Decorated));
    EXPECT_EQ(p.requestSectionMesh(sec(0), stoneCube(), 1), SubmitResult::Accepted);
    EXPECT_EQ(p.stats().rejectedNotReady, 3u);
    EXPECT_EQ(p.trackedSections(), 1u) << "rejected submissions leave no bookkeeping";
}

TEST(PipelineLifecycle, ExternalStagesMustBeReportedInOrder) {
    AsyncChunkPipeline p(strictInline());
    EXPECT_FALSE(p.advanceChunkStage(kChunk, ChunkStage::Lit)) << "terrain must exist before light";
    EXPECT_FALSE(p.advanceChunkStage(kChunk, ChunkStage::Decorated));
    EXPECT_EQ(p.chunkStage(kChunk), ChunkStage::Unloaded);
    ASSERT_TRUE(p.advanceChunkStage(kChunk, ChunkStage::TerrainGen));
    EXPECT_FALSE(p.advanceChunkStage(kChunk, ChunkStage::TerrainGen)) << "no self transition";
    EXPECT_FALSE(p.advanceChunkStage(kChunk, ChunkStage::Decorated)) << "no skipping Lit";
    EXPECT_EQ(p.chunkStage(kChunk), ChunkStage::TerrainGen);
}

TEST(PipelineLifecycle, MeshedAndUploadedArePipelineOwned) {
    AsyncChunkPipeline p(strictInline());
    generate(p);
    EXPECT_FALSE(p.advanceChunkStage(kChunk, ChunkStage::Meshed));
    EXPECT_FALSE(p.advanceChunkStage(kChunk, ChunkStage::Uploaded));
    EXPECT_FALSE(p.advanceChunkStage(kChunk, ChunkStage::Decorated));
    EXPECT_EQ(p.chunkStage(kChunk), ChunkStage::Decorated);
}

TEST(PipelineLifecycle, ColumnAdvancesThroughMeshedToUploaded) {
    AsyncChunkPipeline p(strictInline());
    generate(p);
    ASSERT_EQ(p.requestSectionMesh(sec(0), stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh(sec(1), stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.runPendingInline(1), 1u);
    EXPECT_EQ(p.chunkStage(kChunk), ChunkStage::Decorated) << "one section still pending";
    ASSERT_EQ(p.runPendingInline(1), 1u);
    EXPECT_EQ(p.chunkStage(kChunk), ChunkStage::Meshed);

    const auto first = p.drainUploads(0ns, [](ChunkMeshResult&) {}); // exactly one upload
    ASSERT_EQ(first.uploaded, 1u);
    EXPECT_EQ(p.chunkStage(kChunk), ChunkStage::Meshed) << "one section not on the GPU yet";
    (void)drainAll(p);
    EXPECT_EQ(p.chunkStage(kChunk), ChunkStage::Uploaded);
}

TEST(PipelineLifecycle, EditBringsTheColumnBackToDecorated) {
    AsyncChunkPipeline p(strictInline());
    generate(p);
    ASSERT_EQ(p.requestSectionMesh(sec(3), stoneCube(), 1), SubmitResult::Accepted);
    (void)p.runPendingInline(1);
    (void)drainAll(p);
    ASSERT_EQ(p.chunkStage(kChunk), ChunkStage::Uploaded);

    p.noteSectionVersion(sec(3), 2); // block edited (World relit it synchronously)
    EXPECT_EQ(p.chunkStage(kChunk), ChunkStage::Decorated);
    ASSERT_EQ(p.requestSectionMesh(sec(3), stoneCube(), 2), SubmitResult::Accepted);
    EXPECT_EQ(p.chunkStage(kChunk), ChunkStage::Decorated);
    (void)p.runPendingInline(1);
    EXPECT_EQ(p.chunkStage(kChunk), ChunkStage::Meshed);
    (void)drainAll(p);
    EXPECT_EQ(p.chunkStage(kChunk), ChunkStage::Uploaded);
}

TEST(PipelineLifecycle, RemeshRequestOnUploadedColumnReturnsToDecorated) {
    AsyncChunkPipeline p(strictInline());
    generate(p);
    ASSERT_EQ(p.requestSectionMesh(sec(0), stoneCube(), 1), SubmitResult::Accepted);
    (void)p.runPendingInline(1);
    (void)drainAll(p);
    ASSERT_EQ(p.chunkStage(kChunk), ChunkStage::Uploaded);
    // Neighbour edit: same version, new border.
    ASSERT_EQ(p.requestSectionMesh(sec(0), stoneCube(), 1), SubmitResult::Accepted);
    EXPECT_EQ(p.chunkStage(kChunk), ChunkStage::Decorated);
}

TEST(PipelineLifecycle, StaleStagedMeshDoesNotCompleteTheColumn) {
    AsyncChunkPipeline p(strictInline());
    generate(p);
    ASSERT_EQ(p.requestSectionMesh(sec(0), stoneCube(), 1), SubmitResult::Accepted);
    (void)p.runPendingInline(1);
    ASSERT_EQ(p.chunkStage(kChunk), ChunkStage::Meshed);
    p.noteSectionVersion(sec(0), 2);
    EXPECT_EQ(p.chunkStage(kChunk), ChunkStage::Decorated);
    const auto s = p.drainUploads(1h, [](ChunkMeshResult&) {});
    EXPECT_EQ(s.discarded, 1u);
    EXPECT_EQ(p.chunkStage(kChunk), ChunkStage::Decorated) << "the stale mesh does not count";
}

TEST(PipelineLifecycle, EvictedSectionKeepsTheColumnOutOfMeshed) {
    PipelineConfig cfg = strictInline();
    cfg.maxQueuedJobs = 1;
    AsyncChunkPipeline p(cfg);
    const ChunkCoord near{0, 0};
    generate(p);
    generate(p, near);
    p.setViewerPosition(glm::vec3(8.0f));
    ASSERT_EQ(p.requestSectionMesh(sec(0), stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh(sec(0, near), stoneCube(), 1), SubmitResult::AcceptedEvictedOther);
    (void)p.runPendingInline(10);
    (void)drainAll(p);
    EXPECT_EQ(p.chunkStage(near), ChunkStage::Uploaded);
    EXPECT_EQ(p.chunkStage(kChunk), ChunkStage::Decorated) << "its only section was evicted, never meshed";
}

TEST(PipelineLifecycle, FailedUploadKeepsTheColumnMeshed) {
    AsyncChunkPipeline p(strictInline());
    generate(p);
    ASSERT_EQ(p.requestSectionMesh(sec(0), stoneCube(), 1), SubmitResult::Accepted);
    (void)p.runPendingInline(1);
    EXPECT_ANY_THROW((void)p.drainUploads(1h, [](ChunkMeshResult&) { throw 42; }));
    EXPECT_EQ(p.chunkStage(kChunk), ChunkStage::Meshed) << "the mesh never reached the GPU";
}

TEST(PipelineLifecycle, CancelChunkUnloadsIt) {
    AsyncChunkPipeline p(strictInline());
    generate(p);
    ASSERT_EQ(p.requestSectionMesh(sec(0), stoneCube(), 1), SubmitResult::Accepted);
    p.cancelChunk(kChunk);
    EXPECT_EQ(p.chunkStage(kChunk), ChunkStage::Unloaded);
    EXPECT_EQ(p.requestSectionMesh(sec(0), stoneCube(), 1), SubmitResult::RejectedNotReady);
    EXPECT_EQ(p.trackedChunks(), 0u);
}

TEST(PipelineLifecycle, AdvancingToUnloadedIsAnUnload) {
    AsyncChunkPipeline p(strictInline());
    generate(p);
    ASSERT_EQ(p.requestSectionMesh(sec(0), stoneCube(), 1), SubmitResult::Accepted);
    EXPECT_TRUE(p.advanceChunkStage(kChunk, ChunkStage::Unloaded));
    EXPECT_EQ(p.pendingJobs(), 0u);
    EXPECT_FALSE(p.advanceChunkStage(kChunk, ChunkStage::Unloaded)) << "already unloaded";
}

TEST(PipelineLifecycle, ColumnsAreIndependent) {
    AsyncChunkPipeline p(strictInline());
    const ChunkCoord other{kChunk.x + 1, kChunk.z};
    generate(p);
    ASSERT_EQ(p.requestSectionMesh(sec(0), stoneCube(), 1), SubmitResult::Accepted);
    EXPECT_EQ(p.requestSectionMesh(sec(0, other), stoneCube(), 1), SubmitResult::RejectedNotReady);
    (void)p.runPendingInline(10);
    EXPECT_EQ(p.chunkStage(kChunk), ChunkStage::Meshed);
    EXPECT_EQ(p.chunkStage(other), ChunkStage::Unloaded);
}

TEST(PipelineLifecycle, ThreadedColumnReachesUploaded) {
    PipelineConfig cfg;
    cfg.workerCount = 3;
    AsyncChunkPipeline p(cfg);
    generate(p);
    for (int y = 0; y < SectionCoord::SECTIONS_PER_COLUMN; ++y) {
        ASSERT_EQ(p.requestSectionMesh(sec(y), stoneCube(), 1), SubmitResult::Accepted);
    }
    ASSERT_TRUE(p.waitIdle(kGenerousTimeout));
    EXPECT_EQ(p.chunkStage(kChunk), ChunkStage::Meshed);
    EXPECT_EQ(drainAll(p).size(), 8u);
    EXPECT_EQ(p.chunkStage(kChunk), ChunkStage::Uploaded);
}

TEST(PipelineLifecycle, DisabledEnforcementAcceptsAnyChunk) {
    PipelineConfig cfg = strictInline();
    cfg.enforceChunkLifecycle = false;
    AsyncChunkPipeline p(cfg);
    EXPECT_EQ(p.requestSectionMesh(sec(0), stoneCube(), 1), SubmitResult::Accepted);
    EXPECT_EQ(p.trackedChunks(), 0u);
}
