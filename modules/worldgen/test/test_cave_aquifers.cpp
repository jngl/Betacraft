// "lacs d'eau souterrains confines entre Y=11 et Y=32": aquifer water must be confined,
// i.e. no excavated water voxel may sit beside or above an air voxel it could drain into
// (fluids spread through faces only: 4 horizontal neighbours + the one below).
#include "worldgen/CaveCarver.hpp"
#include "WorldgenTestSupport.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <memory>
#include <unordered_map>

using namespace betacraft::worldgen;
using betacraft::core::ChunkCoord;
using betacraft::core::floorDiv;
using betacraft::core::floorMod;
using betacraft::voxel::BlockId;
using betacraft::voxel::Chunk;

namespace {

struct CarvedWorld {
    static constexpr int32_t LO = -2;
    static constexpr int32_t HI = 2; // inclusive chunk range
    std::unordered_map<ChunkCoord, std::unique_ptr<Chunk>> chunks;

    CarvedWorld(uint64_t seed, const CaveFillPalette& palette, CarveStats& total) {
        test::SyntheticTerrain terrain;
        const CaveCarver carver(seed);
        for (int32_t cz = LO; cz <= HI; ++cz)
            for (int32_t cx = LO; cx <= HI; ++cx) {
                auto c = std::make_unique<Chunk>(cx, cz);
                terrain.fillChunk(*c);
                const CarveStats s = carver.carveChunk(*c, terrain, palette);
                total.air += s.air;
                total.water += s.water;
                total.lava += s.lava;
                chunks.emplace(ChunkCoord{cx, cz}, std::move(c));
            }
    }

    [[nodiscard]] BlockId block(int32_t wx, int32_t wy, int32_t wz) const {
        const auto it = chunks.find(ChunkCoord{floorDiv(wx, Chunk::WIDTH), floorDiv(wz, Chunk::DEPTH)});
        if (it == chunks.end()) return BlockId::Stone;
        return it->second->getBlock(floorMod(wx, Chunk::WIDTH), wy, floorMod(wz, Chunk::DEPTH));
    }
};

void expectAquifersConfined(uint64_t seed, const CaveFillPalette& palette) {
    CarveStats stats{};
    const CarvedWorld w(seed, palette, stats);
    ASSERT_GT(stats.water, 0U) << "fixture must contain aquifer water, seed " << seed;
    constexpr std::array<std::array<int32_t, 3>, 5> drains{{{1, 0, 0}, {-1, 0, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}}};
    constexpr int32_t lo = CarvedWorld::LO * Chunk::WIDTH + 1;
    constexpr int32_t hi = (CarvedWorld::HI + 1) * Chunk::WIDTH - 1;
    int checked = 0;
    for (int32_t wx = lo; wx < hi; ++wx)
        for (int32_t wz = lo; wz < hi; ++wz)
            for (int32_t y = CaveCarver::AQUIFER_MIN_Y; y <= CaveCarver::AQUIFER_MAX_Y; ++y) {
                if (w.block(wx, y, wz) != BlockId::Water) continue;
                ++checked;
                for (const auto& d : drains) {
                    ASSERT_NE(w.block(wx + d[0], y + d[1], wz + d[2]), BlockId::Air)
                        << "seed " << seed << ": aquifer water at " << wx << "," << y << "," << wz
                        << " drains into air at offset " << d[0] << "," << d[1] << "," << d[2];
                }
            }
    EXPECT_GT(checked, 0);
}

} // namespace

TEST(CaveAquifers, WaterIsConfinedWithTheDefaultPalette) {
    for (uint64_t seed : {173173173ULL, 0xDEADBEEFCAFEULL}) expectAquifersConfined(seed, CaveFillPalette{});
}

TEST(CaveAquifers, WaterIsConfinedWithALavaPalette) {
    const CaveFillPalette lava{BlockId::Air, BlockId::Water, BlockId::Obsidian};
    for (uint64_t seed : {173173173ULL, 5ULL}) expectAquifersConfined(seed, lava);
}

// The seal is a property of world columns: it must not depend on which chunk evaluates it.
TEST(CaveAquifers, SealIsAPureColumnPropertyAtTheAquiferEdge) {
    const CaveCarver c(173173173ULL);
    int sealedColumns = 0;
    for (int32_t x = -300; x < 300; ++x)
        for (int32_t z = -40; z < 40; ++z) {
            const bool wet = c.getExcavationFill(x, 20, z) == CaveFill::Water;
            const bool sealed = c.isAquiferSeal(x, 20, z);
            if (sealed) ++sealedColumns;
            if (wet) {
                ASSERT_FALSE(sealed) << x << "," << z;
                continue;
            }
            const bool wetNeighbour = c.getExcavationFill(x + 1, 20, z) == CaveFill::Water ||
                                      c.getExcavationFill(x - 1, 20, z) == CaveFill::Water ||
                                      c.getExcavationFill(x, 20, z + 1) == CaveFill::Water ||
                                      c.getExcavationFill(x, 20, z - 1) == CaveFill::Water;
            ASSERT_EQ(sealed, wetNeighbour) << x << "," << z;
            // Outside the band nothing is sealed.
            ASSERT_FALSE(c.isAquiferSeal(x, CaveCarver::AQUIFER_MIN_Y - 1, z));
            ASSERT_FALSE(c.isAquiferSeal(x, CaveCarver::AQUIFER_MAX_Y + 1, z));
        }
    EXPECT_GT(sealedColumns, 0);
}
