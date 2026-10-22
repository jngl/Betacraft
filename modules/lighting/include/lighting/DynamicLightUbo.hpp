#pragma once

#include "lighting/DynamicLight.hpp"
#include "lighting/Std140.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace betacraft::lighting {

// Forward+ screen tile grid. The grid has a fixed tile COUNT (not a fixed pixel size) so the
// UBO size is resolution-independent; tile size in pixels is uploaded each frame.
inline constexpr uint32_t kTileCols = 16;
inline constexpr uint32_t kTileRows = 9;
inline constexpr uint32_t kTileCount = kTileCols * kTileRows;
// One 32-bit light mask per tile, four tiles per uvec4 so std140 does not pad each to 16 bytes.
inline constexpr uint32_t kTileMasksPerVec4 = 4;
inline constexpr uint32_t kTileMaskVec4Count = kTileCount / kTileMasksPerVec4;
static_assert(kTileCount % kTileMasksPerVec4 == 0, "Tile masks must fill whole uvec4 entries");
static_assert(kMaxDynamicLights <= 32, "Tile masks hold one bit per light in a uint");

// Uniform block binding shared with shaders/dynamic_lights.glsl.
inline constexpr const char* kUboBlockName = "DynamicLightBlock";
inline constexpr uint32_t kUboBindingPoint = 3;

// Byte layout of DynamicLightBlock, derived from the std140 rules (see Std140.hpp):
//
//   struct DynamicLightGPU { vec3 position; float radius; vec3 color; float intensity; };
//   layout(std140) uniform DynamicLightBlock {
//       DynamicLightGPU u_dynLights[16];
//       int   u_dynLightCount;
//       int   u_dynTileCols;
//       int   u_dynTileRows;
//       vec2  u_dynTileSizePx;
//       uvec4 u_dynTileMasks[36];
//   };
namespace ubo {

namespace detail {

struct LightStructLayout {
    size_t position;
    size_t radius;
    size_t color;
    size_t intensity;
    size_t stride;
};

struct BlockLayout {
    size_t lights;
    size_t count;
    size_t tileCols;
    size_t tileRows;
    size_t tileSize;
    size_t tileMasks;
    size_t size;
};

[[nodiscard]] constexpr LightStructLayout computeLightStruct() noexcept {
    std140::LayoutBuilder s;
    LightStructLayout l{};
    l.position = s.add(std140::Type::Vec3);
    l.radius = s.add(std140::Type::Float);
    l.color = s.add(std140::Type::Vec3);
    l.intensity = s.add(std140::Type::Float);
    l.stride = s.structSize();
    return l;
}

[[nodiscard]] constexpr BlockLayout computeBlock() noexcept {
    std140::LayoutBuilder b;
    BlockLayout l{};
    l.lights = b.addStructArray(computeLightStruct().stride, kMaxDynamicLights);
    l.count = b.add(std140::Type::Int);
    l.tileCols = b.add(std140::Type::Int);
    l.tileRows = b.add(std140::Type::Int);
    l.tileSize = b.add(std140::Type::Vec2);
    l.tileMasks = b.addArray(std140::Type::UVec4, kTileMaskVec4Count);
    l.size = b.structSize();
    return l;
}

inline constexpr LightStructLayout kLight = computeLightStruct();
inline constexpr BlockLayout kBlock = computeBlock();

} // namespace detail

inline constexpr size_t kLightPositionOffset = detail::kLight.position;
inline constexpr size_t kLightRadiusOffset = detail::kLight.radius;
inline constexpr size_t kLightColorOffset = detail::kLight.color;
inline constexpr size_t kLightIntensityOffset = detail::kLight.intensity;
inline constexpr size_t kLightStride = detail::kLight.stride;

inline constexpr size_t kLightsOffset = detail::kBlock.lights;
inline constexpr size_t kLightCountOffset = detail::kBlock.count;
inline constexpr size_t kTileColsOffset = detail::kBlock.tileCols;
inline constexpr size_t kTileRowsOffset = detail::kBlock.tileRows;
inline constexpr size_t kTileSizeOffset = detail::kBlock.tileSize;
inline constexpr size_t kTileMasksOffset = detail::kBlock.tileMasks;
inline constexpr size_t kTileMaskStride = std140::arrayStride(std140::Type::UVec4);
inline constexpr size_t kBlockSize = detail::kBlock.size;

[[nodiscard]] constexpr size_t lightOffset(size_t index) noexcept {
    return kLightsOffset + index * kLightStride;
}

// Tile t lives in component (t % 4) of u_dynTileMasks[t / 4].
[[nodiscard]] constexpr size_t tileMaskOffset(size_t tile) noexcept {
    return kTileMasksOffset + (tile / kTileMasksPerVec4) * kTileMaskStride + (tile % kTileMasksPerVec4) * 4u;
}

} // namespace ubo

using UboBuffer = std::array<std::byte, ubo::kBlockSize>;

// Serializes one frame of dynamic lights into `out` (whole buffer rewritten, padding zeroed).
// Lights beyond kMaxDynamicLights are dropped, tile masks beyond kTileCount are ignored, and
// mask bits referring to non-uploaded light slots are cleared so the shader never reads stale
// lights. Invalid lights (!isValid(): NaN, zero radius...) keep their slot index but upload
// as all-zero with their mask bit cleared, and the tile size is clamped to >= 1 px, so no
// byte sequence produced here makes the shader's result implementation-defined.
void packDynamicLightUbo(std::span<const DynamicLight> lights, std::span<const uint32_t> tileMasks,
                         const glm::vec2& tileSizePx, UboBuffer& out) noexcept;

} // namespace betacraft::lighting
