// Phase 2 integration, P3.4 two-pass decoration through the real World API: WorldGen
// produces pass 1 (terrain + caves) only, World runs pass 2 once a chunk's 3x3
// neighbourhood is resident, relights what it decorated and forgets the state on unload.
#include "tests/integration/phase2/P2WorldgenSupport.hpp"
#include "test_support/GLTestContext.hpp"

#include "world/World.hpp"
#include "worldgen/DecorationFeatures.hpp"
#include "worldgen/DecorationPipeline.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <random>
#include <vector>

using namespace betacraft;
using namespace betacraft::test::p2worldgen;

namespace {

class P2WorldgenDecoration : public ::testing::Test {
protected:
    void SetUp() override { voxel::BlockRegistry::initialize(); }
};

// A trunk rooted in the outer 2 columns of its chunk: its radius-2 crown crosses the border.
// The legacy in-chunk decorator kept trunks inside lx/lz 2..13, so none existed before P3.4.
struct BorderTrunk {
    core::BlockPos base{};
    core::ChunkCoord chunk{};
};

[[nodiscard]] std::vector<BorderTrunk> findBorderTrunks(const world::World& w, const std::vector<core::ChunkCoord>& coords) {
    std::vector<BorderTrunk> out;
    for (const core::ChunkCoord c : coords) {
        const Chunk* chunk = w.getChunk(c);
        if (chunk == nullptr) continue;
        for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz) {
            for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
                const bool border = lx <= 1 || lx >= Chunk::WIDTH - 2 || lz <= 1 || lz >= Chunk::DEPTH - 2;
                if (!border) continue;
                for (int32_t y = core::EngineConfig::SEA_LEVEL; y < Chunk::HEIGHT - 1; ++y) {
                    // Trunk base: log on soil.
                    if (isLogBlock(chunk->getBlock(lx, y, lz)) && !isLogBlock(chunk->getBlock(lx, y - 1, lz))) {
                        out.push_back({{c.x * Chunk::WIDTH + lx, y, c.z * Chunk::DEPTH + lz}, c});
                        break;
                    }
                }
            }
        }
    }
    return out;
}

[[nodiscard]] bool hasLeavesAcrossBorder(const world::World& w, const BorderTrunk& t) {
    for (int32_t y = t.base.y; y < std::min(Chunk::HEIGHT, t.base.y + 9); ++y) {
        for (int32_t dz = -2; dz <= 2; ++dz) {
            for (int32_t dx = -2; dx <= 2; ++dx) {
                const int32_t wx = t.base.x + dx;
                const int32_t wz = t.base.z + dz;
                const core::ChunkCoord owner{core::floorDiv(wx, Chunk::WIDTH), core::floorDiv(wz, Chunk::DEPTH)};
                if (owner != t.chunk && w.getBlock(wx, y, wz) == BlockId::Leaves) return true;
            }
        }
    }
    return false;
}

} // namespace

TEST_F(P2WorldgenDecoration, LoneChunkStaysUndecoratedUntilItsNeighbourhoodIsResident) {
    test::TempDir dir;
    world::World w(kSeed, dir.path());

    const Chunk& lone = w.ensureChunkLoaded({0, 0});
    EXPECT_FALSE(w.isChunkDecorated({0, 0}));
    EXPECT_EQ(countDecoration(lone), 0) << "pass 2 must wait for the 3x3 neighbourhood";

    std::vector<core::ChunkCoord> ring = squareRegion({0, 0}, 1);
    ring.erase(std::remove(ring.begin(), ring.end(), core::ChunkCoord{0, 0}), ring.end());
    for (size_t i = 0; i < ring.size(); ++i) {
        (void)w.ensureChunkLoaded(ring[i]);
        if (i + 1 < ring.size()) {
            EXPECT_FALSE(w.isChunkDecorated({0, 0})) << "after " << i + 1 << " neighbours";
        }
    }
    EXPECT_TRUE(w.isChunkDecorated({0, 0}));
    EXPECT_GT(countDecoration(*w.getChunk({0, 0})), 0);
    for (const core::ChunkCoord c : ring) EXPECT_FALSE(w.isChunkDecorated(c)) << c.x << "," << c.z;
}

TEST_F(P2WorldgenDecoration, TreesSpillAcrossChunkBordersIdenticallyForAnyLoadOrder) {
    const std::vector<core::ChunkCoord> region = squareRegion({0, 0}, 2);
    const std::vector<core::ChunkCoord> inner = squareRegion({0, 0}, 1);

    std::vector<core::ChunkCoord> reversed(region.rbegin(), region.rend());
    std::vector<core::ChunkCoord> shuffled = region;
    std::shuffle(shuffled.begin(), shuffled.end(), std::mt19937(1234U));
    // Inside-out spiral, as World::updateChunksAround streams them.
    std::vector<core::ChunkCoord> spiral = region;
    std::stable_sort(spiral.begin(), spiral.end(), [](core::ChunkCoord a, core::ChunkCoord b) {
        return a.x * a.x + a.z * a.z < b.x * b.x + b.z * b.z;
    });

    test::TempDir dirA, dirB, dirC, dirD;
    world::World a(kSeed, dirA.path());
    world::World b(kSeed, dirB.path());
    world::World c(kSeed, dirC.path());
    world::World d(kSeed, dirD.path());
    loadAll(a, region);
    loadAll(b, reversed);
    loadAll(c, shuffled);
    loadAll(d, spiral);

    for (const core::ChunkCoord cc : inner) {
        ASSERT_TRUE(a.isChunkDecorated(cc));
        ASSERT_TRUE(b.isChunkDecorated(cc));
        ASSERT_TRUE(c.isChunkDecorated(cc));
        ASSERT_TRUE(d.isChunkDecorated(cc));
        EXPECT_EQ(firstBlockDifference(*a.getChunk(cc), *b.getChunk(cc)), -1) << cc.x << "," << cc.z;
        EXPECT_EQ(firstBlockDifference(*a.getChunk(cc), *c.getChunk(cc)), -1) << cc.x << "," << cc.z;
        EXPECT_EQ(firstBlockDifference(*a.getChunk(cc), *d.getChunk(cc)), -1) << cc.x << "," << cc.z;
    }

    // Non-vacuous: the compared area holds trees whose crowns really cross a border.
    int32_t crossing = 0;
    for (const BorderTrunk& t : findBorderTrunks(a, inner)) {
        if (hasLeavesAcrossBorder(a, t)) ++crossing;
    }
    EXPECT_GT(crossing, 0) << "no tree crosses a chunk border in the decorated 3x3";
}

TEST_F(P2WorldgenDecoration, PlannedTreesAreRasterizedWholeInDecoratedChunks) {
    test::TempDir dir;
    world::World w(kSeed, dir.path());
    const std::vector<core::ChunkCoord> region = squareRegion({0, 0}, 2);
    loadAll(w, region);

    int32_t trees = 0;
    int32_t checked = 0;
    for (const core::ChunkCoord c : region) {
        const worldgen::FeaturePlan* plan = w.getDecorationPipeline().findPlan(c);
        ASSERT_NE(plan, nullptr) << c.x << "," << c.z;
        for (const worldgen::Feature& f : *plan) {
            if (!worldgen::isTreeFeature(f.type)) continue;
            ++trees;
            worldgen::forEachFeatureBlock(f, [&](core::BlockPos p, BlockId proposed) {
                const core::ChunkCoord owner{core::floorDiv(p.x, Chunk::WIDTH), core::floorDiv(p.z, Chunk::DEPTH)};
                if (!w.isChunkDecorated(owner) || proposed == BlockId::Dirt) return;
                ++checked;
                // Decoration never overwrites terrain, but never leaves a planned voxel empty.
                EXPECT_NE(w.getBlock(p), BlockId::Air) << p.x << "," << p.y << "," << p.z;
            });
        }
    }
    EXPECT_GT(trees, 0);
    EXPECT_GT(checked, 0);
}

TEST_F(P2WorldgenDecoration, DecoratedChunksAreRelit) {
    test::TempDir dir;
    world::World w(kSeed, dir.path());
    loadAll(w, squareRegion({0, 0}, 2));

    int32_t logs = 0;
    for (const core::ChunkCoord c : squareRegion({0, 0}, 1)) {
        Chunk* chunk = w.getChunkMutable(c);
        ASSERT_NE(chunk, nullptr);
        for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz) {
            for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
                int32_t topLog = -1;
                for (int32_t y = 0; y < Chunk::HEIGHT; ++y) {
                    if (!isLogBlock(chunk->getBlock(lx, y, lz))) continue;
                    ++logs;
                    topLog = y;
                    // Logs are opaque (opacity 15): lit as air they would glow at sky level 15.
                    EXPECT_EQ(chunk->getSkyLight(lx, y, lz), 0) << c.x << "," << c.z << " " << lx << "," << y << "," << lz;
                }
                if (topLog >= 0) {
                    EXPECT_GE(chunk->getHeightAt(lx, lz), topLog);
                }
            }
        }
    }
    EXPECT_GT(logs, 0);
}

TEST_F(P2WorldgenDecoration, UnloadingForgetsDecorationStateAndRegeneratesIdentically) {
    const std::vector<core::ChunkCoord> region = squareRegion({0, 0}, 2);
    test::TempDir dir, refDir;
    world::World w(kSeed, dir.path());
    loadAll(w, region);
    ASSERT_GT(w.getDecorationPipeline().plannedChunkCount(), 0U);

    // Stream far away: every chunk of the region is evicted.
    w.updateChunksAround(glm::vec3(4000.0f, 70.0f, 4000.0f), 1, 64);
    for (const core::ChunkCoord c : region) {
        ASSERT_EQ(w.getChunk(c), nullptr);
        EXPECT_FALSE(w.getDecorationPipeline().hasPlan(c)) << c.x << "," << c.z;
    }
    // Unmodified chunks regenerate to the same pass-1 terrain and plan, so World keeps no
    // per-chunk plan copies for them: memory stays bounded however far the player travels.
    EXPECT_EQ(w.getDecorationPipeline().retainedPlanCount(), 0U);

    loadAll(w, region);
    world::World ref(kSeed, refDir.path());
    loadAll(ref, region);
    for (const core::ChunkCoord c : squareRegion({0, 0}, 1)) {
        ASSERT_TRUE(w.isChunkDecorated(c));
        EXPECT_EQ(firstBlockDifference(*w.getChunk(c), *ref.getChunk(c)), -1) << c.x << "," << c.z;
    }
}

TEST_F(P2WorldgenDecoration, SpawnIsSearchedOnDecoratedGround) {
    test::TempDir dir;
    world::World w(kSeed, dir.path());
    const glm::vec3 spawn = w.findSafeSpawnPosition();
    const int32_t wx = static_cast<int32_t>(std::floor(spawn.x));
    const int32_t wz = static_cast<int32_t>(std::floor(spawn.z));
    // A tree planted after the search would bury the player in leaves.
    EXPECT_TRUE(w.isChunkDecorated({core::floorDiv(wx, Chunk::WIDTH), core::floorDiv(wz, Chunk::DEPTH)}));
}

TEST_F(P2WorldgenDecoration, CompletingANeighbourhoodRemeshesTheDecoratedChunkAndItsNeighbours) {
    test::TempDir dir;
    world::World w(kSeed, dir.path());
    std::vector<core::ChunkCoord> area = squareRegion({0, 0}, 1);
    area.erase(std::remove(area.begin(), area.end(), core::ChunkCoord{1, 1}), area.end());
    loadAll(w, area);
    for (const core::ChunkCoord c : area) w.getChunkMutable(c)->setMeshDirty(false); // "uploaded"

    (void)w.ensureChunkLoaded({1, 1}); // diagonal of (0,0): completes its 3x3
    ASSERT_TRUE(w.isChunkDecorated({0, 0}));
    // (0,0) changed, and its border blocks feed the face culling / AO of every neighbour.
    for (const core::ChunkCoord c : area) EXPECT_TRUE(w.getChunk(c)->isMeshDirty()) << c.x << "," << c.z;
}
