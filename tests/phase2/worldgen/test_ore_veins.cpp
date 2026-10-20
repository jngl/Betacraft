// S6.9: Beta 1.7.3 ore veins (WorldGenMinable) traced chunk by chunk from the 9-chunk
// neighbourhood: order independence, seamless borders, Beta's y ranges and frequencies, and
// ore only ever replacing stone.
#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"
#include "worldgen/OreVeins.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <map>
#include <memory>
#include <numbers>
#include <random>
#include <set>
#include <tuple>
#include <vector>

using namespace betacraft;
using voxel::BlockId;
using voxel::Chunk;
using worldgen::kBetaOreVeins;

namespace {

constexpr uint64_t SEED = 173173173ULL;

void fillStone(Chunk& chunk) {
    for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x) chunk.setBlock(x, y, z, BlockId::Stone, false);
}

[[nodiscard]] std::unique_ptr<Chunk> veinedStoneChunk(core::ChunkCoord coord, uint64_t seed = SEED) {
    auto chunk = std::make_unique<Chunk>(coord);
    fillStone(*chunk);
    worldgen::placeOreVeins(*chunk, seed);
    return chunk;
}

void expectSameBlocks(const Chunk& a, const Chunk& b) {
    for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x) ASSERT_EQ(a.getBlock(x, y, z), b.getBlock(x, y, z)) << x << "," << y << "," << z;
}

// Independent re-implementation of Beta WorldGenMinable with its own random source: the mean
// number of distinct cells one vein of `size` fills inside y in [0, 128), its centre y drawn
// like the spec's.
[[nodiscard]] double referenceVeinVolume(const worldgen::OreVeinSpec& spec, int32_t samples) {
    std::mt19937_64 gen(0xBE7A);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    const auto nextInt = [&](int32_t n) { return static_cast<int32_t>(uni(gen) * n); };
    const double n = spec.size;
    double total = 0.0;
    for (int32_t s = 0; s < samples; ++s) {
        const int32_t y = spec.lapisY ? nextInt(16) + nextInt(16) : nextInt(spec.yRange);
        const double f = uni(gen) * std::numbers::pi;
        const double x0 = std::sin(f) * n / 8.0;
        const double x1 = -std::sin(f) * n / 8.0;
        const double z0 = std::cos(f) * n / 8.0;
        const double z1 = -std::cos(f) * n / 8.0;
        const double y0 = y + nextInt(3) - 2;
        const double y1 = y + nextInt(3) - 2;
        std::set<std::tuple<int, int, int>> cells;
        for (int32_t l = 0; l <= spec.size; ++l) {
            const double cx = x0 + (x1 - x0) * l / n;
            const double cy = y0 + (y1 - y0) * l / n;
            const double cz = z0 + (z1 - z0) * l / n;
            const double d = uni(gen) * n / 16.0;
            const double r = ((std::sin(l * std::numbers::pi / n) + 1.0) * d + 1.0) / 2.0;
            for (int bx = static_cast<int>(std::floor(cx - r)); bx <= static_cast<int>(std::floor(cx + r)); ++bx)
                for (int by = static_cast<int>(std::floor(cy - r)); by <= static_cast<int>(std::floor(cy + r)); ++by)
                    for (int bz = static_cast<int>(std::floor(cz - r)); bz <= static_cast<int>(std::floor(cz + r)); ++bz) {
                        const double dx = (bx + 0.5 - cx) / r;
                        const double dy = (by + 0.5 - cy) / r;
                        const double dz = (bz + 0.5 - cz) / r;
                        if (dx * dx + dy * dy + dz * dz < 1.0 && by >= 0 && by < 128) cells.insert({bx, by, bz});
                    }
        }
        total += static_cast<double>(cells.size());
    }
    return total / samples;
}

} // namespace

TEST(OreVeins, TableIsBetas) {
    ASSERT_EQ(kBetaOreVeins.size(), 8U);
    const std::array<std::tuple<BlockId, int32_t, int32_t, int32_t>, 8> expected{{
        {BlockId::Dirt, 20, 32, 128},
        {BlockId::Gravel, 10, 32, 128},
        {BlockId::CoalOre, 20, 16, 128},
        {BlockId::IronOre, 20, 8, 64},
        {BlockId::GoldOre, 2, 8, 32},
        {BlockId::RedstoneOre, 8, 7, 16},
        {BlockId::DiamondOre, 1, 7, 16},
        {BlockId::LapisOre, 1, 6, 16},
    }};
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(kBetaOreVeins[i].ore, std::get<0>(expected[i])) << i;
        EXPECT_EQ(kBetaOreVeins[i].count, std::get<1>(expected[i])) << i;
        EXPECT_EQ(kBetaOreVeins[i].size, std::get<2>(expected[i])) << i;
        EXPECT_EQ(kBetaOreVeins[i].yRange, std::get<3>(expected[i])) << i;
        EXPECT_EQ(kBetaOreVeins[i].lapisY, i == 7) << i;
    }
}

TEST(OreVeins, IndependentOfGenerationOrder) {
    // Same chunk generated first, last, or alone: identical blocks.
    std::vector<std::unique_ptr<Chunk>> forward;
    for (int32_t cz = -1; cz <= 1; ++cz)
        for (int32_t cx = -1; cx <= 1; ++cx) forward.push_back(veinedStoneChunk({cx, cz}));
    std::vector<std::unique_ptr<Chunk>> backward(9);
    for (int32_t i = 8; i >= 0; --i) backward[static_cast<size_t>(i)] = veinedStoneChunk({i % 3 - 1, i / 3 - 1});
    for (size_t i = 0; i < 9; ++i) expectSameBlocks(*forward[i], *backward[i]);
    expectSameBlocks(*forward[4], *veinedStoneChunk({0, 0}));
    // ...and a pure function of the seed.
    const auto other = veinedStoneChunk({0, 0}, SEED + 1);
    bool differs = false;
    for (int32_t y = 0; y < Chunk::HEIGHT && !differs; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH && !differs; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH && !differs; ++x) differs = other->getBlock(x, y, z) != forward[4]->getBlock(x, y, z);
    EXPECT_TRUE(differs);
}

TEST(OreVeins, ContinuousAcrossChunkBorders) {
    // Every cell of the veins chunk (0,0) owns is ore in whichever chunk it falls into,
    // including the cells that spill over into its neighbours.
    std::map<std::pair<int32_t, int32_t>, std::unique_ptr<Chunk>> region;
    for (int32_t cz = -1; cz <= 1; ++cz)
        for (int32_t cx = -1; cx <= 1; ++cx) region[{cx, cz}] = veinedStoneChunk({cx, cz});
    size_t inside = 0;
    size_t spilled = 0;
    worldgen::forEachOreVeinCell(SEED, {0, 0}, [&](BlockId ore, int32_t wx, int32_t wy, int32_t wz) {
        if (wy < 0 || wy >= Chunk::HEIGHT) return;
        const int32_t cx = core::floorDiv(wx, Chunk::WIDTH);
        const int32_t cz = core::floorDiv(wz, Chunk::DEPTH);
        ASSERT_GE(cx, -1);
        ASSERT_LE(cx, 1);
        ASSERT_GE(cz, -1);
        ASSERT_LE(cz, 1);
        const BlockId got = region[{cx, cz}]->getBlock(core::floorMod(wx, Chunk::WIDTH), wy, core::floorMod(wz, Chunk::DEPTH));
        ASSERT_NE(got, BlockId::Stone) << "vein cell left as stone at " << wx << "," << wy << "," << wz;
        (void)ore;
        ++((cx == 0 && cz == 0) ? inside : spilled);
    });
    EXPECT_GT(inside, 0U);
    EXPECT_GT(spilled, 0U) << "the fixture must have veins crossing a border";
}

TEST(OreVeins, VeinsReachAtMostSevenBlocksOutOfTheirChunk) {
    for (int32_t cx = -3; cx <= 3; ++cx) {
        worldgen::forEachOreVeinCell(SEED, {cx, 2}, [&](BlockId, int32_t wx, int32_t, int32_t wz) {
            EXPECT_GE(wx, cx * 16 - worldgen::kOreVeinReach);
            EXPECT_LT(wx, cx * 16 + 16 + worldgen::kOreVeinReach);
            EXPECT_GE(wz, 2 * 16 - worldgen::kOreVeinReach);
            EXPECT_LT(wz, 2 * 16 + 16 + worldgen::kOreVeinReach);
        });
    }
}

TEST(OreVeins, NoDiamondAboveY15AndBetaDepthsForTheRest) {
    std::map<BlockId, int32_t> maxY;
    for (int32_t cz = -4; cz < 4; ++cz)
        for (int32_t cx = -4; cx < 4; ++cx) {
            const auto chunk = veinedStoneChunk({cx, cz});
            for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
                for (int32_t z = 0; z < Chunk::DEPTH; ++z)
                    for (int32_t x = 0; x < Chunk::WIDTH; ++x) {
                        const BlockId b = chunk->getBlock(x, y, z);
                        if (b != BlockId::Stone) maxY[b] = std::max(maxY[b], y);
                    }
        }
    ASSERT_TRUE(maxY.contains(BlockId::DiamondOre));
    EXPECT_LE(maxY[BlockId::DiamondOre], 15);
    EXPECT_LE(maxY[BlockId::RedstoneOre], 15);
    EXPECT_LE(maxY[BlockId::GoldOre], 32);
    EXPECT_LE(maxY[BlockId::IronOre], 64);
    EXPECT_LE(maxY[BlockId::LapisOre], 31);
    EXPECT_GT(maxY[BlockId::CoalOre], 64) << "coal spans the whole height";
}

TEST(OreVeins, OreOnlyReplacesStone) {
    // A chunk of mixed layers: only the stone may change.
    Chunk original(1, -2);
    const std::array<BlockId, 6> layers{BlockId::Stone, BlockId::Air, BlockId::Water, BlockId::Bedrock, BlockId::Sand,
                                        BlockId::Cobblestone};
    for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x)
                original.setBlock(x, y, z, layers[static_cast<size_t>((x + 3 * z + 7 * y) % 9 < 4 ? 0 : (x + y + z) % 6)],
                                  false);
    Chunk veined = original;
    worldgen::placeOreVeins(veined, SEED);
    size_t changed = 0;
    for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x) {
                const BlockId before = original.getBlock(x, y, z);
                const BlockId after = veined.getBlock(x, y, z);
                if (before != after) {
                    ++changed;
                    ASSERT_EQ(before, BlockId::Stone) << x << "," << y << "," << z;
                }
            }
    EXPECT_GT(changed, 0U);
}

TEST(OreVeins, FrequenciesWithinThirtyPercentOfBeta) {
    constexpr int32_t kSide = 12; // 144 chunks of solid stone
    std::map<BlockId, double> counted;
    for (int32_t cz = 0; cz < kSide; ++cz)
        for (int32_t cx = 0; cx < kSide; ++cx) {
            const auto chunk = veinedStoneChunk({cx, cz});
            for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
                for (int32_t z = 0; z < Chunk::DEPTH; ++z)
                    for (int32_t x = 0; x < Chunk::WIDTH; ++x) counted[chunk->getBlock(x, y, z)] += 1.0;
        }
    for (const worldgen::OreVeinSpec& spec : kBetaOreVeins) {
        const double beta = spec.count * referenceVeinVolume(spec, 4000);
        const double got = counted[spec.ore] / (kSide * kSide);
        EXPECT_NEAR(got, beta, 0.3 * beta) << "ore " << static_cast<int>(spec.ore) << ": " << got << " per chunk, Beta "
                                           << beta;
    }
}
