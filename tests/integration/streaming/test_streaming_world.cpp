// Asynchronous chunk streaming: the live World feeds the
// AsyncChunkPipeline through WorldMeshStreamer (lifecycle reports, dirty-section captures,
// re-requests, unload cancellation). A GL-free uploader stands in for WorldRenderer and
// checks that what ends up "on the GPU" is exactly the newest mesh of every section.
#include "test_support/GLTestContext.hpp"

#include "tests/integration/support/WorldgenSupport.hpp"
#include "streaming/SectionNeighborhoodSnapshot18.hpp"
#include "streaming/SnapshotMesher.hpp"
#include "streaming/WorldMeshStreamer.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <map>
#include <memory>
#include <random>
#include <set>
#include <vector>

using namespace betacraft;
using namespace std::chrono_literals;
using streaming::ChunkMeshResult;
using streaming::ChunkStage;
using streaming::RawMeshBuffer;
using voxel::SectionCoord;
using streaming::WorldMeshStreamer;
using streaming::WorldMeshStreamerConfig;
using voxel::BlockId;

namespace {

constexpr int32_t kSections = voxel::SectionCoord::SECTIONS_PER_COLUMN;
constexpr auto kTimeout = std::chrono::seconds(20);

struct SectionLess {
    bool operator()(const voxel::SectionCoord& a, const voxel::SectionCoord& b) const noexcept {
        if (a.x != b.x) return a.x < b.x;
        if (a.z != b.z) return a.z < b.z;
        return a.y < b.y;
    }
};
using SectionSet = std::set<voxel::SectionCoord, SectionLess>;
template <typename V>
using SectionMap = std::map<voxel::SectionCoord, V, SectionLess>;

bool sameVertices(const std::vector<streaming::PackedChunkVertex>& a,
                  const std::vector<streaming::PackedChunkVertex>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].word0 != b[i].word0 || a[i].word1 != b[i].word1) return false;
    }
    return true;
}

bool sameMesh(const RawMeshBuffer& a, const RawMeshBuffer& b) {
    return sameVertices(a.opaqueVertices, b.opaqueVertices) && sameVertices(a.translucentVertices, b.translucentVertices);
}

// What WorldRenderer's GL uploader does, minus GL: one live mesh per section, deleted on an
// empty result.
struct FakeGpu {
    struct Live {
        RawMeshBuffer mesh;
        uint64_t version{0};
    };
    SectionMap<Live> live;
    SectionMap<int> uploads; // non-empty uploads since the last reset
    SectionMap<int> removals;

    void operator()(ChunkMeshResult& r) {
        if (r.buffers.empty()) {
            live.erase(r.coord);
            ++removals[r.coord];
            return;
        }
        live[r.coord] = Live{std::move(r.buffers), r.version};
        ++uploads[r.coord];
    }

    void resetCounts() {
        uploads.clear();
        removals.clear();
    }

    [[nodiscard]] SectionSet uploadedSet() const {
        SectionSet out;
        for (const auto& [c, n] : uploads) out.insert(c);
        return out;
    }
};

[[nodiscard]] std::shared_ptr<const streaming::SectionNeighborhoodSnapshot18> snapshotOf(const world::World& w,
                                                                                    voxel::SectionCoord c) {
    return streaming::SectionNeighborhoodSnapshot18::capture(w, c);
}

[[nodiscard]] RawMeshBuffer freshMesh(const world::World& w, voxel::SectionCoord c, bool smooth = true) {
    streaming::MeshOptions opts;
    opts.smoothLighting = smooth;
    return streaming::SnapshotMesher::build(*snapshotOf(w, c), c.y, opts).value();
}

[[nodiscard]] bool isNonEmpty(const world::World& w, voxel::SectionCoord c) {
    return !snapshotOf(w, c)->isInteriorEmpty();
}

class StreamingWorld : public ::testing::Test {
protected:
    void SetUp() override {
        voxel::BlockRegistry::initialize();
        m_world = std::make_unique<world::World>(test::worldgen_support::kSeed, m_dir.path());
        m_region = test::worldgen_support::squareRegion({0, 0}, 2);
        test::worldgen_support::loadAll(*m_world, m_region);
    }

    [[nodiscard]] static WorldMeshStreamerConfig threaded(size_t workers = 4) {
        WorldMeshStreamerConfig cfg;
        cfg.pipeline.workerCount = workers;
        return cfg;
    }

    [[nodiscard]] static WorldMeshStreamerConfig inlineMode(size_t maxQueued = 4096) {
        WorldMeshStreamerConfig cfg;
        cfg.pipeline.workerCount = 0;
        cfg.pipeline.maxQueuedJobs = maxQueued;
        return cfg;
    }

    // Frame loop until nothing is left to mesh or upload.
    void converge(WorldMeshStreamer& s, FakeGpu& gpu, bool inlineWorkers = false) {
        for (int frame = 0; frame < 2000; ++frame) {
            s.update(*m_world, m_viewer, m_forward);
            if (inlineWorkers) {
                (void)s.runPendingInline(100000);
            }
            ASSERT_TRUE(s.waitIdle(kTimeout));
            (void)s.drainUploads(1s, std::ref(gpu));
            if (!s.hasPendingWork()) return;
        }
        FAIL() << "streamer did not converge";
    }

    // Every non-empty section of every decorated loaded chunk shows exactly the mesh of the
    // current World; nothing else is live.
    void expectGpuMatchesWorld(const FakeGpu& gpu, bool smooth = true) {
        size_t expectedLive = 0;
        for (const auto& [coord, chunk] : m_world->getChunks()) {
            for (int32_t sy = 0; sy < kSections; ++sy) {
                const voxel::SectionCoord sc{coord.x, sy, coord.z};
                const auto it = gpu.live.find(sc);
                if (!m_world->isChunkDecorated(coord) || !isNonEmpty(*m_world, sc)) {
                    EXPECT_TRUE(it == gpu.live.end()) << "unexpected mesh " << sc.x << "," << sc.y << "," << sc.z;
                    continue;
                }
                ++expectedLive;
                ASSERT_TRUE(it != gpu.live.end()) << "missing mesh " << sc.x << "," << sc.y << "," << sc.z;
                EXPECT_TRUE(sameMesh(it->second.mesh, freshMesh(*m_world, sc, smooth)))
                    << "stale mesh " << sc.x << "," << sc.y << "," << sc.z;
            }
        }
        EXPECT_EQ(gpu.live.size(), expectedLive);
        EXPECT_GT(expectedLive, 0u);
    }

    [[nodiscard]] SectionSet nonEmptyAmong(const std::vector<voxel::SectionCoord>& cs) const {
        SectionSet out;
        for (const voxel::SectionCoord c : cs) {
            if (isNonEmpty(*m_world, c)) out.insert(c);
        }
        return out;
    }

    test::TempDir m_dir{"betacraft_streaming_"};
    std::unique_ptr<world::World> m_world;
    std::vector<core::ChunkCoord> m_region;
    glm::vec3 m_viewer{8.0f, 80.0f, 8.0f};
    glm::vec3 m_forward{0.0f, 0.0f, 1.0f};
};

} // namespace

TEST_F(StreamingWorld, NonEmptySectionsOfDecoratedChunksEachGetExactlyTheirNewestMesh) {
    WorldMeshStreamer s(threaded());
    FakeGpu gpu;
    converge(s, gpu);

    expectGpuMatchesWorld(gpu);
    for (const auto& [sc, n] : gpu.uploads) {
        EXPECT_EQ(n, 1) << "section uploaded more than once: " << sc.x << "," << sc.y << "," << sc.z;
    }
    for (const core::ChunkCoord c : m_region) {
        if (m_world->isChunkDecorated(c)) {
            EXPECT_EQ(s.pipeline().chunkStage(c), ChunkStage::Uploaded) << c.x << "," << c.z;
        } else {
            EXPECT_EQ(s.pipeline().chunkStage(c), ChunkStage::Lit) << "frontier " << c.x << "," << c.z;
        }
    }
    EXPECT_EQ(s.trackedChunks(), m_region.size());
}

TEST_F(StreamingWorld, OpaqueSwapAtAChunkCornerRemeshesExactlyTheSectionsThatSeeTheVoxel) {
    WorldMeshStreamer s(threaded());
    FakeGpu gpu;
    (void)m_world->setBlock(15, 31, 15, BlockId::Stone);
    converge(s, gpu);
    gpu.resetCounts();

    // (15, 31, 15) touches four columns and the seam between sections 1 and 2. Swapping one
    // opaque block for another does not relight, so these eight sections are the whole
    // footprint of the edit.
    ASSERT_TRUE(m_world->setBlock(15, 31, 15, BlockId::Cobblestone));
    converge(s, gpu);

    std::vector<voxel::SectionCoord> seeing;
    for (const int32_t cx : {0, 1})
        for (const int32_t cz : {0, 1})
            for (const int32_t sy : {1, 2}) seeing.push_back({cx, sy, cz});
    EXPECT_EQ(gpu.uploadedSet(), nonEmptyAmong(seeing));
    expectGpuMatchesWorld(gpu);
}

TEST_F(StreamingWorld, BlockEditRemeshesNeighbourSectionsAndConvergesToTheNewWorld) {
    WorldMeshStreamer s(threaded());
    FakeGpu gpu;
    converge(s, gpu);
    gpu.resetCounts();

    // A light source on the corner seam: its own section, the border sections across both
    // chunk seams and the vertical seam, plus whatever its light reaches.
    const int32_t x = 16;
    const int32_t z = 16;
    const int32_t y = 48; // section 3, bottom layer: section 2 sees it too
    ASSERT_TRUE(m_world->setBlock(x, y, z, BlockId::Glowstone));
    converge(s, gpu);

    const SectionSet uploaded = gpu.uploadedSet();
    for (const voxel::SectionCoord c : nonEmptyAmong({{0, 2, 0}, {0, 3, 0}, {1, 2, 0}, {1, 3, 0},
                                                      {0, 2, 1}, {0, 3, 1}, {1, 2, 1}, {1, 3, 1}})) {
        EXPECT_TRUE(uploaded.contains(c)) << "not re-meshed: " << c.x << "," << c.y << "," << c.z;
    }
    EXPECT_FALSE(uploaded.contains(voxel::SectionCoord{-2, 7, -2})) << "far section re-meshed";
    expectGpuMatchesWorld(gpu);
}

TEST_F(StreamingWorld, SectionThatEmptiesOutHasItsMeshDeleted) {
    WorldMeshStreamer s(threaded());
    FakeGpu gpu;
    converge(s, gpu);
    const voxel::SectionCoord sky{0, 7, 0};
    ASSERT_FALSE(isNonEmpty(*m_world, sky));
    ASSERT_FALSE(gpu.live.contains(sky));

    ASSERT_TRUE(m_world->setBlock(5, 120, 5, BlockId::Stone));
    converge(s, gpu);
    ASSERT_TRUE(gpu.live.contains(sky));
    expectGpuMatchesWorld(gpu);

    gpu.resetCounts();
    ASSERT_TRUE(m_world->setBlock(5, 120, 5, BlockId::Air));
    converge(s, gpu);
    EXPECT_FALSE(gpu.live.contains(sky));
    EXPECT_EQ(gpu.removals[sky], 1);
    expectGpuMatchesWorld(gpu);
}

TEST_F(StreamingWorld, UnloadCancelsQueuedWorkAndDeletesTheColumnsMeshes) {
    WorldMeshStreamer s(inlineMode());
    FakeGpu gpu;
    converge(s, gpu, /*inlineWorkers=*/true);
    const core::ChunkCoord gone{-1, -1};
    ASSERT_TRUE(m_world->isChunkDecorated(gone));
    SectionSet goneMeshes;
    for (const auto& [sc, live] : gpu.live) {
        if (sc.chunk() == gone) goneMeshes.insert(sc);
    }
    ASSERT_FALSE(goneMeshes.empty());

    // Fresh work for the column is queued but never runs before the unload.
    m_world->getChunkMutable(gone)->setMeshDirty(true);
    s.update(*m_world, m_viewer, m_forward);
    ASSERT_GT(s.pipeline().pendingJobs(), 0u);

    // Far-away streaming centre: everything beyond radius + 3 is evicted, nothing loads.
    m_world->updateChunksAround(glm::vec3(100000.0f, 64.0f, 100000.0f), 0, 0);
    ASSERT_EQ(m_world->getChunk(gone), nullptr);
    gpu.resetCounts();
    s.update(*m_world, m_viewer, m_forward);

    EXPECT_EQ(s.pipeline().pendingJobs(), 0u);
    EXPECT_EQ(s.runPendingInline(100000), 0u);
    EXPECT_EQ(s.pipeline().chunkStage(gone), ChunkStage::Unloaded);
    EXPECT_EQ(s.trackedChunks(), 0u);
    EXPECT_EQ(s.pipeline().trackedSections(), 0u);

    (void)s.drainUploads(1s, std::ref(gpu));
    EXPECT_TRUE(gpu.uploads.empty()) << "a cancelled job reached the GPU";
    for (const voxel::SectionCoord sc : goneMeshes) {
        EXPECT_EQ(gpu.removals[sc], 1) << sc.x << "," << sc.y << "," << sc.z;
    }
    EXPECT_TRUE(gpu.live.empty());
    EXPECT_FALSE(s.hasPendingWork());
}

TEST_F(StreamingWorld, ReloadedChunkIsMeshedAgainFromScratch) {
    WorldMeshStreamer s(threaded());
    FakeGpu gpu;
    converge(s, gpu);
    m_world->updateChunksAround(glm::vec3(100000.0f, 64.0f, 100000.0f), 0, 0);
    converge(s, gpu);
    ASSERT_TRUE(gpu.live.empty());

    test::worldgen_support::loadAll(*m_world, m_region);
    gpu.resetCounts();
    converge(s, gpu);
    expectGpuMatchesWorld(gpu);
    for (const auto& [sc, n] : gpu.uploads) {
        EXPECT_EQ(n, 1) << sc.x << "," << sc.y << "," << sc.z;
    }
}

TEST_F(StreamingWorld, ChunkReplacedBetweenTwoUpdatesRestartsItsLifecycle) {
    WorldMeshStreamer s(threaded());
    FakeGpu gpu;
    converge(s, gpu);
    ASSERT_EQ(s.pipeline().chunkStage({0, 0}), ChunkStage::Uploaded);

    // Unloaded and reloaded alone before the streamer looks again: it is back to pass-1
    // terrain without neighbours, so it must not keep the old Decorated stage.
    m_world->updateChunksAround(glm::vec3(100000.0f, 64.0f, 100000.0f), 0, 0);
    (void)m_world->ensureChunkLoaded({0, 0});
    ASSERT_FALSE(m_world->isChunkDecorated({0, 0}));
    converge(s, gpu);

    EXPECT_EQ(s.pipeline().chunkStage({0, 0}), ChunkStage::Lit);
    EXPECT_TRUE(gpu.live.empty());
}

TEST_F(StreamingWorld, EvictedAndRefusedSectionsAreRequestedAgainUntilEverythingIsMeshed) {
    // A tiny queue and no worker threads: the first update overflows it (far sections are
    // refused or evicted by nearer ones), and nothing runs until the test says so.
    WorldMeshStreamer s(inlineMode(/*maxQueued=*/6));
    FakeGpu gpu;
    for (int frame = 0; frame < 2000; ++frame) {
        s.update(*m_world, m_viewer, m_forward);
        (void)s.runPendingInline(2); // fewer than the queue holds: evictions keep happening
        (void)s.drainUploads(1s, std::ref(gpu));
        if (!s.hasPendingWork()) break;
    }
    EXPECT_FALSE(s.hasPendingWork());
    const auto st = s.pipeline().stats();
    EXPECT_GT(st.rejectedFull + st.evicted, 0u) << "the queue never overflowed";
    expectGpuMatchesWorld(gpu);
}

TEST_F(StreamingWorld, PerUpdateRequestCapDefersTheRestToLaterUpdates) {
    WorldMeshStreamerConfig cfg = threaded();
    cfg.maxRequestsPerUpdate = 3;
    WorldMeshStreamer s(cfg);
    FakeGpu gpu;
    s.update(*m_world, m_viewer, m_forward);
    EXPECT_LE(s.pipeline().stats().submitted, 3u);
    EXPECT_TRUE(s.hasPendingWork()) << "dirty sections left behind count as pending work";
    converge(s, gpu);
    expectGpuMatchesWorld(gpu);
}

TEST_F(StreamingWorld, FrontierChunkIsMeshedOnceItsNeighbourhoodLoadsAndDecoratesIt) {
    WorldMeshStreamer s(threaded());
    FakeGpu gpu;
    converge(s, gpu);
    const core::ChunkCoord edge{2, 0};
    ASSERT_FALSE(m_world->isChunkDecorated(edge));
    for (int32_t sy = 0; sy < kSections; ++sy) {
        EXPECT_FALSE(gpu.live.contains(voxel::SectionCoord{edge.x, sy, edge.z}));
    }

    test::worldgen_support::loadAll(*m_world, {{3, -1}, {3, 0}, {3, 1}});
    ASSERT_TRUE(m_world->isChunkDecorated(edge));
    converge(s, gpu);
    EXPECT_EQ(s.pipeline().chunkStage(edge), ChunkStage::Uploaded);
    expectGpuMatchesWorld(gpu);
}

TEST_F(StreamingWorld, TogglingSmoothLightingRemeshesEveryDecoratedChunk) {
    WorldMeshStreamer s(threaded());
    FakeGpu gpu;
    converge(s, gpu);
    s.setSmoothLighting(false);
    EXPECT_FALSE(s.smoothLighting());
    converge(s, gpu);
    expectGpuMatchesWorld(gpu, /*smooth=*/false);
    s.setSmoothLighting(true);
    converge(s, gpu);
    expectGpuMatchesWorld(gpu, /*smooth=*/true);
}

// Also the ThreadSanitizer target: the main thread edits the World (relighting, seams,
// neighbours) while four workers mesh snapshots and the drain uploads every few edits.
TEST_F(StreamingWorld, MainThreadEditsWhileWorkersMeshConvergeToTheFinalWorld) {
    WorldMeshStreamer s(threaded(4));
    FakeGpu gpu;
    std::mt19937 rng(20261017u);
    std::uniform_int_distribution<int32_t> xz(-12, 27);
    std::uniform_int_distribution<int32_t> ys(40, 90);
    static constexpr BlockId kinds[] = {BlockId::Air, BlockId::Stone, BlockId::Glass, BlockId::Torch,
                                        BlockId::Glowstone, BlockId::Leaves};
    for (int i = 0; i < 300; ++i) {
        (void)m_world->setBlock(xz(rng), ys(rng), xz(rng), kinds[rng() % std::size(kinds)]);
        if (i % 3 == 0) {
            s.update(*m_world, m_viewer, m_forward);
        }
        if (i % 7 == 0) {
            (void)s.drainUploads(200us, std::ref(gpu));
        }
    }
    converge(s, gpu);
    expectGpuMatchesWorld(gpu);
    EXPECT_EQ(s.pipeline().stats().uploadFailed, 0u);
}
