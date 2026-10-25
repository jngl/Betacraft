#pragma once

#include "core/Types.hpp"
#include "render/GLHeaders.hpp"
#include "render/Shader.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace betacraft::world { class World; }

namespace betacraft::render {

// One vertex of the weather pass (layout of shaders/weather.vert).
struct WeatherVertex {
    glm::vec3 position{0.0f};
    glm::vec2 uv{0.0f};     // u across the quad, v along the world height (+ column offset)
    glm::vec4 params{0.0f}; // alpha, light 0..1, kind (WeatherVertexKind), fall speed
};

enum class WeatherVertexKind : uint8_t { Rain = 0, Snow = 1, Bolt = 2 };

// What the weather looks like this frame (Application -> WorldRenderer::setWeather).
struct WeatherFrame {
    float rainStrength{0.0f};    // 0..1, interpolated between ticks
    float thunderStrength{0.0f}; // 0..1, already weighted by the rain
    float lightningFlash{0.0f};  // 0..1 sky flash of a recent strike
    std::optional<glm::vec3> bolt; // Strike point of a bolt still visible
    uint32_t boltSeed{0};          // Shape of the bolt
};

// Rain and snow (Beta EntityRenderer.renderRainSnow): one camera-facing quad per column
// within RADIUS blocks of the eye, from max(precipitation height, eye - RADIUS) to eye +
// RADIUS, rain or snow by the column's biome (nothing in the desert), the pattern scrolling
// down in the shader, alpha = rainStrength (fading towards the edge of the disc). Drawn after
// the translucent blocks, without writing depth.
class WeatherRenderer : private core::NonCopyable {
public:
    static constexpr int32_t RADIUS = 10;
    static constexpr float RAIN_FALL_SPEED = 9.0f; // pattern units per second
    static constexpr float SNOW_FALL_SPEED = 1.6f;

    WeatherRenderer() noexcept = default;
    ~WeatherRenderer() noexcept;

    // Compiles shaders/weather.{vert,frag} (embedded copies when the files are missing).
    [[nodiscard]] bool initialize();
    [[nodiscard]] bool isInitialized() const noexcept { return m_vao != 0; }

    void render(const world::World& world, const glm::vec3& eye, const glm::mat4& viewProj, const WeatherFrame& frame,
                float sunBrightness, float timeSeconds);

    // CPU half, GL-free (tests): appends the column quads (6 vertices each) seen from `eye`
    // for `rainStrength`. Nothing below 0.01.
    static void buildPrecipitation(const world::World& world, const glm::vec3& eye, float rainStrength,
                                   float sunBrightness, std::vector<WeatherVertex>& out);
    // A jagged vertical bolt from the clouds down to `strike`.
    static void buildBolt(const glm::vec3& strike, const glm::vec3& eye, uint32_t seed, float alpha,
                          std::vector<WeatherVertex>& out);

    [[nodiscard]] static std::string_view vertexShaderSource() noexcept;
    [[nodiscard]] static std::string_view fragmentShaderSource() noexcept;

private:
    ShaderProgram m_shader;
    GLuint m_vao{0};
    GLuint m_vbo{0};
    std::vector<WeatherVertex> m_vertices; // reused every frame
};

} // namespace betacraft::render
