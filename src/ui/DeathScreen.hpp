#pragma once

#include "audio/AudioEngine.hpp"
#include "render/BitmapFont.hpp"
#include "ui/TitleScreen.hpp"

#include <cstdint>
#include <vector>

namespace betacraft::ui {

enum class DeathChoice : uint8_t { None, Respawn, TitleMenu };

// Beta GuiGameOver: "You died!" over a red tint, with "Respawn" and "Title menu".
class DeathScreen {
public:
    void render(
        int windowWidth,
        int windowHeight,
        std::vector<render::UiQuadVertex>& outVerts,
        float mouseX,
        float mouseY
    ) const;

    // The button under the cursor (and a click sound), None elsewhere.
    DeathChoice handleMouseClick(
        float mouseX,
        float mouseY,
        int windowWidth,
        int windowHeight,
        audio::AudioEngine& audio
    ) const;

    [[nodiscard]] static std::vector<MenuButton> layoutButtons(int windowWidth, int windowHeight);
};

} // namespace betacraft::ui
