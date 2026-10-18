#pragma once

#include "physics/Player.hpp"
#include "ui/ScreenState.hpp"

#include <SDL2/SDL.h>
#include <array>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>

namespace betacraft::ui {

enum class GameAction : uint8_t {
    MoveForward = 0,
    MoveBackward,
    MoveLeft,
    MoveRight,
    Jump,
    Sneak,
    Sprint,
    Inventory,
    ToggleMusic,
    ToggleDebug,
    DropItem,
    CycleWeather, // S8.13 debug: clear -> rain -> thunder (Creative only, no default key)
    Count
};

struct ActionBinding {
    SDL_Scancode primary{SDL_SCANCODE_UNKNOWN};
    SDL_Scancode secondary{SDL_SCANCODE_UNKNOWN};
};

class InputManager {
public:
    InputManager();
    ~InputManager() = default;

    void resetToDefaults() noexcept;

    void setBinding(GameAction action, SDL_Scancode primary, SDL_Scancode secondary = SDL_SCANCODE_UNKNOWN) noexcept;
    [[nodiscard]] SDL_Scancode getPrimaryBinding(GameAction action) const noexcept;
    [[nodiscard]] SDL_Scancode getSecondaryBinding(GameAction action) const noexcept;

    void updateKeyboardState(const Uint8* keyboardState) noexcept;
    void handleEvent(const SDL_Event& ev) noexcept;

    [[nodiscard]] bool isActionActive(GameAction action) const noexcept;
    [[nodiscard]] bool isActionJustPressed(GameAction action) const noexcept;

    [[nodiscard]] physics::PlayerInput buildPlayerInput() const noexcept;

    [[nodiscard]] float getFov() const noexcept { return m_fov; }
    void setFov(float fov) noexcept { m_fov = fov; }

    [[nodiscard]] int getRenderDistance() const noexcept { return m_renderDistance; }
    void setRenderDistance(int rd) noexcept { m_renderDistance = rd; }

    [[nodiscard]] GuiScalePreference getGuiScalePreference() const noexcept { return m_guiScalePref; }
    void setGuiScalePreference(GuiScalePreference pref) noexcept { m_guiScalePref = pref; }

    // S3.8 mouse sensitivity, 0..1 (Beta mouseSensitivity, default 0.5), key mouse_sensitivity.
    [[nodiscard]] float getMouseSensitivity() const noexcept { return m_mouseSensitivity; }
    void setMouseSensitivity(float s) noexcept;
    // Beta EntityRenderer: f = 0.6s + 0.2, turn = f^3 * 8 * 0.15 degrees per mouse pixel
    // (0.15 at the default 0.5).
    [[nodiscard]] static float degreesPerPixel(float sensitivity) noexcept;
    [[nodiscard]] float getDegreesPerPixel() const noexcept { return degreesPerPixel(m_mouseSensitivity); }

    [[nodiscard]] float getMusicVolume() const noexcept { return m_musicVolume; }
    void setMusicVolume(float vol) noexcept { m_musicVolume = vol; }

    [[nodiscard]] float getSoundVolume() const noexcept { return m_soundVolume; }
    void setSoundVolume(float vol) noexcept { m_soundVolume = vol; }

    [[nodiscard]] bool isMusicEnabled() const noexcept { return m_musicEnabled; }
    void setMusicEnabled(bool enabled) noexcept { m_musicEnabled = enabled; }

    [[nodiscard]] bool loadFromFile(const std::filesystem::path& path = "options.txt");
    bool saveToFile(const std::filesystem::path& path = "options.txt") const;

    [[nodiscard]] static std::string getActionDisplayName(GameAction action);
    [[nodiscard]] static std::string getKeyDisplayName(SDL_Scancode scancode);

private:
    std::array<ActionBinding, static_cast<size_t>(GameAction::Count)> m_bindings{};
    std::array<bool, static_cast<size_t>(GameAction::Count)> m_active{};
    std::array<bool, static_cast<size_t>(GameAction::Count)> m_justPressed{};

    float m_fov{70.0f};
    int m_renderDistance{8};
    GuiScalePreference m_guiScalePref{GuiScalePreference::Auto};
    float m_mouseSensitivity{0.5f};
    float m_musicVolume{1.0f};
    float m_soundVolume{1.0f};
    bool m_musicEnabled{true};
};

} // namespace betacraft::ui
