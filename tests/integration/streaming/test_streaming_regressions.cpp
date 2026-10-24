// Phase 2 integration review fixes, streaming side: GPU meshes of columns unloaded on the
// frame smooth lighting toggles, metadata-only writes (fluid levels) that cannot change any
// geometry, and the renderer's per-frame visible-column count.
#include "test_support/GLTestContext.hpp"

#include "tests/integration/support/WorldgenSupport.hpp"
#include "render/WorldRenderer.hpp"
#include "streaming/WorldMeshStreamer.hpp"
#include "voxel/Chunk.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <memory>
#include <vector>

using namespace betacraft;
using namespace std::chrono_literals;
using streaming::ChunkMeshResult;
using voxel::SectionCoord;
using streaming::WorldMeshStreamer;
using streaming::WorldMeshStreamerConfig;
using voxel::BlockId;

namespace {

struct SectionLess {
    bool operator()(const voxel::SectionCoord& a, const voxel::SectionCoord& b) const noexcept {
        if (a.x != b.x) return a.x < b.x;
        if (a.z != b.z) return a.z < b.z;
        return a.y < b.y;
    }
};

// One live mesh per section, deleted on an empty result (WorldRenderer's uploader minus GL).
struct FakeGpu {
    std::map<voxel::SectionCoord, int, SectionLess> live;
    std::map<voxel::SectionCoord, int, SectionLess> uploads;
    std::map<voxel::SectionCoord, int, SectionLess> removals;

    void operator()(ChunkMeshResult& r) {
        if (r.buffers.empty()) {
            live.erase(r.coord);
            ++removals[r.coord];
            return;
        }
        live[r.coord] = 1;
        ++uploads[r.coord];
    }
};

class StreamingRegressions : public ::testing::Test {
protected:
    void SetUp() override {
        voxel::BlockRegistry::initialize();
        m_world = std::make_unique<world::World>(test::p2worldgen::kSeed, m_dir.path());
        test::p2worldgen::loadAll(*m_world, test::p2worldgen::squareRegion({0, 0}, 2));
    }

    [[nodiscard]] static WorldMeshStreamerConfig inlineMode() {
        WorldMeshStreamerConfig cfg;
        cfg.pipeline.workerCount = 0;
        cfg.pipeline.maxQueuedJobs = 4096;
        return cfg;
    }

    void converge(WorldMeshStreamer& s, FakeGpu& gpu) {
        for (int frame = 0; frame < 2000; ++frame) {
            s.update(*m_world, m_viewer, m_forward);
            (void)s.runPendingInline(100000);
            (void)s.drainUploads(1s, std::ref(gpu));
            if (!s.hasPendingWork()) return;
        }
        FAIL() << "streamer did not converge";
    }

    test::TempDir m_dir{"betacraft_p2_fixes_streaming_"};
    std::unique_ptr<world::World> m_world;
    glm::vec3 m_viewer{8.0f, 80.0f, 8.0f};
    glm::vec3 m_forward{0.0f, 0.0f, 1.0f};
};

} // namespace

TEST_F(StreamingRegressions, TogglingSmoothLightingOnTheUnloadFrameStillDeletesTheUnloadedMeshes) {
    WorldMeshStreamer s(inlineMode());
    FakeGpu gpu;
    converge(s, gpu);
    ASSERT_FALSE(gpu.live.empty());
    const auto meshedBefore = gpu.live;

    // Same frame: Application streams chunks out, then the renderer applies the L toggle
    // before the streamer's update.
    m_world->updateChunksAround(glm::vec3(100000.0f, 64.0f, 100000.0f), 0, 0);
    ASSERT_TRUE(m_world->getChunks().empty());
    s.setSmoothLighting(!s.smoothLighting());
    converge(s, gpu);

    EXPECT_TRUE(gpu.live.empty()) << gpu.live.size() << " meshes of unloaded columns leaked";
    for (const auto& [sc, n] : meshedBefore) {
        EXPECT_EQ(gpu.removals[sc], 1) << sc.x << "," << sc.y << "," << sc.z;
    }
    EXPECT_EQ(s.trackedChunks(), 0U);
}

TEST_F(StreamingRegressions, TogglingSmoothLightingStillRemeshesTheResidentChunks) {
    WorldMeshStreamer s(inlineMode());
    FakeGpu gpu;
    converge(s, gpu);
    const auto meshedBefore = gpu.live;
    gpu.uploads.clear();

    s.setSmoothLighting(!s.smoothLighting());
    converge(s, gpu);
    EXPECT_EQ(gpu.live.size(), meshedBefore.size());
    for (const auto& [sc, n] : meshedBefore) {
        EXPECT_EQ(gpu.uploads[sc], 1) << sc.x << "," << sc.y << "," << sc.z;
    }
}

TEST_F(StreamingRegressions, FluidLevelChangesDoNotQueueRemeshes) {
    // A flowing-water cell high in the air; once it exists, only its level changes.
    ASSERT_TRUE(m_world->setBlockWithMetadata(0, 100, 5, BlockId::Water, 1));
    for (const auto& [coord, chunk] : m_world->getChunks()) chunk->setMeshDirty(false);

    ASSERT_TRUE(m_world->setMetadata(0, 100, 5, 4)); // x = 0: on the seam with column (-1, 0)
    EXPECT_EQ(m_world->getMetadata(0, 100, 5), 4);
    for (const auto& [coord, chunk] : m_world->getChunks()) {
        EXPECT_EQ(chunk->dirtySectionMask(), 0) << coord.x << "," << coord.z;
    }
    EXPECT_TRUE(m_world->getChunk({0, 0})->isModified()) << "the level still has to be saved";
}

TEST(StreamingChunkRegressions, MetadataWriteMarksTheChunkModifiedButNotDirty) {
    voxel::BlockRegistry::initialize();
    voxel::Chunk c({0, 0});
    c.setBlock(3, 70, 3, BlockId::Water, false);
    c.setMeshDirty(false);
    c.setModified(false);
    c.setMetadata(3, 70, 3, 4);
    EXPECT_EQ(c.getMetadata(3, 70, 3), 4);
    EXPECT_FALSE(c.isMeshDirty());
    EXPECT_TRUE(c.isModified());
}

TEST(RenderStatsRegressions, DistinctVisibleColumnsAreCountedInAReusedBuffer) {
    std::vector<core::ChunkCoord> columns{{0, 0}, {0, 0}, {1, 0}, {0, 0}, {-1, 2}, {1, 0}};
    EXPECT_EQ(render::countDistinctColumns(columns), 3U);
    const size_t capacity = columns.capacity();
    columns.clear();
    columns.push_back({5, 5});
    EXPECT_EQ(render::countDistinctColumns(columns), 1U);
    EXPECT_EQ(columns.capacity(), capacity) << "the buffer is reused, not reallocated";
    columns.clear();
    EXPECT_EQ(render::countDistinctColumns(columns), 0U);
}
