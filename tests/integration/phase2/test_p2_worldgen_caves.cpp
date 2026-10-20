// Phase 2 integration, P3.3 volumetric caves in WorldGen pass 1: the legacy tube caves are
// gone, CaveCarver is the only source of cavities, the bedrock guard (Y <= 5) and the 6-block
// sea-floor aquitard hold on real generated chunks, and the magma band is filled with lava.
#include "tests/integration/phase2/P2WorldgenSupport.hpp"
#include "tests/test_helpers.hpp"

#include "world/World.hpp"
#include "world/WorldGen.hpp"
#include "worldgen/CaveCarver.hpp"
#include "worldgen/ColumnSampling.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <vector>

using namespace betacraft;
using namespace betacraft::test::p2worldgen;
using worldgen::CaveCarver;
using worldgen::CaveFill;
using worldgen::ColumnContext;

namespace {

constexpr int32_t kSea = core::EngineConfig::SEA_LEVEL;

// Spawn area of kSeed (land, caves) and an area holding open sea.
constexpr core::ChunkCoord kLandCenter{0, 0};
constexpr core::ChunkCoord kSeaCenter{-5, 8};

class P2WorldgenCaves : public ::testing::Test {
protected:
    void SetUp() override { voxel::BlockRegistry::initialize(); }
};

// Water-body facts of a resident column, read from the generated (carved, decorated) blocks.
// CaveCarver::scanColumn is invariant under carving and ignores decoration for these fields.
[[nodiscard]] std::optional<ColumnContext> residentColumn(const world::World& w, int32_t wx, int32_t wz) {
    const Chunk* c = w.getChunk({core::floorDiv(wx, Chunk::WIDTH), core::floorDiv(wz, Chunk::DEPTH)});
    if (c == nullptr) return std::nullopt;
    return CaveCarver::scanColumn(*c, core::floorMod(wx, Chunk::WIDTH), core::floorMod(wz, Chunk::DEPTH));
}

// Counts violations and remembers the first, so a regression reports one line, not thousands.
struct Violations {
    int32_t count{0};
    std::string first;
    void add(const std::string& what) {
        if (count++ == 0) first = what;
    }
};

[[nodiscard]] std::string at(int32_t x, int32_t y, int32_t z, BlockId b) {
    return std::to_string(x) + "," + std::to_string(y) + "," + std::to_string(z) + " = " +
           std::to_string(static_cast<int>(b));
}

} // namespace

TEST_F(P2WorldgenCaves, BedrockGuardHoldsOnGeneratedChunks) {
    test::TempDir dir;
    world::World w(kSeed, dir.path());
    const std::vector<core::ChunkCoord> region = squareRegion(kLandCenter, 2);
    loadAll(w, region);

    Violations v;
    for (const core::ChunkCoord c : region) {
        const Chunk& chunk = *w.getChunk(c);
        for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz) {
            for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
                const int32_t wx = c.x * Chunk::WIDTH + lx;
                const int32_t wz = c.z * Chunk::DEPTH + lz;
                if (chunk.getBlock(lx, 0, lz) != BlockId::Bedrock) v.add(at(wx, 0, wz, chunk.getBlock(lx, 0, lz)));
                for (int32_t y = 1; y <= CaveCarver::BEDROCK_GUARD_MAX_Y; ++y) {
                    const BlockId b = chunk.getBlock(lx, y, lz);
                    // Pass 1 is all bedrock / stone / ore this deep: any fill block is a carve.
                    if (CaveCarver::isFillBlock(b)) v.add(at(wx, y, wz, b));
                }
            }
        }
    }
    EXPECT_EQ(v.count, 0) << "first: " << v.first;
}

TEST_F(P2WorldgenCaves, SeaFloorKeepsASixBlockAquitardOnGeneratedChunks) {
    test::TempDir dir;
    world::World w(kSeed, dir.path());
    const std::vector<core::ChunkCoord> region = squareRegion(kSeaCenter, 2);
    loadAll(w, region);

    // Below sea level pass 1 holds only rock and water: air there was carved. The carver may
    // not dig at or above water_floor - 6 in a water column, nor in a column beside one
    // (lateral guard: the deepest adjacent floor applies).
    Violations v;
    int32_t waterColumns = 0;
    int32_t guardedNeighbours = 0;
    int32_t caveAir = 0;
    for (const core::ChunkCoord c : squareRegion(kSeaCenter, 1)) {
        for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz) {
            for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
                const int32_t wx = c.x * Chunk::WIDTH + lx;
                const int32_t wz = c.z * Chunk::DEPTH + lz;
                const ColumnContext self = *residentColumn(w, wx, wz);
                std::array<ColumnContext, 4> nb{};
                const std::array<std::array<int32_t, 2>, 4> dirs{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};
                for (size_t i = 0; i < 4; ++i) nb[i] = *residentColumn(w, wx + dirs[i][0], wz + dirs[i][1]);
                const ColumnContext guarded = CaveCarver::guardColumn(self, nb);

                for (int32_t y = CaveCarver::BEDROCK_GUARD_MAX_Y + 1; y <= kSea; ++y) {
                    if (w.getBlock(wx, y, wz) == BlockId::Air) ++caveAir;
                }
                if (!guarded.isWaterBody) continue;
                (self.isWaterBody ? waterColumns : guardedNeighbours) += 1;

                const int32_t ceiling = guarded.waterFloorHeight - static_cast<int32_t>(CaveCarver::MIN_AQUITARD_THICKNESS);
                for (int32_t y = std::max(ceiling, 1); y <= kSea; ++y) {
                    if (w.getBlock(wx, y, wz) == BlockId::Air) {
                        v.add(at(wx, y, wz, BlockId::Air) + " floor " + std::to_string(guarded.waterFloorHeight) +
                              (self.isWaterBody ? " (water column)" : " (shore column)"));
                    }
                }
            }
        }
    }
    EXPECT_EQ(v.count, 0) << "carved inside the aquitard, first: " << v.first;
    EXPECT_GT(waterColumns, 64) << "region must contain open sea";
    EXPECT_GT(guardedNeighbours, 0) << "region must contain a shore";
    EXPECT_GT(caveAir, 0) << "region must contain caves";
}

// Every cavity of a pass-1 chunk is exactly CaveCarver's: the legacy tube caves are gone and
// the carver runs on the final pass-1 blocks with WorldGen's own column function.
TEST_F(P2WorldgenCaves, CaveCarverIsTheOnlySourceOfCavities) {
    world::WorldGen gen(kSeed);
    const CaveCarver& carver = gen.caveCarver();
    auto fn = [&gen](int32_t wx, int32_t wz) noexcept { return gen.columnContext(wx, wz); };
    const worldgen::FunctionColumnSampler<decltype(fn)> sampler(fn);

    Violations rockLeft;
    Violations legacy;
    int32_t excavated = 0;
    int32_t lava = 0;
    for (const core::ChunkCoord c : squareRegion(kLandCenter, 1)) {
        Chunk chunk(c);
        gen.generateChunk(chunk);
        const worldgen::ChunkColumnCache cache(sampler, c);
        for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz) {
            for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
                const int32_t wx = c.x * Chunk::WIDTH + lx;
                const int32_t wz = c.z * Chunk::DEPTH + lz;
                for (int32_t y = 1; y <= kSea; ++y) {
                    const BlockId b = chunk.getBlock(lx, y, lz);
                    const bool carves = carver.excavatesAt(wx, y, wz, cache);
                    if (carves) {
                        ++excavated;
                        // Carvable rock left in place would mean carving ran too early or not at all.
                        if (CaveCarver::isCarvable(b)) rockLeft.add(at(wx, y, wz, b));
                        if (carver.getExcavationFill(wx, y, wz) == CaveFill::Lava && b == BlockId::Lava) ++lava;
                    } else {
                        // Air below sea level that the carver did not dig is a legacy tube cave.
                        if (b == BlockId::Air || voxel::isLavaBlock(b)) legacy.add(at(wx, y, wz, b));
                    }
                }
            }
        }
    }
    EXPECT_EQ(rockLeft.count, 0) << "carvable rock left where the carver digs, first: " << rockLeft.first;
    EXPECT_EQ(legacy.count, 0) << "cavity the carver did not dig, first: " << legacy.first;
    EXPECT_GT(excavated, 0);
    EXPECT_GT(lava, 0);
}

TEST_F(P2WorldgenCaves, MagmaBandIsFilledWithLavaAndNothingAboveIt) {
    ASSERT_TRUE(worldgen::CaveFillPalette{}.providesLava());
    test::TempDir dir;
    world::World w(kSeed, dir.path());
    const std::vector<core::ChunkCoord> region = squareRegion(kLandCenter, 1);
    loadAll(w, region);

    Violations misplaced;
    int32_t lava = 0;
    for (const core::ChunkCoord c : region) {
        const Chunk& chunk = *w.getChunk(c);
        for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
            for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz)
                for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
                    if (!voxel::isLavaBlock(chunk.getBlock(lx, y, lz))) continue;
                    ++lava;
                    const bool inBand = y > CaveCarver::BEDROCK_GUARD_MAX_Y && y <= CaveCarver::LAVA_MAX_Y;
                    // Metadata 0 is a source block under the World fluid convention.
                    if (!inBand || chunk.getMetadata(lx, y, lz) != 0) misplaced.add(at(lx, y, lz, chunk.getBlock(lx, y, lz)));
                }
    }
    EXPECT_EQ(misplaced.count, 0) << "lava outside Y 6..10 or not a source, first: " << misplaced.first;
    EXPECT_GT(lava, 0);
    // Lava emits light 15: the lit chunk must show it.
    bool lit = false;
    for (const core::ChunkCoord c : region) {
        const Chunk& chunk = *w.getChunk(c);
        for (int32_t y = 6; y <= CaveCarver::LAVA_MAX_Y && !lit; ++y)
            for (int32_t lz = 0; lz < Chunk::DEPTH && !lit; ++lz)
                for (int32_t lx = 0; lx < Chunk::WIDTH && !lit; ++lx)
                    lit = voxel::isLavaBlock(chunk.getBlock(lx, y, lz)) && chunk.getBlockLight(lx, y, lz) == 15;
    }
    EXPECT_TRUE(lit);
}
