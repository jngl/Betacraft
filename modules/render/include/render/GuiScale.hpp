#pragma once

#include <cstdint>

namespace betacraft::render {

// The player's GUI scale choice; TextRenderer::calculateGuiScale resolves Auto from the
// window size.
enum class GuiScalePreference : uint8_t {
    Auto = 0,
    Scale1x = 1,
    Scale2x = 2,
    Scale3x = 3,
    Scale4x = 4
};

} // namespace betacraft::render
