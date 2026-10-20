#pragma once

#include "core/Types.hpp"
#include "voxel/Block.hpp"

#include <cstdint>
#include <cstdlib>
#include <tuple>
#include <vector>

namespace betacraft::voxel {
class Chunk;
}

namespace betacraft::worldgen {

// P3.4 decoration, split in two tiers (same philosophy as the Jigsaw spec, Fiche 4 s.1-5):
//   Tier 1 - planning: FeaturePlanner turns a pass-1 chunk into an abstract FeaturePlan
//            (pure function of world seed + that chunk's terrain; no voxel written).
//   Tier 2 - rasterization: rasterizeFeature() writes the part of a feature that lies inside
//            ONE target chunk. A chunk is decorated by rasterizing the plans of its 3x3
//            neighbourhood, so trees spill across borders without ever writing outside the
//            chunk being decorated.

// Largest horizontal distance a feature may extend beyond the chunk that planned it.
// The 3x3 neighbourhood rule is only sufficient while this stays <= 16.
constexpr int32_t MAX_FEATURE_OVERHANG = 8;
static_assert(MAX_FEATURE_OVERHANG <= 16, "features would need a 5x5 neighbourhood");

enum class FeatureType : uint8_t {
    OakTree = 0,
    BirchTree,
    PineTree,
    TallGrass,
    Dandelion,
    Rose,
    DeadBush,
    Cactus,
    SugarCane
};

// Planning only reads the planned chunk, so a border column cannot know whether the water
// rule (sugar cane beside water) applies when its face neighbour lies in the next chunk.
// Such a column plans both outcomes, each tagged with the condition under which it exists;
// pass 2 resolves the condition from the 3x3 neighbourhood, which it has anyway. This is
// exact (same result as an interior column) and order independent, because sea-level water
// is pass-1 data that decoration never changes (mergeDecoration keeps terrain/fluids).
enum class FeatureCondition : uint8_t {
    None = 0,
    AdjacentWater,  // exists iff a face neighbour of the column holds water at surface level
    NoAdjacentWater // the column's ordinary features when that water is absent
};

struct Feature {
    FeatureType type{FeatureType::TallGrass};
    core::BlockPos origin{}; // world position of the first block above the surface
    uint8_t height{1};       // trunk / stalk height (1 for single-block plants)
    uint32_t variant{0};     // hash bits for shape jitter (leaf corners)
    FeatureCondition condition{FeatureCondition::None};

    constexpr bool operator==(const Feature& other) const noexcept = default;

    // Canonical order (x, z, y, type, ...) so plans compare and merge deterministically.
    [[nodiscard]] constexpr bool operator<(const Feature& o) const noexcept {
        return std::tie(origin.x, origin.z, origin.y, type, height, variant, condition) <
               std::tie(o.origin.x, o.origin.z, o.origin.y, o.type, o.height, o.variant, o.condition);
    }
};

struct FeatureBounds {
    core::BlockPos min{};
    core::BlockPos max{}; // inclusive
};

using FeaturePlan = std::vector<Feature>;

// Resolves f.condition. `isWaterAt(wx, y, wz)` reports pass-1 water; it is only called for
// the 4 face neighbours of the origin column at surface level (origin.y - 1), all of which
// lie within the 3x3 chunk neighbourhood of any chunk the feature touches (overhang <= 2).
template <typename IsWater>
[[nodiscard]] constexpr bool featureConditionHolds(const Feature& f, IsWater&& isWaterAt) {
    if (f.condition == FeatureCondition::None) return true;
    const int32_t y = f.origin.y - 1;
    const bool water = isWaterAt(f.origin.x + 1, y, f.origin.z) || isWaterAt(f.origin.x - 1, y, f.origin.z) ||
                       isWaterAt(f.origin.x, y, f.origin.z + 1) || isWaterAt(f.origin.x, y, f.origin.z - 1);
    return (f.condition == FeatureCondition::AdjacentWater) == water;
}

[[nodiscard]] constexpr bool isTreeFeature(FeatureType t) noexcept {
    return t == FeatureType::OakTree || t == FeatureType::BirchTree || t == FeatureType::PineTree;
}

[[nodiscard]] constexpr voxel::BlockId featureLogBlock(FeatureType t) noexcept {
    switch (t) {
        case FeatureType::BirchTree: return voxel::BlockId::BirchLog;
        case FeatureType::PineTree:  return voxel::BlockId::PineLog;
        default:                     return voxel::BlockId::OakLog;
    }
}

[[nodiscard]] constexpr FeatureBounds featureBounds(const Feature& f) noexcept {
    const core::BlockPos o = f.origin;
    const int32_t h = static_cast<int32_t>(f.height);
    if (isTreeFeature(f.type)) {
        // Dirt under the trunk, crown up to 1 block above the trunk top, radius 2.
        return FeatureBounds{o.offset(-2, -1, -2), o.offset(2, h + 1, 2)};
    }
    if (f.type == FeatureType::Cactus || f.type == FeatureType::SugarCane) {
        return FeatureBounds{o, o.offset(0, h - 1, 0)};
    }
    return FeatureBounds{o, o};
}

// Enumerates every (world position, block) a feature wants, each position at most once.
// Geometry is a port of the legacy WorldGen::placeTree crowns (Beta 1.7.3 look).
template <typename Fn>
constexpr void forEachFeatureBlock(const Feature& f, Fn&& emit) {
    using voxel::BlockId;
    const core::BlockPos o = f.origin;
    const int32_t h = static_cast<int32_t>(f.height);

    if (isTreeFeature(f.type)) {
        emit(o.offset(0, -1, 0), BlockId::Dirt);
        for (int32_t ty = 0; ty < h; ++ty) emit(o.offset(0, ty, 0), featureLogBlock(f.type));

        const int32_t trunkTop = o.y + h; // first Y above the last log
        auto leaf = [&](int32_t dx, int32_t ly, int32_t dz) {
            if (dx == 0 && dz == 0 && ly < trunkTop) return; // trunk occupies it
            emit(core::BlockPos{o.x + dx, ly, o.z + dz}, BlockId::Leaves);
        };

        if (f.type == FeatureType::PineTree) {
            // Conifer cone: alternating radius 2 / 1, single tip leaf.
            for (int32_t ly = o.y + 2; ly <= trunkTop + 1; ++ly) {
                const int32_t r = (ly == trunkTop + 1) ? 0 : ((ly % 2 == 0) ? 2 : 1);
                for (int32_t dz = -r; dz <= r; ++dz)
                    for (int32_t dx = -r; dx <= r; ++dx) {
                        if (r > 0 && std::abs(dx) == r && std::abs(dz) == r) continue;
                        leaf(dx, ly, dz);
                    }
            }
        } else {
            // Oak / birch: two wide layers then two narrow ones, jittered wide corners.
            for (int32_t ly = trunkTop - 2; ly <= trunkTop + 1; ++ly) {
                const int32_t dy = ly - trunkTop;
                const int32_t r = (dy >= 0) ? 1 : 2;
                for (int32_t dz = -r; dz <= r; ++dz)
                    for (int32_t dx = -r; dx <= r; ++dx) {
                        if (std::abs(dx) == r && std::abs(dz) == r) {
                            const uint32_t jitter = f.variant + static_cast<uint32_t>(dx * 7 + dz * 13 + ly);
                            if (dy >= 0 || (jitter & 1U) == 0U) continue;
                        }
                        leaf(dx, ly, dz);
                    }
            }
        }
        return;
    }

    switch (f.type) {
        case FeatureType::Cactus:
        case FeatureType::SugarCane: {
            const BlockId b = (f.type == FeatureType::Cactus) ? BlockId::Cactus : BlockId::SugarCane;
            for (int32_t i = 0; i < h; ++i) emit(o.offset(0, i, 0), b);
            break;
        }
        case FeatureType::TallGrass: emit(o, BlockId::TallGrass); break;
        case FeatureType::Dandelion: emit(o, BlockId::Dandelion); break;
        case FeatureType::Rose:      emit(o, BlockId::Rose); break;
        case FeatureType::DeadBush:  emit(o, BlockId::DeadBush); break;
        default: break;
    }
}

// Decoration layering priority; -1 = terrain (never overwritten by decoration).
[[nodiscard]] constexpr int decorationPriority(voxel::BlockId b) noexcept {
    using voxel::BlockId;
    switch (b) {
        case BlockId::Air: return 0;
        case BlockId::TallGrass: case BlockId::Dandelion: case BlockId::Rose: case BlockId::DeadBush: return 1;
        case BlockId::Leaves: return 2;
        case BlockId::OakLog: case BlockId::BirchLog: case BlockId::PineLog:
        case BlockId::Cactus: case BlockId::SugarCane: return 3;
        default: return -1;
    }
}

// Commutative, associative merge of a decoration proposal into an existing block: the
// result of applying any set of proposals is independent of their order, which is what
// makes overlapping trees from different chunks identical whatever the load order.
[[nodiscard]] constexpr voxel::BlockId mergeDecoration(voxel::BlockId existing, voxel::BlockId proposed) noexcept {
    using voxel::BlockId;
    if (proposed == BlockId::Dirt) {
        // Only soil conversion under trunks; never creates floating dirt.
        return (existing == BlockId::Grass || existing == BlockId::SnowGrass) ? BlockId::Dirt : existing;
    }
    const int pe = decorationPriority(existing);
    const int pp = decorationPriority(proposed);
    if (pe < 0 || pp < 0) return existing;
    if (pp > pe || (pp == pe && static_cast<uint8_t>(proposed) > static_cast<uint8_t>(existing))) return proposed;
    return existing;
}

[[nodiscard]] bool featureIntersectsChunk(const Feature& f, core::ChunkCoord coord) noexcept;

// Writes the part of `f` inside `chunk` through mergeDecoration. Returns the number of
// voxels changed. Writes are generation output: not flagged as player modifications.
uint32_t rasterizeFeature(const Feature& f, voxel::Chunk& chunk) noexcept;

// Tier 1 planner. Exactly one RNG draw per column in a fixed scan order, from a stream
// seeded by (world seed, chunk coord): plans never depend on chunk generation order.
class FeaturePlanner {
public:
    explicit FeaturePlanner(uint64_t worldSeed) noexcept : m_seed(worldSeed) {}

    [[nodiscard]] uint64_t getSeed() const noexcept { return m_seed; }

    // `chunk` must hold pass-1 terrain only (terrain + caves, no decoration yet).
    [[nodiscard]] FeaturePlan plan(const voxel::Chunk& chunk) const;

private:
    uint64_t m_seed;
};

} // namespace betacraft::worldgen
