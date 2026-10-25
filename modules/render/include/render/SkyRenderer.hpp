#pragma once

#include "core/Types.hpp"
#include "render/GLHeaders.hpp"
#include "render/Shader.hpp"

#include <glm/glm.hpp>

namespace betacraft::render {

struct SkyAtmosphere {
    glm::vec3 zenithColor{0.47f, 0.65f, 1.00f};
    glm::vec3 horizonColor{0.72f, 0.83f, 1.00f};
    float sunBrightness{1.0f}; // 0.16 (night) .. 1.0 (noon)
    glm::vec3 sunDir{0.0f, 1.0f, 0.0f};
    float rainStrength{0.0f}; // hides the sun, moon and stars (Beta 1 - rain alpha)
};

class SkyRenderer : private core::NonCopyable {
public:
    SkyRenderer() noexcept = default;
    ~SkyRenderer() noexcept;

    [[nodiscard]] bool initialize();

    [[nodiscard]] static SkyAtmosphere evaluateAtmosphere(float timeOfDay, bool underWater) noexcept;
    // Weather: the clear-sky atmosphere, with sunBrightness *= 1 - 0.3125 rain - 0.3125
    // thunder and the sky colours greyed and darkened like Beta World.getSkyColor (rain mixes
    // 75 % towards 0.6 x luminance, thunder 75 % towards 0.2 x luminance). `lightningFlash`
    // (0..1) brightens the sky towards (0.8, 0.8, 1) by up to 45 %. Under water the weather is
    // not seen.
    [[nodiscard]] static SkyAtmosphere evaluateAtmosphere(float timeOfDay, bool underWater, float rainStrength,
                                                          float thunderStrength, float lightningFlash = 0.0f) noexcept;

    void renderSkyAndCelestials(
        const glm::mat4& view,
        const glm::mat4& proj,
        const glm::vec3& cameraPos,
        float timeOfDay,
        float totalTimeSeconds,
        const SkyAtmosphere& atmos
    );

private:
    ShaderProgram m_skyShader;
    GLuint m_celestialVao{0};
    GLuint m_celestialVbo{0};
    GLuint m_starsVao{0};
    GLuint m_starsVbo{0};
    size_t m_starVertexCount{0};
};

} // namespace betacraft::render
