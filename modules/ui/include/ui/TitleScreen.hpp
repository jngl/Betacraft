#pragma once

#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "render/BitmapFont.hpp"
#include "ui/ScreenState.hpp"

#include <vector>

namespace betacraft::ui {

struct MenuButton {
    float x{0.0f};
    float y{0.0f};
    float width{200.0f};
    float height{40.0f};
    const char* label{""};
    int id{0};
};

class TitleScreen {
public:
    TitleScreen() = default;
    ~TitleScreen() = default;

    void render(
        int windowWidth,
        int windowHeight,
        std::vector<render::UiQuadVertex>& outVerts,
        float mouseX,
        float mouseY
    );

    // S7.8: "Singleplayer" opens the World Select screen, unless a world is already loaded
    // (EngineConfig::autoEnterWorld), which it then enters directly.
    void setWorldLoaded(bool loaded) noexcept { m_worldLoaded = loaded; }
    [[nodiscard]] bool isWorldLoaded() const noexcept { return m_worldLoaded; }

    // A new world loaded at startup (autoEnterWorld) shows the "Game Mode" button (id 4)
    // under Singleplayer; clicking it alternates Survival / Creative. Worlds made from the menu
    // pick their mode on the Create World screen (S7.8).
    void setNewWorld(bool newWorld) noexcept { m_newWorld = newWorld; }
    [[nodiscard]] bool isNewWorld() const noexcept { return m_newWorld; }
    void setSelectedMode(core::GameMode mode) noexcept { m_selectedMode = mode; }
    [[nodiscard]] core::GameMode selectedMode() const noexcept { return m_selectedMode; }

    bool handleMouseClick(
        float mouseX,
        float mouseY,
        int windowWidth,
        int windowHeight,
        ScreenState& outNextState,
        bool& outQuitGame,
        audio::AudioEngine& audio
    );

private:
    [[nodiscard]] std::vector<MenuButton> layoutButtons(int windowWidth, int windowHeight) const;

    bool m_worldLoaded{true};
    bool m_newWorld{false};
    core::GameMode m_selectedMode{core::GameMode::Survival};
};

} // namespace betacraft::ui
