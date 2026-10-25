// "sans couloirs vides": shoreline features (sugar cane) must also grow on chunk border
// columns whose only adjacent water lies in the neighbouring chunk. Planning reads only the
// planned chunk, so such columns get water-conditional features that pass 2 resolves against
// the (decoration-invariant) sea-level water of the 3x3 neighbourhood.
#include "worldgen/DecorationPipeline.hpp"
#include "worldgen/SeededRandom.hpp"
#include "WorldgenTestSupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace betacraft::worldgen;
using betacraft::core::ChunkCoord;
using betacraft::core::floorDiv;
using betacraft::core::floorMod;
using betacraft::voxel::BiomeId;
using betacraft::voxel::BlockId;
using betacraft::voxel::Chunk;

namespace {

constexpr uint64_t SEED = 173173173ULL;

// Marsh: grass at sea level everywhere except a 1-column water channel on every chunk's
// lx == 0 column. Columns at lx == 15 therefore see water only across the chunk border.
[[nodiscard]] bool marshWater(int32_t wx, int32_t) noexcept { return floorMod(wx, Chunk::WIDTH) == 0; }

void fillMarsh(Chunk& c) {
    for (int32_t z = 0; z < Chunk::DEPTH; ++z)
        for (int32_t x = 0; x < Chunk::WIDTH; ++x) {
            const int32_t wx = c.getCoord().x * Chunk::WIDTH + x;
            const int32_t wz = c.getCoord().z * Chunk::DEPTH + z;
            c.setColumnBiome(x, z, BiomeId::Swampland, glm::vec3(0.5f), glm::vec3(0.5f));
            c.setBlock(x, 0, z, BlockId::Bedrock, false);
            for (int32_t y = 1; y < test::SEA; ++y) c.setBlock(x, y, z, BlockId::Dirt, false);
            c.setBlock(x, test::SEA, z, marshWater(wx, wz) ? BlockId::Water : BlockId::Grass, false);
        }
}

class MarshStore final : public ChunkAccess {
public:
    [[nodiscard]] Chunk* findChunk(ChunkCoord c) noexcept override {
        const auto it = m_chunks.find(c);
        return it == m_chunks.end() ? nullptr : it->second.get();
    }
    void generate(ChunkCoord c) {
        auto chunk = std::make_unique<Chunk>(c);
        fillMarsh(*chunk);
        m_chunks[c] = std::move(chunk);
    }
    void erase(ChunkCoord c) { m_chunks.erase(c); }
    [[nodiscard]] BlockId block(int32_t wx, int32_t y, int32_t wz) const {
        const auto it = m_chunks.find(ChunkCoord{floorDiv(wx, Chunk::WIDTH), floorDiv(wz, Chunk::DEPTH)});
        if (it == m_chunks.end()) return BlockId::Air;
        return it->second->getBlock(floorMod(wx, Chunk::WIDTH), y, floorMod(wz, Chunk::DEPTH));
    }

private:
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> m_chunks;
};

std::vector<ChunkCoord> grid(int32_t lo, int32_t hi) {
    std::vector<ChunkCoord> out;
    for (int32_t z = lo; z <= hi; ++z)
        for (int32_t x = lo; x <= hi; ++x) out.push_back(ChunkCoord{x, z});
    return out;
}

struct MarshWorld {
    MarshStore store;
    DecorationPipeline pipeline{SEED, store};
    explicit MarshWorld(const std::vector<ChunkCoord>& order) {
        for (ChunkCoord c : order) {
            store.generate(c);
            (void)pipeline.onTerrainReady(c);
        }
    }
};

bool waterBeside(const MarshStore& s, int32_t wx, int32_t wz) {
    return s.block(wx + 1, test::SEA, wz) == BlockId::Water || s.block(wx - 1, test::SEA, wz) == BlockId::Water ||
           s.block(wx, test::SEA, wz + 1) == BlockId::Water || s.block(wx, test::SEA, wz - 1) == BlockId::Water;
}

} // namespace

TEST(FeaturePlanner, BorderShoreColumnsGetWaterConditionalSugarCane) {
    const FeaturePlanner planner(SEED);
    int conditionalCanes = 0;
    for (int32_t cx = -4; cx < 4; ++cx)
        for (int32_t cz = -2; cz < 2; ++cz) {
            Chunk c(cx, cz);
            fillMarsh(c);
            for (const Feature& f : planner.plan(c)) {
                const int32_t lx = floorMod(f.origin.x, Chunk::WIDTH);
                const int32_t lz = floorMod(f.origin.z, Chunk::DEPTH);
                const bool border = lx == 0 || lx == Chunk::WIDTH - 1 || lz == 0 || lz == Chunk::DEPTH - 1;
                if (f.condition != FeatureCondition::None) {
                    ASSERT_TRUE(border) << "only border columns have out-of-chunk water neighbours";
                }
                if (f.type == FeatureType::SugarCane && f.condition == FeatureCondition::AdjacentWater) {
                    ++conditionalCanes;
                }
            }
        }
    EXPECT_GT(conditionalCanes, 0);
}

TEST(FeatureCondition, IsResolvedFromTheFourFaceNeighboursAtSurfaceLevel) {
    Feature f{FeatureType::SugarCane, betacraft::core::BlockPos{10, test::SEA + 1, -3}, 2, 0U};
    f.condition = FeatureCondition::AdjacentWater;
    int probes = 0;
    auto none = [&](int32_t, int32_t y, int32_t) {
        ++probes;
        EXPECT_EQ(y, test::SEA);
        return false;
    };
    EXPECT_FALSE(featureConditionHolds(f, none));
    EXPECT_EQ(probes, 4);
    auto east = [](int32_t x, int32_t, int32_t z) { return x == 11 && z == -3; };
    EXPECT_TRUE(featureConditionHolds(f, east));
    f.condition = FeatureCondition::NoAdjacentWater;
    EXPECT_FALSE(featureConditionHolds(f, east));
    EXPECT_TRUE(featureConditionHolds(f, none));
    f.condition = FeatureCondition::None;
    EXPECT_TRUE(featureConditionHolds(f, east));
    EXPECT_TRUE(featureConditionHolds(f, none));
}

TEST(DecorationPipelineShore, SugarCaneGrowsOnBorderColumnsBesideWaterInTheNextChunk) {
    MarshWorld w(grid(-2, 2));
    int borderCanes = 0;
    int canes = 0;
    for (ChunkCoord cc : grid(-1, 1)) {
        ASSERT_TRUE(w.pipeline.isDecorated(cc));
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x) {
                const int32_t wx = cc.x * Chunk::WIDTH + x;
                const int32_t wz = cc.z * Chunk::DEPTH + z;
                const BlockId above = w.store.block(wx, test::SEA + 1, wz);
                if (above != BlockId::SugarCane) continue;
                ++canes;
                ASSERT_TRUE(waterBeside(w.store, wx, wz)) << "cane without water at " << wx << "," << wz;
                if (x == Chunk::WIDTH - 1) ++borderCanes;
            }
    }
    EXPECT_GT(canes, 0);
    EXPECT_GT(borderCanes, 0) << "border shore columns must not stay cane-free";
}

TEST(DecorationPipelineShore, UnmetConditionsFallBackToTheOrdinaryFlora) {
    // A border column without water anywhere must look exactly like an interior one: never a
    // cane, and its ordinary flora (conditional on "no water") does appear somewhere.
    MarshWorld w(grid(-2, 2));
    int fallbackFeatures = 0;
    for (ChunkCoord cc : grid(-1, 1)) {
        for (const Feature& f : *w.pipeline.findPlan(cc)) {
            if (f.condition != FeatureCondition::NoAdjacentWater) continue;
            if (waterBeside(w.store, f.origin.x, f.origin.z)) continue;
            ++fallbackFeatures;
            const BlockId got = w.store.block(f.origin.x, f.origin.y, f.origin.z);
            EXPECT_NE(got, BlockId::SugarCane);
            EXPECT_NE(got, BlockId::Air) << "fallback feature missing at " << f.origin.x << "," << f.origin.z;
        }
    }
    EXPECT_GT(fallbackFeatures, 0) << "fixture must exercise the fallback branch";
}

TEST(DecorationPipelineShore, ResultIsIdenticalForPermutedLoadOrders) {
    const MarshWorld reference(grid(-2, 2));
    for (uint64_t s : {3ULL, 17ULL, 0xBEEFULL}) {
        std::vector<ChunkCoord> order = grid(-2, 2);
        SeededRandom rng(s);
        for (size_t i = order.size() - 1; i > 0; --i) std::swap(order[i], order[rng.nextBounded(static_cast<uint32_t>(i + 1))]);
        const MarshWorld w(order);
        for (ChunkCoord cc : grid(-1, 1))
            for (int32_t z = 0; z < Chunk::DEPTH; ++z)
                for (int32_t x = 0; x < Chunk::WIDTH; ++x)
                    for (int32_t y = test::SEA; y < test::SEA + 10; ++y) {
                        const int32_t wx = cc.x * Chunk::WIDTH + x;
                        const int32_t wz = cc.z * Chunk::DEPTH + z;
                        ASSERT_EQ(w.store.block(wx, y, wz), reference.store.block(wx, y, wz))
                            << "seed " << s << " at " << wx << "," << y << "," << wz;
                    }
    }
}

// A conditional feature needs its neighbours' water; if one of the 3x3 chunks is registered
// but not resident any more, decoration is deferred instead of guessing.
TEST(DecorationPipelineShore, DecorationWaitsWhileAProbedNeighbourIsNotResident) {
    MarshStore store;
    DecorationPipeline pipeline(SEED, store);
    for (ChunkCoord c : grid(-1, 1)) {
        if (c == ChunkCoord{1, 0} || c == ChunkCoord{0, 0}) continue;
        store.generate(c);
        (void)pipeline.onTerrainReady(c);
    }
    store.generate(ChunkCoord{1, 0});
    (void)pipeline.onTerrainReady(ChunkCoord{1, 0});
    store.erase(ChunkCoord{1, 0}); // evicted without notification
    store.generate(ChunkCoord{0, 0});
    const auto done = pipeline.onTerrainReady(ChunkCoord{0, 0});
    // (0,0)'s lx == 15 columns probe (1,0): it must not be decorated with a guess.
    EXPECT_TRUE(std::find(done.begin(), done.end(), ChunkCoord{0, 0}) == done.end());
    EXPECT_FALSE(pipeline.isDecorated(ChunkCoord{0, 0}));
}
