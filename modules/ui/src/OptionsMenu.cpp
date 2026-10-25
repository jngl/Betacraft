#include "ui/OptionsMenu.hpp"
#include "render/TextRenderer.hpp"

#include <cmath>
#include <iomanip>
#include <sstream>

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

std::vector<MenuButton> OptionsMenu::layoutOptionsButtons(
    int windowWidth,
    int windowHeight,
    const InputManager& /*input*/
) const {
    const float sw = static_cast<float>(windowWidth);
    const float sh = static_cast<float>(windowHeight);
    const float btnW = 320.0f;
    const float btnH = 36.0f;
    const float cx = (sw - btnW) * 0.5f;
    const float startY = sh * 0.30f;
    const float halfW = (btnW - 10.0f) * 0.5f;

    return {
        {cx, startY + 0.0f,   btnW, btnH, "FOV", 1},
        {cx, startY + 45.0f,  btnW, btnH, "Render Distance", 2},
        // GUI Scale and Sensitivity share a row, each half as wide.
        {cx, startY + 90.0f,  halfW, btnH, "GUI Scale", 3},
        {cx + btnW - halfW, startY + 90.0f, halfW, btnH, "Sensitivity", 7},
        {cx, startY + 135.0f, btnW, btnH, "Music", 4},
        {cx, startY + 180.0f, btnW, btnH, "Controls...", 5},
        {cx, startY + 235.0f, btnW, btnH, "Done", 6}
    };
}

std::vector<MenuButton> OptionsMenu::layoutKeybindButtons(
    int windowWidth,
    int windowHeight,
    const InputManager& /*input*/
) const {
    const float sw = static_cast<float>(windowWidth);
    const float sh = static_cast<float>(windowHeight);
    const float btnW = 340.0f;
    const float btnH = 32.0f;
    const float cx = (sw - btnW) * 0.5f;
    const float startY = sh * 0.22f;

    return {
        {cx, startY + 0.0f,   btnW, btnH, "Forward", 100},
        {cx, startY + 38.0f,  btnW, btnH, "Backward", 101},
        {cx, startY + 76.0f,  btnW, btnH, "Left", 102},
        {cx, startY + 114.0f, btnW, btnH, "Right", 103},
        {cx, startY + 152.0f, btnW, btnH, "Jump", 104},
        {cx, startY + 190.0f, btnW, btnH, "Sneak", 105},
        {cx, startY + 228.0f, btnW, btnH, "Sprint", 106},
        {cx, startY + 266.0f, btnW, btnH, "Inventory", 107},
        {cx, startY + 315.0f, btnW, btnH, "Reset Keys", 108},
        {cx, startY + 355.0f, btnW, btnH, "Done", 109}
    };
}

void OptionsMenu::render(
    int windowWidth,
    int windowHeight,
    std::vector<render::UiQuadVertex>& outVerts,
    float mouseX,
    float mouseY,
    ScreenState activeState,
    const InputManager& input
) {
    const float sw = static_cast<float>(windowWidth);
    const float sh = static_cast<float>(windowHeight);

    // Dark translucent background
    pushSolidQuad(outVerts, 0.0f, 0.0f, sw, sh, glm::vec4(0.08f, 0.08f, 0.10f, 0.94f));

    const bool isKeybinds = (activeState == ScreenState::KeybindingMenu);
    const std::string header = isKeybinds ? "Controls" : "Options";
    const float hw = render::TextRenderer::measureTextWidth(header, 2.5f);
    render::TextRenderer::appendText(
        outVerts,
        header,
        (sw - hw) * 0.5f,
        sh * 0.12f,
        2.5f,
        glm::vec4(1.0f, 0.95f, 0.65f, 1.0f),
        true
    );

    const auto buttons = isKeybinds ? layoutKeybindButtons(windowWidth, windowHeight, input)
                                    : layoutOptionsButtons(windowWidth, windowHeight, input);

    for (const auto& btn : buttons) {
        const bool hovered = (mouseX >= btn.x && mouseX <= btn.x + btn.width &&
                              mouseY >= btn.y && mouseY <= btn.y + btn.height);

        // Border
        pushSolidQuad(outVerts, btn.x - 2.0f, btn.y - 2.0f, btn.x + btn.width + 2.0f, btn.y + btn.height + 2.0f, glm::vec4(0.0f, 0.0f, 0.0f, 0.9f));

        // Body
        glm::vec4 bodyCol = hovered ? glm::vec4(0.42f, 0.42f, 0.50f, 1.0f) : glm::vec4(0.22f, 0.22f, 0.25f, 1.0f);

        // Dynamic label string
        std::string labelStr = btn.label;
        if (!isKeybinds) {
            if (btn.id == 1) {
                labelStr = "FOV: " + std::to_string(static_cast<int>(input.getFov())) + "\xc2\xb0";
            } else if (btn.id == 2) {
                labelStr = "Render Distance: " + std::to_string(input.getRenderDistance()) + " chunks";
            } else if (btn.id == 3) {
                const auto pref = input.getGuiScalePreference();
                std::string sName = "Auto";
                if (pref == render::GuiScalePreference::Scale1x) sName = "1x";
                else if (pref == render::GuiScalePreference::Scale2x) sName = "2x";
                else if (pref == render::GuiScalePreference::Scale3x) sName = "3x";
                else if (pref == render::GuiScalePreference::Scale4x) sName = "4x";
                labelStr = "GUI Scale: " + sName;
            } else if (btn.id == 7) {
                labelStr = "Sensitivity: " + std::to_string(static_cast<int>(std::lround(input.getMouseSensitivity() * 100.0f))) + "%";
            } else if (btn.id == 4) {
                labelStr = "Music: " + std::string(input.isMusicEnabled() ? "ON" : "OFF");
            }
        } else {
            // Keybinding buttons
            if (btn.id >= 100 && btn.id <= 107) {
                const GameAction act = static_cast<GameAction>(btn.id - 100);
                if (m_rebindingAction.has_value() && m_rebindingAction.value() == act) {
                    labelStr = InputManager::getActionDisplayName(act) + ": > ??? <";
                    bodyCol = glm::vec4(0.6f, 0.55f, 0.15f, 1.0f); // Amber highlight for listening
                } else {
                    const auto sc = input.getPrimaryBinding(act);
                    labelStr = InputManager::getActionDisplayName(act) + ": " + InputManager::getKeyDisplayName(sc);
                }
            }
        }

        pushSolidQuad(outVerts, btn.x, btn.y, btn.x + btn.width, btn.y + btn.height, bodyCol);

        const float tw = render::TextRenderer::measureTextWidth(labelStr, 1.4f);
        const float tx = btn.x + (btn.width - tw) * 0.5f;
        const float ty = btn.y + (btn.height - 14.0f) * 0.5f;
        const glm::vec4 textCol = hovered ? glm::vec4(1.0f, 1.0f, 0.6f, 1.0f) : glm::vec4(0.9f, 0.9f, 0.9f, 1.0f);
        render::TextRenderer::appendText(outVerts, labelStr, tx, ty, 1.4f, textCol, true);
    }
}

bool OptionsMenu::handleMouseClick(
    float mouseX,
    float mouseY,
    int windowWidth,
    int windowHeight,
    ScreenState& currentState,
    ScreenState previousState,
    InputManager& input,
    audio::AudioEngine& audio
) {
    if (m_rebindingAction.has_value()) {
        // Clicking outside cancels rebinding
        m_rebindingAction.reset();
        audio.playUiClick();
        return true;
    }

    const bool isKeybinds = (currentState == ScreenState::KeybindingMenu);
    const auto buttons = isKeybinds ? layoutKeybindButtons(windowWidth, windowHeight, input)
                                    : layoutOptionsButtons(windowWidth, windowHeight, input);

    for (const auto& btn : buttons) {
        if (mouseX >= btn.x && mouseX <= btn.x + btn.width &&
            mouseY >= btn.y && mouseY <= btn.y + btn.height) {
            audio.playUiClick();

            if (!isKeybinds) {
                if (btn.id == 1) { // FOV
                    float fov = input.getFov() + 10.0f;
                    if (fov > 110.0f) fov = 60.0f;
                    input.setFov(fov);
                    (void)input.saveToFile(m_optionsPath);
                } else if (btn.id == 2) { // Render Distance
                    int rd = input.getRenderDistance() + 4;
                    if (rd > 16) rd = 4;
                    input.setRenderDistance(rd);
                    (void)input.saveToFile(m_optionsPath);
                } else if (btn.id == 3) { // GUI Scale
                    int nextPref = static_cast<int>(input.getGuiScalePreference()) + 1;
                    if (nextPref > 4) nextPref = 0;
                    input.setGuiScalePreference(static_cast<render::GuiScalePreference>(nextPref));
                    (void)input.saveToFile(m_optionsPath);
                } else if (btn.id == 7) { // Mouse sensitivity, 10 % steps, wraps after 100 %
                    const int step = static_cast<int>(std::lround(input.getMouseSensitivity() * 10.0f)) + 1;
                    input.setMouseSensitivity(static_cast<float>(step > 10 ? 0 : step) / 10.0f);
                    (void)input.saveToFile(m_optionsPath);
                } else if (btn.id == 4) { // Music
                    input.setMusicEnabled(!input.isMusicEnabled());
                    if (audio.isMusicEnabled() != input.isMusicEnabled()) {
                        audio.toggleMusic();
                    }
                    (void)input.saveToFile(m_optionsPath);
                } else if (btn.id == 5) { // Controls...
                    currentState = ScreenState::KeybindingMenu;
                } else if (btn.id == 6) { // Done
                    (void)input.saveToFile(m_optionsPath);
                    currentState = previousState;
                }
                return true;
            } else {
                if (btn.id >= 100 && btn.id <= 107) {
                    m_rebindingAction = static_cast<GameAction>(btn.id - 100);
                    return true;
                } else if (btn.id == 108) { // Reset Keys
                    input.resetToDefaults();
                    (void)input.saveToFile(m_optionsPath);
                    return true;
                } else if (btn.id == 109) { // Done
                    (void)input.saveToFile(m_optionsPath);
                    currentState = ScreenState::OptionsMenu;
                    return true;
                }
            }
        }
    }

    return false;
}

bool OptionsMenu::handleKeyDown(
    SDL_Scancode key,
    InputManager& input,
    audio::AudioEngine& audio
) {
    if (!m_rebindingAction.has_value()) return false;

    if (key == SDL_SCANCODE_ESCAPE) {
        // Cancel rebinding
        m_rebindingAction.reset();
        audio.playUiClick();
        return true;
    }

    input.setBinding(m_rebindingAction.value(), key);
    m_rebindingAction.reset();
    (void)input.saveToFile(m_optionsPath);
    audio.playUiClick();
    return true;
}

} // namespace betacraft::ui
