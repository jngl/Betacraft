#pragma once

#include "lighting/DynamicLight.hpp"
#include "lighting/DynamicLightUbo.hpp"

#include <glm/glm.hpp>

#include <array>
#include <cstdint>
#include <optional>
#include <span>

namespace betacraft::lighting {

// Screen-space rectangle in normalized device coordinates, clamped to [-1, 1].
struct NdcRect {
    glm::vec2 min{0.0f};
    glm::vec2 max{0.0f};
};

// Conservative screen bounds of a light sphere: projects the 8 corners of its bounding cube.
// Returns nullopt when the cube is entirely outside one clip plane (behind the camera,
// off-screen, beyond the far plane). When the cube straddles the camera plane (w <= 0) the
// projection is unbounded, so the whole screen is returned rather than risking a missed tile.
[[nodiscard]] std::optional<NdcRect> projectSphereToNdc(const glm::vec3& center, float radius,
                                                        const glm::mat4& viewProj) noexcept;

// Forward+ light grid: the screen is split into kTileCols x kTileRows tiles and each tile
// records which of the uploaded lights can reach it. The fragment shader then loops over a
// 16-bit mask instead of all lights. Row 0 is the BOTTOM row, matching gl_FragCoord.
class TileLightGrid {
public:
    // `lights` are the uploaded lights in UBO order (only the first kMaxDynamicLights count).
    void build(std::span<const DynamicLight> lights, const glm::mat4& viewProj, const glm::vec2& viewportPx) noexcept;

    // Bit i set <=> lights[i] may affect the tile. Out-of-range tiles return 0.
    [[nodiscard]] uint32_t mask(uint32_t col, uint32_t row) const noexcept;

    // Row-major masks (index = row * kTileCols + col), ready for packDynamicLightUbo().
    [[nodiscard]] std::span<const uint32_t> masks() const noexcept { return m_masks; }

    // Ascending light indices affecting the tile (the classic Forward+ per-tile list).
    [[nodiscard]] std::span<const uint8_t> lightIndices(uint32_t col, uint32_t row) const noexcept;

    // viewport / (16, 9), never below 1 px (the shader clamps the same way).
    [[nodiscard]] glm::vec2 tileSizePx() const noexcept { return m_tileSizePx; }

    // CPU mirror of dynTileIndex() in dynamic_lights.glsl (checked by executing the shader).
    [[nodiscard]] uint32_t tileIndexForFragCoord(const glm::vec2& fragCoord) const noexcept;

private:
    std::array<uint32_t, kTileCount> m_masks{};
    std::array<std::array<uint8_t, kMaxDynamicLights>, kTileCount> m_indices{};
    std::array<uint8_t, kTileCount> m_indexCounts{};
    glm::vec2 m_tileSizePx{1.0f};
};

} // namespace betacraft::lighting
