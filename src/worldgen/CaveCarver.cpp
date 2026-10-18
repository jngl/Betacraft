#include "worldgen/CaveCarver.hpp"
#include "worldgen/SeededRandom.hpp"
#include "world/Chunk.hpp"

#include <array>

namespace betacraft::worldgen {

CaveCarver::CaveCarver(uint64_t seed) noexcept
    : m_seed(seed),
      // Independent fields: the two worm fields must be uncorrelated or N1^2 + N2^2
      // degenerates into a single-field sheet instead of a 1D tunnel.
      m_wormNoiseA(mix64(seed ^ 0x9E3779B97F4A7C15ULL)),
      m_wormNoiseB(mix64(seed ^ 0x85EBCA6BC2B2AE35ULL)),
      m_cheeseNoise(mix64(seed ^ 0xC2B2AE3D27D4EB4FULL)),
      m_aquiferNoise(mix64(seed ^ 0x165667B19E3779F9ULL)) {}

CaveKind CaveCarver::classify(int32_t wx, int32_t wy, int32_t wz, const ColumnContext& column) const noexcept {
    // Cheap early-outs before any noise evaluation (spec order).
    if (wy <= BEDROCK_GUARD_MAX_Y || wy >= column.surfaceHeight - static_cast<int32_t>(LAND_CEILING_OFFSET)) {
        return CaveKind::None;
    }
    const float envelope = computeVerticalEnvelope(wy, column);
    if (envelope <= 0.0001f) return CaveKind::None;

    const double fx = static_cast<double>(wx) * HORIZONTAL_FREQ;
    const double fy = static_cast<double>(wy) * VERTICAL_FREQ;
    const double fz = static_cast<double>(wz) * HORIZONTAL_FREQ;

    // Spaghetti: the intersection of two zero-isosurfaces is a 1D curve -> winding tunnels.
    const double wa = m_wormNoiseA.noise3D(fx, fy, fz);
    const double wb = m_wormNoiseB.noise3D(fx + 128.5, fy + 43.2, fz - 89.7);
    if ((wa * wa + wb * wb) < WORM_THRESHOLD * static_cast<double>(envelope)) {
        return CaveKind::Spaghetti;
    }

    // Cheese: low-frequency blobs exceeding the threshold -> large chambers with pillars.
    if (wy < CHEESE_MAX_Y) {
        const double cheese = m_cheeseNoise.noise3D(fx * 0.5, fy * 0.6, fz * 0.5);
        if (cheese * static_cast<double>(envelope) > CHEESE_THRESHOLD) {
            return CaveKind::Cheese;
        }
    }
    return CaveKind::None;
}

bool CaveCarver::isAquiferColumn(int32_t wx, int32_t wz) const noexcept {
    const double aq = m_aquiferNoise.noise2D(static_cast<double>(wx) * AQUIFER_FREQ,
                                             static_cast<double>(wz) * AQUIFER_FREQ);
    return aq > AQUIFER_THRESHOLD;
}

bool CaveCarver::isAquiferSealColumn(int32_t wx, int32_t wz) const noexcept {
    if (isAquiferColumn(wx, wz)) return false;
    return isAquiferColumn(wx + 1, wz) || isAquiferColumn(wx - 1, wz) ||
           isAquiferColumn(wx, wz + 1) || isAquiferColumn(wx, wz - 1);
}

CaveFill CaveCarver::getExcavationFill(int32_t wx, int32_t wy, int32_t wz) const noexcept {
    if (wy <= LAVA_MAX_Y) return CaveFill::Lava;
    if (wy >= AQUIFER_MIN_Y && wy <= AQUIFER_MAX_Y && isAquiferColumn(wx, wz)) return CaveFill::Water;
    return CaveFill::Air;
}

ColumnContext CaveCarver::resolveColumn(const ColumnSampler& sampler, int32_t wx, int32_t wz) noexcept {
    const std::array<ColumnContext, 4> neighbours{
        sampler.sampleColumn(wx + 1, wz), sampler.sampleColumn(wx - 1, wz),
        sampler.sampleColumn(wx, wz + 1), sampler.sampleColumn(wx, wz - 1)};
    return guardColumn(sampler.sampleColumn(wx, wz), neighbours);
}

ColumnContext CaveCarver::scanColumn(const world::Chunk& chunk, int32_t lx, int32_t lz) noexcept {
    return scanColumnBlocks([&](int32_t y) noexcept { return chunk.getBlock(lx, y, lz); }, world::Chunk::HEIGHT);
}

bool CaveCarver::excavatesAt(int32_t wx, int32_t wy, int32_t wz, const ColumnSampler& sampler,
                             const CaveFillPalette& palette) const noexcept {
    if (!shouldCarveAt(wx, wy, wz, sampler)) return false;
    if (isAquiferSeal(wx, wy, wz)) return false;
    return palette.providesLava() || getExcavationFill(wx, wy, wz) != CaveFill::Lava;
}

CarveStats CaveCarver::carveChunk(world::Chunk& chunk, const ColumnSampler& sampler,
                                  const CaveFillPalette& palette) const noexcept {
    CarveStats stats{};
    const int32_t baseX = chunk.getCoord().x * world::Chunk::WIDTH;
    const int32_t baseZ = chunk.getCoord().z * world::Chunk::DEPTH;

    for (int32_t lz = 0; lz < world::Chunk::DEPTH; ++lz) {
        for (int32_t lx = 0; lx < world::Chunk::WIDTH; ++lx) {
            const int32_t wx = baseX + lx;
            const int32_t wz = baseZ + lz;
            // One guarded column lookup per column, not per voxel.
            const ColumnContext column = resolveColumn(sampler, wx, wz);
            const int32_t yMax = std::min(world::Chunk::HEIGHT - 1,
                                          column.surfaceHeight - static_cast<int32_t>(LAND_CEILING_OFFSET) - 1);
            // Same decision as isAquiferSeal, hoisted: 5 aquifer samples per column, not per voxel.
            const bool sealColumn = isAquiferSealColumn(wx, wz);
            for (int32_t y = BEDROCK_GUARD_MAX_Y + 1; y <= yMax; ++y) {
                if (!isCarvable(chunk.getBlock(lx, y, lz))) continue;
                if (!shouldCarve(wx, y, wz, column)) continue;
                if (sealColumn && y >= AQUIFER_MIN_Y && y <= AQUIFER_MAX_Y) continue;
                const CaveFill fill = getExcavationFill(wx, y, wz);
                if (fill == CaveFill::Lava && !palette.providesLava()) continue;
                chunk.setBlock(lx, y, lz, palette.blockFor(fill), false);
                switch (fill) {
                    case CaveFill::Air:   ++stats.air; break;
                    case CaveFill::Water: ++stats.water; break;
                    case CaveFill::Lava:  ++stats.lava; break;
                }
            }
        }
    }
    return stats;
}

} // namespace betacraft::worldgen
