#pragma once
// Shared fixtures for the P3.3 / P3.4 worldgen tests: an analytic synthetic terrain that
// mixes oceans (floor < 64) and hills, usable both as a ColumnSampler and as a chunk filler.

#include "core/Config.hpp"
#include "voxel/Chunk.hpp"
#include "worldgen/CaveCarver.hpp"
#include "worldgen/ColumnSampling.hpp"

#include <cmath>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace betacraft::worldgen::test {

using world::BlockId;
using world::Chunk;

constexpr int32_t SEA = core::EngineConfig::SEA_LEVEL;

// Surface height oscillates between ~44 and ~80: deep ocean, shores and hills.
[[nodiscard]] inline int32_t syntheticSurface(int32_t wx, int32_t wz) noexcept {
    const double h = 62.0 + 10.0 * std::sin(wx * 0.09) + 8.0 * std::cos(wz * 0.07);
    return static_cast<int32_t>(std::lround(h));
}

class SyntheticTerrain final : public ColumnSampler {
public:
    [[nodiscard]] ColumnContext sampleColumn(int32_t wx, int32_t wz) const noexcept override {
        const int32_t s = syntheticSurface(wx, wz);
        return ColumnContext{s, s, s < SEA};
    }

    // Pass-1 style column: bedrock, stone, 3 dirt, grass/sand top, water up to sea level.
    void fillChunk(Chunk& chunk) const noexcept {
        const int32_t baseX = chunk.getCoord().x * Chunk::WIDTH;
        const int32_t baseZ = chunk.getCoord().z * Chunk::DEPTH;
        for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz) {
            for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
                const int32_t s = syntheticSurface(baseX + lx, baseZ + lz);
                for (int32_t y = 0; y < Chunk::HEIGHT; ++y) {
                    BlockId b = BlockId::Air;
                    if (y == 0) b = BlockId::Bedrock;
                    else if (y < s - 3) b = BlockId::Stone;
                    else if (y < s) b = BlockId::Dirt;
                    else if (y == s) b = (s >= SEA) ? BlockId::Grass : BlockId::Sand;
                    else if (y <= SEA) b = BlockId::Water;
                    if (b != BlockId::Air) chunk.setBlock(lx, y, lz, b, false);
                }
            }
        }
    }
};

// Constant-height sampler: lets tests pin the envelope exactly.
class FlatSampler final : public ColumnSampler {
public:
    explicit FlatSampler(ColumnContext ctx) noexcept : m_ctx(ctx) {}
    [[nodiscard]] ColumnContext sampleColumn(int32_t, int32_t) const noexcept override { return m_ctx; }

private:
    ColumnContext m_ctx;
};

} // namespace betacraft::worldgen::test

namespace betacraft::worldgen::test {

// Pass-1 terrain for decoration tests: gentle grass hills over 40x40-block biome patches
// (Forest / Taiga / Desert), round lakes on a 40-block lattice (water up to sea level, a
// sea-level sand / grass shore ring) so water-adjacent rules are exercised, and pass-1 caves
// carved by CaveCarver underneath, as WorldGen's pass 1 will produce them.
[[nodiscard]] inline world::BiomeId decorationBiome(int32_t wx, int32_t wz) noexcept {
    const int32_t patch = core::floorMod(core::floorDiv(wx, 40) + core::floorDiv(wz, 40), 3);
    if (patch == 0) return world::BiomeId::Forest;
    if (patch == 1) return world::BiomeId::Taiga;
    return world::BiomeId::Desert;
}

[[nodiscard]] inline int32_t decorationSurface(int32_t wx, int32_t wz) noexcept {
    const double h = 67.0 + 2.0 * std::sin(wx * 0.21) + 2.0 * std::cos(wz * 0.17);
    return static_cast<int32_t>(std::lround(h));
}

struct DecorationColumn {
    int32_t surface{0}; // topmost solid block (lake bed for lake columns)
    bool lake{false};   // water from surface + 1 up to sea level
};

// Lake centres sit near lattice corners but off the integer grid, so shores cross chunk
// borders at irregular positions (including columns whose only water lies in the next chunk).
[[nodiscard]] inline DecorationColumn decorationColumn(int32_t wx, int32_t wz) noexcept {
    constexpr double SPACING = 40.0;
    const double cx = std::floor((wx + 20) / SPACING) * SPACING + 0.37;
    const double cz = std::floor((wz + 20) / SPACING) * SPACING - 0.21;
    const double d = std::hypot(static_cast<double>(wx) - cx, static_cast<double>(wz) - cz);
    const int32_t base = decorationSurface(wx, wz);
    if (d < 6.3) return DecorationColumn{SEA - 3, true};
    if (d < 8.3) return DecorationColumn{SEA, false};
    if (d < 12.3) {
        const double t = (d - 8.3) / 4.0;
        return DecorationColumn{static_cast<int32_t>(std::lround(SEA + t * (base - SEA))), false};
    }
    return DecorationColumn{base, false};
}

constexpr uint64_t DECORATION_CAVE_SEED = 0x5EED0CA7EULL;

inline void buildDecorationTerrain(Chunk& chunk) noexcept {
    const int32_t baseX = chunk.getCoord().x * Chunk::WIDTH;
    const int32_t baseZ = chunk.getCoord().z * Chunk::DEPTH;
    for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz) {
        for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
            const int32_t wx = baseX + lx;
            const int32_t wz = baseZ + lz;
            const world::BiomeId biome = decorationBiome(wx, wz);
            chunk.setColumnBiome(lx, lz, biome, glm::vec3(0.5f), glm::vec3(0.5f));
            const DecorationColumn col = decorationColumn(wx, wz);
            const int32_t s = col.surface;
            const bool shore = !col.lake && s == SEA;
            BlockId top = (biome == world::BiomeId::Desert) ? BlockId::Sand
                        : (biome == world::BiomeId::Taiga) ? BlockId::SnowGrass : BlockId::Grass;
            if (col.lake) top = BlockId::Sand;
            else if (shore && biome != world::BiomeId::Desert) top = BlockId::Grass;
            for (int32_t y = 0; y <= s; ++y) {
                BlockId b = BlockId::Stone;
                if (y == 0) b = BlockId::Bedrock;
                else if (y == s) b = top;
                else if (y >= s - 3) b = (biome == world::BiomeId::Desert) ? BlockId::Sand : BlockId::Dirt;
                chunk.setBlock(lx, y, lz, b, false);
            }
            if (col.lake) {
                for (int32_t y = s + 1; y <= SEA; ++y) chunk.setBlock(lx, y, lz, BlockId::Water, false);
            }
        }
    }
    // Pass-1 caves: same carver and analytic column function for every chunk, so the
    // fixture stays seamless and order independent.
    const CaveCarver carver(DECORATION_CAVE_SEED);
    const auto sampler = FunctionColumnSampler([](int32_t wx, int32_t wz) noexcept {
        const DecorationColumn c = decorationColumn(wx, wz);
        return ColumnContext{c.surface, c.surface, c.lake};
    });
    (void)carver.carveChunk(chunk, sampler);
}

// Pass 1 is a pure function of the coordinate, so it is generated once per coordinate and
// copied afterwards (keeps the permuted-order suites fast). Tests are single-threaded.
inline void fillDecorationTerrain(Chunk& chunk) {
    static std::unordered_map<core::ChunkCoord, std::unique_ptr<Chunk>> cache;
    auto it = cache.find(chunk.getCoord());
    if (it == cache.end()) {
        auto fresh = std::make_unique<Chunk>(chunk.getCoord());
        buildDecorationTerrain(*fresh);
        it = cache.emplace(chunk.getCoord(), std::move(fresh)).first;
    }
    chunk = *it->second;
}

} // namespace betacraft::worldgen::test
