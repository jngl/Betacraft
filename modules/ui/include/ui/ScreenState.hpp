#pragma once

#include <cstdint>

namespace betacraft::ui {

enum class ScreenState : uint8_t {
    TitleScreen = 0,
    InGame,
    PauseMenu,
    OptionsMenu,
    KeybindingMenu,
    DeathScreen, // new states only ever go at the end
    Sleeping,    // in a bed, the screen fades out until the morning
    WorldSelect, // the saved worlds, from the title's "Singleplayer"
    CreateWorld, // name, seed and mode of a new world
    Loading      // a world being opened (migration progress bar)
};

} // namespace betacraft::ui
