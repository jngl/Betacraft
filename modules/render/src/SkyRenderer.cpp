#include "render/SkyRenderer.hpp"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace betacraft::render {

namespace {

struct SkyVertex {
    glm::vec3 pos;
    glm::vec4 color;
};

constexpr const char* SKY_VS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec4 aColor;

uniform mat4 uMVP;

out vec4 vColor;

void main() {
    vColor = aColor;
    gl_Position = uMVP * vec4(aPos, 1.0);
}
)";

constexpr const char* SKY_FS = R"(#version 330 core
in vec4 vColor;
out vec4 FragColor;

void main() {
    FragColor = vColor;
}
)";

void pushQuad(
    std::vector<SkyVertex>& verts,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c,
    const glm::vec3& d,
    const glm::vec4& col
) {
    verts.push_back({a, col});
    verts.push_back({b, col});
    verts.push_back({c, col});
    verts.push_back({a, col});
    verts.push_back({c, col});
    verts.push_back({d, col});
}

} // namespace

SkyRenderer::~SkyRenderer() noexcept {
    if (m_celestialVao != 0 && bc_glDeleteVertexArrays) {
        bc_glDeleteVertexArrays(1, &m_celestialVao);
        bc_glDeleteBuffers(1, &m_celestialVbo);
    }
    if (m_starsVao != 0 && bc_glDeleteVertexArrays) {
        bc_glDeleteVertexArrays(1, &m_starsVao);
        bc_glDeleteBuffers(1, &m_starsVbo);
    }
}

bool SkyRenderer::initialize() {
    if (!m_skyShader.compileFromSource(SKY_VS, SKY_FS)) {
        return false;
    }

    bc_glGenVertexArrays(1, &m_celestialVao);
    bc_glGenBuffers(1, &m_celestialVbo);

    // Pre-generate starfield quads on celestial sphere
    std::vector<SkyVertex> starVerts;
    starVerts.reserve(1800);
    std::mt19937 rng(173173U);
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> bright(0.65f, 1.0f);

    for (int i = 0; i < 280; ++i) {
        glm::vec3 dir(dist(rng), dist(rng), dist(rng));
        if (glm::length(dir) < 0.1f) continue;
        dir = glm::normalize(dir) * 95.0f;

        const glm::vec3 up = (std::abs(dir.y) < 0.9f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 t1 = glm::normalize(glm::cross(dir, up)) * 0.42f;
        const glm::vec3 t2 = glm::normalize(glm::cross(dir, t1)) * 0.42f;
        const float b = bright(rng);
        const glm::vec4 col(b, b, b * 1.05f, 0.9f);

        pushQuad(starVerts, dir - t1 - t2, dir + t1 - t2, dir + t1 + t2, dir - t1 + t2, col);
    }

    m_starVertexCount = starVerts.size();
    bc_glGenVertexArrays(1, &m_starsVao);
    bc_glGenBuffers(1, &m_starsVbo);
    bc_glBindVertexArray(m_starsVao);
    bc_glBindBuffer(GL_ARRAY_BUFFER, m_starsVbo);
    bc_glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(starVerts.size() * sizeof(SkyVertex)),
        starVerts.data(),
        GL_STATIC_DRAW
    );
    bc_glEnableVertexAttribArray(0);
    bc_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SkyVertex), reinterpret_cast<const void*>(offsetof(SkyVertex, pos)));
    bc_glEnableVertexAttribArray(1);
    bc_glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(SkyVertex), reinterpret_cast<const void*>(offsetof(SkyVertex, color)));
    bc_glBindVertexArray(0);

    return true;
}

SkyAtmosphere SkyRenderer::evaluateAtmosphere(float timeOfDay, bool underWater) noexcept {
    if (underWater) {
        return SkyAtmosphere{
            .zenithColor = {0.04f, 0.14f, 0.36f},
            .horizonColor = {0.06f, 0.18f, 0.42f},
            .sunBrightness = 0.75f,
            .sunDir = {0.0f, 1.0f, 0.0f}
        };
    }

    // timeOfDay in [0..1]: 0.0 = dawn, 0.25 = noon, 0.50 = dusk, 0.75 = midnight
    const float angle = timeOfDay * 6.2831853f;
    const glm::vec3 sunDir = glm::normalize(glm::vec3(std::cos(angle), std::sin(angle), 0.18f));
    const float sunHeight = sunDir.y; // -1..1

    const float dayFactor = std::clamp((sunHeight + 0.15f) / 0.45f, 0.0f, 1.0f);
    const float sunsetFactor = std::clamp(1.0f - std::abs(sunHeight) * 3.2f, 0.0f, 1.0f);

    const glm::vec3 dayZenith(0.46f, 0.66f, 1.00f);
    const glm::vec3 dayHorizon(0.73f, 0.84f, 1.00f);
    const glm::vec3 nightZenith(0.02f, 0.03f, 0.08f);
    const glm::vec3 nightHorizon(0.05f, 0.07f, 0.14f);
    const glm::vec3 sunsetTint(0.95f, 0.48f, 0.24f);

    const glm::vec3 zenith = glm::mix(nightZenith, dayZenith, dayFactor);
    glm::vec3 horizon = glm::mix(nightHorizon, dayHorizon, dayFactor);
    horizon = glm::mix(horizon, sunsetTint, sunsetFactor * 0.65f);

    const float sunBrightness = std::clamp(0.16f + dayFactor * 0.84f, 0.16f, 1.0f);

    return SkyAtmosphere{
        .zenithColor = zenith,
        .horizonColor = horizon,
        .sunBrightness = sunBrightness,
        .sunDir = sunDir
    };
}

SkyAtmosphere SkyRenderer::evaluateAtmosphere(float timeOfDay, bool underWater, float rainStrength,
                                             float thunderStrength, float lightningFlash) noexcept {
    SkyAtmosphere atmos = evaluateAtmosphere(timeOfDay, underWater);
    if (underWater) {
        return atmos;
    }
    const auto finiteUnit = [](float v) { return std::isfinite(v) ? std::clamp(v, 0.0f, 1.0f) : 0.0f; };
    const float rain = finiteUnit(rainStrength);
    const float thunder = finiteUnit(thunderStrength);
    const float flash = finiteUnit(lightningFlash) * 0.45f;

    atmos.sunBrightness = std::max(0.0f, atmos.sunBrightness * (1.0f - 0.3125f * rain - 0.3125f * thunder));
    const auto weatherTint = [&](glm::vec3 c) {
        const float luminance = c.r * 0.3f + c.g * 0.59f + c.b * 0.11f;
        if (rain > 0.0f) {
            const float k = 1.0f - rain * 0.75f;
            c = c * k + glm::vec3(luminance * 0.6f) * (1.0f - k);
        }
        if (thunder > 0.0f) {
            const float k = 1.0f - thunder * 0.75f;
            c = c * k + glm::vec3(luminance * 0.2f) * (1.0f - k);
        }
        if (flash > 0.0f) {
            c = c * (1.0f - flash) + glm::vec3(0.8f, 0.8f, 1.0f) * flash;
        }
        return c;
    };
    atmos.rainStrength = rain;
    atmos.zenithColor = weatherTint(atmos.zenithColor);
    atmos.horizonColor = weatherTint(atmos.horizonColor);
    return atmos;
}

void SkyRenderer::renderSkyAndCelestials(
    const glm::mat4& view,
    const glm::mat4& proj,
    const glm::vec3& cameraPos,
    float timeOfDay,
    float totalTimeSeconds,
    const SkyAtmosphere& atmos
) {
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_skyShader.bind();

    // Rotation-only view matrix for skybox/celestials
    glm::mat4 rotView = view;
    rotView[3] = glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);

    // 1. Render Stars at night
    // Rain hides the sky's bodies (Beta renderSky: alpha 1 - rain).
    const float clearSky = 1.0f - std::clamp(atmos.rainStrength, 0.0f, 1.0f);
    // Clear-sky brightness, so the stars do not come out because rain darkened the day.
    const float clearBrightness = atmos.sunBrightness /
                                  std::max(0.05f, 1.0f - 0.3125f * atmos.rainStrength);
    const float nightAlpha = std::clamp(1.0f - (clearBrightness - 0.16f) * 2.2f, 0.0f, 1.0f) * clearSky;
    if (nightAlpha > 0.02f && m_starVertexCount > 0) {
        const float angle = timeOfDay * 6.2831853f;
        const glm::mat4 starModel = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 0.0f, 1.0f));
        m_skyShader.setMat4("uMVP", proj * rotView * starModel);
        bc_glBindVertexArray(m_starsVao);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_starVertexCount));
        bc_glBindVertexArray(0);
    }

    // 2. Render Sun & Moon quads
    std::vector<SkyVertex> dynVerts;
    dynVerts.reserve(512);

    const glm::vec3 sunCenter = atmos.sunDir * 85.0f;
    const glm::vec3 upRef = (std::abs(atmos.sunDir.y) < 0.9f) ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 sunRight = glm::normalize(glm::cross(atmos.sunDir, upRef)) * 9.5f;
    const glm::vec3 sunUp = glm::normalize(glm::cross(sunRight, atmos.sunDir)) * 9.5f;

    // Outer solar glow + inner bright sun square (Beta 1.7.3 pixelated sun)
    pushQuad(
        dynVerts,
        sunCenter - sunRight * 1.45f - sunUp * 1.45f,
        sunCenter + sunRight * 1.45f - sunUp * 1.45f,
        sunCenter + sunRight * 1.45f + sunUp * 1.45f,
        sunCenter - sunRight * 1.45f + sunUp * 1.45f,
        glm::vec4(1.0f, 0.88f, 0.45f, 0.28f * clearSky)
    );
    pushQuad(
        dynVerts,
        sunCenter - sunRight - sunUp,
        sunCenter + sunRight - sunUp,
        sunCenter + sunRight + sunUp,
        sunCenter - sunRight + sunUp,
        glm::vec4(1.0f, 0.98f, 0.78f, 0.98f * clearSky)
    );

    // Moon opposite to Sun
    const glm::vec3 moonCenter = -atmos.sunDir * 85.0f;
    const glm::vec3 moonRight = sunRight * 0.8f;
    const glm::vec3 moonUp = sunUp * 0.8f;
    pushQuad(
        dynVerts,
        moonCenter - moonRight - moonUp,
        moonCenter + moonRight - moonUp,
        moonCenter + moonRight + moonUp,
        moonCenter - moonRight + moonUp,
        glm::vec4(0.84f, 0.90f, 1.0f, 0.95f * clearSky)
    );

    m_skyShader.setMat4("uMVP", proj * rotView);
    bc_glBindVertexArray(m_celestialVao);
    bc_glBindBuffer(GL_ARRAY_BUFFER, m_celestialVbo);
    bc_glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(dynVerts.size() * sizeof(SkyVertex)),
        dynVerts.data(),
        GL_DYNAMIC_DRAW
    );
    bc_glEnableVertexAttribArray(0);
    bc_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(SkyVertex), reinterpret_cast<const void*>(offsetof(SkyVertex, pos)));
    bc_glEnableVertexAttribArray(1);
    bc_glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(SkyVertex), reinterpret_cast<const void*>(offsetof(SkyVertex, color)));
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(dynVerts.size()));

    // 3. Render Beta 1.7.3 Blocky Clouds layer at Y = 108
    glDepthMask(GL_TRUE);
    dynVerts.clear();

    constexpr float cloudY = 108.0f;
    constexpr float cloudCellSize = 12.0f;
    const float driftX = totalTimeSeconds * 0.85f;
    const int32_t camCellX = static_cast<int32_t>(std::floor((cameraPos.x + driftX) / cloudCellSize));
    const int32_t camCellZ = static_cast<int32_t>(std::floor(cameraPos.z / cloudCellSize));
    const glm::vec4 cloudCol(
        0.95f * atmos.sunBrightness,
        0.96f * atmos.sunBrightness,
        1.00f * atmos.sunBrightness,
        0.78f
    );

    for (int32_t cz = -10; cz <= 10; ++cz) {
        for (int32_t cx = -10; cx <= 10; ++cx) {
            const int32_t gx = camCellX + cx;
            const int32_t gz = camCellZ + cz;
            const uint32_t h = (static_cast<uint32_t>(gx) * 73856093U) ^ (static_cast<uint32_t>(gz) * 19349663U);
            if ((h % 11U) < 3U) {
                const float x0 = static_cast<float>(gx) * cloudCellSize - driftX;
                const float z0 = static_cast<float>(gz) * cloudCellSize;
                const float x1 = x0 + cloudCellSize;
                const float z1 = z0 + cloudCellSize;
                // Bottom & top of cloud slab
                pushQuad(dynVerts, {x0, cloudY, z0}, {x1, cloudY, z0}, {x1, cloudY, z1}, {x0, cloudY, z1}, cloudCol * glm::vec4(0.88f, 0.88f, 0.92f, 1.0f));
                pushQuad(dynVerts, {x0, cloudY + 3.0f, z1}, {x1, cloudY + 3.0f, z1}, {x1, cloudY + 3.0f, z0}, {x0, cloudY + 3.0f, z0}, cloudCol);
            }
        }
    }

    if (!dynVerts.empty()) {
        m_skyShader.setMat4("uMVP", proj * view);
        bc_glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(dynVerts.size() * sizeof(SkyVertex)),
            dynVerts.data(),
            GL_DYNAMIC_DRAW
        );
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(dynVerts.size()));
    }

    bc_glBindVertexArray(0);
    glEnable(GL_CULL_FACE);
}

} // namespace betacraft::render
