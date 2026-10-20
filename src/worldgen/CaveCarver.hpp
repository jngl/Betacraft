#pragma once

#include "voxel/Block.hpp"
#include "worldgen/SimplexNoise.hpp"

#include <algorithm>
#include <cstdint>
#include <span>

namespace betacraft::voxel {
class Chunk;
}

namespace betacraft::worldgen {

// Per-column terrain facts the carver needs. Must be a pure function of the world column
// (wx, wz) so that carving stays seamless across chunk borders.
struct ColumnContext {
    int32_t surfaceHeight{0};    // Y of the topmost solid terrain block
    int32_t waterFloorHeight{0}; // Y of the topmost solid block under a water body (sea bed)
    bool isWaterBody{false};     // fluid (ocean / river, Y <= SEA_LEVEL) rests on this column
};

// Source of ColumnContext for arbitrary world columns, including columns of chunks that are
// not generated yet (the lateral guard reads the 4 face neighbours). WorldGen implements it
// analytically from its height function.
class ColumnSampler {
public:
    virtual ~ColumnSampler() = default;
    [[nodiscard]] virtual ColumnContext sampleColumn(int32_t wx, int32_t wz) const noexcept = 0;
};

enum class CaveKind : uint8_t { None = 0, Spaghetti, Cheese };

// What an excavated voxel becomes. Kept abstract so a palette (CaveFillPalette) maps it to
// concrete blocks; the default one uses Air / Water / Lava from voxel::BlockId.
enum class CaveFill : uint8_t { Air = 0, Water, Lava };

namespace detail {
// Detecting the enumerator instead of naming it kept this module buildable before Block.hpp
// gained BlockId::Lava; the default palette now resolves to it (pinned by the Phase 2
// worldgen integration tests).
template <typename E>
inline constexpr bool hasLavaBlock = requires { E::Lava; };

template <typename E>
[[nodiscard]] constexpr E lavaBlockOr(E fallback) noexcept {
    if constexpr (hasLavaBlock<E>) {
        return E::Lava;
    } else {
        return fallback;
    }
}
} // namespace detail

struct CaveFillPalette {
    voxel::BlockId air{voxel::BlockId::Air};
    voxel::BlockId water{voxel::BlockId::Water};
    // BlockId::Lava when the registry has it; otherwise == air, i.e. "no lava available".
    voxel::BlockId lava{detail::lavaBlockOr(voxel::BlockId::Air)};

    // False when lava would degrade to air. carveChunk then leaves the magma band (Y <= 10)
    // as rock: an empty void there would drain the aquifers right above it (Y >= 11) and is
    // further from the spec's lava sheets than solid ground.
    [[nodiscard]] constexpr bool providesLava() const noexcept { return lava != air; }

    [[nodiscard]] constexpr voxel::BlockId blockFor(CaveFill fill) const noexcept {
        switch (fill) {
            case CaveFill::Water: return water;
            case CaveFill::Lava:  return lava;
            case CaveFill::Air:   break;
        }
        return air;
    }
};

struct CarveStats {
    uint32_t air{0};
    uint32_t water{0};
    uint32_t lava{0};
    [[nodiscard]] constexpr uint32_t total() const noexcept { return air + water + lava; }
};

// P3.3 volumetric cave carver (Fiche Technique 4, section 2.2), on Simplex 3D noise:
//  * spaghetti tunnels where N1^2 + N2^2 < T_worm * A(y)  (two ridged fields crossing 0),
//  * cheese chambers where N_cheese * A(y) > 0.58, below Y = 50,
//  * A(y) = 0 at Y <= 5 (bedrock guard) and above surface - 3 / water_floor - 6 (aquitard).
// Fill (getExcavationFill, verbatim from the spec): lava at Y <= 10, aquifer water at
// Y 11..32 where the 2D field aq > 0.42, air elsewhere. carveChunk adds two local rules: a
// one-block rock seal around aquifers (isAquiferSeal) and, for a palette without lava, a
// solid magma band (CaveFillPalette::providesLava).
// Every decision is a pure function of (seed, world position, ColumnSampler) - never of
// chunk-local state - which is what makes carving seamless and generation-order independent.
class CaveCarver {
public:
    static constexpr int32_t BEDROCK_GUARD_MAX_Y = 5;        // A(y) = 0 for y <= 5
    static constexpr float BEDROCK_FADE_IN = 4.0f;           // ramp Y 5 -> 9
    static constexpr float MIN_AQUITARD_THICKNESS = 6.0f;    // rock vault kept under water
    static constexpr float LAND_CEILING_OFFSET = 3.0f;       // caves stop 3 blocks under grass
    static constexpr float SURFACE_FADE_OUT = 4.0f;

    static constexpr double HORIZONTAL_FREQ = 0.038;
    static constexpr double VERTICAL_FREQ = 0.048;
    static constexpr double WORM_THRESHOLD = 0.0165;
    static constexpr double CHEESE_THRESHOLD = 0.58;
    static constexpr int32_t CHEESE_MAX_Y = 50;              // cheese chambers only below Y=50
    static constexpr int32_t LAVA_MAX_Y = 10;                // magma sheets at Y <= 10
    static constexpr int32_t AQUIFER_MIN_Y = 11;
    static constexpr int32_t AQUIFER_MAX_Y = 32;
    static constexpr double AQUIFER_FREQ = 0.015;
    static constexpr double AQUIFER_THRESHOLD = 0.42;

    explicit CaveCarver(uint64_t seed) noexcept;

    [[nodiscard]] uint64_t getSeed() const noexcept { return m_seed; }

    // Vertical attenuation envelope A(y). The carving threshold is multiplied by A(y), so
    // A(y) = 0 makes excavation mathematically impossible.
    [[nodiscard]] static constexpr float computeVerticalEnvelope(
        int32_t wy, int32_t surfaceHeight, int32_t waterFloorHeight, bool isWaterBody) noexcept {
        const float y = static_cast<float>(wy);

        // 1. Bedrock guard: absolute lower barrier.
        if (wy <= BEDROCK_GUARD_MAX_Y) return 0.0f;
        const float bottomFactor =
            std::clamp((y - static_cast<float>(BEDROCK_GUARD_MAX_Y)) / BEDROCK_FADE_IN, 0.0f, 1.0f);

        // 2. Upper barrier: aquitard under oceans / rivers, soil cover on land.
        const float effectiveCeiling = isWaterBody
            ? (static_cast<float>(waterFloorHeight) - MIN_AQUITARD_THICKNESS)
            : (static_cast<float>(surfaceHeight) - LAND_CEILING_OFFSET);
        if (y >= effectiveCeiling) return 0.0f;
        const float topFactor = std::clamp((effectiveCeiling - y) / SURFACE_FADE_OUT, 0.0f, 1.0f);

        return hermite(bottomFactor) * hermite(topFactor);
    }

    [[nodiscard]] static constexpr float computeVerticalEnvelope(int32_t wy, const ColumnContext& column) noexcept {
        return computeVerticalEnvelope(wy, column.surfaceHeight, column.waterFloorHeight, column.isWaterBody);
    }

    [[nodiscard]] CaveKind classify(int32_t wx, int32_t wy, int32_t wz, const ColumnContext& column) const noexcept;

    [[nodiscard]] bool shouldCarve(int32_t wx, int32_t wy, int32_t wz,
                                   int32_t surfaceHeight, int32_t waterFloorHeight, bool isWaterBody) const noexcept {
        return classify(wx, wy, wz, ColumnContext{surfaceHeight, waterFloorHeight, isWaterBody}) != CaveKind::None;
    }
    [[nodiscard]] bool shouldCarve(int32_t wx, int32_t wy, int32_t wz, const ColumnContext& column) const noexcept {
        return classify(wx, wy, wz, column) != CaveKind::None;
    }

    [[nodiscard]] CaveFill getExcavationFill(int32_t wx, int32_t wy, int32_t wz) const noexcept;

    // True for a voxel of the aquifer band (Y 11..32) in a dry column that touches a wet
    // (aq > 0.42) column through a face. carveChunk leaves such voxels solid: without this
    // one-block rock seal a tunnel crossing the aq = 0.42 isoline would hold a water wall
    // beside open air, which the fluid simulator would drain into the dry half - the spec
    // asks for lakes "confines entre Y=11 et Y=32". Pure function of the world column.
    [[nodiscard]] bool isAquiferSeal(int32_t wx, int32_t wy, int32_t wz) const noexcept {
        return wy >= AQUIFER_MIN_Y && wy <= AQUIFER_MAX_Y && isAquiferSealColumn(wx, wz);
    }

    // Lateral aquitard guard. The spec's envelope only protects the column a cave is in; a
    // cave in a land column next to an ocean column would still open sideways into the sea.
    // A column touching a water body therefore inherits the deepest adjacent sea floor as
    // its aquitard ceiling (water_floor - 6 applies laterally as well).
    [[nodiscard]] static constexpr ColumnContext guardColumn(const ColumnContext& center,
                                                             std::span<const ColumnContext> neighbours) noexcept {
        ColumnContext out = center;
        for (const ColumnContext& n : neighbours) {
            if (!n.isWaterBody) continue;
            out.waterFloorHeight = out.isWaterBody ? std::min(out.waterFloorHeight, n.waterFloorHeight)
                                                   : n.waterFloorHeight;
            out.isWaterBody = true;
        }
        return out;
    }

    // Column + its 4 face neighbours, guarded. Pure function of the world column.
    [[nodiscard]] static ColumnContext resolveColumn(const ColumnSampler& sampler, int32_t wx, int32_t wz) noexcept;

    // Natural stone / soil only. Bedrock, fluids, sand (would float) and anything placed
    // by players or decoration are never excavated.
    [[nodiscard]] static constexpr bool isCarvable(voxel::BlockId id) noexcept {
        using voxel::BlockId;
        switch (id) {
            case BlockId::Stone: case BlockId::Dirt: case BlockId::Grass: case BlockId::SnowGrass:
            case BlockId::Gravel: case BlockId::Sandstone: case BlockId::Clay:
            case BlockId::CoalOre: case BlockId::IronOre: case BlockId::GoldOre:
            case BlockId::LapisOre: case BlockId::RedstoneOre: case BlockId::DiamondOre:
                return true;
            default:
                return false;
        }
    }

    // Blocks a default-palette excavation can leave behind (air, aquifer water, magma-band lava).
    [[nodiscard]] static constexpr bool isFillBlock(voxel::BlockId id) noexcept {
        return id == voxel::BlockId::Air || id == voxel::BlockId::Water || voxel::isLavaBlock(id);
    }

    // Derives a ColumnContext from a chunk column: surface = topmost solid block; fluid above
    // AQUIFER_MAX_Y (also under a stone overhang) marks a water body whose floor is the first
    // solid block beneath the topmost such fluid. Invariant under carveChunk (aquifer water
    // lives at Y <= AQUIFER_MAX_Y and caves stay below the aquitard), so a sampler built on
    // resident chunks gives the same answer before and after a neighbour is carved.
    [[nodiscard]] static ColumnContext scanColumn(const voxel::Chunk& chunk, int32_t lx, int32_t lz) noexcept;

    // scanColumn over any column source, `blockAt(y)` for y in [0, height). Lets WorldGen
    // derive the context of a column it has not generated from the same pass-1 blocks, with
    // exactly the rules scanColumn applies to a resident chunk.
    template <typename BlockAt>
    [[nodiscard]] static constexpr ColumnContext scanColumnBlocks(BlockAt&& blockAt, int32_t height) noexcept {
        using voxel::BlockId;
        auto isFluid = [](BlockId b) noexcept { return b == BlockId::Water || b == BlockId::Ice; };

        int32_t y = height - 1;
        while (y >= 0 && blockAt(y) == BlockId::Air) --y;
        if (y < 0) return ColumnContext{0, 0, false};
        const bool fluidOnTop = isFluid(blockAt(y));
        const int32_t topSolid = y;

        // The water body is not necessarily at the top: WorldGen's 3D overhang density can roof
        // a sea with stone. Find the topmost fluid anywhere in the column, or the aquitard would
        // be computed from the overhang and caves would open under the water.
        // Only fluid ABOVE the aquifer band counts: carveChunk itself writes aquifer water at
        // Y 11..32, and a scan that saw it would report a sea for an already-carved column and
        // make carving depend on generation order. Pass-1 water (WorldGen fills to sea level)
        // always reaches above the band, so every genuine sea is still found.
        while (y > AQUIFER_MAX_Y && !isFluid(blockAt(y))) --y;
        if (y <= AQUIFER_MAX_Y) return ColumnContext{topSolid, topSolid, false};

        while (y >= 0 && (isFluid(blockAt(y)) || blockAt(y) == BlockId::Air)) --y;
        const int32_t floorY = std::max(y, 0);
        return ColumnContext{fluidOnTop ? floorY : topSolid, floorY, true};
    }

    // World-space carving predicate including the lateral aquitard guard.
    [[nodiscard]] bool shouldCarveAt(int32_t wx, int32_t wy, int32_t wz, const ColumnSampler& sampler) const noexcept {
        return shouldCarve(wx, wy, wz, resolveColumn(sampler, wx, wz));
    }

    // World-space twin of carveChunk: true iff carveChunk excavates (wx, wy, wz) when the
    // pass-1 block there is carvable. Adds the aquifer seal and the palette rule (no lava
    // block -> magma band stays solid) to shouldCarveAt. Pure function of (seed, position,
    // sampler, palette).
    [[nodiscard]] bool excavatesAt(int32_t wx, int32_t wy, int32_t wz, const ColumnSampler& sampler,
                                   const CaveFillPalette& palette = {}) const noexcept;

    // Chunk-local carving: writes only into `chunk`, reads only `chunk` + sampler, so chunks
    // can be carved in any order (or in parallel) with identical results. Writes are not
    // flagged as player modifications.
    CarveStats carveChunk(voxel::Chunk& chunk, const ColumnSampler& sampler,
                          const CaveFillPalette& palette = {}) const noexcept;

private:
    [[nodiscard]] bool isAquiferColumn(int32_t wx, int32_t wz) const noexcept;
    [[nodiscard]] bool isAquiferSealColumn(int32_t wx, int32_t wz) const noexcept;

    // Hermite C1 smoothstep S(t) = 3t^2 - 2t^3: no visible "shelf" where caves fade out.
    [[nodiscard]] static constexpr float hermite(float t) noexcept { return t * t * (3.0f - 2.0f * t); }

    uint64_t m_seed;
    SimplexNoise m_wormNoiseA;
    SimplexNoise m_wormNoiseB;
    SimplexNoise m_cheeseNoise;
    SimplexNoise m_aquiferNoise;
};

} // namespace betacraft::worldgen
