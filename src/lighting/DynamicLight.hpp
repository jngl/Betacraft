#pragma once

#include "world/Block.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <span>

namespace betacraft::lighting {

// P4.6 — real-time dynamic lights (held torch, dropped glowstone, ...). These lights are
// evaluated per fragment from a UBO so chunks never need to be re-meshed when the player
// walks around holding a torch.

// Capacity of the GPU light array (spec: "jusqu'à 16 sources lumineuses dynamiques").
inline constexpr uint32_t kMaxDynamicLights = 16;

// Block light is quantized on 0..15 in Beta 1.7.3; dynamic lights reuse that scale so a held
// torch reaches as far as a placed one.
inline constexpr uint8_t kMaxBlockLight = 15;

enum class LightSource : uint8_t {
    Custom = 0,
    HeldItem,    // Item in the player's selected hotbar slot
    DroppedItem, // Glowing item entity lying in the world
    Entity       // Burning mobs, projectiles, ...
};

struct DynamicLight {
    glm::vec3 position{0.0f}; // World space
    float radius{0.0f};       // Distance (blocks) where the contribution reaches exactly zero
    glm::vec3 color{1.0f};    // Linear RGB tint, nominally in [0, 1]
    float intensity{1.0f};    // Scalar multiplier applied on top of color
    LightSource source{LightSource::Custom};

    // A light that can never contribute (or would poison the shader with NaN) is rejected
    // before it costs one of the 16 precious UBO slots.
    [[nodiscard]] bool isValid() const noexcept;
};

// NaN-safe clamp: std::clamp propagates NaN, which would leak into the UBO.
[[nodiscard]] constexpr float saturate(float v) noexcept {
    if (!(v > 0.0f)) {
        return 0.0f;
    }
    return v < 1.0f ? v : 1.0f;
}

// Spec P4.6: Light = Color * (1 - d/R)^2. Bit-for-bit equivalent to dynLightAttenuation() in
// shaders/dynamic_lights.glsl for finite inputs (test_glsl_execution.cpp runs the shader code
// itself). A NaN radius takes the early-out on both sides; NaN distances are implementation-
// defined on GPUs, which is why packDynamicLightUbo() never uploads a non-finite light.
[[nodiscard]] constexpr float attenuation(float distance, float radius) noexcept {
    if (!(radius > 0.0f)) {
        return 0.0f; // Also rejects NaN radius
    }
    if (!(distance == distance)) {
        return 0.0f; // NaN distance
    }
    const float t = saturate(1.0f - distance / radius);
    return t * t;
}

// Block light decays by one level per block, so an emitter of level L goes dark after L blocks.
[[nodiscard]] constexpr float radiusForEmission(uint8_t emission) noexcept {
    const uint8_t level = emission > kMaxBlockLight ? kMaxBlockLight : emission;
    return static_cast<float>(level);
}

// Block-light-equivalent level (0..15) of a light that is `peak` at its source and reaches zero
// at `radius`. The decay is LINEAR, like Beta 1.7.3 block light (L - d at integer distances),
// not the quadratic visual falloff of the shader: CPU consumers that think in levels (hand
// shading, gameplay) compare against static block light and must read a held torch exactly
// like a placed one. Invariant: the level is > 0 exactly where attenuation() is > 0, so no
// consumer calls "dark" a point the GPU lights (hence the minimum of 1 inside the radius).
[[nodiscard]] constexpr uint8_t linearBlockLevel(float distance, float radius, float peak) noexcept {
    if (!(radius > 0.0f) || !(peak > 0.0f)) {
        return 0;
    }
    // Same t as attenuation(): attenuation = t^2, so both are zero at exactly the same points.
    const float t = saturate(1.0f - distance / radius);
    if (!(t > 0.0f)) {
        return 0;
    }
    const float maxLevel = static_cast<float>(kMaxBlockLight);
    const float value = (peak < maxLevel ? peak : maxLevel) * t + 0.5f;
    const auto level = static_cast<uint8_t>(value < maxLevel ? value : maxLevel);
    return level == 0 ? uint8_t{1} : level;
}

// Equivalent block-light level (0..15) produced by an emitter of `emission` at `distance`:
// max(0, emission - distance) at integer distances, as for a placed emitter in Beta.
[[nodiscard]] constexpr uint8_t blockLightLevelAt(float distance, uint8_t emission) noexcept {
    const uint8_t level = emission > kMaxBlockLight ? kMaxBlockLight : emission;
    return linearBlockLevel(distance, radiusForEmission(level), static_cast<float>(level));
}

// Level of an arbitrary light at `worldPos`, using its real radius and intensity: the peak is
// radius * intensity (== emission for makeEmitterLight() lights, whose radius is the emission
// and intensity 1), clamped to 15. Invalid lights give 0.
[[nodiscard]] uint8_t blockLightLevelOf(const DynamicLight& light, const glm::vec3& worldPos) noexcept;

// Emission (0..15) of a block or ITEM (held / dropped), read from BlockRegistry (Torch = 14,
// Glowstone = 15) except where the registry departs from Beta 1.7.3 item behaviour (redstone
// ore does not glow as an item).
[[nodiscard]] uint8_t emissionOf(world::BlockId id) noexcept;

// Visual tint per emitter; warm torch matches the 1.0/0.94/0.82 static torch tint of the chunk
// fragment shader (PACKED_CHUNK_FS in WorldRenderer.cpp).
[[nodiscard]] glm::vec3 emitterColor(world::BlockId id) noexcept;

// Builds the light emitted by a glowing block/item, or nullopt when it does not glow.
[[nodiscard]] std::optional<DynamicLight> makeEmitterLight(world::BlockId id, const glm::vec3& position,
                                                           LightSource source) noexcept;

// CPU reference of computeDynamicLight() in dynamic_lights.glsl: sums the contributions of the
// lights whose bit is set in `mask` (bit i == lights[i]). Used to validate the shader math.
[[nodiscard]] glm::vec3 evaluateDynamicLight(const glm::vec3& worldPos, std::span<const DynamicLight> lights,
                                             uint32_t mask = 0xFFFFFFFFu) noexcept;

} // namespace betacraft::lighting
