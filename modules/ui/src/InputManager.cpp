#include "ui/InputManager.hpp"
#include "core/Logger.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>

namespace betacraft::ui {

InputManager::InputManager() {
    resetToDefaults();
}

void InputManager::resetToDefaults() noexcept {
    m_bindings.fill(ActionBinding{SDL_SCANCODE_UNKNOWN, SDL_SCANCODE_UNKNOWN});
    m_active.fill(false);
    m_justPressed.fill(false);

    setBinding(GameAction::MoveForward, SDL_SCANCODE_W, SDL_SCANCODE_UP);
    setBinding(GameAction::MoveBackward, SDL_SCANCODE_S, SDL_SCANCODE_DOWN);
    setBinding(GameAction::MoveLeft, SDL_SCANCODE_A, SDL_SCANCODE_LEFT);
    setBinding(GameAction::MoveRight, SDL_SCANCODE_D, SDL_SCANCODE_RIGHT);
    setBinding(GameAction::Jump, SDL_SCANCODE_SPACE);
    setBinding(GameAction::Sneak, SDL_SCANCODE_LSHIFT);
    setBinding(GameAction::Sprint, SDL_SCANCODE_LCTRL);
    setBinding(GameAction::Inventory, SDL_SCANCODE_E);
    setBinding(GameAction::ToggleMusic, SDL_SCANCODE_M);
    setBinding(GameAction::ToggleDebug, SDL_SCANCODE_F3);
    setBinding(GameAction::DropItem, SDL_SCANCODE_Q);

    m_fov = 70.0f;
    m_renderDistance = 8;
    m_guiScalePref = render::GuiScalePreference::Auto;
    m_mouseSensitivity = 0.5f;
    m_musicVolume = 1.0f;
    m_soundVolume = 1.0f;
    m_musicEnabled = true;
}

void InputManager::setMouseSensitivity(float s) noexcept {
    m_mouseSensitivity = std::isfinite(s) ? std::clamp(s, 0.0f, 1.0f) : 0.5f;
}

float InputManager::degreesPerPixel(float sensitivity) noexcept {
    const float f = 0.6f * std::clamp(sensitivity, 0.0f, 1.0f) + 0.2f;
    return f * f * f * 8.0f * 0.15f;
}

void InputManager::setBinding(GameAction action, SDL_Scancode primary, SDL_Scancode secondary) noexcept {
    const size_t idx = static_cast<size_t>(action);
    if (idx < m_bindings.size()) {
        m_bindings[idx].primary = primary;
        if (secondary != SDL_SCANCODE_UNKNOWN) {
            m_bindings[idx].secondary = secondary;
        }
    }
}

SDL_Scancode InputManager::getPrimaryBinding(GameAction action) const noexcept {
    const size_t idx = static_cast<size_t>(action);
    return (idx < m_bindings.size()) ? m_bindings[idx].primary : SDL_SCANCODE_UNKNOWN;
}

SDL_Scancode InputManager::getSecondaryBinding(GameAction action) const noexcept {
    const size_t idx = static_cast<size_t>(action);
    return (idx < m_bindings.size()) ? m_bindings[idx].secondary : SDL_SCANCODE_UNKNOWN;
}

void InputManager::updateKeyboardState(const Uint8* keyboardState) noexcept {
    if (!keyboardState) return;

    for (size_t i = 0; i < m_bindings.size(); ++i) {
        bool active = false;
        const auto pri = m_bindings[i].primary;
        const auto sec = m_bindings[i].secondary;

        if (pri != SDL_SCANCODE_UNKNOWN && keyboardState[pri] != 0) {
            active = true;
        }
        if (sec != SDL_SCANCODE_UNKNOWN && keyboardState[sec] != 0) {
            active = true;
        }

        m_justPressed[i] = (!m_active[i] && active);
        m_active[i] = active;
    }
}

void InputManager::handleEvent(const SDL_Event& ev) noexcept {
    if (ev.type == SDL_KEYDOWN && !ev.key.repeat) {
        const SDL_Scancode sc = ev.key.keysym.scancode;
        for (size_t i = 0; i < m_bindings.size(); ++i) {
            if (m_bindings[i].primary == sc || m_bindings[i].secondary == sc) {
                m_active[i] = true;
                m_justPressed[i] = true;
            }
        }
    } else if (ev.type == SDL_KEYUP) {
        const SDL_Scancode sc = ev.key.keysym.scancode;
        for (size_t i = 0; i < m_bindings.size(); ++i) {
            if (m_bindings[i].primary == sc || m_bindings[i].secondary == sc) {
                m_active[i] = false;
            }
        }
    }
}

bool InputManager::isActionActive(GameAction action) const noexcept {
    const size_t idx = static_cast<size_t>(action);
    return (idx < m_active.size()) ? m_active[idx] : false;
}

bool InputManager::isActionJustPressed(GameAction action) const noexcept {
    const size_t idx = static_cast<size_t>(action);
    return (idx < m_justPressed.size()) ? m_justPressed[idx] : false;
}

physics::PlayerInput InputManager::buildPlayerInput() const noexcept {
    physics::PlayerInput pi{};
    pi.forward  = isActionActive(GameAction::MoveForward);
    pi.backward = isActionActive(GameAction::MoveBackward);
    pi.left     = isActionActive(GameAction::MoveLeft);
    pi.right    = isActionActive(GameAction::MoveRight);
    pi.jump     = isActionActive(GameAction::Jump);
    pi.sneak    = isActionActive(GameAction::Sneak);
    pi.sprint   = isActionActive(GameAction::Sprint);
    return pi;
}

bool InputManager::saveToFile(const std::filesystem::path& path) const {
    std::ofstream out(path);
    if (!out.is_open()) {
        core::Logger::warn("InputManager", "Failed to open ", path.string(), " for writing options.");
        return false;
    }

    out << "# BetaCraft 1.7.3 Options & Keybindings\n";
    out << "key_forward:" << static_cast<int>(getPrimaryBinding(GameAction::MoveForward)) << "\n";
    out << "key_backward:" << static_cast<int>(getPrimaryBinding(GameAction::MoveBackward)) << "\n";
    out << "key_left:" << static_cast<int>(getPrimaryBinding(GameAction::MoveLeft)) << "\n";
    out << "key_right:" << static_cast<int>(getPrimaryBinding(GameAction::MoveRight)) << "\n";
    out << "key_jump:" << static_cast<int>(getPrimaryBinding(GameAction::Jump)) << "\n";
    out << "key_sneak:" << static_cast<int>(getPrimaryBinding(GameAction::Sneak)) << "\n";
    out << "key_sprint:" << static_cast<int>(getPrimaryBinding(GameAction::Sprint)) << "\n";
    out << "key_inventory:" << static_cast<int>(getPrimaryBinding(GameAction::Inventory)) << "\n";
    out << "key_toggle_music:" << static_cast<int>(getPrimaryBinding(GameAction::ToggleMusic)) << "\n";
    out << "key_toggle_debug:" << static_cast<int>(getPrimaryBinding(GameAction::ToggleDebug)) << "\n";
    out << "key_drop:" << static_cast<int>(getPrimaryBinding(GameAction::DropItem)) << "\n";
    out << "key_cycle_weather:" << static_cast<int>(getPrimaryBinding(GameAction::CycleWeather)) << "\n";

    out << "fov:" << m_fov << "\n";
    out << "render_distance:" << m_renderDistance << "\n";
    out << "gui_scale:" << static_cast<int>(m_guiScalePref) << "\n";
    out << "mouse_sensitivity:" << m_mouseSensitivity << "\n";
    out << "music_volume:" << m_musicVolume << "\n";
    out << "sound_volume:" << m_soundVolume << "\n";
    out << "music_enabled:" << (m_musicEnabled ? "true" : "false") << "\n";

    return true;
}

bool InputManager::loadFromFile(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        return false;
    }

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        const std::string key = line.substr(0, colon);
        const std::string val = line.substr(colon + 1);
        if (val.empty()) continue;

        try {
            if (key == "key_forward") {
                setBinding(GameAction::MoveForward, static_cast<SDL_Scancode>(std::stoi(val)));
            } else if (key == "key_backward") {
                setBinding(GameAction::MoveBackward, static_cast<SDL_Scancode>(std::stoi(val)));
            } else if (key == "key_left") {
                setBinding(GameAction::MoveLeft, static_cast<SDL_Scancode>(std::stoi(val)));
            } else if (key == "key_right") {
                setBinding(GameAction::MoveRight, static_cast<SDL_Scancode>(std::stoi(val)));
            } else if (key == "key_jump") {
                setBinding(GameAction::Jump, static_cast<SDL_Scancode>(std::stoi(val)));
            } else if (key == "key_sneak") {
                setBinding(GameAction::Sneak, static_cast<SDL_Scancode>(std::stoi(val)));
            } else if (key == "key_sprint") {
                setBinding(GameAction::Sprint, static_cast<SDL_Scancode>(std::stoi(val)));
            } else if (key == "key_inventory") {
                setBinding(GameAction::Inventory, static_cast<SDL_Scancode>(std::stoi(val)));
            } else if (key == "key_toggle_music" || key == "key_music") {
                setBinding(GameAction::ToggleMusic, static_cast<SDL_Scancode>(std::stoi(val)));
            } else if (key == "key_toggle_debug" || key == "key_debug") {
                setBinding(GameAction::ToggleDebug, static_cast<SDL_Scancode>(std::stoi(val)));
            } else if (key == "key_drop") {
                setBinding(GameAction::DropItem, static_cast<SDL_Scancode>(std::stoi(val)));
            } else if (key == "key_cycle_weather") {
                setBinding(GameAction::CycleWeather, static_cast<SDL_Scancode>(std::stoi(val)));
            } else if (key == "fov") {
                m_fov = std::stof(val);
            } else if (key == "render_distance" || key == "renderDistance") {
                m_renderDistance = std::stoi(val);
            } else if (key == "gui_scale" || key == "guiScale") {
                const int s = std::clamp(std::stoi(val), 0, 4);
                m_guiScalePref = static_cast<render::GuiScalePreference>(s);
            } else if (key == "mouse_sensitivity" || key == "mouseSensitivity") {
                setMouseSensitivity(std::stof(val));
            } else if (key == "music_volume" || key == "musicVolume") {
                m_musicVolume = std::stof(val);
            } else if (key == "sound_volume" || key == "soundVolume") {
                m_soundVolume = std::stof(val);
            } else if (key == "music_enabled" || key == "musicEnabled") {
                m_musicEnabled = (val == "true" || val == "1");
            }
        } catch (...) {
            // Ignore malformed numeric values gracefully
        }
    }

    return true;
}

std::string InputManager::getActionDisplayName(GameAction action) {
    switch (action) {
        case GameAction::MoveForward:  return "Forward";
        case GameAction::MoveBackward: return "Backward";
        case GameAction::MoveLeft:     return "Left";
        case GameAction::MoveRight:    return "Right";
        case GameAction::Jump:         return "Jump";
        case GameAction::Sneak:        return "Sneak";
        case GameAction::Sprint:       return "Sprint";
        case GameAction::Inventory:    return "Inventory";
        case GameAction::ToggleMusic:  return "Toggle Music";
        case GameAction::ToggleDebug:  return "Toggle Debug";
        case GameAction::DropItem:     return "Drop Item";
        case GameAction::CycleWeather: return "Cycle Weather";
        default:                       return "Action";
    }
}

std::string InputManager::getKeyDisplayName(SDL_Scancode scancode) {
    if (scancode == SDL_SCANCODE_SPACE) {
        return "SPACE";
    }
    const char* name = SDL_GetScancodeName(scancode);
    if (!name || *name == '\0') {
        return "NONE";
    }
    std::string s(name);
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

} // namespace betacraft::ui
