#include "ui/TitleScreen.hpp"
#include "render/TextRenderer.hpp"

namespace betacraft::ui {

namespace {

void pushSolidQuad(
    std::vector<render::UiQuadVertex>& out,
    float x0,
    float y0,
    float x1,
    float y1,
    const glm::vec4& color
) {
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

} // namespace

std::vector<MenuButton> TitleScreen::layoutButtons(int windowWidth, int windowHeight) const {
    const float sw = static_cast<float>(windowWidth);
    const float sh = static_cast<float>(windowHeight);
    const float btnW = 280.0f;
    const float btnH = 38.0f;
    const float btnX = (sw - btnW) * 0.5f;
    const float startY = sh * 0.5f - 20.0f;

    std::vector<MenuButton> buttons{
        {btnX, startY, btnW, btnH, "Singleplayer", 1},
        {btnX, startY + 50.0f, btnW, btnH, "Options...", 2},
        {btnX, startY + 100.0f, btnW, btnH, "Quit Game", 3}
    };
    if (m_newWorld && m_worldLoaded) {
        // Slotted under Singleplayer; the other buttons move down one row.
        for (size_t i = 1; i < buttons.size(); ++i) {
            buttons[i].y += 50.0f;
        }
        const char* label = m_selectedMode == core::GameMode::Creative ? "Game Mode: Creative" : "Game Mode: Survival";
        buttons.insert(buttons.begin() + 1, MenuButton{btnX, startY + 50.0f, btnW, btnH, label, 4});
    }
    return buttons;
}

void TitleScreen::render(
    int windowWidth,
    int windowHeight,
    std::vector<render::UiQuadVertex>& outVerts,
    float mouseX,
    float mouseY
) {
    const float sw = static_cast<float>(windowWidth);
    const float sh = static_cast<float>(windowHeight);

    // Dark dirt background
    pushSolidQuad(outVerts, 0.0f, 0.0f, sw, sh, glm::vec4(0.12f, 0.08f, 0.05f, 1.0f));

    // Logo / Title banner
    const std::string title = "BetaCraft 1.7.3";
    const float titleW = render::TextRenderer::measureTextWidth(title, 3.2f);
    render::TextRenderer::appendText(
        outVerts,
        title,
        (sw - titleW) * 0.5f,
        sh * 0.22f,
        3.2f,
        glm::vec4(1.0f, 0.95f, 0.65f, 1.0f),
        true
    );

    const std::string subtitle = "High Performance C++20 Modernization";
    const float subW = render::TextRenderer::measureTextWidth(subtitle, 1.4f);
    render::TextRenderer::appendText(
        outVerts,
        subtitle,
        (sw - subW) * 0.5f,
        sh * 0.22f + 45.0f,
        1.4f,
        glm::vec4(0.85f, 0.85f, 0.85f, 1.0f),
        true
    );

    // Buttons
    const auto buttons = layoutButtons(windowWidth, windowHeight);
    for (const auto& btn : buttons) {
        const bool hovered = (mouseX >= btn.x && mouseX <= btn.x + btn.width &&
                              mouseY >= btn.y && mouseY <= btn.y + btn.height);

        // Border
        pushSolidQuad(outVerts, btn.x - 2.0f, btn.y - 2.0f, btn.x + btn.width + 2.0f, btn.y + btn.height + 2.0f, glm::vec4(0.0f, 0.0f, 0.0f, 0.9f));

        // Body
        const glm::vec4 bodyCol = hovered ? glm::vec4(0.45f, 0.45f, 0.55f, 1.0f) : glm::vec4(0.25f, 0.25f, 0.28f, 1.0f);
        pushSolidQuad(outVerts, btn.x, btn.y, btn.x + btn.width, btn.y + btn.height, bodyCol);

        // Text
        const float tw = render::TextRenderer::measureTextWidth(btn.label, 1.6f);
        const float tx = btn.x + (btn.width - tw) * 0.5f;
        const float ty = btn.y + (btn.height - 16.0f) * 0.5f;
        const glm::vec4 textCol = hovered ? glm::vec4(1.0f, 1.0f, 0.6f, 1.0f) : glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
        render::TextRenderer::appendText(outVerts, btn.label, tx, ty, 1.6f, textCol, true);
    }

    // Footer
    const std::string footer = "BetaCraft 1.7.3 Modernization. All rights reserved.";
    render::TextRenderer::appendText(outVerts, footer, 12.0f, sh - 24.0f, 1.2f, glm::vec4(0.6f, 0.6f, 0.6f, 1.0f), true);
}

bool TitleScreen::handleMouseClick(
    float mouseX,
    float mouseY,
    int windowWidth,
    int windowHeight,
    ScreenState& outNextState,
    bool& outQuitGame,
    audio::AudioEngine& audio
) {
    const auto buttons = layoutButtons(windowWidth, windowHeight);
    for (const auto& btn : buttons) {
        if (mouseX >= btn.x && mouseX <= btn.x + btn.width &&
            mouseY >= btn.y && mouseY <= btn.y + btn.height) {
            audio.playUiClick();
            if (btn.id == 1) { // Singleplayer
                outNextState = m_worldLoaded ? ScreenState::InGame : ScreenState::WorldSelect;
                return true;
            } else if (btn.id == 2) { // Options...
                outNextState = ScreenState::OptionsMenu;
                return true;
            } else if (btn.id == 3) { // Quit
                outQuitGame = true;
                return true;
            } else if (btn.id == 4) { // Game Mode toggle (new worlds only)
                m_selectedMode = m_selectedMode == core::GameMode::Creative ? core::GameMode::Survival
                                                                          : core::GameMode::Creative;
                return true;
            }
        }
    }
    return false;
}

} // namespace betacraft::ui
