#pragma once

#include "audio/AudioEngine.hpp"
#include "ui/InputManager.hpp"
#include "ui/ScreenState.hpp"
#include "ui/TitleScreen.hpp"

#include <filesystem>
#include <optional>
#include <utility>
#include <vector>

namespace betacraft::ui {

class OptionsMenu {
public:
    OptionsMenu() = default;
    ~OptionsMenu() = default;

    void render(
        int windowWidth,
        int windowHeight,
        std::vector<UiQuadVertex>& outVerts,
        float mouseX,
        float mouseY,
        ScreenState activeState,
        const InputManager& input
    );

    bool handleMouseClick(
        float mouseX,
        float mouseY,
        int windowWidth,
        int windowHeight,
        ScreenState& currentState,
        ScreenState previousState,
        InputManager& input,
        audio::AudioEngine& audio
    );

    bool handleKeyDown(
        SDL_Scancode key,
        InputManager& input,
        audio::AudioEngine& audio
    );

    [[nodiscard]] bool isListeningForKey() const noexcept {
        return m_rebindingAction.has_value();
    }

    // File every change is saved to at once (options.txt in the working directory by
    // default); tests point it at a private directory.
    void setOptionsPath(std::filesystem::path path) { m_optionsPath = std::move(path); }
    [[nodiscard]] const std::filesystem::path& optionsPath() const noexcept { return m_optionsPath; }

private:
    [[nodiscard]] std::vector<MenuButton> layoutOptionsButtons(int windowWidth, int windowHeight, const InputManager& input) const;
    [[nodiscard]] std::vector<MenuButton> layoutKeybindButtons(int windowWidth, int windowHeight, const InputManager& input) const;

    std::optional<GameAction> m_rebindingAction{std::nullopt};
    std::filesystem::path m_optionsPath{"options.txt"};
};

} // namespace betacraft::ui
