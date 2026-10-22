// Priority by distance AND field of view (roadmap P1.1 "chunks proches du joueur et situés
// dans le champ de vision"; Diagram 1 node B "Évaluation Distance Joueur & Frustum").
// Sections outside the view cone are not excluded, only pushed back: their distance key
// is scaled, so a near section behind the player still beats a far one in front.

#include "streaming/AsyncChunkPipeline.hpp"

#include "PipelineTestDoubles.hpp"
#include "test_support/StreamingTestHelpers.hpp"

#include <gtest/gtest.h>

#include <limits>
#include <vector>

using betacraft::streaming::AsyncChunkPipeline;
using betacraft::streaming::PipelineConfig;
using betacraft::streaming::SubmitResult;
using betacraft::streaming::TaskPriority;
using betacraft::streaming::test::inlineConfig;
using betacraft::streaming::test::RecordingMesher;
using betacraft::streaming::test::stoneCube;
using betacraft::voxel::SectionCoord;

namespace {

const glm::vec3 kEye{8.0f, 8.0f, 8.0f}; // centre of section (0,0,0)
const glm::vec3 kLookPosX{1.0f, 0.0f, 0.0f};

std::vector<int> runAndCollectXs(AsyncChunkPipeline& p, RecordingMesher& rec) {
    (void)p.runPendingInline(1000);
    std::vector<int> xs;
    for (const auto& c : rec.snapshotOrder()) xs.push_back(c.x);
    return xs;
}

} // namespace

TEST(PipelineViewPriority, AtEqualDistanceSectionsInViewComeFirst) {
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    p.setViewer(kEye, kLookPosX);
    ASSERT_EQ(p.requestSectionMesh({-4, 0, 0}, stoneCube(), 1), SubmitResult::Accepted); // behind, first in
    ASSERT_EQ(p.requestSectionMesh({4, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);  // ahead
    EXPECT_EQ(runAndCollectXs(p, rec), (std::vector<int>{4, -4}));
}

TEST(PipelineViewPriority, NearSectionsBehindThePlayerAreNeverPenalised) {
    // Turning around must not leave a hole right behind the camera.
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    p.setViewer(kEye, kLookPosX);
    ASSERT_EQ(p.requestSectionMesh({3, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);  // 48 blocks ahead
    ASSERT_EQ(p.requestSectionMesh({-1, 0, 0}, stoneCube(), 1), SubmitResult::Accepted); // 16 blocks behind
    EXPECT_EQ(runAndCollectXs(p, rec), (std::vector<int>{-1, 3}));
}

TEST(PipelineViewPriority, OutOfViewIsAWeightNotAnExclusion) {
    // Default scale 2: a section 64 blocks behind ranks like one 128 blocks ahead.
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    p.setViewer(kEye, kLookPosX);
    ASSERT_EQ(p.requestSectionMesh({9, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);  // 144 ahead
    ASSERT_EQ(p.requestSectionMesh({-4, 0, 0}, stoneCube(), 1), SubmitResult::Accepted); // 64 behind -> 128
    ASSERT_EQ(p.requestSectionMesh({6, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);  // 96 ahead
    EXPECT_EQ(runAndCollectXs(p, rec), (std::vector<int>{6, -4, 9}));
}

TEST(PipelineViewPriority, SectionsGrazingTheConeEdgeCountAsInView) {
    // The test is conservative (bounding sphere vs cone): a section whose centre is just
    // outside the 60 degree half-angle but whose volume pokes into the cone is in view.
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    p.setViewer(kEye, kLookPosX);
    // Centre at 63.4 degrees off-axis (dx=2, dz=4 sections), radius ~13.9 blocks.
    ASSERT_EQ(p.requestSectionMesh({-2, 0, 4}, stoneCube(), 1), SubmitResult::Accepted); // same dist, behind
    ASSERT_EQ(p.requestSectionMesh({2, 0, 4}, stoneCube(), 1), SubmitResult::Accepted);
    EXPECT_EQ(runAndCollectXs(p, rec), (std::vector<int>{2, -2}));
}

TEST(PipelineViewPriority, TurningAroundReprioritisesPendingJobs) {
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    p.setViewer(kEye, kLookPosX);
    ASSERT_EQ(p.requestSectionMesh({4, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh({-4, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    p.setViewer(kEye, -kLookPosX);
    EXPECT_EQ(runAndCollectXs(p, rec), (std::vector<int>{-4, 4}));
}

TEST(PipelineViewPriority, ZeroOrNonFiniteForwardMeansDistanceOnly) {
    for (const glm::vec3 fwd : {glm::vec3(0.0f), glm::vec3(std::numeric_limits<float>::quiet_NaN(), 0.0f, 0.0f)}) {
        RecordingMesher rec;
        AsyncChunkPipeline p(inlineConfig(rec.fn()));
        p.setViewer(kEye, fwd);
        ASSERT_EQ(p.requestSectionMesh({-3, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
        ASSERT_EQ(p.requestSectionMesh({4, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
        EXPECT_EQ(runAndCollectXs(p, rec), (std::vector<int>{-3, 4}));
    }
}

TEST(PipelineViewPriority, ForwardNeedNotBeNormalised) {
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    p.setViewer(kEye, kLookPosX * 37.0f);
    ASSERT_EQ(p.requestSectionMesh({-4, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh({4, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    EXPECT_EQ(runAndCollectXs(p, rec), (std::vector<int>{4, -4}));
}

TEST(PipelineViewPriority, PriorityClassStillDominatesTheView) {
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    p.setViewer(kEye, kLookPosX);
    ASSERT_EQ(p.requestSectionMesh({2, 0, 0}, stoneCube(), 1, TaskPriority::MediumFar), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh({-6, 0, 0}, stoneCube(), 1, TaskPriority::ImmediateCamera), SubmitResult::Accepted);
    EXPECT_EQ(runAndCollectXs(p, rec), (std::vector<int>{-6, 2}));
}

TEST(PipelineViewPriority, ScaleOfOneDisablesTheViewWeighting) {
    RecordingMesher rec;
    PipelineConfig cfg = inlineConfig(rec.fn());
    cfg.outOfViewDistanceScale = 1.0f;
    AsyncChunkPipeline p(cfg);
    p.setViewer(kEye, kLookPosX);
    ASSERT_EQ(p.requestSectionMesh({-3, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh({4, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    EXPECT_EQ(runAndCollectXs(p, rec), (std::vector<int>{-3, 4}));
}

TEST(PipelineViewPriority, SetViewerPositionKeepsTheLastViewDirection) {
    RecordingMesher rec;
    AsyncChunkPipeline p(inlineConfig(rec.fn()));
    p.setViewer(glm::vec3(1000.0f), kLookPosX);
    p.setViewerPosition(kEye); // movement without a new look direction
    ASSERT_EQ(p.requestSectionMesh({-4, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh({4, 0, 0}, stoneCube(), 1), SubmitResult::Accepted);
    EXPECT_EQ(runAndCollectXs(p, rec), (std::vector<int>{4, -4}));
}
