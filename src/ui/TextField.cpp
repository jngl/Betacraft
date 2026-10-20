#include "ui/TextField.hpp"
#include "render/TextRenderer.hpp"

namespace betacraft::ui {

TextField::TextField(size_t maxLength, std::string_view forbidden)
    : m_forbidden(forbidden), m_maxLength(maxLength) {}

bool TextField::isAllowedChar(char c, std::string_view forbidden) noexcept {
    const auto byte = static_cast<unsigned char>(c);
    return byte >= 0x20 && byte < 0x7F && forbidden.find(c) == std::string_view::npos;
}

size_t TextField::insertText(std::string_view utf8) {
    size_t added = 0;
    for (const char c : utf8) {
        if (m_text.size() >= m_maxLength) {
            break;
        }
        // Bytes >= 0x80 are the parts of a multi-byte UTF-8 sequence: all of them are refused.
        if (isAllowedChar(c, m_forbidden)) {
            m_text.push_back(c);
            ++added;
        }
    }
    return added;
}

bool TextField::backspace() noexcept {
    if (m_text.empty()) {
        return false;
    }
    m_text.pop_back();
    return true;
}

void TextField::setText(std::string_view text) {
    m_text.clear();
    (void)insertText(text);
}

void TextField::render(std::vector<render::UiQuadVertex>& out, float x, float y, float width, float height,
                       bool caretVisible) const {
    // Beta GuiTextField: a light grey frame (white-ish when focused) around a black box.
    const glm::vec4 border = m_focused ? glm::vec4(1.0f, 1.0f, 1.0f, 1.0f) : glm::vec4(0.63f, 0.63f, 0.63f, 1.0f);
    appendSolidQuad(out, x - 1.0f, y - 1.0f, x + width + 1.0f, y + height + 1.0f, border);
    appendSolidQuad(out, x, y, x + width, y + height, glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));

    constexpr float kScale = 1.5f;
    std::string shown = m_text;
    if (m_focused && caretVisible) {
        shown.push_back('_');
    }
    const float ty = y + (height - 10.0f * kScale) * 0.5f;
    render::TextRenderer::appendText(out, shown, x + 6.0f, ty, kScale, glm::vec4(0.88f, 0.88f, 0.88f, 1.0f), true);
}

void appendSolidQuad(std::vector<render::UiQuadVertex>& out, float x0, float y0, float x1, float y1, const glm::vec4& color) {
    const render::UiQuadVertex v0{{x0, y0}, {0.0f, 0.0f}, color, 0.0f};
    const render::UiQuadVertex v1{{x1, y0}, {0.0f, 0.0f}, color, 0.0f};
    const render::UiQuadVertex v2{{x1, y1}, {0.0f, 0.0f}, color, 0.0f};
    const render::UiQuadVertex v3{{x0, y1}, {0.0f, 0.0f}, color, 0.0f};
    out.push_back(v0);
    out.push_back(v1);
    out.push_back(v2);
    out.push_back(v0);
    out.push_back(v2);
    out.push_back(v3);
}

void appendMenuButton(std::vector<render::UiQuadVertex>& out, const MenuButton& button, std::string_view label, bool hovered,
                      bool enabled) {
    const bool lit = hovered && enabled;
    appendSolidQuad(out, button.x - 2.0f, button.y - 2.0f, button.x + button.width + 2.0f,
                    button.y + button.height + 2.0f, glm::vec4(0.0f, 0.0f, 0.0f, 0.9f));
    const glm::vec4 body = !enabled ? glm::vec4(0.15f, 0.15f, 0.16f, 1.0f)
                           : lit    ? glm::vec4(0.45f, 0.45f, 0.55f, 1.0f)
                                    : glm::vec4(0.25f, 0.25f, 0.28f, 1.0f);
    appendSolidQuad(out, button.x, button.y, button.x + button.width, button.y + button.height, body);

    const float tw = render::TextRenderer::measureTextWidth(label, 1.6f);
    const float tx = button.x + (button.width - tw) * 0.5f;
    const float ty = button.y + (button.height - 16.0f) * 0.5f;
    const glm::vec4 text = !enabled ? glm::vec4(0.55f, 0.55f, 0.55f, 1.0f)
                           : lit    ? glm::vec4(1.0f, 1.0f, 0.6f, 1.0f)
                                    : glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
    render::TextRenderer::appendText(out, label, tx, ty, 1.6f, text, true);
}

bool isInside(const MenuButton& button, float x, float y) noexcept {
    return x >= button.x && x <= button.x + button.width && y >= button.y && y <= button.y + button.height;
}

} // namespace betacraft::ui
