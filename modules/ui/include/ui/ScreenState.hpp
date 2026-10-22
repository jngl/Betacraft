#pragma once

#include <cstdint>

namespace betacraft::ui {

enum class ScreenState : uint8_t {
    TitleScreen = 0,
    InGame,
    PauseMenu,
    OptionsMenu,
    KeybindingMenu,
    DeathScreen, // S1.6; new states only ever go at the end
    Sleeping,    // S5.8: in a bed, the screen fades out until the morning
    WorldSelect, // S7.8: the saved worlds, from the title's "Singleplayer"
    CreateWorld, // S7.8: name, seed and mode of a new world
    Loading      // S7.8/S7.9: a world being opened (migration progress bar)
};

} // namespace betacraft::ui
