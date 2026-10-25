// Robustness of AsyncChunkPipeline against hostile inputs and failing collaborators:
// non-finite viewer data, uploaders that throw, and telemetry consistency.

#include "streaming/AsyncChunkPipeline.hpp"

#include "PipelineTestDoubles.hpp"
#include "test_support/StreamingTestHelpers.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <vector>

using namespace std::chrono_literals;
using betacraft::streaming::AsyncChunkPipeline;
using betacraft::streaming::ChunkMeshResult;
using betacraft::streaming::PipelineConfig;
using betacraft::streaming::SubmitResult;
using betacraft::streaming::TaskPriority;
using betacraft::streaming::test::drainAll;
using betacraft::streaming::test::inlineConfig;
using betacraft::streaming::test::kGenerousTimeout;
using betacraft::streaming::test::RecordingMesher;
using betacraft::streaming::test::stoneCube;
using betacraft::streaming::test::threadedConfig;
using betacraft::voxel::SectionCoord;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

std::vector<int> meshedXs(RecordingMesher& rec) {
    std::vector<int> xs;
    for (const auto& c : rec.snapshotOrder()) xs.push_back(c.x);
    return xs;
}

} // namespace

// ------------------------------------------------------ non-finite viewer ----

TEST(PipelineRobustness, NaNViewerPositionIsIgnored) {
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    p.setViewerPosition(glm::vec3(8.0f));
    p.setViewerPosition(glm::vec3(kNaN, 8.0f, 8.0f)); // physics divide-by-zero
    for (int x : {3, 1, 2}) ASSERT_EQ(p.requestSectionMesh({x, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    EXPECT_EQ(p.pendingJobs(), 3u);
    EXPECT_EQ(p.runPendingInline(10), 3u) << "every job must stay schedulable";
    EXPECT_EQ(meshedXs(rec), (std::vector<int>{1, 2, 3})) << "last finite position still orders the queue";
}

TEST(PipelineRobustness, NaNAfterSubmissionKeepsEveryPendingJobSchedulable) {
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    for (int x = 0; x < 6; ++x) ASSERT_EQ(p.requestSectionMesh({x, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    p.setViewerPosition(glm::vec3(kNaN));
    p.cancelSection({4, 0, 0}); // must erase exactly that job, not an "equivalent" one
    EXPECT_EQ(p.pendingJobs(), 5u);
    EXPECT_EQ(p.runPendingInline(10), 5u);
    EXPECT_EQ(meshedXs(rec), (std::vector<int>{0, 1, 2, 3, 5}));
}

TEST(PipelineRobustness, InfiniteViewerPositionIsIgnored) {
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    p.setViewerPosition(glm::vec3(8.0f));
    p.setViewerPosition(glm::vec3(kInf, -kInf, 0.0f));
    for (int x : {2, 0, 1}) ASSERT_EQ(p.requestSectionMesh({x, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    EXPECT_EQ(p.runPendingInline(10), 3u);
    EXPECT_EQ(meshedXs(rec), (std::vector<int>{0, 1, 2}));
}

TEST(PipelineRobustness, HugeFiniteViewerOverflowingDistanceStaysFifoAndComplete) {
    // Finite but so far away that distance^2 overflows to +inf for every section.
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    p.setViewerPosition(glm::vec3(3.0e38f));
    for (int x : {5, 1, 3, 2}) ASSERT_EQ(p.requestSectionMesh({x, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    EXPECT_EQ(p.runPendingInline(10), 4u);
    EXPECT_EQ(meshedXs(rec), (std::vector<int>{5, 1, 3, 2})) << "equal (saturated) keys are FIFO";
}

TEST(PipelineRobustness, ThreadedWaitIdleIsNotFooledByNaNViewer) {
    RecordingMesher rec;
    AsyncChunkPipeline p(threadedConfig(2, rec.fn()));
    p.pause();
    p.setViewerPosition(glm::vec3(kNaN));
    for (int x = 0; x < 8; ++x) ASSERT_EQ(p.requestSectionMesh({x, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    p.resume();
    ASSERT_TRUE(p.waitIdle(kGenerousTimeout));
    EXPECT_EQ(p.pendingJobs(), 0u) << "waitIdle implies nothing is pending";
    EXPECT_EQ(drainAll(p).size(), 8u);
}

// ------------------------------------------------------ throwing uploader ----

TEST(PipelineRobustness, ThrowingUploaderStillAccountsForEveryConsumedItem) {
    AsyncChunkPipeline p(inlineConfig());
    for (int x = 0; x < 5; ++x) ASSERT_EQ(p.requestSectionMesh({x, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    (void)p.runPendingInline(10);
    p.noteSectionVersion({0, 0, 0}, 2); // first staged item becomes stale: discarded at drain

    int calls = 0;
    EXPECT_THROW((void)p.drainUploads(1h,
                                      [&](ChunkMeshResult&) {
                                          if (++calls == 3) throw std::runtime_error("GL_OUT_OF_MEMORY");
                                      }),
                 std::runtime_error);
    auto st = p.stats();
    EXPECT_EQ(st.discardedAtDrain, 1u);
    EXPECT_EQ(st.uploaded, 2u) << "uploads that completed before the throw are counted";
    EXPECT_EQ(st.uploadFailed, 1u) << "the consumed item whose upload threw is counted";

    // The remaining item is still delivered next frame, and the books balance.
    EXPECT_EQ(drainAll(p).size(), 1u);
    st = p.stats();
    EXPECT_EQ(st.completed, st.uploaded + st.discardedAtDrain + st.uploadFailed);
    EXPECT_EQ(p.stagedResults(), 0u);
}

// ------------------------------------------- authoritative version at drain ----
// The drain filter compares against the World's version, so an integrator
// that edits a block but forgets noteSectionVersion still never uploads a stale mesh.

namespace {

struct FakeWorldVersions {
    std::map<std::tuple<int, int, int>, uint64_t> versions;
    int calls{0};

    betacraft::streaming::SectionVersionSource source() {
        return [this](SectionCoord c) -> std::optional<uint64_t> {
            ++calls;
            const auto it = versions.find({c.x, c.y, c.z});
            return it == versions.end() ? std::nullopt : std::optional<uint64_t>(it->second);
        };
    }
};

} // namespace

TEST(PipelineRobustness, AuthoritativeVersionDiscardsMeshTheCallerForgotToInvalidate) {
    FakeWorldVersions world;
    PipelineConfig cfg = inlineConfig();
    cfg.authoritativeVersion = world.source();
    AsyncChunkPipeline p(cfg);
    world.versions[{0, 0, 0}] = 1;
    world.versions[{1, 0, 0}] = 1;
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh({1, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    (void)p.runPendingInline(10);
    world.versions[{0, 0, 0}] = 2; // block edited; integrator did NOT call noteSectionVersion

    std::vector<SectionCoord> uploaded;
    const auto s = p.drainUploads(1h, [&](ChunkMeshResult& r) { uploaded.push_back(r.coord); });
    EXPECT_EQ(uploaded, (std::vector<SectionCoord>{{1, 0, 0}}));
    EXPECT_EQ(s.discarded, 1u);
    EXPECT_GE(world.calls, 2);
}

TEST(PipelineRobustness, AuthoritativeVersionUnknownSectionIsDiscarded) {
    FakeWorldVersions world; // empty: the chunk was unloaded behind the pipeline's back
    PipelineConfig cfg = inlineConfig();
    cfg.authoritativeVersion = world.source();
    AsyncChunkPipeline p(cfg);
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    (void)p.runPendingInline(10);
    int uploads = 0;
    (void)p.drainUploads(1h, [&](ChunkMeshResult&) { ++uploads; });
    EXPECT_EQ(uploads, 0);
    EXPECT_EQ(p.stats().discardedAtDrain, 1u);
}

TEST(PipelineRobustness, AuthoritativeVersionCannotResurrectASupersededSubmission) {
    // Both checks must pass: the World agreeing on the version is not enough when a newer
    // submission (same version, new border) superseded the staged one.
    FakeWorldVersions world;
    world.versions[{0, 0, 0}] = 3;
    PipelineConfig cfg = inlineConfig();
    cfg.authoritativeVersion = world.source();
    AsyncChunkPipeline p(cfg);
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 3), SubmitResult::Accepted);
    (void)p.runPendingInline(1);
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneCube(), 3), SubmitResult::Accepted);
    const auto s = p.drainUploads(1h, [](ChunkMeshResult&) {});
    EXPECT_EQ(s.uploaded, 0u);
    EXPECT_EQ(s.discarded, 1u);
}
