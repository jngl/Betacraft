#include "render/WeatherRenderer.hpp"

#include "core/Logger.hpp"
#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"
#include "world/Weather.hpp"
#include "world/World.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <sstream>
#include <string>

namespace betacraft::render {

namespace {

// Verbatim copies of shaders/weather.vert and shaders/weather.frag (test_weather checks it),
// used when the files are not next to the binary.
constexpr const char* WEATHER_VS = R"(#version 330 core
// S8.11 rain and snow columns (render/WeatherRenderer.cpp keeps an embedded copy).
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;     // u across the quad, v = world height + column offset
layout(location = 2) in vec4 aParams; // x alpha, y light 0..1, z kind (0 rain, 1 snow, 2 bolt), w fall speed

uniform mat4 uViewProj;
uniform float uTime;

out vec2 vUV;
out float vAlpha;
out float vLight;
out float vKind;

void main() {
    // Scrolling the pattern up moves the streaks down: v + t * speed.
    vUV = vec2(aUV.x, aUV.y + uTime * aParams.w);
    vAlpha = aParams.x;
    vLight = aParams.y;
    vKind = aParams.z;
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

constexpr const char* WEATHER_FS = R"(#version 330 core
// S8.11 procedural rain streaks and snowflakes (no texture; render/WeatherRenderer.cpp keeps
// an embedded copy).
in vec2 vUV;
in float vAlpha;
in float vLight;
in float vKind;

uniform float uTime;

out vec4 FragColor;

float hash11(float n) {
    return fract(sin(n * 127.1) * 43758.5453);
}

float hash21(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}

void main() {
    if (vKind > 1.5) {
        // Lightning bolt: flat bright blue-white.
        FragColor = vec4(0.85, 0.88, 1.0, vAlpha);
        return;
    }
    float mask = 0.0;
    vec3 color;
    if (vKind < 0.5) {
        // Rain: five lanes across the quad, each with dashes of random length.
        float lanes = 5.0;
        float lane = floor(vUV.x * lanes);
        float lu = fract(vUV.x * lanes);
        float centre = 0.25 + 0.5 * hash11(lane + 3.0);
        float width = 0.07;
        float across = 1.0 - smoothstep(width * 0.5, width, abs(lu - centre));
        float v = vUV.y * 0.6 + hash11(lane) * 17.0;
        float segment = floor(v);
        float on = step(0.35, hash11(segment * 7.3 + lane * 13.1));
        float along = step(fract(v), 0.55);
        mask = across * on * along;
        color = vec3(0.62, 0.70, 0.86);
    } else {
        // Snow: one flake per cell at most, drifting sideways as it falls.
        vec2 p = vec2(vUV.x * 3.0 + 0.15 * sin(vUV.y * 0.9 + uTime * 0.7), vUV.y * 2.0);
        vec2 cell = floor(p);
        vec2 local = fract(p);
        float present = step(0.55, hash21(cell));
        vec2 at = vec2(0.2 + 0.6 * hash21(cell + 5.0), 0.2 + 0.6 * hash21(cell + 9.0));
        float d = length(local - at);
        mask = present * (1.0 - smoothstep(0.06, 0.11, d));
        color = vec3(1.0);
    }
    float alpha = mask * vAlpha;
    if (alpha < 0.01) {
        discard;
    }
    FragColor = vec4(color * vLight, alpha);
}
)";

[[nodiscard]] std::string readFileOr(const char* path, const char* fallback) {
    std::ifstream file(path, std::ios::binary);
    if (!file.is_open()) {
        return fallback;
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

[[nodiscard]] uint32_t columnHash(int32_t x, int32_t z) noexcept {
    uint32_t h = static_cast<uint32_t>(x) * 73856093U ^ static_cast<uint32_t>(z) * 19349663U;
    h ^= h >> 13U;
    h *= 0x5BD1E995U;
    return h ^ (h >> 15U);
}

void pushQuad(std::vector<WeatherVertex>& out, const glm::vec3& a, const glm::vec3& b, float y0, float y1,
              float v0, float v1, const glm::vec4& params) {
    const WeatherVertex bl{{a.x, y0, a.z}, {0.0f, v0}, params};
    const WeatherVertex br{{b.x, y0, b.z}, {1.0f, v0}, params};
    const WeatherVertex tr{{b.x, y1, b.z}, {1.0f, v1}, params};
    const WeatherVertex tl{{a.x, y1, a.z}, {0.0f, v1}, params};
    out.push_back(bl);
    out.push_back(br);
    out.push_back(tr);
    out.push_back(bl);
    out.push_back(tr);
    out.push_back(tl);
}

} // namespace

WeatherRenderer::~WeatherRenderer() noexcept {
    if (m_vao != 0 && bc_glDeleteVertexArrays) {
        bc_glDeleteVertexArrays(1, &m_vao);
        bc_glDeleteBuffers(1, &m_vbo);
    }
}

std::string_view WeatherRenderer::vertexShaderSource() noexcept {
    return WEATHER_VS;
}

std::string_view WeatherRenderer::fragmentShaderSource() noexcept {
    return WEATHER_FS;
}

bool WeatherRenderer::initialize() {
    const std::string vs = readFileOr("shaders/weather.vert", WEATHER_VS);
    const std::string fs = readFileOr("shaders/weather.frag", WEATHER_FS);
    if (!m_shader.compileFromSource(vs, fs)) {
        core::Logger::warn("WeatherRenderer", "Weather shader failed to compile: no rain or snow will be drawn.");
        return false;
    }
    bc_glGenVertexArrays(1, &m_vao);
    bc_glGenBuffers(1, &m_vbo);
    bc_glBindVertexArray(m_vao);
    bc_glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    bc_glEnableVertexAttribArray(0);
    bc_glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(WeatherVertex),
                             reinterpret_cast<const void*>(offsetof(WeatherVertex, position)));
    bc_glEnableVertexAttribArray(1);
    bc_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(WeatherVertex),
                             reinterpret_cast<const void*>(offsetof(WeatherVertex, uv)));
    bc_glEnableVertexAttribArray(2);
    bc_glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(WeatherVertex),
                             reinterpret_cast<const void*>(offsetof(WeatherVertex, params)));
    bc_glBindVertexArray(0);
    return true;
}

void WeatherRenderer::buildPrecipitation(const world::World& world, const glm::vec3& eye, float rainStrength,
                                         float sunBrightness, std::vector<WeatherVertex>& out) {
    if (!(rainStrength >= 0.01f) || !std::isfinite(eye.x) || !std::isfinite(eye.y) || !std::isfinite(eye.z)) {
        return;
    }
    const float strength = std::min(rainStrength, 1.0f);
    const auto ex = static_cast<int32_t>(std::floor(eye.x));
    const auto ez = static_cast<int32_t>(std::floor(eye.z));
    const auto ey = static_cast<int32_t>(std::floor(eye.y));
    const float top = static_cast<float>(ey + RADIUS);
    const float sun = std::clamp(sunBrightness, 0.0f, 1.0f);
    for (int32_t dz = -RADIUS; dz <= RADIUS; ++dz) {
        for (int32_t dx = -RADIUS; dx <= RADIUS; ++dx) {
            if (dx * dx + dz * dz > RADIUS * RADIUS) {
                continue;
            }
            const int32_t x = ex + dx;
            const int32_t z = ez + dz;
            const world::Precipitation kind = world::precipitationAt(world.getBiomeAt(x, z));
            if (kind == world::Precipitation::None) {
                continue;
            }
            // Beta: from max(precipitation height, eye - radius) up to eye + radius.
            const int32_t ground = world::precipitationHeight(world, x, z);
            const float bottom = static_cast<float>(std::max(ground, ey - RADIUS));
            if (bottom >= top) {
                continue; // Under the eye's roof line: the column is all below the ground
            }
            // The quad faces the eye around the vertical axis, through the column's centre.
            const glm::vec2 centre(static_cast<float>(x) + 0.5f, static_cast<float>(z) + 0.5f);
            glm::vec2 toEye(eye.x - centre.x, eye.z - centre.y);
            const float dist = glm::length(toEye);
            toEye = dist > 1.0e-3f ? toEye / dist : glm::vec2(0.0f, 1.0f);
            const glm::vec2 side(-toEye.y * 0.5f, toEye.x * 0.5f);
            const glm::vec3 a(centre.x - side.x, 0.0f, centre.y - side.y);
            const glm::vec3 b(centre.x + side.x, 0.0f, centre.y + side.y);

            // Beta: alpha (1 - d^2) * 0.5 + 0.5 over the disc, times the rain strength.
            const float d = dist / static_cast<float>(RADIUS);
            const float alpha = ((1.0f - d * d) * 0.5f + 0.5f) * strength;
            const auto lightY = static_cast<int32_t>(std::max(bottom, static_cast<float>(ey)));
            const float skyLight = static_cast<float>(world.getSkyLight(x, lightY, z)) / 15.0f * sun;
            const float blockLight = static_cast<float>(world.getBlockLight(x, lightY, z)) / 15.0f;
            const float light = std::clamp(std::max(skyLight, blockLight), 0.2f, 1.0f);
            const uint32_t h = columnHash(x, z);
            const float offset = static_cast<float>(h & 0xFFFFU) / 65535.0f * 32.0f;
            const bool snow = kind == world::Precipitation::Snow;
            const float speed = (snow ? SNOW_FALL_SPEED : RAIN_FALL_SPEED) *
                                (0.85f + 0.3f * static_cast<float>((h >> 16U) & 0xFFU) / 255.0f);
            const glm::vec4 params(alpha, light, snow ? 1.0f : 0.0f, speed);
            pushQuad(out, a, b, bottom, top, bottom + offset, top + offset, params);
        }
    }
}

void WeatherRenderer::buildBolt(const glm::vec3& strike, const glm::vec3& eye, uint32_t seed, float alpha,
                                std::vector<WeatherVertex>& out) {
    if (!(alpha > 0.0f)) {
        return;
    }
    constexpr int32_t kSegments = 12;
    const float topY = std::max(strike.y + 8.0f, 128.0f);
    const float step = (topY - strike.y) / static_cast<float>(kSegments);
    uint32_t state = seed * 2654435761U + 1U;
    const auto jitter = [&state]() {
        state ^= state << 13U;
        state ^= state >> 17U;
        state ^= state << 5U;
        return (static_cast<float>(state & 0xFFFFU) / 65535.0f - 0.5f) * 2.0f;
    };
    // Top-down so the bolt ends exactly on the strike point.
    glm::vec3 upper(strike.x + jitter() * 3.0f, topY, strike.z + jitter() * 3.0f);
    const glm::vec4 params(std::min(alpha, 1.0f), 1.0f, static_cast<float>(WeatherVertexKind::Bolt), 0.0f);
    for (int32_t i = 1; i <= kSegments; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kSegments);
        const glm::vec3 lower = i == kSegments ? strike
                                               : glm::vec3(strike.x + jitter() * 3.0f * (1.0f - t), topY - step * i,
                                                           strike.z + jitter() * 3.0f * (1.0f - t));
        glm::vec2 toEye(eye.x - lower.x, eye.z - lower.z);
        const float len = glm::length(toEye);
        toEye = len > 1.0e-3f ? toEye / len : glm::vec2(0.0f, 1.0f);
        const glm::vec3 side(-toEye.y * 0.15f, 0.0f, toEye.x * 0.15f);
        const WeatherVertex a{lower - side, {0.0f, 0.0f}, params};
        const WeatherVertex b{lower + side, {1.0f, 0.0f}, params};
        const WeatherVertex c{upper + side, {1.0f, 1.0f}, params};
        const WeatherVertex d{upper - side, {0.0f, 1.0f}, params};
        out.push_back(a);
        out.push_back(b);
        out.push_back(c);
        out.push_back(a);
        out.push_back(c);
        out.push_back(d);
        upper = lower;
    }
}

void WeatherRenderer::render(const world::World& world, const glm::vec3& eye, const glm::mat4& viewProj,
                             const WeatherFrame& frame, float sunBrightness, float timeSeconds) {
    if (m_vao == 0) {
        return;
    }
    m_vertices.clear();
    buildPrecipitation(world, eye, frame.rainStrength, sunBrightness, m_vertices);
    if (frame.bolt) {
        buildBolt(*frame.bolt, eye, frame.boltSeed, frame.lightningFlash, m_vertices);
    }
    if (m_vertices.empty()) {
        return;
    }
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glDisable(GL_CULL_FACE);

    m_shader.bind();
    m_shader.setMat4("uViewProj", viewProj);
    // Wrapped so the float keeps its precision in long sessions.
    m_shader.setFloat("uTime", static_cast<float>(std::fmod(static_cast<double>(timeSeconds), 3600.0)));
    bc_glBindVertexArray(m_vao);
    bc_glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    bc_glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(m_vertices.size() * sizeof(WeatherVertex)),
                    m_vertices.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertices.size()));
    bc_glBindVertexArray(0);

    glEnable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
}

} // namespace betacraft::render
