// P3.3 - A ColumnSampler built from resident chunks (scanColumn) must give the same answer
// whether or not the scanned chunk has already been carved: carving writes aquifer water at
// Y 11..32, which must never be mistaken for a sea and lower a neighbour's aquitard ceiling.
#include "worldgen/CaveCarver.hpp"
#include "worldgen/ColumnSampling.hpp"
#include "WorldgenTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <vector>

using namespace betacraft::worldgen;
using betacraft::core::ChunkCoord;
using betacraft::core::floorDiv;
using betacraft::core::floorMod;
using betacraft::voxel::BlockId;
using betacraft::voxel::Chunk;

namespace {

constexpr uint64_t SEED = 173173173ULL;

// 4x4 resident chunks, pass-1 synthetic terrain.
struct World {
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> chunks;
    test::SyntheticTerrain analytic;

    World() {
        for (int32_t cz = -2; cz <= 1; ++cz)
            for (int32_t cx = -2; cx <= 1; ++cx) {
                auto c = std::make_unique<Chunk>(cx, cz);
                analytic.fillChunk(*c);
                chunks.emplace(ChunkCoord{cx, cz}, std::move(c));
            }
    }

    // What an integrator would naturally write: scan the resident chunk, fall back to the
    // analytic function for columns of chunks that are not resident.
    [[nodiscard]] ColumnContext scan(int32_t wx, int32_t wz) const noexcept {
        const auto it = chunks.find(ChunkCoord{floorDiv(wx, Chunk::WIDTH), floorDiv(wz, Chunk::DEPTH)});
        if (it == chunks.end()) return analytic.sampleColumn(wx, wz);
        return CaveCarver::scanColumn(*it->second, floorMod(wx, Chunk::WIDTH), floorMod(wz, Chunk::DEPTH));
    }
};

std::vector<ChunkCoord> allCoords() {
    std::vector<ChunkCoord> out;
    for (int32_t cz = -2; cz <= 1; ++cz)
        for (int32_t cx = -2; cx <= 1; ++cx) out.push_back(ChunkCoord{cx, cz});
    return out;
}

bool sameColumn(const ColumnContext& a, const ColumnContext& b) {
    return a.surfaceHeight == b.surfaceHeight && a.waterFloorHeight == b.waterFloorHeight &&
           a.isWaterBody == b.isWaterBody;
}

} // namespace

TEST(CaveChunkSampler, ScanColumnIsInvariantUnderCarving) {
    const CaveCarver carver(SEED);
    test::SyntheticTerrain terrain;
    uint32_t water = 0;
    for (ChunkCoord cc : allCoords()) {
        Chunk before(cc);
        Chunk after(cc);
        terrain.fillChunk(before);
        terrain.fillChunk(after);
        water += carver.carveChunk(after, terrain).water;
        for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz)
            for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
                const ColumnContext b = CaveCarver::scanColumn(before, lx, lz);
                const ColumnContext a = CaveCarver::scanColumn(after, lx, lz);
                ASSERT_TRUE(sameColumn(a, b))
                    << "chunk " << cc.x << "," << cc.z << " col " << lx << "," << lz << ": before {" << b.surfaceHeight
                    << "," << b.waterFloorHeight << "," << b.isWaterBody << "} after {" << a.surfaceHeight << ","
                    << a.waterFloorHeight << "," << a.isWaterBody << "}";
            }
    }
    EXPECT_GT(water, 0U) << "fixture must contain aquifer water";
}

TEST(CaveChunkSampler, ChunkBackedSamplerIsGenerationOrderIndependent) {
    const CaveCarver carver(SEED);
    // Reference: every chunk carved against the analytic pass-1 function.
    World reference;
    for (ChunkCoord cc : allCoords()) (void)carver.carveChunk(*reference.chunks.at(cc), reference.analytic);

    struct Run {
        std::array<size_t, 16> order;
        bool cached; // through ChunkColumnCache, or a live scan of the chunk being carved
    };
    const std::array<Run, 2> runs{{
        {{15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0}, false},
        {{5, 10, 0, 15, 6, 9, 3, 12, 1, 14, 7, 8, 2, 13, 4, 11}, true},
    }};
    const std::vector<ChunkCoord> coords = allCoords();
    for (const Run& run : runs) {
        const auto& order = run.order;
        const bool cached = run.cached;
        {
            World w;
            const auto sampler = FunctionColumnSampler([&w](int32_t wx, int32_t wz) noexcept { return w.scan(wx, wz); });
            for (size_t idx : order) {
                Chunk& c = *w.chunks.at(coords[idx]);
                if (cached) {
                    const ChunkColumnCache cache(sampler, c.getCoord());
                    (void)carver.carveChunk(c, cache);
                } else {
                    (void)carver.carveChunk(c, sampler); // live scan of a chunk being carved
                }
            }
            for (ChunkCoord cc : coords) {
                const Chunk& got = *w.chunks.at(cc);
                const Chunk& want = *reference.chunks.at(cc);
                for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
                    for (int32_t z = 0; z < Chunk::DEPTH; ++z)
                        for (int32_t x = 0; x < Chunk::WIDTH; ++x)
                            ASSERT_EQ(got.getBlock(x, y, z), want.getBlock(x, y, z))
                                << "cached=" << cached << " chunk " << cc.x << "," << cc.z << " at " << x << "," << y
                                << "," << z;
            }
        }
    }
}

// Real pass-1 water bodies (oceans, also under a stone overhang) are still recognised.
TEST(CaveChunkSampler, SeaWaterAboveTheAquiferBandIsStillAWaterBody) {
    Chunk c(0, 0);
    for (int32_t y = 0; y <= 40; ++y) c.setBlock(3, y, 3, BlockId::Stone, false);
    for (int32_t y = 41; y <= test::SEA; ++y) c.setBlock(3, y, 3, BlockId::Water, false);
    const ColumnContext ctx = CaveCarver::scanColumn(c, 3, 3);
    EXPECT_TRUE(ctx.isWaterBody);
    EXPECT_EQ(ctx.waterFloorHeight, 40);
    // Deep sea whose bed lies inside the aquifer band: the body reaches above the band.
    Chunk d(0, 0);
    for (int32_t y = 0; y <= 20; ++y) d.setBlock(5, y, 5, BlockId::Stone, false);
    for (int32_t y = 21; y <= test::SEA; ++y) d.setBlock(5, y, 5, BlockId::Water, false);
    const ColumnContext deep = CaveCarver::scanColumn(d, 5, 5);
    EXPECT_TRUE(deep.isWaterBody);
    EXPECT_EQ(deep.waterFloorHeight, 20);
}

// Water entirely inside the aquifer band under land is aquifer output, not a sea.
TEST(CaveChunkSampler, BuriedWaterInsideTheAquiferBandIsNotAWaterBody) {
    Chunk c(0, 0);
    for (int32_t y = 0; y <= 70; ++y) c.setBlock(2, y, 9, BlockId::Stone, false);
    for (int32_t y = CaveCarver::AQUIFER_MIN_Y; y <= CaveCarver::AQUIFER_MAX_Y; ++y)
        c.setBlock(2, y, 9, BlockId::Water, false);
    const ColumnContext ctx = CaveCarver::scanColumn(c, 2, 9);
    EXPECT_FALSE(ctx.isWaterBody);
    EXPECT_EQ(ctx.surfaceHeight, 70);
}
