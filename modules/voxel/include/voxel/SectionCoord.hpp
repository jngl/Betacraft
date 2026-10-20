#pragma once

#include "core/Config.hpp"
#include "core/Types.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <glm/glm.hpp>

namespace betacraft::voxel {

// Address of one 16x16x16 sub-chunk: x/z are chunk coordinates, y is the vertical
// section index inside the 128-block column (0..7). This is the unit of meshing,
// versioning and GPU upload in the asynchronous pipeline.
struct SectionCoord {
    static constexpr int32_t SIZE = 16;
    static constexpr int32_t SECTIONS_PER_COLUMN = core::EngineConfig::CHUNK_HEIGHT / SIZE;

    int32_t x{0};
    int32_t y{0};
    int32_t z{0};

    constexpr bool operator==(const SectionCoord& other) const noexcept = default;

    [[nodiscard]] static constexpr SectionCoord fromWorldBlock(int32_t wx, int32_t wy, int32_t wz) noexcept {
        return {core::floorDiv(wx, SIZE), core::floorDiv(wy, SIZE), core::floorDiv(wz, SIZE)};
    }

    [[nodiscard]] constexpr core::ChunkCoord chunk() const noexcept { return {x, z}; }

    [[nodiscard]] constexpr glm::ivec3 worldOrigin() const noexcept {
        return {x * SIZE, y * SIZE, z * SIZE};
    }

    [[nodiscard]] constexpr bool hasValidY() const noexcept {
        return y >= 0 && y < SECTIONS_PER_COLUMN;
    }

    [[nodiscard]] glm::vec3 centre() const noexcept {
        return glm::vec3(worldOrigin()) + glm::vec3(static_cast<float>(SIZE) * 0.5f);
    }

    // Squared distance from the section centre; used as the scheduling key so the
    // closest sections are meshed first (centrifugal spiral around the player).
    [[nodiscard]] float distanceSqTo(const glm::vec3& p) const noexcept {
        const glm::vec3 d = centre() - p;
        return d.x * d.x + d.y * d.y + d.z * d.z;
    }
};

// Inclusive box of sections (Y clipped to the column; empty when max.y < min.y).
struct SectionBox {
    SectionCoord min{};
    SectionCoord max{};
};

// Sections whose 18^3 meshing neighbourhood contains world voxel (wx, wy, wz): its own
// section and, through the 1-voxel guard, the ones across every seam it lies on (at most
// two per axis). A changed voxel invalidates exactly these meshes.
[[nodiscard]] constexpr SectionBox sectionsSeeingVoxel(int32_t wx, int32_t wy, int32_t wz) noexcept {
    constexpr int32_t S = SectionCoord::SIZE;
    const int32_t yLo = core::floorDiv(wy - 1, S);
    const int32_t yHi = core::floorDiv(wy + 1, S);
    return {{core::floorDiv(wx - 1, S), yLo < 0 ? 0 : yLo, core::floorDiv(wz - 1, S)},
            {core::floorDiv(wx + 1, S), yHi >= SectionCoord::SECTIONS_PER_COLUMN ? SectionCoord::SECTIONS_PER_COLUMN - 1 : yHi,
             core::floorDiv(wz + 1, S)}};
}

} // namespace betacraft::voxel

namespace std {

template <>
struct hash<betacraft::voxel::SectionCoord> {
    size_t operator()(const betacraft::voxel::SectionCoord& c) const noexcept {
        // y fits in 3 bits for valid sections; mix it in with a large odd multiplier.
        const uint64_t ux = static_cast<uint32_t>(c.x);
        const uint64_t uz = static_cast<uint32_t>(c.z);
        const uint64_t uy = static_cast<uint32_t>(c.y);
        uint64_t h = (ux << 32ULL) ^ uz;
        h ^= uy * 0x9E3779B97F4A7C15ULL;
        h ^= h >> 29ULL;
        h *= 0xBF58476D1CE4E5B9ULL;
        h ^= h >> 32ULL;
        return static_cast<size_t>(h);
    }
};

} // namespace std
