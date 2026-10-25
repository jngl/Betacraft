#pragma once

#include "core/Types.hpp"
#include "voxel/Block.hpp"
#include "worldgen/SeededRandom.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace betacraft::voxel { class Chunk; }

namespace betacraft::worldgen {

// Beta 1.7.3 ore veins (WorldGenMinable, ChunkProviderGenerate.populate), part of pass 1.
// Each chunk draws its veins from its own stream (chunkSeed(seed ^ kOreVeinSalt, coord)), with
// the vein centre inside the chunk; a vein reaches at most 7 blocks out, so a chunk traces the
// veins of its 9-chunk neighbourhood clipped to itself. The result is a pure function of the
// chunk's pass-1 blocks and the seed, independent of the generation order, and seamless across
// borders. Ore only ever replaces Stone.
struct OreVeinSpec {
    voxel::BlockId ore{voxel::BlockId::Stone};
    int32_t count{0}; // Veins per chunk
    int32_t size{0};  // WorldGenMinable numberOfBlocks
    int32_t yRange{128}; // y = rand(yRange)
    bool lapisY{false};  // y = rand(16) + rand(16) instead
};

// Beta populate order: dirt, gravel, coal, iron, gold, redstone, diamond, lapis.
inline constexpr std::array<OreVeinSpec, 8> kBetaOreVeins{{
    {voxel::BlockId::Dirt, 20, 32, 128, false},
    {voxel::BlockId::Gravel, 10, 32, 128, false},
    {voxel::BlockId::CoalOre, 20, 16, 128, false},
    {voxel::BlockId::IronOre, 20, 8, 64, false},
    {voxel::BlockId::GoldOre, 2, 8, 32, false},
    {voxel::BlockId::RedstoneOre, 8, 7, 16, false},
    {voxel::BlockId::DiamondOre, 1, 7, 16, false},
    {voxel::BlockId::LapisOre, 1, 6, 16, true},
}};

inline constexpr uint64_t kOreVeinSalt = 0x0BE5EED5A17C0DE5ULL;
// Farthest a vein cell lies from its chunk's bounds (size 32: 4 along the axis + 2.5 radius).
inline constexpr int32_t kOreVeinReach = 7;

// Columns a caller cares about: [minX, maxX] x [minZ, maxZ] (world, inclusive). Tracing skips
// the parts of a vein outside it without changing the random draws.
struct OreClip {
    int32_t minX{INT32_MIN / 2};
    int32_t maxX{INT32_MAX / 2};
    int32_t minZ{INT32_MIN / 2};
    int32_t maxZ{INT32_MAX / 2};
};

// Beta WorldGenMinable.generate for one vein whose centre is (x, y, z): calls
// `cell(wx, wy, wz)` for every cell of the vein inside `clip` (repeats possible).
template <class Cell>
void traceOreVein(SeededRandom& rng, int32_t size, int32_t x, int32_t y, int32_t z, Cell&& cell,
                  const OreClip& clip = {}) {
    const double n = static_cast<double>(size);
    const double angle = rng.nextDouble() * std::numbers::pi;
    const double x0 = x + std::sin(angle) * n / 8.0;
    const double x1 = x - std::sin(angle) * n / 8.0;
    const double z0 = z + std::cos(angle) * n / 8.0;
    const double z1 = z - std::cos(angle) * n / 8.0;
    const double y0 = static_cast<double>(y + static_cast<int32_t>(rng.nextBounded(3U)) - 2);
    const double y1 = static_cast<double>(y + static_cast<int32_t>(rng.nextBounded(3U)) - 2);
    // Widest radius any step can reach: (2 x size / 16 + 1) / 2.
    const double reach = n / 16.0 + 0.5;
    if (std::max(x0, x1) + reach < clip.minX || std::min(x0, x1) - reach > clip.maxX + 1 ||
        std::max(z0, z1) + reach < clip.minZ || std::min(z0, z1) - reach > clip.maxZ + 1) {
        for (int32_t l = 0; l <= size; ++l) (void)rng.nextDouble(); // Same draws as a traced vein
        return;
    }
    for (int32_t l = 0; l <= size; ++l) {
        const double t = static_cast<double>(l) / n;
        const double cx = x0 + (x1 - x0) * t;
        const double cy = y0 + (y1 - y0) * t;
        const double cz = z0 + (z1 - z0) * t;
        const double spread = rng.nextDouble() * n / 16.0;
        const double diameter = (std::sin(static_cast<double>(l) * std::numbers::pi / n) + 1.0) * spread + 1.0;
        const double r = diameter / 2.0;
        const auto lo = [r](double c) { return static_cast<int32_t>(std::floor(c - r)); };
        const auto hi = [r](double c) { return static_cast<int32_t>(std::floor(c + r)); };
        const int32_t zLo = std::max(lo(cz), clip.minZ);
        const int32_t zHi = std::min(hi(cz), clip.maxZ);
        for (int32_t bx = std::max(lo(cx), clip.minX); bx <= std::min(hi(cx), clip.maxX); ++bx) {
            const double dx = (bx + 0.5 - cx) / r;
            if (dx * dx >= 1.0) continue;
            for (int32_t by = lo(cy); by <= hi(cy); ++by) {
                const double dy = (by + 0.5 - cy) / r;
                if (dx * dx + dy * dy >= 1.0) continue;
                for (int32_t bz = zLo; bz <= zHi; ++bz) {
                    const double dz = (bz + 0.5 - cz) / r;
                    if (dx * dx + dy * dy + dz * dz < 1.0) {
                        cell(bx, by, bz);
                    }
                }
            }
        }
    }
}

// Every vein the chunk `source` owns, in kBetaOreVeins order: `cell(ore, wx, wy, wz)` for each
// of its cells inside `clip` (world coordinates, y may leave [0, 128)).
template <class Cell>
void forEachOreVeinCell(uint64_t seed, core::ChunkCoord source, Cell&& cell, const OreClip& clip = {}) {
    SeededRandom rng(chunkSeed(seed ^ kOreVeinSalt, source));
    const int32_t baseX = source.x * 16;
    const int32_t baseZ = source.z * 16;
    for (const OreVeinSpec& spec : kBetaOreVeins) {
        for (int32_t i = 0; i < spec.count; ++i) {
            const int32_t x = baseX + static_cast<int32_t>(rng.nextBounded(16U));
            const int32_t y = spec.lapisY ? static_cast<int32_t>(rng.nextBounded(16U) + rng.nextBounded(16U))
                                          : static_cast<int32_t>(rng.nextBounded(static_cast<uint32_t>(spec.yRange)));
            const int32_t z = baseZ + static_cast<int32_t>(rng.nextBounded(16U));
            traceOreVein(rng, spec.size, x, y, z,
                         [&](int32_t wx, int32_t wy, int32_t wz) { cell(spec.ore, wx, wy, wz); }, clip);
        }
    }
}

// Pass 1, after the caves (WorldGen::generateChunk): the veins of the 9 chunks around this one,
// in increasing (z, x) chunk order, clipped to it; a cell becomes ore only while it is Stone.
void placeOreVeins(voxel::Chunk& chunk, uint64_t seed);

} // namespace betacraft::worldgen
