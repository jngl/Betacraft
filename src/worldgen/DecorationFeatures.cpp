#include "worldgen/DecorationFeatures.hpp"
#include "worldgen/SeededRandom.hpp"

#include "core/Config.hpp"
#include "voxel/Chunk.hpp"

#include <algorithm>

namespace betacraft::worldgen {

using world::BiomeId;
using world::BlockId;
using world::Chunk;

bool featureIntersectsChunk(const Feature& f, core::ChunkCoord coord) noexcept {
    const FeatureBounds b = featureBounds(f);
    const int32_t minX = coord.x * Chunk::WIDTH;
    const int32_t minZ = coord.z * Chunk::DEPTH;
    return b.max.x >= minX && b.min.x < minX + Chunk::WIDTH &&
           b.max.z >= minZ && b.min.z < minZ + Chunk::DEPTH;
}

uint32_t rasterizeFeature(const Feature& f, Chunk& chunk) noexcept {
    const core::ChunkCoord cc = chunk.getCoord();
    if (!featureIntersectsChunk(f, cc)) return 0U;

    const int32_t minX = cc.x * Chunk::WIDTH;
    const int32_t minZ = cc.z * Chunk::DEPTH;
    uint32_t written = 0;
    forEachFeatureBlock(f, [&](core::BlockPos p, BlockId proposed) {
        // Clip to this chunk: local coords are guaranteed in [0, 16) before any write.
        const int32_t lx = p.x - minX;
        const int32_t lz = p.z - minZ;
        if (!Chunk::inBounds(lx, p.y, lz)) return;
        const BlockId existing = chunk.getBlock(lx, p.y, lz);
        const BlockId merged = mergeDecoration(existing, proposed);
        if (merged != existing) {
            chunk.setBlock(lx, p.y, lz, merged, false);
            ++written;
        }
    });
    return written;
}

namespace {

[[nodiscard]] uint32_t treeChanceFor(BiomeId biome) noexcept {
    switch (biome) {
        case BiomeId::Rainforest:
        case BiomeId::Forest:
        case BiomeId::SeasonalForest: return 42U;
        case BiomeId::Taiga:          return 55U;
        case BiomeId::Plains:
        case BiomeId::Savanna:        return 320U;
        default:                      return 180U;
    }
}

[[nodiscard]] bool columnClear(const Chunk& c, int32_t lx, int32_t y0, int32_t y1, int32_t lz) noexcept {
    for (int32_t y = y0; y <= y1; ++y) {
        const BlockId b = c.getBlock(lx, y, lz);
        if (b != BlockId::Air && b != BlockId::Leaves) return false;
    }
    return true;
}

} // namespace

FeaturePlan FeaturePlanner::plan(const Chunk& chunk) const {
    constexpr int32_t seaLevel = core::EngineConfig::SEA_LEVEL;
    const core::ChunkCoord cc = chunk.getCoord();
    const int32_t baseX = cc.x * Chunk::WIDTH;
    const int32_t baseZ = cc.z * Chunk::DEPTH;

    SeededRandom rng(chunkSeed(m_seed, cc));
    FeaturePlan plan;

    for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz) {
        for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
            // Draw unconditionally so every column's roll is fixed by its scan index alone.
            const uint64_t roll = rng.next();
            const uint32_t colHash = static_cast<uint32_t>(roll);
            const uint32_t hiHash = static_cast<uint32_t>(roll >> 32U);

            int32_t topY = -1;
            for (int32_t y = Chunk::HEIGHT - 2; y >= seaLevel; --y) {
                const BlockId b = chunk.getBlock(lx, y, lz);
                if (b != BlockId::Air && b != BlockId::Leaves) {
                    topY = y;
                    break;
                }
            }
            if (topY < seaLevel || topY >= Chunk::HEIGHT - 8) continue;

            const BlockId surface = chunk.getBlock(lx, topY, lz);
            const BiomeId biome = chunk.getColumnBiome(lx, lz);
            const core::BlockPos origin{baseX + lx, topY + 1, baseZ + lz};
            FeatureCondition condition = FeatureCondition::None;
            auto push = [&](FeatureType t, int32_t h) {
                plan.push_back(Feature{t, origin, static_cast<uint8_t>(h), hiHash, condition});
            };

            // Sugar cane beside water at sea level. Planning only reads this chunk: when a
            // face neighbour lies in the next chunk and no in-chunk neighbour is water, both
            // outcomes are planned with a condition that pass 2 resolves (FeatureCondition).
            if (topY == seaLevel && (surface == BlockId::Sand || surface == BlockId::Grass) &&
                (colHash % 38U) == 0U) {
                bool nearWater = false;
                bool unknownNeighbour = false;
                constexpr int32_t DIRS[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (const auto& d : DIRS) {
                    const int32_t nx = lx + d[0];
                    const int32_t nz = lz + d[1];
                    if (nx < 0 || nx >= Chunk::WIDTH || nz < 0 || nz >= Chunk::DEPTH) {
                        unknownNeighbour = true;
                    } else if (chunk.getBlock(nx, seaLevel, nz) == BlockId::Water) {
                        nearWater = true;
                    }
                }
                if (nearWater || unknownNeighbour) {
                    const int32_t h = 2 + static_cast<int32_t>((colHash >> 8U) % 2U);
                    const bool clear = columnClear(chunk, lx, topY + 1, topY + h, lz);
                    if (nearWater) {
                        if (clear) push(FeatureType::SugarCane, h);
                        continue;
                    }
                    condition = FeatureCondition::AdjacentWater;
                    if (clear) push(FeatureType::SugarCane, h);
                    condition = FeatureCondition::NoAdjacentWater; // ordinary rules below
                }
            }

            if (surface == BlockId::Sand && biome == BiomeId::Desert) {
                if ((colHash % 140U) == 0U) {
                    const int32_t h = 2 + static_cast<int32_t>((colHash >> 7U) % 2U);
                    if (columnClear(chunk, lx, topY + 1, topY + h, lz)) push(FeatureType::Cactus, h);
                } else if ((colHash % 95U) == 1U) {
                    push(FeatureType::DeadBush, 1);
                }
                continue;
            }

            if (surface != BlockId::Grass && surface != BlockId::SnowGrass) continue;

            // Trees may now root on ANY column, border columns included: their crowns are
            // rasterized into the neighbours during pass 2 instead of being clipped away.
            if ((colHash % treeChanceFor(biome)) == 0U) {
                FeatureType type = FeatureType::OakTree;
                if (biome == BiomeId::Taiga || biome == BiomeId::Tundra) {
                    type = FeatureType::PineTree;
                } else if ((colHash >> 5U) % 5U == 0U) {
                    type = FeatureType::BirchTree;
                }
                const int32_t h = 4 + static_cast<int32_t>(hiHash % 3U);
                const int32_t y = topY + 1;
                if (y + h + 2 < Chunk::HEIGHT && columnClear(chunk, lx, y, y + h + 1, lz)) push(type, h);
                continue;
            }

            if (surface == BlockId::Grass && chunk.getBlock(lx, topY + 1, lz) == BlockId::Air) {
                const uint32_t floraRoll = (colHash >> 4U) % 100U;
                if (floraRoll < 14U) {
                    push(FeatureType::TallGrass, 1);
                } else if (floraRoll == 15U) {
                    push(FeatureType::Dandelion, 1);
                } else if (floraRoll == 16U && ((colHash >> 12U) % 2U == 0U)) {
                    push(FeatureType::Rose, 1);
                }
            }
        }
    }

    std::sort(plan.begin(), plan.end());
    return plan;
}

} // namespace betacraft::worldgen
