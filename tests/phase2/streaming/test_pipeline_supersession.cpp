// Regression tests for result supersession in AsyncChunkPipeline.
//
// Versions are supplied by the caller and can repeat: a reloaded chunk restarts its
// counter, and a neighbour edit changes a section's 1-voxel border without bumping its
// own version. Staleness therefore cannot rely on the version number alone; only the most
// recent ACCEPTED submission of a section may ever reach the uploader.

#include "streaming/AsyncChunkPipeline.hpp"

#include "StreamingTestHelpers.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <optional>
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
using betacraft::streaming::test::kGenerousTimeout;
using betacraft::streaming::VoxelSample;
using betacraft::voxel::SectionCoord;
using betacraft::voxel::BlockId;
using betacraft::voxel::BlockStateId;
using Snap = SectionNeighborhoodSnapshot18;
using SnapPtr = std::shared_ptr<const Snap>;

namespace {

// Snapshot with `n` stone blocks along the X axis: the real mesher then emits a quad
// count that identifies which snapshot a result was built from.
SnapPtr stoneRow(int n) {
    return Snap::captureFrom([n](int32_t x, int32_t y, int32_t z) -> std::optional<VoxelSample> {
        const bool on = y == 2 && z == 2 && x >= 0 && x < n;
        return VoxelSample{static_cast<BlockStateId>(on ? BlockId::Stone : BlockId::Air), 15, 0};
    });
}

constexpr uint32_t quadsForRow(int n) noexcept { return static_cast<uint32_t>(4 * n + 2); }

PipelineConfig inlineConfig() {
    PipelineConfig cfg;
    cfg.workerCount = 0;
    cfg.enforceChunkLifecycle = false; // scheduler-only scenarios
    return cfg;
}

std::vector<ChunkMeshResult> drainAll(AsyncChunkPipeline& p) {
    std::vector<ChunkMeshResult> out;
    while (p.drainUploads(1h, [&](ChunkMeshResult& r) { out.push_back(std::move(r)); }).uploaded > 0) {
    }
    return out;
}

// Real mesher behind a gate: the FIRST call blocks until released (or stopped), later
// calls run straight through. Lets a test pin one job in flight deterministically.
struct FirstCallGate {
    std::promise<void> startedPromise;
    std::shared_future<void> started{startedPromise.get_future().share()};
    std::atomic<int> calls{0};
    std::mutex mutex;
    std::condition_variable_any cv;
    bool released{false};

    MeshFunction fn() {
        return [this](const Snap& s, SectionCoord c, const MeshOptions& o,
                      std::stop_token st) -> std::optional<RawMeshBuffer> {
            if (calls.fetch_add(1) == 0) {
                startedPromise.set_value();
                std::unique_lock lock(mutex);
                cv.wait(lock, st, [this] { return released; });
            }
            return betacraft::streaming::SnapshotMesher::build(s, c.y, o, st);
        };
    }
    void release() {
        {
            const std::lock_guard lock(mutex);
            released = true;
        }
        cv.notify_all();
    }
};

} // namespace

TEST(PipelineSupersession, StoneRowQuadCountsAreDistinct) {
    betacraft::voxel::BlockRegistry::initialize();
    const auto m1 = betacraft::streaming::SnapshotMesher::build(*stoneRow(1), 0, {});
    const auto m3 = betacraft::streaming::SnapshotMesher::build(*stoneRow(3), 0, {});
    ASSERT_TRUE(m1 && m3);
    EXPECT_EQ(m1->opaqueQuadCount, quadsForRow(1));
    EXPECT_EQ(m3->opaqueQuadCount, quadsForRow(3));
}

TEST(PipelineSupersession, CancelThenReRequestSameVersionDoesNotResurrectStagedResult) {
    AsyncChunkPipeline p(inlineConfig());
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneRow(1), 1), SubmitResult::Accepted);
    ASSERT_EQ(p.runPendingInline(1), 1u); // old chunk contents meshed and staged
    p.cancelSection({0, 0, 0});           // chunk unloaded...
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneRow(3), 1), SubmitResult::Accepted); // ...and reloaded

    EXPECT_TRUE(drainAll(p).empty()) << "pre-unload mesh must not be uploaded for the reloaded chunk";
    ASSERT_EQ(p.runPendingInline(1), 1u);
    const auto r = drainAll(p);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].buffers.opaqueQuadCount, quadsForRow(3));
}

TEST(PipelineSupersession, CancelThenReRequestSameVersionDiscardsInFlightResult) {
    FirstCallGate gate;
    PipelineConfig cfg;
    cfg.workerCount = 1;
    cfg.enforceChunkLifecycle = false;
    cfg.mesher = gate.fn();
    AsyncChunkPipeline p(cfg); // single worker: the re-request waits behind the gated job
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneRow(1), 1), SubmitResult::Accepted);
    ASSERT_EQ(gate.started.wait_for(kGenerousTimeout), std::future_status::ready);
    p.cancelSection({0, 0, 0});
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneRow(3), 1), SubmitResult::Accepted);
    gate.release();
    ASSERT_TRUE(p.waitIdle(kGenerousTimeout));

    const auto r = drainAll(p);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].buffers.opaqueQuadCount, quadsForRow(3));
}

TEST(PipelineSupersession, SameVersionResubmissionWhileInFlightSupersedesOlderSnapshot) {
    // Neighbour edit: the border changed, the section's own version did not.
    FirstCallGate gate;
    PipelineConfig cfg;
    cfg.workerCount = 1;
    cfg.enforceChunkLifecycle = false;
    cfg.mesher = gate.fn();
    AsyncChunkPipeline p(cfg);
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneRow(1), 4), SubmitResult::Accepted);
    ASSERT_EQ(gate.started.wait_for(kGenerousTimeout), std::future_status::ready);
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneRow(3), 4), SubmitResult::Accepted);
    gate.release();
    ASSERT_TRUE(p.waitIdle(kGenerousTimeout));

    const auto r = drainAll(p);
    ASSERT_EQ(r.size(), 1u) << "only the newest submission may be uploaded";
    EXPECT_EQ(r[0].buffers.opaqueQuadCount, quadsForRow(3));
    EXPECT_EQ(r[0].version, 4u);
    // The superseded in-flight job is stopped through its stop_token (abortedStale); had it
    // already finished it would be discarded instead. Either way, exactly one.
    EXPECT_EQ(p.stats().discardedStale + p.stats().abortedStale, 1u);
}

TEST(PipelineSupersession, SameVersionResubmissionAfterStagingSupersedesStagedResult) {
    AsyncChunkPipeline p(inlineConfig());
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneRow(1), 2), SubmitResult::Accepted);
    ASSERT_EQ(p.runPendingInline(1), 1u);
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneRow(3), 2), SubmitResult::Accepted);
    ASSERT_EQ(p.runPendingInline(1), 1u);
    const auto r = drainAll(p);
    ASSERT_EQ(r.size(), 1u);
    EXPECT_EQ(r[0].buffers.opaqueQuadCount, quadsForRow(3));
}

TEST(PipelineSupersession, PublicIsCurrentStillReportsVersionFreshness) {
    AsyncChunkPipeline p(inlineConfig());
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneRow(1), 2), SubmitResult::Accepted);
    ASSERT_EQ(p.requestSectionMesh({0, 0, 0}, stoneRow(1), 2), SubmitResult::Replaced);
    EXPECT_TRUE(p.isCurrent({0, 0, 0}, 2));
    p.noteSectionVersion({0, 0, 0}, 3);
    EXPECT_FALSE(p.isCurrent({0, 0, 0}, 2));
    EXPECT_TRUE(p.isCurrent({0, 0, 0}, 3));
}
