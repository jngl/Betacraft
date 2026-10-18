#include "lighting/TileLightCuller.hpp"

#include <algorithm>
#include <cmath>

namespace betacraft::lighting {

namespace {

// Clip-space w below this means the point is on/behind the camera plane.
constexpr float kMinClipW = 1e-6f;

[[nodiscard]] uint32_t clampTile(float px, float tileSize, uint32_t tileCount) noexcept {
    const float t = std::floor(px / tileSize);
    if (!(t > 0.0f)) {
        return 0; // Also maps NaN to 0
    }
    const float last = static_cast<float>(tileCount - 1);
    return static_cast<uint32_t>(t < last ? t : last);
}

} // namespace

std::optional<NdcRect> projectSphereToNdc(const glm::vec3& center, float radius, const glm::mat4& viewProj) noexcept {
    if (!(radius > 0.0f)) {
        return std::nullopt;
    }

    std::array<glm::vec4, 8> clip{};
    for (size_t i = 0; i < clip.size(); ++i) {
        const glm::vec3 corner = center + glm::vec3((i & 1u) ? radius : -radius, (i & 2u) ? radius : -radius,
                                                    (i & 4u) ? radius : -radius);
        clip[i] = viewProj * glm::vec4(corner, 1.0f);
    }

    // Trivial rejection: every corner outside the same clip plane (-w <= x,y,z <= w).
    for (int axis = 0; axis < 3; ++axis) {
        bool allBelow = true;
        bool allAbove = true;
        for (const glm::vec4& c : clip) {
            allBelow = allBelow && (c[axis] < -c.w);
            allAbove = allAbove && (c[axis] > c.w);
        }
        if (allBelow || allAbove) {
            return std::nullopt;
        }
    }

    NdcRect rect{glm::vec2(1.0f), glm::vec2(-1.0f)};
    for (const glm::vec4& c : clip) {
        if (c.w <= kMinClipW) {
            return NdcRect{glm::vec2(-1.0f), glm::vec2(1.0f)};
        }
        const glm::vec2 ndc = glm::vec2(c) / c.w;
        rect.min = glm::min(rect.min, ndc);
        rect.max = glm::max(rect.max, ndc);
    }

    rect.min = glm::max(rect.min, glm::vec2(-1.0f));
    rect.max = glm::min(rect.max, glm::vec2(1.0f));
    if (rect.min.x > rect.max.x || rect.min.y > rect.max.y) {
        return std::nullopt;
    }
    return rect;
}

void TileLightGrid::build(std::span<const DynamicLight> lights, const glm::mat4& viewProj,
                          const glm::vec2& viewportPx) noexcept {
    m_masks.fill(0u);
    m_indexCounts.fill(0u);

    // A minimized window reports a 0x0 viewport; clamp so the shader never divides by zero.
    const glm::vec2 viewport = glm::max(viewportPx, glm::vec2(1.0f));
    // Tiles never shrink below one pixel: the shader divides by max(u_dynTileSizePx, 1.0), so
    // the CPU must bin lights with the very same size or tiny viewports address other tiles.
    // (Tiles past the viewport edge then simply stay unused.)
    m_tileSizePx = glm::max(viewport / glm::vec2(static_cast<float>(kTileCols), static_cast<float>(kTileRows)),
                            glm::vec2(1.0f));

    const size_t count = std::min<size_t>(lights.size(), kMaxDynamicLights);
    for (size_t i = 0; i < count; ++i) {
        if (!lights[i].isValid()) {
            continue;
        }
        const auto rect = projectSphereToNdc(lights[i].position, lights[i].radius, viewProj);
        if (!rect) {
            continue;
        }
        const glm::vec2 pxMin = (rect->min * 0.5f + 0.5f) * viewport;
        const glm::vec2 pxMax = (rect->max * 0.5f + 0.5f) * viewport;
        const uint32_t c0 = clampTile(pxMin.x, m_tileSizePx.x, kTileCols);
        const uint32_t c1 = clampTile(pxMax.x, m_tileSizePx.x, kTileCols);
        const uint32_t r0 = clampTile(pxMin.y, m_tileSizePx.y, kTileRows);
        const uint32_t r1 = clampTile(pxMax.y, m_tileSizePx.y, kTileRows);

        // Lights are visited in ascending order, so per-tile index lists come out sorted.
        const uint32_t bit = 1u << i;
        for (uint32_t row = r0; row <= r1; ++row) {
            for (uint32_t col = c0; col <= c1; ++col) {
                const uint32_t tile = row * kTileCols + col;
                m_masks[tile] |= bit;
                m_indices[tile][m_indexCounts[tile]++] = static_cast<uint8_t>(i);
            }
        }
    }
}

uint32_t TileLightGrid::mask(uint32_t col, uint32_t row) const noexcept {
    if (col >= kTileCols || row >= kTileRows) {
        return 0u;
    }
    return m_masks[row * kTileCols + col];
}

std::span<const uint8_t> TileLightGrid::lightIndices(uint32_t col, uint32_t row) const noexcept {
    if (col >= kTileCols || row >= kTileRows) {
        return {};
    }
    const uint32_t tile = row * kTileCols + col;
    return {m_indices[tile].data(), m_indexCounts[tile]};
}

uint32_t TileLightGrid::tileIndexForFragCoord(const glm::vec2& fragCoord) const noexcept {
    const uint32_t col = clampTile(fragCoord.x, m_tileSizePx.x, kTileCols);
    const uint32_t row = clampTile(fragCoord.y, m_tileSizePx.y, kTileRows);
    return row * kTileCols + col;
}

} // namespace betacraft::lighting
