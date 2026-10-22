#include "lighting/DynamicLightFrame.hpp"

#include "math/Frustum.hpp"

#include <algorithm>

namespace betacraft::lighting {

void DynamicLightFrame::build(const DynamicLightManager& manager, const glm::vec3& cameraPos,
                              const glm::mat4& viewProj, const glm::vec2& viewportPx) noexcept {
    math::Frustum frustum;
    frustum.update(viewProj);
    m_selection = manager.select(cameraPos, &frustum);
    m_grid.build(m_selection.view(), viewProj, viewportPx);
    packDynamicLightUbo(m_selection.view(), m_grid.masks(), m_grid.tileSizePx(), m_ubo);
}

glm::vec3 DynamicLightFrame::shade(const glm::vec3& worldPos, const glm::vec2& fragCoord) const noexcept {
    const uint32_t mask = m_grid.masks()[m_grid.tileIndexForFragCoord(fragCoord)];
    return evaluateDynamicLight(worldPos, m_selection.view(), mask);
}

uint8_t DynamicLightFrame::blockLightLevelAt(const glm::vec3& worldPos) const noexcept {
    uint8_t best = 0;
    for (const DynamicLight& l : m_selection.view()) {
        // Real radius and intensity, so Entity/Custom lights agree with what the GPU shades.
        best = std::max(best, blockLightLevelOf(l, worldPos));
    }
    return best;
}

} // namespace betacraft::lighting
