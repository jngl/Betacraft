#include "streaming/SnapshotMesher.hpp"

#include "StreamingTestHelpers.hpp"
#include "render/ChunkMesher.hpp"
#include "streaming/SectionNeighborhoodSnapshot18.hpp"

#include <gtest/gtest.h>

#include <optional>
#include <random>
#include <stop_token>
#include <vector>

using betacraft::streaming::MeshOptions;
using betacraft::streaming::PackedChunkVertex;
using betacraft::streaming::RawMeshBuffer;
using betacraft::streaming::SectionNeighborhoodSnapshot18;
using betacraft::streaming::SnapshotMesher;
using betacraft::streaming::VoxelSample;
using betacraft::voxel::SectionCoord;
using betacraft::voxel::BlockId;
using betacraft::voxel::BlockStateId;
using betacraft::world::World;
using Snap = SectionNeighborhoodSnapshot18;

namespace {

constexpr BlockStateId st(BlockId id) noexcept { return static_cast<BlockStateId>(id); }

// Snapshot with given blocks at local positions, everything else open air under full sky.
std::shared_ptr<const Snap> makeSnap(std::vector<std::pair<glm::ivec3, BlockId>> blocks) {
    return Snap::captureFrom([&](int32_t x, int32_t y, int32_t z) -> std::optional<VoxelSample> {
        for (const auto& [p, id] : blocks) {
            if (p == glm::ivec3(x, y, z)) {
                return VoxelSample{st(id), 15, 0};
            }
        }
        return VoxelSample{st(BlockId::Air), 15, 0};
    });
}

RawMeshBuffer mesh(const Snap& s, int32_t secY = 0, MeshOptions opt = {}) {
    auto r = SnapshotMesher::build(s, secY, opt);
    EXPECT_TRUE(r.has_value());
    return r.value_or(RawMeshBuffer{});
}

bool sameVertices(const std::vector<PackedChunkVertex>& a, const std::vector<PackedChunkVertex>& b,
                  std::string* why) {
    if (a.size() != b.size()) {
        *why = "size " + std::to_string(a.size()) + " vs " + std::to_string(b.size());
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i) {
        if (a[i].word0 != b[i].word0 || a[i].word1 != b[i].word1) {
            *why = "first mismatch at vertex " + std::to_string(i);
            return false;
        }
    }
    return true;
}

} // namespace

class SnapshotMesherBasics : public ::testing::Test {
protected:
    void SetUp() override { betacraft::voxel::BlockRegistry::initialize(); }
};

TEST_F(SnapshotMesherBasics, EmptySectionProducesNoGeometry) {
    const auto snap = makeSnap({});
    const RawMeshBuffer m = mesh(*snap);
    EXPECT_TRUE(m.opaqueVertices.empty());
    EXPECT_TRUE(m.translucentVertices.empty());
    EXPECT_EQ(m.opaqueQuadCount, 0u);
    EXPECT_EQ(m.translucentQuadCount, 0u);
    EXPECT_TRUE(m.empty());
}

TEST_F(SnapshotMesherBasics, LoneCubeEmitsSixQuads) {
    const auto snap = makeSnap({{{4, 5, 6}, BlockId::Stone}});
    const RawMeshBuffer m = mesh(*snap, 3);
    EXPECT_EQ(m.opaqueVertices.size(), 24u);
    EXPECT_EQ(m.opaqueQuadCount, 6u);
    EXPECT_TRUE(m.translucentVertices.empty());
    for (const auto& v : m.opaqueVertices) {
        EXPECT_GE(v.getX(), 4u);
        EXPECT_LE(v.getX(), 5u);
        EXPECT_GE(v.getY(), 5u);
        EXPECT_LE(v.getY(), 6u);
        EXPECT_GE(v.getZ(), 6u);
        EXPECT_LE(v.getZ(), 7u);
        EXPECT_EQ(v.getReserved(), 3u) << "section Y index is stored in the reserved bits";
        EXPECT_LE(v.getNormal(), 5u);
    }
}

TEST_F(SnapshotMesherBasics, SharedFaceBetweenOpaqueCubesIsCulled) {
    const auto snap = makeSnap({{{4, 5, 6}, BlockId::Stone}, {{5, 5, 6}, BlockId::Stone}});
    EXPECT_EQ(mesh(*snap).opaqueQuadCount, 10u);
}

TEST_F(SnapshotMesherBasics, BorderNeighbourCullsBoundaryFace) {
    const auto withNeighbour = makeSnap({{{15, 0, 0}, BlockId::Stone}, {{16, 0, 0}, BlockId::Stone}});
    const auto withoutNeighbour = makeSnap({{{15, 0, 0}, BlockId::Stone}});
    EXPECT_EQ(mesh(*withNeighbour).opaqueQuadCount, 5u);
    EXPECT_EQ(mesh(*withoutNeighbour).opaqueQuadCount, 6u);
}

TEST_F(SnapshotMesherBasics, BorderBlocksThemselvesAreNeverMeshed) {
    const auto snap = makeSnap({{{-1, 3, 3}, BlockId::Stone}, {{16, 3, 3}, BlockId::Stone},
                                {{3, -1, 3}, BlockId::Stone}, {{3, 16, 3}, BlockId::Stone}});
    EXPECT_TRUE(mesh(*snap).empty());
}

TEST_F(SnapshotMesherBasics, TranslucentBlocksGoToTranslucentBuffer) {
    const auto snap = makeSnap({{{2, 2, 2}, BlockId::Water}});
    const RawMeshBuffer m = mesh(*snap);
    EXPECT_TRUE(m.opaqueVertices.empty());
    EXPECT_EQ(m.translucentQuadCount, 6u);
    for (const auto& v : m.translucentVertices) {
        EXPECT_TRUE(v.getTranslucent());
    }
}

TEST_F(SnapshotMesherBasics, VertexCountsAreAlwaysFourPerQuad) {
    std::mt19937 rng(42u);
    static constexpr BlockId kinds[] = {BlockId::Stone, BlockId::Water, BlockId::Glass, BlockId::Torch,
                                        BlockId::TallGrass, BlockId::CobbleSlab, BlockId::Cactus, BlockId::Leaves};
    std::uniform_int_distribution<int> pick(0, 7);
    std::uniform_int_distribution<int> coord(-1, 16);
    std::vector<std::pair<glm::ivec3, BlockId>> blocks;
    for (int i = 0; i < 300; ++i) {
        blocks.push_back({{coord(rng), coord(rng), coord(rng)}, kinds[pick(rng)]});
    }
    const RawMeshBuffer m = mesh(*makeSnap(blocks));
    EXPECT_EQ(m.opaqueVertices.size(), static_cast<size_t>(m.opaqueQuadCount) * 4u);
    EXPECT_EQ(m.translucentVertices.size(), static_cast<size_t>(m.translucentQuadCount) * 4u);
    EXPECT_FALSE(m.empty());
}

TEST_F(SnapshotMesherBasics, StopRequestedBeforeStartCancels) {
    const auto snap = makeSnap({{{4, 5, 6}, BlockId::Stone}});
    std::stop_source src;
    src.request_stop();
    EXPECT_FALSE(SnapshotMesher::build(*snap, 0, MeshOptions{}, src.get_token()).has_value());
}

// Stop requested while the build is running (here: after 5 of 16 layers, from the
// progress hook, which runs on the meshing thread exactly where a racing request_stop
// could land). The mesher must notice at the next layer boundary and give up.
TEST_F(SnapshotMesherBasics, StopRequestedMidBuildAbortsAtTheNextLayer) {
    const auto snap = Snap::captureFrom([](int32_t, int32_t, int32_t) -> std::optional<VoxelSample> {
        return VoxelSample{st(BlockId::Stone), 0, 0};
    });
    std::stop_source src;
    std::vector<int32_t> layersDone;
    const auto r = SnapshotMesher::build(*snap, 0, MeshOptions{}, src.get_token(), [&](int32_t done) {
        layersDone.push_back(done);
        if (done == 5) src.request_stop();
    });
    EXPECT_FALSE(r.has_value());
    EXPECT_EQ(layersDone, (std::vector<int32_t>{1, 2, 3, 4, 5})) << "no layer may start after the stop";
}

TEST_F(SnapshotMesherBasics, ProgressHookSeesEveryLayerWhenNotStopped) {
    const auto snap = makeSnap({{{4, 5, 6}, BlockId::Stone}});
    int32_t last = 0;
    int calls = 0;
    const auto r = SnapshotMesher::build(*snap, 0, MeshOptions{}, {}, [&](int32_t done) {
        last = done;
        ++calls;
    });
    ASSERT_TRUE(r.has_value());
    EXPECT_EQ(calls, 16);
    EXPECT_EQ(last, 16);
    EXPECT_EQ(r->opaqueQuadCount, 6u);
}

TEST_F(SnapshotMesherBasics, DeterministicAcrossRuns) {
    const auto snap = makeSnap({{{1, 1, 1}, BlockId::Grass}, {{1, 2, 1}, BlockId::TallGrass}, {{8, 8, 8}, BlockId::Torch}});
    std::string why;
    const RawMeshBuffer a = mesh(*snap);
    const RawMeshBuffer b = mesh(*snap);
    EXPECT_TRUE(sameVertices(a.opaqueVertices, b.opaqueVertices, &why)) << why;
    EXPECT_TRUE(sameVertices(a.translucentVertices, b.translucentVertices, &why)) << why;
}

// ------------------------------------------------------ parity with legacy ----

class SnapshotMesherParity : public ::testing::Test {
protected:
    void SetUp() override {
        betacraft::voxel::BlockRegistry::initialize();
        m_world = std::make_unique<World>(173173173ULL, m_dir.path());
    }

    // The legacy mesher walks the full 128-high column bottom-up, so concatenating the
    // per-section snapshot meshes in section order must give bit-identical buffers.
    void expectParity(betacraft::core::ChunkCoord cc, bool smooth) {
        const auto* chunk = m_world->getChunk(cc);
        ASSERT_NE(chunk, nullptr);
        const auto legacy = betacraft::render::ChunkMesher::buildMesh(*m_world, *chunk, smooth);

        std::vector<PackedChunkVertex> opaque;
        std::vector<PackedChunkVertex> translucent;
        for (int32_t sy = 0; sy < SectionCoord::SECTIONS_PER_COLUMN; ++sy) {
            const auto snap = Snap::capture(*m_world, SectionCoord{cc.x, sy, cc.z});
            const RawMeshBuffer m = mesh(*snap, sy, MeshOptions{.smoothLighting = smooth});
            opaque.insert(opaque.end(), m.opaqueVertices.begin(), m.opaqueVertices.end());
            translucent.insert(translucent.end(), m.translucentVertices.begin(), m.translucentVertices.end());
        }
        std::string why;
        EXPECT_TRUE(sameVertices(legacy.opaqueVertices, opaque, &why)) << "opaque: " << why;
        EXPECT_TRUE(sameVertices(legacy.translucentVertices, translucent, &why)) << "translucent: " << why;
        EXPECT_FALSE(legacy.opaqueVertices.empty());
    }

    betacraft::streaming::test::ScopedTempDir m_dir;
    std::unique_ptr<World> m_world;
};

TEST_F(SnapshotMesherParity, GeneratedTerrainSmoothLightingSurroundedChunk) {
    for (int cz = -1; cz <= 1; ++cz)
        for (int cx = -1; cx <= 1; ++cx)
            m_world->ensureChunkLoaded({cx, cz});
    expectParity({0, 0}, true);
}

TEST_F(SnapshotMesherParity, GeneratedTerrainFlatLighting) {
    for (int cz = -1; cz <= 1; ++cz)
        for (int cx = -1; cx <= 1; ++cx)
            m_world->ensureChunkLoaded({cx, cz});
    expectParity({0, 0}, false);
}

TEST_F(SnapshotMesherParity, ChunkWithUnloadedNeighbours) {
    m_world->ensureChunkLoaded({3, -2});
    expectParity({3, -2}, true);
}

TEST_F(SnapshotMesherParity, RandomSpecialShapesAcrossSectionAndChunkBorders) {
    for (int cz = -1; cz <= 0; ++cz)
        for (int cx = -1; cx <= 0; ++cx)
            m_world->ensureChunkLoaded({cx, cz});

    static constexpr BlockId kinds[] = {BlockId::Air, BlockId::Stone, BlockId::Water, BlockId::Glass,
                                        BlockId::Torch, BlockId::TallGrass, BlockId::CobbleSlab, BlockId::Cactus,
                                        BlockId::Leaves, BlockId::Glowstone, BlockId::Grass, BlockId::Rose};
    std::mt19937 rng(0xBEEFu);
    std::uniform_int_distribution<int> pick(0, 11);
    std::uniform_int_distribution<int> light(0, 15);
    // Concentrate edits on chunk (-1,-1) near its +X/+Z borders and section seams.
    for (int i = 0; i < 4000; ++i) {
        const int lx = 10 + static_cast<int>(rng() % 8u); // 10..17 -> spills into neighbour chunk
        const int lz = 10 + static_cast<int>(rng() % 8u);
        const int y = 14 + static_cast<int>(rng() % 36u);  // spans sections 0..3 seams
        const int wx = -16 + lx;
        const int wz = -16 + lz;
        auto* chunk = m_world->getChunkMutable({betacraft::core::floorDiv(wx, 16), betacraft::core::floorDiv(wz, 16)});
        ASSERT_NE(chunk, nullptr);
        const int cx = betacraft::core::floorMod(wx, 16);
        const int cz = betacraft::core::floorMod(wz, 16);
        chunk->setBlock(cx, y, cz, kinds[pick(rng)], false);
        chunk->setSkyLight(cx, y, cz, static_cast<uint8_t>(light(rng)));
        chunk->setBlockLight(cx, y, cz, static_cast<uint8_t>(light(rng)));
    }
    expectParity({-1, -1}, true);
    expectParity({0, 0}, true);
    expectParity({-1, -1}, false);
}
