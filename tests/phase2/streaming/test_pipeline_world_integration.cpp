// End-to-end: live World -> main-thread 18^3 captures -> threaded workers -> budgeted
// drain. The concatenated per-section meshes must be bit-identical to the legacy
// whole-column ChunkMesher, whatever order the workers finished in. Also a TSAN target:
// the main thread keeps editing the World while workers mesh.

#include "streaming/AsyncChunkPipeline.hpp"

#include "StreamingTestHelpers.hpp"
#include "render/ChunkMesher.hpp"

#include <gtest/gtest.h>

#include <array>
#include <map>
#include <random>
#include <vector>

using namespace std::chrono_literals;
using betacraft::core::ChunkCoord;
using betacraft::render::PackedChunkVertex;
using betacraft::streaming::AsyncChunkPipeline;
using betacraft::streaming::ChunkMeshResult;
using betacraft::streaming::ChunkStage;
using betacraft::streaming::PipelineConfig;
using betacraft::streaming::SubmitResult;
using betacraft::streaming::test::kGenerousTimeout;
using betacraft::voxel::SectionCoord;
using betacraft::world::BlockId;
using betacraft::world::World;

namespace {

constexpr int kSections = SectionCoord::SECTIONS_PER_COLUMN;

bool sameWords(const std::vector<PackedChunkVertex>& a, const std::vector<PackedChunkVertex>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].word0 != b[i].word0 || a[i].word1 != b[i].word1) {
            return false;
        }
    }
    return true;
}

class PipelineWorldIntegration : public ::testing::Test {
protected:
    void SetUp() override {
        betacraft::world::BlockRegistry::initialize();
        m_world = std::make_unique<World>(424242ULL, m_dir.path());
        for (int cz = -1; cz <= 1; ++cz)
            for (int cx = -1; cx <= 1; ++cx)
                m_world->ensureChunkLoaded({cx, cz});
    }

    // The integration contract: World::ensureChunkLoaded generated, lit and decorated the
    // chunk synchronously, so the streamer reports those three stages right after loading.
    void reportLoadedChunks(AsyncChunkPipeline& p) {
        for (int cz = -1; cz <= 1; ++cz) {
            for (int cx = -1; cx <= 1; ++cx) {
                ASSERT_TRUE(p.advanceChunkStage({cx, cz}, ChunkStage::TerrainGen));
                ASSERT_TRUE(p.advanceChunkStage({cx, cz}, ChunkStage::Lit));
                ASSERT_TRUE(p.advanceChunkStage({cx, cz}, ChunkStage::Decorated));
            }
        }
    }

    // Drains with a small budget, frame after frame, the way the render loop will.
    std::map<int, ChunkMeshResult> drainBySectionY(AsyncChunkPipeline& p) {
        std::map<int, ChunkMeshResult> bySection;
        for (int frame = 0; frame < 10000; ++frame) {
            const auto s = p.drainUploads(500us, [&](ChunkMeshResult& r) { bySection[r.coord.y] = std::move(r); });
            if (s.uploaded == 0 && s.remaining == 0) {
                break;
            }
        }
        return bySection;
    }

    void expectColumnParity(ChunkCoord cc, const std::map<int, ChunkMeshResult>& bySection) {
        ASSERT_EQ(bySection.size(), static_cast<size_t>(kSections));
        std::vector<PackedChunkVertex> opaque;
        std::vector<PackedChunkVertex> translucent;
        for (const auto& [sy, r] : bySection) {
            opaque.insert(opaque.end(), r.buffers.opaqueVertices.begin(), r.buffers.opaqueVertices.end());
            translucent.insert(translucent.end(), r.buffers.translucentVertices.begin(),
                               r.buffers.translucentVertices.end());
        }
        const auto legacy = betacraft::render::ChunkMesher::buildMesh(*m_world, *m_world->getChunk(cc), true);
        EXPECT_FALSE(legacy.opaqueVertices.empty());
        EXPECT_TRUE(sameWords(legacy.opaqueVertices, opaque)) << legacy.opaqueVertices.size() << " vs " << opaque.size();
        EXPECT_TRUE(sameWords(legacy.translucentVertices, translucent));
    }

    betacraft::streaming::test::ScopedTempDir m_dir;
    std::unique_ptr<World> m_world;
};

} // namespace

TEST_F(PipelineWorldIntegration, ThreadedColumnMeshMatchesLegacyMesher) {
    PipelineConfig cfg;
    cfg.workerCount = 4;
    AsyncChunkPipeline p(cfg);
    reportLoadedChunks(p);
    p.setViewerPosition(glm::vec3(8.0f, 70.0f, 8.0f));
    for (int sy = 0; sy < kSections; ++sy) {
        ASSERT_EQ(p.requestSectionMeshFromWorld(*m_world, {0, sy, 0}, 1), SubmitResult::Accepted);
    }
    ASSERT_TRUE(p.waitIdle(kGenerousTimeout));
    EXPECT_EQ(p.chunkStage({0, 0}), ChunkStage::Meshed);
    expectColumnParity({0, 0}, drainBySectionY(p));
    EXPECT_EQ(p.chunkStage({0, 0}), ChunkStage::Uploaded);
    EXPECT_EQ(p.chunkStage({1, 0}), ChunkStage::Decorated) << "never meshed";
}

TEST_F(PipelineWorldIntegration, EditsDuringMeshingConvergeToTheFinalWorld) {
    PipelineConfig cfg;
    cfg.workerCount = 4;
    AsyncChunkPipeline p(cfg);
    reportLoadedChunks(p);
    std::array<uint64_t, kSections> version{};
    for (int sy = 0; sy < kSections; ++sy) {
        version[static_cast<size_t>(sy)] = 1;
        ASSERT_EQ(p.requestSectionMeshFromWorld(*m_world, {0, sy, 0}, 1), SubmitResult::Accepted);
    }

    // The main thread keeps editing chunk (0,0) while workers are busy. Each edit bumps
    // the section version and re-captures it, as the integrated tick loop will. Edits stay
    // 1+ voxel inside the chunk so no neighbouring column's mesh depends on them, and
    // away from section seams so only the edited section needs a remesh.
    std::mt19937 rng(77u);
    std::uniform_int_distribution<int> local(1, 14);
    std::uniform_int_distribution<int> sec(0, kSections - 1);
    static constexpr BlockId kinds[] = {BlockId::Air, BlockId::Stone, BlockId::Glass, BlockId::Water, BlockId::Torch};
    auto* chunk = m_world->getChunkMutable({0, 0});
    ASSERT_NE(chunk, nullptr);
    for (int i = 0; i < 200; ++i) {
        const int sy = sec(rng);
        const int y = sy * 16 + local(rng);
        chunk->setBlock(local(rng), y, local(rng), kinds[rng() % 5u], false);
        const uint64_t v = ++version[static_cast<size_t>(sy)];
        p.noteSectionVersion({0, sy, 0}, v);
        ASSERT_EQ(p.requestSectionMeshFromWorld(*m_world, {0, sy, 0}, v) == SubmitResult::RejectedStale, false);
        if (i % 16 == 0) {
            (void)p.drainUploads(200us, [](ChunkMeshResult&) {});
        }
    }
    ASSERT_TRUE(p.waitIdle(kGenerousTimeout));
    for (int sy = 0; sy < kSections; ++sy) {
        EXPECT_TRUE(p.isCurrent({0, sy, 0}, version[static_cast<size_t>(sy)]));
    }
    std::map<int, ChunkMeshResult> latest;
    for (int sy = 0; sy < kSections; ++sy) {
        // Same-version re-request (as after a neighbour edit): it must supersede whatever
        // is still staged from the edit loop, so each section yields exactly one mesh.
        ASSERT_EQ(p.requestSectionMeshFromWorld(*m_world, {0, sy, 0}, version[static_cast<size_t>(sy)]),
                  SubmitResult::Accepted);
    }
    ASSERT_TRUE(p.waitIdle(kGenerousTimeout));
    for (auto& [sy, r] : drainBySectionY(p)) {
        EXPECT_EQ(r.version, version[static_cast<size_t>(sy)]);
        latest[sy] = std::move(r);
    }
    expectColumnParity({0, 0}, latest);
    EXPECT_EQ(p.chunkStage({0, 0}), ChunkStage::Uploaded);
}
