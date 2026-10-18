#include "lighting/DynamicLight.hpp"

#include <algorithm>
#include <cmath>

namespace betacraft::lighting {

namespace {

[[nodiscard]] bool isFinite(const glm::vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

bool DynamicLight::isValid() const noexcept {
    if (!isFinite(position) || !isFinite(color) || !std::isfinite(radius) || !std::isfinite(intensity)) {
        return false;
    }
    if (radius <= 0.0f || intensity <= 0.0f) {
        return false;
    }
    if (color.r < 0.0f || color.g < 0.0f || color.b < 0.0f) {
        return false;
    }
    return (color.r + color.g + color.b) > 0.0f;
}

uint8_t blockLightLevelOf(const DynamicLight& light, const glm::vec3& worldPos) noexcept {
    if (!light.isValid()) {
        return 0;
    }
    return linearBlockLevel(glm::length(worldPos - light.position), light.radius, light.radius * light.intensity);
}

uint8_t emissionOf(world::BlockId id) noexcept {
    switch (id) {
        case world::BlockId::RedstoneOre:
            // The registry gives id 73 a static-world glow of 4, but in Beta 1.7.3 only the
            // transient glowing ore block (id 74) emits; the ore as an item never does.
            return 0;
        case world::BlockId::Lava:
        case world::BlockId::StillLava:
            // Placed lava emits 15, but Beta has no lava item (only the lava bucket, which
            // does not glow), so a held or dropped lava id never lights anything.
            return 0;
        case world::BlockId::LitFurnace:
            // A burning furnace glows only while placed; it drops (and is held as) a furnace.
            return 0;
        default:
            return world::BlockRegistry::get(id).lightEmission;
    }
}

glm::vec3 emitterColor(world::BlockId id) noexcept {
    switch (id) {
        case world::BlockId::Torch:
            // Same warm tint as the static torch light in PACKED_CHUNK_FS, so held and
            // placed torches look identical.
            return {1.0f, 0.94f, 0.82f};
        case world::BlockId::Glowstone:
            return {1.0f, 0.92f, 0.70f};
        default:
            return {1.0f, 1.0f, 1.0f};
    }
}

std::optional<DynamicLight> makeEmitterLight(world::BlockId id, const glm::vec3& position,
                                             LightSource source) noexcept {
    const uint8_t emission = emissionOf(id);
    if (emission == 0) {
        return std::nullopt;
    }
    DynamicLight light;
    light.position = position;
    light.radius = radiusForEmission(emission);
    light.color = emitterColor(id);
    light.intensity = 1.0f;
    light.source = source;
    return light;
}

glm::vec3 evaluateDynamicLight(const glm::vec3& worldPos, std::span<const DynamicLight> lights,
                               uint32_t mask) noexcept {
    // Mirrors computeDynamicLight() in dynamic_lights.glsl: only the first kMaxDynamicLights
    // entries exist on the GPU.
    const size_t count = std::min<size_t>(lights.size(), kMaxDynamicLights);
    glm::vec3 sum(0.0f);
    for (size_t i = 0; i < count; ++i) {
        if ((mask & (1u << i)) == 0u) {
            continue;
        }
        const DynamicLight& l = lights[i];
        const float d = glm::length(worldPos - l.position);
        sum += l.color * (l.intensity * attenuation(d, l.radius));
    }
    return sum;
}

} // namespace betacraft::lighting
