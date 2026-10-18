#include "ui/DeathScreen.hpp"
#include "render/TextRenderer.hpp"

#include <string>

namespace betacraft::ui {

namespace {

constexpr int kRespawnId = 1;
constexpr int kTitleMenuId = 2;

void pushSolidQuad(std::vector<UiQuadVertex>& out, float x0, float y0, float x1, float y1, const glm::vec4& color) {
    const UiQuadVertex v0{{x0, y0}, {0.0f, 0.0f}, color, 0.0f};
    const UiQuadVertex v1{{x1, y0}, {0.0f, 0.0f}, color, 0.0f};
    const UiQuadVertex v2{{x1, y1}, {0.0f, 0.0f}, color, 0.0f};
    const UiQuadVertex v3{{x0, y1}, {0.0f, 0.0f}, color, 0.0f};
    out.push_back(v0);
    out.push_back(v1);
    out.push_back(v2);
    out.push_back(v0);
    out.push_back(v2);
    out.push_back(v3);
}

[[nodiscard]] bool contains(const MenuButton& btn, float x, float y) noexcept {
    return x >= btn.x && x <= btn.x + btn.width && y >= btn.y && y <= btn.y + btn.height;
}

} // namespace

std::vector<MenuButton> DeathScreen::layoutButtons(int windowWidth, int windowHeight) {
    const float sw = static_cast<float>(windowWidth);
    const float sh = static_cast<float>(windowHeight);
    const float btnW = 280.0f;
    const float btnH = 38.0f;
    const float btnX = (sw - btnW) * 0.5f;
    const float startY = sh * 0.5f;
    return {
        {btnX, startY, btnW, btnH, "Respawn", kRespawnId},
        {btnX, startY + 50.0f, btnW, btnH, "Title menu", kTitleMenuId}
    };
}

void DeathScreen::render(int windowWidth, int windowHeight, std::vector<UiQuadVertex>& outVerts, float mouseX,
                         float mouseY) const {
    const float sw = static_cast<float>(windowWidth);
    const float sh = static_cast<float>(windowHeight);

    // Beta's gradient from dark red to a lighter red, flattened to one translucent tint.
    pushSolidQuad(outVerts, 0.0f, 0.0f, sw, sh, glm::vec4(0.38f, 0.0f, 0.0f, 0.55f));

    const std::string title = "You died!";
    const float titleW = render::TextRenderer::measureTextWidth(title, 3.2f);
    render::TextRenderer::appendText(outVerts, title, (sw - titleW) * 0.5f, sh * 0.25f, 3.2f,
                                     glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), true);

    for (const MenuButton& btn : layoutButtons(windowWidth, windowHeight)) {
        const bool hovered = contains(btn, mouseX, mouseY);
        pushSolidQuad(outVerts, btn.x - 2.0f, btn.y - 2.0f, btn.x + btn.width + 2.0f, btn.y + btn.height + 2.0f,
                      glm::vec4(0.0f, 0.0f, 0.0f, 0.9f));
        const glm::vec4 bodyCol = hovered ? glm::vec4(0.45f, 0.45f, 0.55f, 1.0f) : glm::vec4(0.25f, 0.25f, 0.28f, 1.0f);
        pushSolidQuad(outVerts, btn.x, btn.y, btn.x + btn.width, btn.y + btn.height, bodyCol);

        const float tw = render::TextRenderer::measureTextWidth(btn.label, 1.6f);
        const glm::vec4 textCol = hovered ? glm::vec4(1.0f, 1.0f, 0.6f, 1.0f) : glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
        render::TextRenderer::appendText(outVerts, btn.label, btn.x + (btn.width - tw) * 0.5f,
                                         btn.y + (btn.height - 16.0f) * 0.5f, 1.6f, textCol, true);
    }
}

DeathChoice DeathScreen::handleMouseClick(float mouseX, float mouseY, int windowWidth, int windowHeight,
                                          audio::AudioEngine& audio) const {
    for (const MenuButton& btn : layoutButtons(windowWidth, windowHeight)) {
        if (!contains(btn, mouseX, mouseY)) continue;
        audio.playUiClick();
        return btn.id == kRespawnId ? DeathChoice::Respawn : DeathChoice::TitleMenu;
    }
    return DeathChoice::None;
}

} // namespace betacraft::ui
