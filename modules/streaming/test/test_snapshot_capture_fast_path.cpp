// Exactness of the row-wise / homogeneous fast paths of SectionNeighborhoodSnapshot18::
// capture, plus a capture-cost benchmark so regressions are visible (Fiche 1 §4 target:
// "Capture du snapshot 18^3 < 1.5 us").

#include "streaming/SectionNeighborhoodSnapshot18.hpp"

#include "test_support/StreamingTestHelpers.hpp"
#include "voxel/Chunk.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <memory>

using betacraft::streaming::SectionNeighborhoodSnapshot18;
using betacraft::voxel::SectionCoord;
using betacraft::voxel::BlockId;
using betacraft::voxel::BlockStateId;
using betacraft::voxel::Chunk;
using betacraft::voxel::PalettedSection;
using betacraft::world::World;
using Snap = SectionNeighborhoodSnapshot18;

namespace {

class SnapshotCaptureFastPath : public ::testing::Test {
protected:
    void SetUp() override {
        betacraft::voxel::BlockRegistry::initialize();
        m_world = std::make_unique<World>(987654ULL, m_dir.path());
    }

    void loadGenerated3x3() {
        for (int cz = -1; cz <= 1; ++cz)
            for (int cx = -1; cx <= 1; ++cx) m_world->ensureChunkLoaded({cx, cz});
    }

    // Reference value straight from the owning PalettedSection (full state id, not just
    // the block id that World::getBlock exposes), with World's out-of-column semantics.
    struct Ref {
        BlockStateId state;
        uint8_t sky;
        uint8_t block;
    };
    Ref reference(int wx, int wy, int wz) const {
        if (wy >= Chunk::HEIGHT) return {static_cast<BlockStateId>(BlockId::Air), 15, 0};
        if (wy < 0) return {static_cast<BlockStateId>(BlockId::Air), 0, 0};
        const Chunk* c = m_world->getChunk({betacraft::core::floorDiv(wx, 16), betacraft::core::floorDiv(wz, 16)});
        if (c == nullptr) return {static_cast<BlockStateId>(BlockId::Air), 15, 0};
        const PalettedSection& s = c->getSection(static_cast<size_t>(wy >> 4));
        const int x = betacraft::core::floorMod(wx, 16), y = wy & 15, z = betacraft::core::floorMod(wz, 16);
        return {s.getBlockState(x, y, z), s.getSkyLight(x, y, z), s.getBlockLight(x, y, z)};
    }

    void expectExact(SectionCoord sc) const {
        const auto snap = Snap::capture(*m_world, sc);
        const glm::ivec3 o = sc.worldOrigin();
        bool interiorEmpty = true;
        for (int y = -1; y <= 16; ++y)
            for (int z = -1; z <= 16; ++z)
                for (int x = -1; x <= 16; ++x) {
                    const Ref r = reference(o.x + x, o.y + y, o.z + z);
                    ASSERT_EQ(snap->getBlockState(x, y, z), r.state) << sc.y << ":" << x << "," << y << "," << z;
                    ASSERT_EQ(snap->getSkyLight(x, y, z), r.sky) << sc.y << ":" << x << "," << y << "," << z;
                    ASSERT_EQ(snap->getBlockLight(x, y, z), r.block) << sc.y << ":" << x << "," << y << "," << z;
                    if (x >= 0 && x < 16 && y >= 0 && y < 16 && z >= 0 && z < 16 &&
                        r.state != static_cast<BlockStateId>(BlockId::Air)) {
                        interiorEmpty = false;
                    }
                }
        EXPECT_EQ(snap->isInteriorEmpty(), interiorEmpty) << "section " << sc.y;
    }

    betacraft::streaming::test::ScopedTempDir m_dir;
    std::unique_ptr<World> m_world;
};

} // namespace

TEST_F(SnapshotCaptureFastPath, GeneratedTerrainIsCapturedExactlyInEverySection) {
    loadGenerated3x3();
    // Generated sky sections hold a single block but dense light: the block and light
    // fast paths are independent, and this scenario must exercise the block one.
    bool sawSingleBlock = false;
    bool sawDense = false;
    for (size_t i = 0; i < Chunk::SECTIONS_PER_CHUNK; ++i) {
        const PalettedSection& s = m_world->getChunk({0, 0})->getSection(i);
        sawSingleBlock |= s.getBitsPerVoxel() == 0;
        sawDense |= s.getBitsPerVoxel() != 0;
    }
    EXPECT_TRUE(sawSingleBlock);
    EXPECT_TRUE(sawDense);
    for (int sy = 0; sy < SectionCoord::SECTIONS_PER_COLUMN; ++sy) expectExact({0, sy, 0});
}

TEST_F(SnapshotCaptureFastPath, UniformSectionWithNonDefaultLightAndStateIsExact) {
    loadGenerated3x3();
    // A homogeneous section whose single state, sky and block light all differ from the
    // defaults, so a fast path that forgot any of the three would be caught.
    m_world->getChunkMutable({0, 0})->getSection(3) = PalettedSection(static_cast<BlockStateId>(0x0105u), 7, 3);
    m_world->getChunkMutable({1, 0})->getSection(3) = PalettedSection(BlockId::Glass, 2, 9);
    ASSERT_TRUE(m_world->getChunk({0, 0})->getSection(3).isHomogeneous());
    for (int sy : {2, 3, 4}) expectExact({0, sy, 0});
}

TEST_F(SnapshotCaptureFastPath, MixedHomogeneousAndDenseNeighboursAtNegativeCoords) {
    for (int cz = -2; cz <= 0; ++cz)
        for (int cx = -2; cx <= 0; ++cx) m_world->ensureChunkLoaded({cx, cz});
    m_world->getChunkMutable({-2, -1})->getSection(1) = PalettedSection(BlockId::Stone, 0, 0);
    auto* dense = m_world->getChunkMutable({-1, -2});
    for (int y = 16; y < 32; ++y) dense->setBlockLight(5, y, 15, static_cast<uint8_t>(y & 15));
    for (int sy = 0; sy < SectionCoord::SECTIONS_PER_COLUMN; ++sy) expectExact({-1, sy, -1});
}

TEST_F(SnapshotCaptureFastPath, MissingNeighboursAndColumnEdgesAreExact) {
    m_world->ensureChunkLoaded({0, 0}); // all eight neighbours unloaded
    expectExact({0, 0, 0});              // y = -1 border: below bedrock
    expectExact({0, 7, 0});              // y = 128 border: above the column
}

// Not a pass/fail performance gate: Debug and sanitizer builds are 10-100x slower than
// Release. The measurement is printed and recorded (gtest XML property) so a regression
// is visible; optimised builds additionally enforce a generous ceiling.
TEST_F(SnapshotCaptureFastPath, CaptureCostIsMeasured) {
    loadGenerated3x3();
    constexpr int kIters = 200;
    const auto t0 = std::chrono::steady_clock::now();
    size_t guard = 0;
    for (int i = 0; i < kIters; ++i) {
        const auto snap = Snap::capture(*m_world, {0, i % SectionCoord::SECTIONS_PER_COLUMN, 0});
        guard += snap->getBlockState(i % 16, 3, 5);
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - t0).count();
    const double usPerCapture = static_cast<double>(ns) / kIters / 1000.0;
    RecordProperty("capture_us", std::to_string(usPerCapture));
    std::cout << "[ perf     ] SectionNeighborhoodSnapshot18::capture: " << usPerCapture << " us/capture (guard "
              << guard % 7 << ")\n";
#if defined(NDEBUG) && !defined(__SANITIZE_THREAD__)
    EXPECT_LT(usPerCapture, 100.0);
#endif
}
