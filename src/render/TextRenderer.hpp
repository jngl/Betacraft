#pragma once

#include "ui/BitmapFont.hpp"
#include "ui/ScreenState.hpp"
#include "render/GLHeaders.hpp"
#include "render/Shader.hpp"

#include <algorithm>
#include <glm/glm.hpp>
#include <memory>
#include <string_view>
#include <vector>

namespace betacraft::render {

struct MsdfGlyphMetrics {
    char32_t codepoint{0};
    float u0{0.0f};
    float v0{0.0f};
    float u1{1.0f};
    float v1{1.0f};
    float planeWidth{8.0f};
    float planeHeight{10.0f};
    float advance{8.0f};
};

class TextRenderer {
public:
    static constexpr float DEFAULT_PX_RANGE = 4.0f;
    static constexpr float BASE_GLYPH_WIDTH = 8.0f;
    static constexpr float BASE_GLYPH_HEIGHT = 10.0f;
    static constexpr float BASE_GLYPH_ADVANCE = 8.0f;

    TextRenderer() noexcept = default;
    ~TextRenderer() noexcept;

    [[nodiscard]] bool initialize();
    void shutdown() noexcept;

    [[nodiscard]] static float msdfMedian(float r, float g, float b) noexcept {
        return std::max(std::min(r, g), std::min(std::max(r, g), b));
    }

    [[nodiscard]] static float evaluateMsdfFragment(glm::vec3 msd, float pxRange = DEFAULT_PX_RANGE) noexcept {
        const float sd = msdfMedian(msd.r, msd.g, msd.b) - 0.5f;
        const float screenPxDistance = pxRange * sd;
        return std::clamp(screenPxDistance + 0.5f, 0.0f, 1.0f);
    }

    [[nodiscard]] static int calculateGuiScale(
        int windowWidth,
        int windowHeight,
        ui::GuiScalePreference pref = ui::GuiScalePreference::Auto
    ) noexcept;

    [[nodiscard]] static float measureTextWidth(std::string_view text, float scale) noexcept;

    static void appendText(
        std::vector<ui::UiQuadVertex>& outVerts,
        std::string_view text,
        float x,
        float y,
        float scale,
        const glm::vec4& color,
        bool dropShadow = false
    );

    void renderBatch(
        const std::vector<ui::UiQuadVertex>& vertices,
        const glm::mat4& orthoProjection
    );

private:
    [[nodiscard]] static MsdfGlyphMetrics getGlyphMetrics(char c) noexcept;

    std::unique_ptr<ShaderProgram> m_shader{nullptr};
    GLuint m_atlasTexture{0};
    GLuint m_vao{0};
    GLuint m_vbo{0};
    bool m_initialized{false};
};

} // namespace betacraft::render
