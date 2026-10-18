// P3.3 integration hooks - column sampling adapters used by WorldGen to feed the carver.
//   * FunctionColumnSampler: wraps WorldGen's analytic column function.
//   * ChunkColumnCache: samples every column the carver needs for one chunk exactly once
//     (chunk + 1-block face ring), so resolveColumn's 5 lookups per column cost 1.25.
#include "worldgen/ColumnSampling.hpp"
#include "WorldgenTestSupport.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <map>
#include <type_traits>
#include <utility>

using namespace betacraft::worldgen;
using betacraft::core::ChunkCoord;
using betacraft::world::BlockId;
using betacraft::world::Chunk;

namespace {

constexpr uint64_t SEED = 173173173ULL;

// Counts how many times each column is requested from the underlying source.
class CountingSampler final : public ColumnSampler {
public:
    [[nodiscard]] ColumnContext sampleColumn(int32_t wx, int32_t wz) const noexcept override {
        ++m_calls[{wx, wz}];
        ++m_total;
        return m_terrain.sampleColumn(wx, wz);
    }
    [[nodiscard]] int total() const noexcept { return m_total; }
    [[nodiscard]] int callsFor(int32_t wx, int32_t wz) const {
        const auto it = m_calls.find({wx, wz});
        return it == m_calls.end() ? 0 : it->second;
    }
    void reset() noexcept {
        m_calls.clear();
        m_total = 0;
    }

private:
    test::SyntheticTerrain m_terrain;
    mutable std::map<std::pair<int32_t, int32_t>, int> m_calls;
    mutable int m_total{0};
};

bool sameContext(const ColumnContext& a, const ColumnContext& b) {
    return a.surfaceHeight == b.surfaceHeight && a.waterFloorHeight == b.waterFloorHeight &&
           a.isWaterBody == b.isWaterBody;
}

} // namespace

// ---------------------------------------------------------------- FunctionColumnSampler

TEST(FunctionColumnSampler, ForwardsCoordinatesToTheCallable) {
    const FunctionColumnSampler sampler([](int32_t wx, int32_t wz) noexcept {
        return ColumnContext{wx + 100, wz - 7, (wx + wz) % 2 == 0};
    });
    const ColumnContext c = sampler.sampleColumn(5, -3);
    EXPECT_EQ(c.surfaceHeight, 105);
    EXPECT_EQ(c.waterFloorHeight, -10);
    EXPECT_TRUE(c.isWaterBody);
    EXPECT_FALSE(sampler.sampleColumn(6, -3).isWaterBody);
}

TEST(FunctionColumnSampler, DrivesTheCarverLikeAHandWrittenSampler) {
    const test::SyntheticTerrain terrain;
    const FunctionColumnSampler fn(
        [&terrain](int32_t wx, int32_t wz) noexcept { return terrain.sampleColumn(wx, wz); });
    const CaveCarver carver(SEED);
    Chunk viaTerrain(-1, 2);
    Chunk viaFunction(-1, 2);
    terrain.fillChunk(viaTerrain);
    terrain.fillChunk(viaFunction);
    const CarveStats a = carver.carveChunk(viaTerrain, terrain);
    const CarveStats b = carver.carveChunk(viaFunction, fn);
    EXPECT_GT(a.total(), 0U);
    EXPECT_EQ(a.total(), b.total());
    for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x)
                ASSERT_EQ(viaTerrain.getBlock(x, y, z), viaFunction.getBlock(x, y, z));
}

// ---------------------------------------------------------------- ChunkColumnCache

TEST(ChunkColumnCache, ContainsTheChunkPlusItsFaceRingButNotTheCorners) {
    const test::SyntheticTerrain terrain;
    const ChunkColumnCache cache(terrain, ChunkCoord{2, -1}); // world x 32..47, z -16..-1
    EXPECT_EQ(cache.getCoord(), (ChunkCoord{2, -1}));
    EXPECT_TRUE(cache.contains(32, -16));
    EXPECT_TRUE(cache.contains(47, -1));
    EXPECT_TRUE(cache.contains(31, -8));  // west ring
    EXPECT_TRUE(cache.contains(48, -8));  // east ring
    EXPECT_TRUE(cache.contains(40, -17)); // north ring
    EXPECT_TRUE(cache.contains(40, 0));   // south ring
    EXPECT_FALSE(cache.contains(31, -17)); // corners are never read by resolveColumn
    EXPECT_FALSE(cache.contains(48, 0));
    EXPECT_FALSE(cache.contains(30, -8));
    EXPECT_FALSE(cache.contains(40, 1));
}

TEST(ChunkColumnCache, SamplesEveryCachedColumnExactlyOnceUpFront) {
    CountingSampler source;
    const ChunkColumnCache cache(source, ChunkCoord{-3, 4});
    EXPECT_EQ(source.total(), 16 * 16 + 4 * 16);
    const int32_t bx = -3 * Chunk::WIDTH;
    const int32_t bz = 4 * Chunk::DEPTH;
    for (int32_t z = bz - 1; z <= bz + 16; ++z)
        for (int32_t x = bx - 1; x <= bx + 16; ++x)
            ASSERT_EQ(source.callsFor(x, z), cache.contains(x, z) ? 1 : 0) << x << "," << z;
}

TEST(ChunkColumnCache, ReturnsExactlyWhatTheSourceReturns) {
    const test::SyntheticTerrain terrain;
    for (ChunkCoord cc : {ChunkCoord{0, 0}, ChunkCoord{-1, -1}, ChunkCoord{7, -9}}) {
        const ChunkColumnCache cache(terrain, cc);
        const int32_t bx = cc.x * Chunk::WIDTH;
        const int32_t bz = cc.z * Chunk::DEPTH;
        for (int32_t z = bz - 3; z <= bz + 18; ++z)
            for (int32_t x = bx - 3; x <= bx + 18; ++x)
                ASSERT_TRUE(sameContext(cache.sampleColumn(x, z), terrain.sampleColumn(x, z))) << x << "," << z;
    }
}

TEST(ChunkColumnCache, ColumnsOutsideTheCacheFallBackToTheSource) {
    CountingSampler source;
    const ChunkColumnCache cache(source, ChunkCoord{0, 0});
    source.reset();
    (void)cache.sampleColumn(5, 5);   // cached
    (void)cache.sampleColumn(-1, -1); // corner: delegated
    (void)cache.sampleColumn(100, 3); // far away: delegated
    EXPECT_EQ(source.total(), 2);
    EXPECT_EQ(source.callsFor(-1, -1), 1);
    EXPECT_EQ(source.callsFor(100, 3), 1);
}

TEST(ChunkColumnCache, CarvingThroughTheCacheNeverHitsTheSourceAndIsIdentical) {
    const test::SyntheticTerrain terrain;
    CountingSampler source;
    const CaveCarver carver(SEED);
    for (ChunkCoord cc : {ChunkCoord{0, 0}, ChunkCoord{-1, 0}, ChunkCoord{3, -2}}) {
        Chunk direct(cc);
        Chunk cached(cc);
        terrain.fillChunk(direct);
        terrain.fillChunk(cached);
        (void)carver.carveChunk(direct, terrain);

        const ChunkColumnCache cache(source, cc);
        source.reset();
        (void)carver.carveChunk(cached, cache);
        EXPECT_EQ(source.total(), 0) << "carver read a column outside the cache";
        for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
            for (int32_t z = 0; z < Chunk::DEPTH; ++z)
                for (int32_t x = 0; x < Chunk::WIDTH; ++x)
                    ASSERT_EQ(direct.getBlock(x, y, z), cached.getBlock(x, y, z));
    }
}

TEST(ChunkColumnCache, HandlesExtremeChunkCoordinates) {
    const FunctionColumnSampler fn([](int32_t wx, int32_t wz) noexcept {
        return ColumnContext{static_cast<int32_t>(static_cast<uint32_t>(wx ^ wz) % 100U), 0, false};
    });
    constexpr int32_t far = 100'000'000; // ~1.6e9 blocks: still inside int32 world space
    const ChunkColumnCache cache(fn, ChunkCoord{far, -far});
    const int32_t bx = far * Chunk::WIDTH;
    const int32_t bz = -far * Chunk::DEPTH;
    EXPECT_TRUE(cache.contains(bx, bz));
    EXPECT_EQ(cache.sampleColumn(bx + 15, bz + 15).surfaceHeight, fn.sampleColumn(bx + 15, bz + 15).surfaceHeight);
    EXPECT_EQ(cache.sampleColumn(bx - 1, bz + 3).surfaceHeight, fn.sampleColumn(bx - 1, bz + 3).surfaceHeight);
}

// The cache keeps a reference to its source and falls back to it outside the ring: binding a
// temporary sampler would dangle on the first out-of-ring query, so it must not compile.
TEST(ChunkColumnCache, RefusesATemporarySourceAtCompileTime) {
    using Lambda = decltype([](int32_t, int32_t) noexcept { return ColumnContext{}; });
    using Sampler = FunctionColumnSampler<Lambda>;
    EXPECT_TRUE((std::is_constructible_v<ChunkColumnCache, const Sampler&, ChunkCoord>));
    EXPECT_TRUE((std::is_constructible_v<ChunkColumnCache, Sampler&, ChunkCoord>));
    EXPECT_FALSE((std::is_constructible_v<ChunkColumnCache, Sampler&&, ChunkCoord>));
    EXPECT_FALSE((std::is_constructible_v<ChunkColumnCache, Sampler, ChunkCoord>));
    EXPECT_FALSE((std::is_constructible_v<ChunkColumnCache, test::SyntheticTerrain&&, ChunkCoord>));
}
