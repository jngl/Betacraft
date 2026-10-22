#pragma once

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <string_view>
#include <vector>

namespace betacraft::render {

struct UiQuadVertex {
    glm::vec2 pos;
    glm::vec2 uv;
    glm::vec4 color;
    float useTexture{0.0f}; // 0.0 = solid color, 1.0 = sample TextureAtlas
};

class BitmapFont {
public:
    static constexpr int32_t GLYPH_WIDTH = 5;
    static constexpr int32_t GLYPH_HEIGHT = 7;

    static void appendText(
        std::vector<UiQuadVertex>& outVerts,
        std::string_view text,
        float x,
        float y,
        float scale,
        const glm::vec4& color,
        bool dropShadow = true
    );

    [[nodiscard]] static float measureTextWidth(std::string_view text, float scale) noexcept;
    [[nodiscard]] static const std::array<uint8_t, GLYPH_HEIGHT>& getGlyphRows(char c) noexcept;
};

} // namespace betacraft::render
