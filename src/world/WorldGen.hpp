#pragma once

#include "math/PerlinNoise.hpp"
#include "world/Chunk.hpp"
#include "worldgen/CaveCarver.hpp"

#include <array>
#include <cstdint>

// Concurrency (S7.7): pass 1 runs on the generation workers (streaming::ChunkGenPipeline),
// several threads sharing one const WorldGen. Nothing here or in worldgen/ may hold `mutable`
// members or non-const `static` state, and generation never reads World.
namespace betacraft::world {

struct BiomeSample {
    BiomeId biome{BiomeId::Plains};
    float temperature{0.5f};
    float humidity{0.5f};
    glm::vec3 grassTint{0.54f, 0.78f, 0.34f};
    glm::vec3 foliageTint{0.45f, 0.72f, 0.27f};
};

class WorldGen {
public:
    explicit WorldGen(uint64_t seed = 173173173ULL) noexcept;

    void setSeed(uint64_t seed) noexcept;
    [[nodiscard]] uint64_t getSeed() const noexcept { return m_seed; }

    [[nodiscard]] BiomeSample sampleBiome(int32_t worldX, int32_t worldZ) const noexcept;
    void populateBiomeColumns(Chunk& chunk) const noexcept;

    // P3.4 pass 1 only: biomes, terrain, surface, P3.3 caves, S6.9 ore veins and S6.10
    // dungeons, computed from this chunk alone. Decoration (pass 2) and lighting are World's job once the 3x3
    // neighbourhood is resident; the chunk is left unlit and unmodified.
    void generateChunk(Chunk& chunk) const noexcept;

    // Pass-1 facts of any world column (generated or not), as CaveCarver::scanColumn would
    // read them from the generated chunk. Feeds the carver's lateral guard across borders.
    [[nodiscard]] worldgen::ColumnContext columnContext(int32_t worldX, int32_t worldZ) const noexcept;
    [[nodiscard]] const worldgen::CaveCarver& caveCarver() const noexcept { return m_caveCarver; }

private:
    using Column = std::array<BlockId, Chunk::HEIGHT>;
    // Step 1 of pass 1 for one column: bedrock, 3D overhang density, sea fill.
    void fillTerrainColumn(int32_t worldX, int32_t worldZ, BiomeId biome, Column& out) const noexcept;

    uint64_t m_seed{173173173ULL};
    math::PerlinNoise m_continentNoise;
    math::PerlinNoise m_detailNoise;
    math::PerlinNoise m_overhangNoise;
    math::PerlinNoise m_tempNoise;
    math::PerlinNoise m_humidNoise;
    math::PerlinNoise m_beachNoise;
    worldgen::CaveCarver m_caveCarver{m_seed};
};

} // namespace betacraft::world
