#include "lighting/DynamicLightUbo.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace betacraft::lighting {

namespace {

// memcpy keeps the writes free of strict-aliasing UB; GL expects native-endian IEEE floats.
template <typename T>
void put(UboBuffer& out, size_t offset, const T& value) noexcept {
    std::memcpy(out.data() + offset, &value, sizeof(T));
}

void putVec3(UboBuffer& out, size_t offset, const glm::vec3& v) noexcept {
    put(out, offset + 0, v.x);
    put(out, offset + 4, v.y);
    put(out, offset + 8, v.z);
}

} // namespace

void packDynamicLightUbo(std::span<const DynamicLight> lights, std::span<const uint32_t> tileMasks,
                         const glm::vec2& tileSizePx, UboBuffer& out) noexcept {
    out.fill(std::byte{0});

    const size_t count = std::min<size_t>(lights.size(), kMaxDynamicLights);
    uint32_t allowedBits = 0u;
    for (size_t i = 0; i < count; ++i) {
        const DynamicLight& l = lights[i];
        if (!l.isValid()) {
            // Keep the slot (mask bit i must still mean lights[i]) but upload it as zeros:
            // radius 0 takes the shader's early-out, so NaN never reaches clamp() on the GPU.
            continue;
        }
        allowedBits |= 1u << i;
        const size_t base = ubo::lightOffset(i);
        putVec3(out, base + ubo::kLightPositionOffset, l.position);
        put(out, base + ubo::kLightRadiusOffset, l.radius);
        putVec3(out, base + ubo::kLightColorOffset, l.color);
        put(out, base + ubo::kLightIntensityOffset, l.intensity);
    }

    put(out, ubo::kLightCountOffset, static_cast<int32_t>(count));
    put(out, ubo::kTileColsOffset, static_cast<int32_t>(kTileCols));
    put(out, ubo::kTileRowsOffset, static_cast<int32_t>(kTileRows));
    // Same >= 1 px clamp as TileLightGrid (NaN-safe: !(v >= 1) also catches NaN).
    const auto tileSize = [](float v) noexcept { return v >= 1.0f && std::isfinite(v) ? v : 1.0f; };
    put(out, ubo::kTileSizeOffset + 0, tileSize(tileSizePx.x));
    put(out, ubo::kTileSizeOffset + 4, tileSize(tileSizePx.y));

    const size_t tiles = std::min<size_t>(tileMasks.size(), kTileCount);
    for (size_t t = 0; t < tiles; ++t) {
        put(out, ubo::tileMaskOffset(t), static_cast<uint32_t>(tileMasks[t] & allowedBits));
    }
}

} // namespace betacraft::lighting
