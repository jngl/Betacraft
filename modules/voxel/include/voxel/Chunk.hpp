#pragma once

#include "core/Config.hpp"
#include "core/Types.hpp"
#include "voxel/Block.hpp"
#include "voxel/PalettedSection.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace betacraft::voxel {

enum class BiomeId : uint8_t {
    Rainforest = 0,
    Swampland,
    SeasonalForest,
    Forest,
    Savanna,
    Shrubland,
    Taiga,
    Desert,
    Plains,
    Tundra
};

[[nodiscard]] constexpr std::string_view biomeName(BiomeId biome) noexcept {
    switch (biome) {
        case BiomeId::Rainforest:     return "Rainforest";
        case BiomeId::Swampland:      return "Swampland";
        case BiomeId::SeasonalForest: return "Seasonal Forest";
        case BiomeId::Forest:         return "Forest";
        case BiomeId::Savanna:        return "Savanna";
        case BiomeId::Shrubland:      return "Shrubland";
        case BiomeId::Taiga:          return "Taiga";
        case BiomeId::Desert:         return "Desert";
        case BiomeId::Plains:         return "Plains";
        case BiomeId::Tundra:         return "Tundra";
    }
    return "Plains";
}

class Chunk {
public:
    static constexpr int32_t WIDTH = core::EngineConfig::CHUNK_WIDTH;
    static constexpr int32_t HEIGHT = core::EngineConfig::CHUNK_HEIGHT;
    static constexpr int32_t DEPTH = core::EngineConfig::CHUNK_DEPTH;
    static constexpr int32_t VOLUME = core::EngineConfig::CHUNK_VOLUME;
    static constexpr int32_t COLUMNS = WIDTH * DEPTH;
    static constexpr size_t SECTIONS_PER_CHUNK = static_cast<size_t>(HEIGHT / PalettedSection::SIZE); // 8 sections
    static_assert(SECTIONS_PER_CHUNK <= 8, "the dirty-section mask is one byte");
    static constexpr uint8_t ALL_SECTIONS_MASK = static_cast<uint8_t>((1U << SECTIONS_PER_CHUNK) - 1U);

    explicit Chunk(core::ChunkCoord coord) noexcept;
    explicit Chunk(int32_t cx, int32_t cz) noexcept : Chunk(core::ChunkCoord{cx, cz}) {}

    [[nodiscard]] constexpr core::ChunkCoord getCoord() const noexcept { return m_coord; }

    [[nodiscard]] static constexpr bool inBounds(int32_t x, int32_t y, int32_t z) noexcept {
        return x >= 0 && x < WIDTH && y >= 0 && y < HEIGHT && z >= 0 && z < DEPTH;
    }

    [[nodiscard]] static constexpr size_t blockIndex(int32_t x, int32_t y, int32_t z) noexcept {
        return static_cast<size_t>((y * DEPTH + z) * WIDTH + x);
    }

    [[nodiscard]] static constexpr size_t columnIndex(int32_t x, int32_t z) noexcept {
        return static_cast<size_t>(z * WIDTH + x);
    }

    [[nodiscard]] BlockId getBlock(int32_t x, int32_t y, int32_t z) const noexcept;
    void setBlock(int32_t x, int32_t y, int32_t z, BlockId id, bool markModified = true) noexcept;

    [[nodiscard]] uint8_t getSkyLight(int32_t x, int32_t y, int32_t z) const noexcept;
    [[nodiscard]] uint8_t getBlockLight(int32_t x, int32_t y, int32_t z) const noexcept;
    void setSkyLight(int32_t x, int32_t y, int32_t z, uint8_t val) noexcept;
    void setBlockLight(int32_t x, int32_t y, int32_t z, uint8_t val) noexcept;

    // 4-bit per-voxel metadata (fluid level nibble); 0 outside the chunk. Writes mark the
    // chunk modified but no section dirty.
    [[nodiscard]] uint8_t getMetadata(int32_t x, int32_t y, int32_t z) const noexcept;
    void setMetadata(int32_t x, int32_t y, int32_t z, uint8_t meta, bool markModified = true) noexcept;

    [[nodiscard]] uint8_t getHeightAt(int32_t x, int32_t z) const noexcept;

    void setColumnBiome(int32_t x, int32_t z, BiomeId biome, const glm::vec3& grassTint, const glm::vec3& foliageTint) noexcept;
    [[nodiscard]] BiomeId getColumnBiome(int32_t x, int32_t z) const noexcept;
    [[nodiscard]] glm::vec3 getGrassTint(int32_t x, int32_t z) const noexcept;
    [[nodiscard]] glm::vec3 getFoliageTint(int32_t x, int32_t z) const noexcept;

    void recalculateLighting() noexcept;
    void spreadBlockLight(int32_t x, int32_t y, int32_t z, uint8_t light) noexcept;
    void spreadSkyLight(int32_t x, int32_t y, int32_t z, uint8_t light) noexcept;

    // Sub-chunk PalettedSection access
    [[nodiscard]] PalettedSection& getSection(size_t index) noexcept { return m_sections[index]; }
    [[nodiscard]] const PalettedSection& getSection(size_t index) const noexcept { return m_sections[index]; }
    [[nodiscard]] std::array<PalettedSection, SECTIONS_PER_CHUNK>& getSections() noexcept { return m_sections; }
    [[nodiscard]] const std::array<PalettedSection, SECTIONS_PER_CHUNK>& getSections() const noexcept { return m_sections; }

    // Mesh staleness is tracked per 16-high section (bit s = section s) so an edit re-meshes
    // only the sections whose 18^3 meshing neighbourhood it touched.
    [[nodiscard]] bool isMeshDirty() const noexcept { return m_dirtySections != 0; }
    void setMeshDirty(bool dirty) noexcept { m_dirtySections = dirty ? ALL_SECTIONS_MASK : uint8_t{0}; }
    [[nodiscard]] uint8_t dirtySectionMask() const noexcept { return m_dirtySections; }
    [[nodiscard]] bool isSectionDirty(size_t index) const noexcept {
        return index < SECTIONS_PER_CHUNK && (m_dirtySections & (1U << index)) != 0;
    }
    void markSectionDirty(size_t index) noexcept {
        if (index < SECTIONS_PER_CHUNK) m_dirtySections = static_cast<uint8_t>(m_dirtySections | (1U << index));
    }
    void clearSectionDirty(size_t index) noexcept {
        if (index < SECTIONS_PER_CHUNK) m_dirtySections = static_cast<uint8_t>(m_dirtySections & ~(1U << index));
    }
    // A voxel at local height y is seen by its own section and, on a section seam, by the
    // one it borders (the 1-voxel guard of the 18^3 snapshot).
    void markSectionsAround(int32_t y) noexcept;

    [[nodiscard]] bool isModified() const noexcept { return m_modified; }
    void setModified(bool modified) noexcept { m_modified = modified; }

    // Sticky "the player changed this column" flag (World edits with EditOrigin::Player, or a
    // chunk read back from disk, which only ever holds edited chunks). Fluid simulation edits
    // are saved only next to such chunks (World::persistSimulationEdits), so generated water
    // settling in unexplored terrain never writes a .chk.
    [[nodiscard]] bool isPlayerTouched() const noexcept { return m_playerTouched; }
    void markPlayerTouched() noexcept { m_playerTouched = true; }

    // RLE Serialization for disk persistence
    [[nodiscard]] std::vector<uint8_t> serializeRLE() const;
    bool deserializeRLE(std::span<const uint8_t> data);

private:
    core::ChunkCoord m_coord{};
    std::array<PalettedSection, SECTIONS_PER_CHUNK> m_sections{
        PalettedSection(BlockId::Air, 15, 0),
        PalettedSection(BlockId::Air, 15, 0),
        PalettedSection(BlockId::Air, 15, 0),
        PalettedSection(BlockId::Air, 15, 0),
        PalettedSection(BlockId::Air, 15, 0),
        PalettedSection(BlockId::Air, 15, 0),
        PalettedSection(BlockId::Air, 15, 0),
        PalettedSection(BlockId::Air, 15, 0)
    };
    std::array<uint8_t, COLUMNS> m_heightMap{};
    std::array<BiomeId, COLUMNS> m_biomes{};
    std::array<glm::vec3, COLUMNS> m_grassTint{};
    std::array<glm::vec3, COLUMNS> m_foliageTint{};
    uint8_t m_dirtySections{ALL_SECTIONS_MASK};
    bool m_modified{false};
    bool m_playerTouched{false};
};

} // namespace betacraft::voxel
