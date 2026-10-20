#include "render/TextRenderer.hpp"
#include "core/Logger.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <memory>
#include <sstream>

namespace betacraft::render {

namespace {

constexpr const char* FALLBACK_MSDF_VS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;
layout(location = 3) in float aUseTexture;

uniform mat4 uOrtho;

out vec2 vUV;
out vec4 vColor;
out float vUseTexture;

void main() {
    vUV = aUV;
    vColor = aColor;
    vUseTexture = aUseTexture;
    gl_Position = uOrtho * vec4(aPos, 0.0, 1.0);
}
)";

constexpr const char* FALLBACK_MSDF_FS = R"(#version 330 core
in vec2 vUV;
in vec4 vColor;
in float vUseTexture;
out vec4 FragColor;

uniform sampler2D uMsdfAtlas;
uniform float uPxRange;

float median(float r, float g, float b) {
    return max(min(r, g), min(max(r, g), b));
}

void main() {
    if (vUseTexture < 0.5) {
        FragColor = vColor;
        return;
    }

    vec3 msd = texture(uMsdfAtlas, vUV).rgb;
    float sd = median(msd.r, msd.g, msd.b);

    vec2 screenTexSize = vec2(1.0) / max(fwidth(vUV), vec2(0.0001));
    vec2 unitRange = vec2(uPxRange) / vec2(textureSize(uMsdfAtlas, 0));
    float screenPxDistance = max(0.5 * dot(unitRange, screenTexSize), 1.0) * (sd - 0.5);
    float opacity = clamp(screenPxDistance + 0.5, 0.0, 1.0);

    if (opacity < 0.01) {
        discard;
    }
    FragColor = vec4(vColor.rgb, vColor.a * opacity);
}
)";

std::string readFileOrDefault(const std::string& path, const char* defaultSrc) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return std::string(defaultSrc);
    }
    std::stringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

} // namespace

TextRenderer::~TextRenderer() noexcept {
    shutdown();
}

int TextRenderer::calculateGuiScale(
    int windowWidth,
    int windowHeight,
    GuiScalePreference pref
) noexcept {
    if (pref != GuiScalePreference::Auto) {
        return std::clamp(static_cast<int>(pref), 1, 4);
    }
    int scale = 1;
    while ((windowWidth / (scale + 1) >= 320) && (windowHeight / (scale + 1) >= 240)) {
        ++scale;
    }
    return std::clamp(scale, 1, 4);
}

MsdfGlyphMetrics TextRenderer::getGlyphMetrics(char c) noexcept {
    MsdfGlyphMetrics m{};
    m.codepoint = static_cast<char32_t>(static_cast<unsigned char>(c));
    m.planeWidth = BASE_GLYPH_WIDTH;
    m.planeHeight = BASE_GLYPH_HEIGHT;
    m.advance = BASE_GLYPH_ADVANCE;

    if (c == ' ') {
        m.u0 = m.v0 = m.u1 = m.v1 = 0.0f;
        m.planeWidth = 0.0f;
        m.advance = 6.0f;
        return m;
    }

    // Grid layout for ASCII 32..126 (16 cols x 8 rows in 512x512 atlas)
    const int idx = std::clamp(static_cast<int>(static_cast<unsigned char>(c)) - 32, 0, 95);
    const int col = idx % 16;
    const int row = idx / 16;

    constexpr float cellU = 1.0f / 16.0f;
    constexpr float cellV = 1.0f / 8.0f;

    m.u0 = static_cast<float>(col) * cellU;
    m.v0 = static_cast<float>(row) * cellV;
    m.u1 = m.u0 + cellU;
    m.v1 = m.v0 + cellV;

    return m;
}

float TextRenderer::measureTextWidth(std::string_view text, float scale) noexcept {
    if (text.empty()) return 0.0f;
    float width = 0.0f;
    for (char c : text) {
        width += getGlyphMetrics(c).advance * scale;
    }
    return width;
}

void TextRenderer::appendText(
    std::vector<UiQuadVertex>& outVerts,
    std::string_view text,
    float x,
    float y,
    float scale,
    const glm::vec4& color,
    bool dropShadow
) {
    if (text.empty()) return;

    auto emitPass = [&](float px, float py, const glm::vec4& col) {
        float cursorX = px;
        for (char c : text) {
            const auto m = getGlyphMetrics(c);
            if (c != ' ') {
                const float x0 = cursorX;
                const float y0 = py;
                const float x1 = cursorX + m.planeWidth * scale;
                const float y1 = py + m.planeHeight * scale;

                // 2 Triangles: (0, 1, 2) and (0, 2, 3) -> exactly 1 quad (6 vertices)
                const UiQuadVertex v0{{x0, y0}, {m.u0, m.v0}, col, 1.0f};
                const UiQuadVertex v1{{x1, y0}, {m.u1, m.v0}, col, 1.0f};
                const UiQuadVertex v2{{x1, y1}, {m.u1, m.v1}, col, 1.0f};
                const UiQuadVertex v3{{x0, y1}, {m.u0, m.v1}, col, 1.0f};

                outVerts.push_back(v0);
                outVerts.push_back(v1);
                outVerts.push_back(v2);

                outVerts.push_back(v0);
                outVerts.push_back(v2);
                outVerts.push_back(v3);
            }
            cursorX += m.advance * scale;
        }
    };

    if (dropShadow) {
        const glm::vec4 shadowCol(color.r * 0.22f, color.g * 0.22f, color.b * 0.22f, color.a);
        emitPass(x + scale, y + scale, shadowCol);
    }
    emitPass(x, y, color);
}

bool TextRenderer::initialize() {
    if (m_initialized) return true;

    m_shader = std::make_unique<ShaderProgram>();
    const std::string vsCode = readFileOrDefault("shaders/msdf_vertex.glsl", FALLBACK_MSDF_VS);
    const std::string fsCode = readFileOrDefault("shaders/msdf_fragment.glsl", FALLBACK_MSDF_FS);

    if (!m_shader->compileFromSource(vsCode, fsCode)) {
        core::Logger::error("TextRenderer", "Failed to compile MSDF shader program.");
        return false;
    }

    // Generate 512x512 procedural MSDF/SDF atlas from BitmapFont
    constexpr int atlasSize = 512;
    std::vector<uint8_t> atlasData(atlasSize * atlasSize * 3, 0);

    struct GlyphBox { float x0, y0, x1, y1; };

    for (int idx = 0; idx < 96; ++idx) {
        const char c = static_cast<char>(32 + idx);
        if (c == ' ') continue;

        const auto& rows = BitmapFont::getGlyphRows(c);
        std::vector<GlyphBox> onBoxes;
        onBoxes.reserve(35);

        for (int gy = 0; gy < BitmapFont::GLYPH_HEIGHT; ++gy) {
            const uint8_t rowBits = rows[static_cast<size_t>(gy)];
            if (rowBits == 0) continue;
            for (int gx = 0; gx < BitmapFont::GLYPH_WIDTH; ++gx) {
                if ((rowBits & (1U << (4 - gx))) != 0U) {
                    const float bx0 = 6.0f + static_cast<float>(gx) * 4.0f;
                    const float by0 = 9.6f + static_cast<float>(gy) * 6.4f;
                    onBoxes.push_back({bx0, by0, bx0 + 4.0f, by0 + 6.4f});
                }
            }
        }

        if (onBoxes.empty()) continue;

        const int col = idx % 16;
        const int row = idx / 16;
        const int cellBaseX = col * 32;
        const int cellBaseY = row * 64;

        for (int cy = 0; cy < 64; ++cy) {
            const float py = static_cast<float>(cy) + 0.5f;
            for (int cx = 0; cx < 32; ++cx) {
                const float px = static_cast<float>(cx) + 0.5f;

                bool inside = false;
                float maxInsideDist = -1.0f;
                for (const auto& b : onBoxes) {
                    if (px >= b.x0 && px <= b.x1 && py >= b.y0 && py <= b.y1) {
                        inside = true;
                        const float id = std::min({px - b.x0, b.x1 - px, py - b.y0, b.y1 - py});
                        if (id > maxInsideDist) maxInsideDist = id;
                    }
                }

                float sd = 0.0f;
                if (inside) {
                    sd = maxInsideDist;
                } else {
                    float minOutsideDist = 1e9f;
                    for (const auto& b : onBoxes) {
                        const float dx = std::max({b.x0 - px, 0.0f, px - b.x1});
                        const float dy = std::max({b.y0 - py, 0.0f, py - b.y1});
                        const float d = std::sqrt(dx * dx + dy * dy);
                        if (d < minOutsideDist) minOutsideDist = d;
                    }
                    sd = -minOutsideDist;
                }

                constexpr float SPREAD = 3.0f;
                const float norm = (sd / SPREAD) * 0.5f + 0.5f;
                const uint8_t byteVal = static_cast<uint8_t>(std::clamp(norm * 255.0f, 0.0f, 255.0f));

                const size_t pixelIdx = (static_cast<size_t>(cellBaseY + cy) * atlasSize + static_cast<size_t>(cellBaseX + cx)) * 3;
                atlasData[pixelIdx + 0] = byteVal;
                atlasData[pixelIdx + 1] = byteVal;
                atlasData[pixelIdx + 2] = byteVal;
            }
        }
    }

    glGenTextures(1, &m_atlasTexture);
    glBindTexture(GL_TEXTURE_2D, m_atlasTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, atlasSize, atlasSize, 0, GL_RGB, GL_UNSIGNED_BYTE, atlasData.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    bc_glGenVertexArrays(1, &m_vao);
    bc_glGenBuffers(1, &m_vbo);

    bc_glBindVertexArray(m_vao);
    bc_glBindBuffer(GL_ARRAY_BUFFER, m_vbo);

    // Vertex layout: pos (vec2), uv (vec2), color (vec4), useTexture (float)
    constexpr GLsizei stride = sizeof(UiQuadVertex);
    bc_glEnableVertexAttribArray(0);
    bc_glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(0));

    bc_glEnableVertexAttribArray(1);
    bc_glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(8));

    bc_glEnableVertexAttribArray(2);
    bc_glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(16));

    bc_glEnableVertexAttribArray(3);
    bc_glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, stride, reinterpret_cast<const void*>(32));

    bc_glBindVertexArray(0);
    bc_glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_initialized = true;
    core::Logger::info("TextRenderer", "MSDF Vector TextRenderer initialized.");
    return true;
}

void TextRenderer::shutdown() noexcept {
    if (!m_initialized) return;

    if (m_vbo != 0) {
        bc_glDeleteBuffers(1, &m_vbo);
        m_vbo = 0;
    }
    if (m_vao != 0) {
        bc_glDeleteVertexArrays(1, &m_vao);
        m_vao = 0;
    }
    if (m_atlasTexture != 0) {
        glDeleteTextures(1, &m_atlasTexture);
        m_atlasTexture = 0;
    }
    m_shader.reset();
    m_initialized = false;
}

void TextRenderer::renderBatch(
    const std::vector<UiQuadVertex>& vertices,
    const glm::mat4& orthoProjection
) {
    if (!m_initialized || vertices.empty()) return;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    m_shader->bind();
    m_shader->setMat4("uOrtho", orthoProjection);
    m_shader->setFloat("uPxRange", DEFAULT_PX_RANGE);
    m_shader->setInt("uMsdfAtlas", 0);

    bc_glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_atlasTexture);

    bc_glBindVertexArray(m_vao);
    bc_glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    bc_glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(UiQuadVertex)), vertices.data(), GL_DYNAMIC_DRAW);

    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));

    bc_glBindVertexArray(0);
    bc_glBindBuffer(GL_ARRAY_BUFFER, 0);
    ShaderProgram::unbind();
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
}

} // namespace betacraft::render
