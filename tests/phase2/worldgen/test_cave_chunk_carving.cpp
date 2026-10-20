// P3.3 - Carving real Chunk data: sanctuaries, seamless borders, order independence.
#include "worldgen/CaveCarver.hpp"
#include "WorldgenTestSupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

using namespace betacraft::worldgen;
using betacraft::core::ChunkCoord;
using betacraft::core::floorDiv;
using betacraft::core::floorMod;
using betacraft::voxel::BlockId;
using betacraft::voxel::Chunk;

namespace {

constexpr uint64_t SEED = 173173173ULL;

// A 3x3 block of chunks centred on (0,0), spanning negative coordinates on purpose.
struct Region {
    std::vector<std::unique_ptr<Chunk>> chunks;

    Region() {
        test::SyntheticTerrain terrain;
        for (int32_t cz = -1; cz <= 1; ++cz)
            for (int32_t cx = -1; cx <= 1; ++cx) {
                chunks.push_back(std::make_unique<Chunk>(cx, cz));
                terrain.fillChunk(*chunks.back());
            }
    }

    [[nodiscard]] Chunk& at(int32_t cx, int32_t cz) const {
        return *chunks[static_cast<size_t>((cz + 1) * 3 + (cx + 1))];
    }

    [[nodiscard]] BlockId block(int32_t wx, int32_t wy, int32_t wz) const {
        const int32_t cx = floorDiv(wx, Chunk::WIDTH);
        const int32_t cz = floorDiv(wz, Chunk::DEPTH);
        if (cx < -1 || cx > 1 || cz < -1 || cz > 1) return BlockId::Air;
        return at(cx, cz).getBlock(floorMod(wx, Chunk::WIDTH), wy, floorMod(wz, Chunk::DEPTH));
    }

    static constexpr int32_t MIN_W = -16;
    static constexpr int32_t MAX_W = 31; // inclusive
};

void expectSameBlocks(const Chunk& a, const Chunk& b) {
    for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x)
                ASSERT_EQ(a.getBlock(x, y, z), b.getBlock(x, y, z))
                    << "chunk(" << a.getCoord().x << "," << a.getCoord().z << ") at " << x << "," << y << "," << z;
}

} // namespace

TEST(CaveCarvable, OnlyNaturalStoneAndSoilAreCarvable) {
    for (BlockId b : {BlockId::Stone, BlockId::Dirt, BlockId::Grass, BlockId::SnowGrass, BlockId::Gravel,
                      BlockId::Sandstone, BlockId::Clay, BlockId::CoalOre, BlockId::IronOre, BlockId::GoldOre,
                      BlockId::LapisOre, BlockId::RedstoneOre, BlockId::DiamondOre}) {
        EXPECT_TRUE(CaveCarver::isCarvable(b)) << static_cast<int>(b);
    }
    for (BlockId b : {BlockId::Air, BlockId::Bedrock, BlockId::Water, BlockId::Ice, BlockId::Sand,
                      BlockId::OakLog, BlockId::Leaves, BlockId::Cobblestone, BlockId::OakPlanks,
                      BlockId::Torch, BlockId::Obsidian}) {
        EXPECT_FALSE(CaveCarver::isCarvable(b)) << static_cast<int>(b);
    }
}

TEST(CaveScanColumn, RecoversColumnContextFromPassOneChunk) {
    test::SyntheticTerrain terrain;
    for (ChunkCoord cc : {ChunkCoord{0, 0}, ChunkCoord{-3, 2}, ChunkCoord{5, -4}}) {
        Chunk chunk(cc);
        terrain.fillChunk(chunk);
        for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz)
            for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
                const ColumnContext expected =
                    terrain.sampleColumn(cc.x * Chunk::WIDTH + lx, cc.z * Chunk::DEPTH + lz);
                const ColumnContext got = CaveCarver::scanColumn(chunk, lx, lz);
                ASSERT_EQ(got.surfaceHeight, expected.surfaceHeight);
                ASSERT_EQ(got.isWaterBody, expected.isWaterBody);
                if (expected.isWaterBody) {
                    ASSERT_EQ(got.waterFloorHeight, expected.waterFloorHeight);
                }
            }
    }
}

TEST(CaveScanColumn, EmptyColumnIsDryAtZero) {
    const Chunk chunk(0, 0);
    const ColumnContext c = CaveCarver::scanColumn(chunk, 3, 4);
    EXPECT_EQ(c.surfaceHeight, 0);
    EXPECT_FALSE(c.isWaterBody);
}

TEST(CaveChunkCarving, SanctuariesHoldOnRealChunks) {
    const Region original;
    const Region carved;
    test::SyntheticTerrain terrain;
    const CaveCarver carver(SEED);
    CarveStats total{};
    for (auto& c : carved.chunks) {
        const CarveStats s = carver.carveChunk(*c, terrain);
        total.air += s.air;
        total.water += s.water;
        total.lava += s.lava;
    }
    ASSERT_GT(total.total(), 0U) << "fixture must actually contain caves";

    for (int32_t wx = Region::MIN_W; wx <= Region::MAX_W; ++wx) {
        for (int32_t wz = Region::MIN_W; wz <= Region::MAX_W; ++wz) {
            const ColumnContext col = terrain.sampleColumn(wx, wz);
            for (int32_t y = 0; y < Chunk::HEIGHT; ++y) {
                const BlockId before = original.block(wx, y, wz);
                const BlockId after = carved.block(wx, y, wz);
                // Bedrock layer Y <= 5 is never touched; bedrock blocks are never removed.
                if (y <= 5 || before == BlockId::Bedrock) {
                    ASSERT_EQ(after, before) << wx << "," << y << "," << wz;
                }
                // Only carvable blocks may change.
                if (after != before) {
                    ASSERT_TRUE(CaveCarver::isCarvable(before));
                }
                // Aquitard: >= 6 solid blocks kept directly under every water body.
                if (col.isWaterBody && y >= col.waterFloorHeight - 6 && y <= col.waterFloorHeight) {
                    ASSERT_EQ(after, before) << "vault breached at " << wx << "," << y << "," << wz;
                }
                // Land: 3-block soil cover kept under the surface.
                if (!col.isWaterBody && y >= col.surfaceHeight - 3) {
                    ASSERT_EQ(after, before);
                }
            }
        }
    }
}

TEST(CaveChunkCarving, NoExcavatedVoxelTouchesSeaWaterEvenAcrossColumns) {
    const Region original;
    const Region carved;
    test::SyntheticTerrain terrain;
    const CaveCarver carver(SEED);
    for (auto& c : carved.chunks) (void)carver.carveChunk(*c, terrain);

    constexpr std::array<std::array<int32_t, 3>, 6> dirs{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0},
                                                         {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    int excavated = 0;
    for (int32_t wx = Region::MIN_W + 1; wx < Region::MAX_W; ++wx)
        for (int32_t wz = Region::MIN_W + 1; wz < Region::MAX_W; ++wz)
            for (int32_t y = 1; y < Chunk::HEIGHT - 1; ++y) {
                if (carved.block(wx, y, wz) == original.block(wx, y, wz)) continue;
                ++excavated;
                for (const auto& d : dirs) {
                    ASSERT_NE(original.block(wx + d[0], y + d[1], wz + d[2]), BlockId::Water)
                        << "cave at " << wx << "," << y << "," << wz << " opens into the sea";
                }
            }
    EXPECT_GT(excavated, 0);
}

TEST(CaveChunkCarving, MatchesTheWorldSpaceFunctionVoxelForVoxel) {
    const Region original;
    const Region carved;
    test::SyntheticTerrain terrain;
    const CaveCarver carver(SEED);
    const CaveFillPalette palette{};
    for (auto& c : carved.chunks) (void)carver.carveChunk(*c, terrain, palette);

    for (int32_t wx = Region::MIN_W; wx <= Region::MAX_W; ++wx)
        for (int32_t wz = Region::MIN_W; wz <= Region::MAX_W; ++wz)
            for (int32_t y = 0; y < Chunk::HEIGHT; ++y) {
                const BlockId before = original.block(wx, y, wz);
                const bool expectCarve = CaveCarver::isCarvable(before) && carver.excavatesAt(wx, y, wz, terrain, palette);
                const BlockId expected = expectCarve ? palette.blockFor(carver.getExcavationFill(wx, y, wz)) : before;
                ASSERT_EQ(carved.block(wx, y, wz), expected) << wx << "," << y << "," << wz;
            }
}

TEST(CaveChunkCarving, IsIndependentOfGenerationOrderAndInstance) {
    test::SyntheticTerrain terrain;
    const Region reference;
    {
        const CaveCarver carver(SEED);
        for (auto& c : reference.chunks) (void)carver.carveChunk(*c, terrain);
    }
    const std::array<std::array<size_t, 9>, 3> orders{{
        {8, 7, 6, 5, 4, 3, 2, 1, 0},
        {4, 0, 8, 2, 6, 1, 7, 3, 5},
        {3, 5, 1, 7, 0, 8, 4, 6, 2},
    }};
    for (const auto& order : orders) {
        const Region permuted;
        const CaveCarver carver(SEED); // fresh instance: no hidden cross-chunk state
        for (size_t idx : order) (void)carver.carveChunk(*permuted.chunks[idx], terrain);
        for (size_t i = 0; i < 9; ++i) expectSameBlocks(*reference.chunks[i], *permuted.chunks[i]);
    }
}

TEST(CaveChunkCarving, TunnelsContinueAcrossChunkBorders) {
    // Every carved voxel on a chunk's border face whose world neighbour across the border
    // is predicted carved by the world function must be carved in the neighbour chunk too.
    const Region carved;
    test::SyntheticTerrain terrain;
    const CaveCarver carver(SEED);
    for (auto& c : carved.chunks) (void)carver.carveChunk(*c, terrain);
    int crossings = 0;
    for (int32_t wz = Region::MIN_W; wz <= Region::MAX_W; ++wz)
        for (int32_t y = 6; y < 80; ++y) {
            const bool left = carver.excavatesAt(-1, y, wz, terrain);
            const bool right = carver.excavatesAt(0, y, wz, terrain);
            if (left && right) {
                ++crossings;
                ASSERT_TRUE(CaveCarver::isFillBlock(carved.block(-1, y, wz)));
                ASSERT_TRUE(CaveCarver::isFillBlock(carved.block(0, y, wz)));
            }
        }
    EXPECT_GT(crossings, 0) << "fixture should contain at least one border-crossing tunnel";
}

TEST(CaveChunkCarving, StatsCountEveryChangedVoxelAndFillBands) {
    test::SyntheticTerrain terrain;
    const CaveCarver carver(SEED);
    for (ChunkCoord cc : {ChunkCoord{0, 0}, ChunkCoord{-1, 1}, ChunkCoord{2, -3}}) {
        Chunk before(cc);
        Chunk after(cc);
        terrain.fillChunk(before);
        terrain.fillChunk(after);
        const CarveStats s = carver.carveChunk(after, terrain);
        uint32_t changed = 0;
        uint32_t lava = 0;
        for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
            for (int32_t z = 0; z < Chunk::DEPTH; ++z)
                for (int32_t x = 0; x < Chunk::WIDTH; ++x) {
                    if (before.getBlock(x, y, z) == after.getBlock(x, y, z)) continue;
                    ++changed;
                    // Every change in the magma band is a lava fill (or none without a lava block).
                    if (y <= CaveCarver::LAVA_MAX_Y) ++lava;
                    if (after.getBlock(x, y, z) == BlockId::Water) {
                        ASSERT_GE(y, CaveCarver::AQUIFER_MIN_Y);
                        ASSERT_LE(y, CaveCarver::AQUIFER_MAX_Y);
                    }
                }
        EXPECT_EQ(s.total(), changed);
        EXPECT_EQ(s.lava, lava);
        if (!CaveFillPalette{}.providesLava()) {
            EXPECT_EQ(lava, 0U);
        }
    }
}

TEST(CaveChunkCarving, PaletteRemapsLava) {
    test::SyntheticTerrain terrain;
    const CaveCarver carver(SEED);
    const CaveFillPalette palette{BlockId::Air, BlockId::Water, BlockId::Obsidian};
    uint32_t lavaCells = 0;
    uint32_t obsidian = 0;
    for (int32_t cx = -2; cx <= 2; ++cx)
        for (int32_t cz = -2; cz <= 2; ++cz) {
            Chunk c(cx, cz);
            terrain.fillChunk(c);
            lavaCells += carver.carveChunk(c, terrain, palette).lava;
            for (int32_t y = 6; y <= 10; ++y)
                for (int32_t z = 0; z < Chunk::DEPTH; ++z)
                    for (int32_t x = 0; x < Chunk::WIDTH; ++x)
                        if (c.getBlock(x, y, z) == BlockId::Obsidian) ++obsidian;
        }
    EXPECT_GT(lavaCells, 0U);
    EXPECT_EQ(obsidian, lavaCells);
}

TEST(CaveChunkCarving, IsIdempotent) {
    test::SyntheticTerrain terrain;
    const CaveCarver carver(SEED);
    Chunk once(1, 1);
    terrain.fillChunk(once);
    (void)carver.carveChunk(once, terrain);
    Chunk twice(1, 1);
    terrain.fillChunk(twice);
    (void)carver.carveChunk(twice, terrain);
    const CarveStats second = carver.carveChunk(twice, terrain);
    EXPECT_EQ(second.total(), 0U);
    expectSameBlocks(once, twice);
}

TEST(CaveChunkCarving, CarvingDoesNotMarkChunkAsPlayerModified) {
    test::SyntheticTerrain terrain;
    const CaveCarver carver(SEED);
    Chunk c(0, 0);
    terrain.fillChunk(c);
    (void)carver.carveChunk(c, terrain);
    EXPECT_FALSE(c.isModified()); // generation output must not be flagged for saving as an edit
}

TEST(CaveChunkCarving, EmptyChunkStaysEmpty) {
    test::FlatSampler sampler(ColumnContext{100, 100, false});
    const CaveCarver carver(SEED);
    Chunk c(0, 0);
    EXPECT_EQ(carver.carveChunk(c, sampler).total(), 0U);
}

// ---- Overhangs over water: WorldGen's 3D density (Y 42..108) can roof a sea with stone ----

namespace {

// Every column: bedrock, stone 1..59 (sea bed 59), water 60..64, air 65..69, overhang 70..75.
void fillOverhangOverSea(Chunk& c) {
    for (int32_t z = 0; z < Chunk::DEPTH; ++z)
        for (int32_t x = 0; x < Chunk::WIDTH; ++x)
            for (int32_t y = 0; y <= 75; ++y) {
                BlockId b = BlockId::Stone;
                if (y == 0) b = BlockId::Bedrock;
                else if (y >= 60 && y <= 64) b = BlockId::Water;
                else if (y >= 65 && y <= 69) b = BlockId::Air;
                if (b != BlockId::Air) c.setBlock(x, y, z, b, false);
            }
}

} // namespace

TEST(CaveScanColumn, WaterUnderAnOverhangIsAWaterBody) {
    Chunk c(0, 0);
    fillOverhangOverSea(c);
    const ColumnContext col = CaveCarver::scanColumn(c, 4, 9);
    EXPECT_TRUE(col.isWaterBody);
    EXPECT_EQ(col.waterFloorHeight, 59);
    EXPECT_EQ(col.surfaceHeight, 75); // topmost solid block is still the overhang
    EXPECT_EQ(CaveCarver::computeVerticalEnvelope(53, col), 0.0f);
}

TEST(CaveScanColumn, IceCappedWaterIsAWaterBody) {
    Chunk c(0, 0);
    for (int32_t y = 0; y <= 64; ++y)
        c.setBlock(2, y, 2, y <= 50 ? BlockId::Stone : (y == 64 ? BlockId::Ice : BlockId::Water), false);
    const ColumnContext col = CaveCarver::scanColumn(c, 2, 2);
    EXPECT_TRUE(col.isWaterBody);
    EXPECT_EQ(col.waterFloorHeight, 50);
}

TEST(CaveChunkCarving, OverhangDoesNotLetCavesBreachTheSeaBelowIt) {
    const CaveCarver carver(SEED);
    Chunk original(0, 0);
    Chunk carved(0, 0);
    fillOverhangOverSea(original);
    fillOverhangOverSea(carved);
    const test::FlatSampler scanned(CaveCarver::scanColumn(original, 0, 0)); // uniform columns
    (void)carver.carveChunk(carved, scanned);
    for (int32_t z = 0; z < Chunk::DEPTH; ++z)
        for (int32_t x = 0; x < Chunk::WIDTH; ++x)
            for (int32_t y = 59 - 6; y <= 59; ++y)
                ASSERT_EQ(carved.getBlock(x, y, z), BlockId::Stone) << "vault breached at " << x << "," << y << "," << z;
}

// ---- Steep sea cliff: the lateral aquitard guard is what keeps the sea out ----

namespace {

// x < 0: ocean (floor 40, water 41..64). x >= 0: land plateau at 90, i.e. a 50-block cliff.
class SeaCliff final : public ColumnSampler {
public:
    [[nodiscard]] ColumnContext sampleColumn(int32_t wx, int32_t) const noexcept override {
        return wx < 0 ? ColumnContext{40, 40, true} : ColumnContext{90, 90, false};
    }
    void fill(Chunk& c) const noexcept {
        for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz)
            for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
                const ColumnContext col = sampleColumn(c.getCoord().x * Chunk::WIDTH + lx, 0);
                for (int32_t y = 0; y < Chunk::HEIGHT; ++y) {
                    BlockId b = BlockId::Air;
                    if (y == 0) b = BlockId::Bedrock;
                    else if (y <= col.surfaceHeight) b = BlockId::Stone;
                    else if (col.isWaterBody && y <= test::SEA) b = BlockId::Water;
                    if (b != BlockId::Air) c.setBlock(lx, y, lz, b, false);
                }
            }
    }
};

} // namespace

TEST(CaveChunkCarving, CliffSideCavesNeverOpenIntoTheAdjacentSea) {
    const SeaCliff cliff;
    for (uint64_t seed : std::array<uint64_t, 3>{SEED, 1ULL, 77ULL}) {
        const CaveCarver carver(seed);
        for (int32_t cz = -2; cz <= 2; ++cz) {
            Chunk land(0, cz);
            cliff.fill(land);
            (void)carver.carveChunk(land, cliff);
            // Column x = 0 faces the water of column x = -1 for y in 41..64.
            for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz)
                for (int32_t y = 41; y <= test::SEA; ++y)
                    ASSERT_EQ(land.getBlock(0, y, lz), BlockId::Stone)
                        << "seed " << seed << ": cliff wall breached at y=" << y << " z=" << cz * 16 + lz;
            // Away from the cliff the plateau is carved normally (guard is local).
            int deep = 0;
            for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz)
                for (int32_t lx = 2; lx < Chunk::WIDTH; ++lx)
                    for (int32_t y = 41; y <= 80; ++y)
                        if (land.getBlock(lx, y, lz) != BlockId::Stone) ++deep;
            EXPECT_GT(deep, 0) << "seed " << seed;
        }
    }
}
