#pragma once

#include "lighting/DynamicLightManager.hpp"
#include "lighting/DynamicLightUbo.hpp"
#include "lighting/TileLightCuller.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace betacraft::lighting {

// One frame of dynamic lighting, computed on the game thread once per rendered frame:
//   manager lights -> frustum + distance selection (<= 16) -> Forward+ tile masks -> std140 bytes.
// The resulting uboBytes() are what DynamicLightUniformBuffer uploads; shade() is the CPU
// reference of what computeDynamicLight() returns on the GPU (test_glsl_execution.cpp runs the
// real shader against it).
class DynamicLightFrame {
public:
    void build(const DynamicLightManager& manager, const glm::vec3& cameraPos, const glm::mat4& viewProj,
               const glm::vec2& viewportPx) noexcept;

    [[nodiscard]] const LightSelection& selection() const noexcept { return m_selection; }
    [[nodiscard]] const TileLightGrid& grid() const noexcept { return m_grid; }
    [[nodiscard]] const UboBuffer& uboBytes() const noexcept { return m_ubo; }

    // Dynamic light reaching `worldPos` for a fragment at `fragCoord` (gl_FragCoord.xy).
    [[nodiscard]] glm::vec3 shade(const glm::vec3& worldPos, const glm::vec2& fragCoord) const noexcept;

    // Strongest block-light-equivalent level (0..15) of the selected lights at `worldPos`
    // (blockLightLevelOf(): real radius and intensity, Beta-like linear decay). Lets the
    // first-person hand / held item be lit by the torch it holds (diagnostic §5.1.6): combine
    // as max(staticBlockLight, this).
    [[nodiscard]] uint8_t blockLightLevelAt(const glm::vec3& worldPos) const noexcept;

private:
    LightSelection m_selection;
    TileLightGrid m_grid;
    UboBuffer m_ubo{};
};

} // namespace betacraft::lighting
