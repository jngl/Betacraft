#pragma once

#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "ui/BitmapFont.hpp"
#include "ui/TextField.hpp"
#include "ui/TitleScreen.hpp"

#include <SDL2/SDL.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace betacraft::ui {

// Java String.hashCode (s[0]*31^(n-1) + ... + s[n-1], wrapping int32) over the text's bytes,
// which are its UTF-16 code units for the ASCII a TextField accepts.
[[nodiscard]] int32_t javaStringHashCode(std::string_view text) noexcept;

// Beta GuiCreateWorld seed: an empty field gives `randomSeed`; text Long.parseLong accepts
// (Java 6: optional '-', decimal digits, within int64) is taken as is, except 0 which also
// gives `randomSeed`; anything else is hashed with String.hashCode (sign-extended). Negative
// seeds are stored as their two's complement.
[[nodiscard]] uint64_t seedFromText(std::string_view text, uint64_t randomSeed) noexcept;

// Beta GuiCreateWorld folder name: trimmed, every kForbiddenFileNameChars and control
// character replaced with '_', "World" when nothing is left. Also (not in Beta, which never
// met them): a leading '.' becomes '_' (no hidden folder, never "." or ".."), and the Windows
// device names (CON, NUL, COM1...) get a '_' appended.
[[nodiscard]] std::string sanitizeFolderName(std::string_view name);
// sanitizeFolderName, then '-' appended while `savesRoot` already has an entry of that name
// (Beta generateUnusedFolderName).
[[nodiscard]] std::string uniqueFolderName(const std::filesystem::path& savesRoot, std::string_view name);

enum class CreateWorldChoice : uint8_t { None, Create, Cancel };

// Beta GuiCreateWorld: "World Name" and "Seed for the World Generator" fields, the S1.4 game
// mode button (moved here from the title screen), "Create New World" and "Cancel".
class CreateWorldScreen {
public:
    CreateWorldScreen();

    // Back to the defaults: "New World", empty seed, `mode`, the name field focused.
    void reset(core::GameMode mode = core::GameMode::Survival);

    [[nodiscard]] TextField& nameField() noexcept { return m_name; }
    [[nodiscard]] TextField& seedField() noexcept { return m_seed; }
    [[nodiscard]] core::GameMode selectedMode() const noexcept { return m_mode; }
    void setSelectedMode(core::GameMode mode) noexcept { m_mode = mode; }
    // Shown as "Will be saved in: <folder>" under the name.
    void setSavesRoot(std::filesystem::path savesRoot) { m_savesRoot = std::move(savesRoot); }

    // The world to create: the name (or "World"), seedFromText and the mode. `folder` gets
    // uniqueFolderName(savesRoot, name).
    [[nodiscard]] core::NewWorldParams params(uint64_t randomSeed) const;
    [[nodiscard]] std::filesystem::path folder(const std::filesystem::path& savesRoot) const;

    void render(int windowWidth, int windowHeight, std::vector<UiQuadVertex>& outVerts, float mouseX, float mouseY,
                bool caretVisible) const;
    CreateWorldChoice handleMouseClick(float mouseX, float mouseY, int windowWidth, int windowHeight,
                                       audio::AudioEngine& audio);
    // SDL_TEXTINPUT into the focused field.
    void handleTextInput(std::string_view utf8);
    // Backspace, Tab (next field), Enter (create), Escape (cancel).
    CreateWorldChoice handleKeyDown(SDL_Keycode key, audio::AudioEngine& audio);

    // Buttons: id 1 Game Mode, 2 Create New World, 3 Cancel.
    [[nodiscard]] std::vector<MenuButton> layoutButtons(int windowWidth, int windowHeight) const;
    // Field boxes: 0 name, 1 seed.
    [[nodiscard]] static MenuButton fieldBox(int index, int windowWidth, int windowHeight) noexcept;

private:
    TextField m_name{TextField::kDefaultMaxLength, kForbiddenFileNameChars};
    TextField m_seed{TextField::kDefaultMaxLength};
    core::GameMode m_mode{core::GameMode::Survival};
    std::filesystem::path m_savesRoot{"saves"};
};

} // namespace betacraft::ui
