#include "world/WorldGen.hpp"
#include "core/Config.hpp"
#include "worldgen/ColumnSampling.hpp"
#include "worldgen/Dungeons.hpp"
#include "worldgen/OreVeins.hpp"

#include <algorithm>
#include <cmath>

namespace betacraft::world {

WorldGen::WorldGen(uint64_t seed) noexcept {
    setSeed(seed);
}

void WorldGen::setSeed(uint64_t seed) noexcept {
    m_seed = seed;
    m_continentNoise.reseed(seed ^ 0x1A2B3C4D5E6F7788ULL);
    m_detailNoise.reseed(seed ^ 0x8877665544332211ULL);
    m_overhangNoise.reseed(seed ^ 0xCAFEBABE12345678ULL);
    m_tempNoise.reseed(seed ^ 0x13579BDF2468ACE0ULL);
    m_humidNoise.reseed(seed ^ 0x0ECA8642FDB97531ULL);
    m_beachNoise.reseed(seed ^ 0xA5A55A5AF0F00F0FULL);
    m_caveCarver = worldgen::CaveCarver(seed);
}

BiomeSample WorldGen::sampleBiome(int32_t worldX, int32_t worldZ) const noexcept {
    const double tRaw = m_tempNoise.fbm2D(worldX * 0.0022, worldZ * 0.0022, 3, 2.0, 0.5);
    const double hRaw = m_humidNoise.fbm2D(worldX * 0.0025 + 113.0, worldZ * 0.0025 - 97.0, 3, 2.0, 0.5);

    const float temp = std::clamp(static_cast<float>(tRaw * 0.85 + 0.52), 0.0f, 1.0f);
    const float humid = std::clamp(static_cast<float>(hRaw * 0.85 + 0.50) * temp, 0.0f, 1.0f);

    BiomeId biome = BiomeId::Plains;
    if (temp < 0.22f) {
        biome = (humid < 0.12f) ? BiomeId::Tundra : BiomeId::Taiga;
    } else if (temp < 0.45f) {
        biome = (humid < 0.22f) ? BiomeId::Shrubland : BiomeId::Forest;
    } else if (temp < 0.76f) {
        if (humid < 0.22f) {
            biome = BiomeId::Plains;
        } else if (humid < 0.56f) {
            biome = BiomeId::Forest;
        } else {
            biome = BiomeId::SeasonalForest;
        }
    } else {
        if (humid < 0.19f) {
            biome = BiomeId::Desert;
        } else if (humid < 0.42f) {
            biome = BiomeId::Savanna;
        } else if (humid < 0.68f) {
            biome = BiomeId::Swampland;
        } else {
            biome = BiomeId::Rainforest;
        }
    }

    // Authentic Beta 1.7.3 grasscolor.png / foliagecolor.png bilinear color ramp
    const glm::vec3 dryWarmGrass(0.74f, 0.72f, 0.33f);
    const glm::vec3 wetWarmGrass(0.32f, 0.84f, 0.24f);
    const glm::vec3 coldGrass(0.50f, 0.71f, 0.55f);

    const glm::vec3 warmBlend = glm::mix(dryWarmGrass, wetWarmGrass, humid);
    const glm::vec3 grassTint = glm::mix(coldGrass, warmBlend, temp);

    const glm::vec3 dryWarmFoliage(0.68f, 0.66f, 0.26f);
    const glm::vec3 wetWarmFoliage(0.24f, 0.78f, 0.18f);
    const glm::vec3 coldFoliage(0.38f, 0.62f, 0.46f);

    const glm::vec3 warmFoliage = glm::mix(dryWarmFoliage, wetWarmFoliage, humid);
    const glm::vec3 foliageTint = glm::mix(coldFoliage, warmFoliage, temp);

    return BiomeSample{
        .biome = biome,
        .temperature = temp,
        .humidity = humid,
        .grassTint = grassTint,
        .foliageTint = foliageTint
    };
}

void WorldGen::populateBiomeColumns(Chunk& chunk) const noexcept {
    const int32_t baseX = chunk.getCoord().x * Chunk::WIDTH;
    const int32_t baseZ = chunk.getCoord().z * Chunk::DEPTH;

    for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz) {
        for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
            const BiomeSample sample = sampleBiome(baseX + lx, baseZ + lz);
            chunk.setColumnBiome(lx, lz, sample.biome, sample.grassTint, sample.foliageTint);
        }
    }
}

void WorldGen::fillTerrainColumn(int32_t wx, int32_t wz, BiomeId biome, Column& out) const noexcept {
    constexpr int32_t seaLevel = core::EngineConfig::SEA_LEVEL;

    const double cont = m_continentNoise.fbm2D(wx * 0.0042, wz * 0.0042, 4, 2.0, 0.5);
    const double detail = m_detailNoise.fbm2D(wx * 0.018, wz * 0.018, 4, 2.0, 0.5);
    const double mountainMask = std::max(0.0, m_continentNoise.noise2D(wx * 0.0065 + 40.0, wz * 0.0065 - 40.0));

    double targetHeight = static_cast<double>(seaLevel) + 4.0 + cont * 20.0 + detail * 8.0 + mountainMask * 22.0;
    if (biome == BiomeId::Desert || biome == BiomeId::Plains) {
        targetHeight = static_cast<double>(seaLevel) + 3.0 + cont * 10.0 + detail * 4.0;
    }

    for (int32_t y = 0; y < Chunk::HEIGHT; ++y) {
        BlockId& block = out[static_cast<size_t>(y)];
        if (y == 0) {
            block = BlockId::Bedrock;
            continue;
        }
        if (y <= 3) {
            const uint32_t h = math::PerlinNoise::hashCoords(wx, y, wz, m_seed);
            if (static_cast<int32_t>(h % 4U) >= y) {
                block = BlockId::Bedrock;
                continue;
            }
        }

        // 3D overhang density around surface/hills
        double density = (targetHeight - static_cast<double>(y)) / 12.0;
        if (y > 42 && y < 108) {
            const double n3d = m_overhangNoise.fbm3D(wx * 0.026, y * 0.032, wz * 0.026, 3, 2.0, 0.5);
            density += n3d * (0.45 + mountainMask * 0.75);
        }

        if (density > 0.0) {
            block = BlockId::Stone;
        } else if (y <= seaLevel) {
            block = (y == seaLevel && biome == BiomeId::Tundra) ? BlockId::Ice : BlockId::Water;
        } else {
            block = BlockId::Air;
        }
    }
}

worldgen::ColumnContext WorldGen::columnContext(int32_t worldX, int32_t worldZ) const noexcept {
    // Surface replacement (step 2) only swaps solid for solid and the ore veins and dungeons
    // come after the scan, so the step-1 column already has the solid / fluid / air layout
    // the scan reads.
    Column column{};
    fillTerrainColumn(worldX, worldZ, sampleBiome(worldX, worldZ).biome, column);
    return worldgen::CaveCarver::scanColumnBlocks(
        [&column](int32_t y) noexcept { return column[static_cast<size_t>(y)]; }, Chunk::HEIGHT);
}

void WorldGen::generateChunk(Chunk& chunk) const noexcept {
    populateBiomeColumns(chunk);

    const int32_t baseX = chunk.getCoord().x * Chunk::WIDTH;
    const int32_t baseZ = chunk.getCoord().z * Chunk::DEPTH;
    constexpr int32_t seaLevel = core::EngineConfig::SEA_LEVEL;

    // 1. Base 3D terrain density + 2D continental heightmap (Beta 1.7.3 overhang terrain)
    Column column{};
    for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz) {
        for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
            fillTerrainColumn(baseX + lx, baseZ + lz, chunk.getColumnBiome(lx, lz), column);
            for (int32_t y = 0; y < Chunk::HEIGHT; ++y) {
                chunk.setBlock(lx, y, lz, column[static_cast<size_t>(y)], false);
            }
        }
    }

    // 2. Surface replacement (Grass, Dirt, Sand, Gravel beaches, Sandstone, Clay)
    for (int32_t lz = 0; lz < Chunk::DEPTH; ++lz) {
        for (int32_t lx = 0; lx < Chunk::WIDTH; ++lx) {
            const int32_t wx = baseX + lx;
            const int32_t wz = baseZ + lz;
            const BiomeId biome = chunk.getColumnBiome(lx, lz);
            const double beachVal = m_beachNoise.noise2D(wx * 0.035, wz * 0.035);

            int32_t runDepth = -1;
            for (int32_t y = Chunk::HEIGHT - 1; y >= 1; --y) {
                const BlockId current = chunk.getBlock(lx, y, lz);
                if (current == BlockId::Air || current == BlockId::Water || current == BlockId::Ice) {
                    runDepth = -1;
                    continue;
                }

                if (current == BlockId::Stone) {
                    ++runDepth;
                    const bool underwater = (y + 1 < Chunk::HEIGHT) &&
                                            (chunk.getBlock(lx, y + 1, lz) == BlockId::Water ||
                                             chunk.getBlock(lx, y + 1, lz) == BlockId::Ice);

                    if (runDepth == 0) {
                        if (underwater) {
                            if (beachVal > 0.35 && y >= seaLevel - 4) {
                                chunk.setBlock(lx, y, lz, BlockId::Clay, false);
                            } else if (beachVal < -0.15) {
                                chunk.setBlock(lx, y, lz, BlockId::Gravel, false);
                            } else if (y >= seaLevel - 3) {
                                chunk.setBlock(lx, y, lz, BlockId::Sand, false);
                            } else {
                                chunk.setBlock(lx, y, lz, BlockId::Dirt, false);
                            }
                        } else if (y <= seaLevel + 1 && beachVal > -0.25) {
                            chunk.setBlock(lx, y, lz, (beachVal > 0.42) ? BlockId::Gravel : BlockId::Sand, false);
                        } else if (biome == BiomeId::Desert) {
                            chunk.setBlock(lx, y, lz, BlockId::Sand, false);
                        } else if (biome == BiomeId::Tundra) {
                            chunk.setBlock(lx, y, lz, BlockId::SnowGrass, false);
                        } else {
                            chunk.setBlock(lx, y, lz, BlockId::Grass, false);
                        }
                    } else if (runDepth <= 3) {
                        if (biome == BiomeId::Desert || (y <= seaLevel + 1 && beachVal > -0.25)) {
                            chunk.setBlock(lx, y, lz, (runDepth == 3) ? BlockId::Sandstone : BlockId::Sand, false);
                        } else {
                            chunk.setBlock(lx, y, lz, BlockId::Dirt, false);
                        }
                    }
                }
            }
        }
    }

    // 3. P3.3 caves. After step 2, which restarts its soil run at every air gap: carving
    // first would turn cave roofs into grass. In-chunk columns are read back from the blocks
    // just generated; ring columns of neighbours come from the identical column function.
    const core::ChunkCoord coord = chunk.getCoord();
    auto sampleColumn = [this, &chunk, coord](int32_t wx, int32_t wz) noexcept {
        if (core::floorDiv(wx, Chunk::WIDTH) == coord.x && core::floorDiv(wz, Chunk::DEPTH) == coord.z) {
            return worldgen::CaveCarver::scanColumn(chunk, core::floorMod(wx, Chunk::WIDTH), core::floorMod(wz, Chunk::DEPTH));
        }
        return columnContext(wx, wz);
    };
    const worldgen::FunctionColumnSampler<decltype(sampleColumn)> source(sampleColumn);
    // The cache samples every column once up front, before any block is carved.
    const worldgen::ChunkColumnCache columns(source, coord);
    (void)m_caveCarver.carveChunk(chunk, columns);

    // 4. S6.9 Beta ore veins (dirt, gravel and the ores), after the caves so the scans above
    // saw the bare terrain; they only replace stone. 5. S6.10 dungeons inside the chunk, their
    // ceilings under the surface the cache recorded before carving.
    worldgen::placeOreVeins(chunk, m_seed);
    (void)worldgen::placeDungeons(chunk, m_seed, columns);

    chunk.setModified(false);
}

} // namespace betacraft::world
