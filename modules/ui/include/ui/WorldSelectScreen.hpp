#pragma once

#include "audio/AudioEngine.hpp"
#include "core/Config.hpp"
#include "render/BitmapFont.hpp"
#include "storage/WorldStorage.hpp"
#include "ui/TitleScreen.hpp"

#include <SDL2/SDL.h>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace betacraft::ui {

// One line of the World Select list (Beta SaveFormatComparator).
struct WorldSummary {
    std::string name;           // level.dat "name", or the folder name
    std::filesystem::path path; // The save directory
    uint64_t seed{0};
    core::GameMode mode{core::GameMode::Survival};
    int64_t lastPlayed{0}; // Unix seconds: when level.dat (or the folder) was last written
    // "à migrer": a pre-region chunks/ directory and storage_format below 2; opening the
    // world converts it.
    bool needsMigration{false};
    // Missing for a world never entered; TooNew / Corrupt open read-only (conventions §1.2).
    storage::LevelLoadStatus status{storage::LevelLoadStatus::Missing};
};

// Every world under `savesRoot`: the subdirectories holding a level.dat, a region/ or a
// chunks/ directory (hidden ones skipped), most recently played first, then by name. Nothing
// is created or written; a missing root lists nothing.
[[nodiscard]] std::vector<WorldSummary> listWorlds(const std::filesystem::path& savesRoot);

// "YYYY-MM-DD HH:MM" in local time.
[[nodiscard]] std::string formatLastPlayed(int64_t unixSeconds);

enum class WorldSelectChoice : uint8_t { None, Play, Create, Cancel };

// Beta GuiSelectWorld: the world list with "Play Selected World", "Create New World" and
// "Cancel". A click selects a row, a second click on the selected row plays it.
class WorldSelectScreen {
public:
    static constexpr float kRowHeight = 44.0f;
    static constexpr float kListWidth = 440.0f;

    void refresh(const std::filesystem::path& savesRoot);
    void setWorlds(std::vector<WorldSummary> worlds);
    [[nodiscard]] const std::vector<WorldSummary>& worlds() const noexcept { return m_worlds; }
    [[nodiscard]] std::optional<size_t> selectedIndex() const noexcept { return m_selected; }
    // Null when nothing is selected.
    [[nodiscard]] const WorldSummary* selectedWorld() const noexcept;

    void render(int windowWidth, int windowHeight, std::vector<render::UiQuadVertex>& outVerts, float mouseX,
                float mouseY) const;
    WorldSelectChoice handleMouseClick(float mouseX, float mouseY, int windowWidth, int windowHeight,
                                       audio::AudioEngine& audio);
    // Escape cancels, Enter plays the selection, Up / Down move it.
    WorldSelectChoice handleKeyDown(SDL_Keycode key, audio::AudioEngine& audio);
    // Mouse wheel: positive scrolls up.
    void scroll(int wheelY, int windowHeight) noexcept;

    // Buttons: id 1 Play Selected World, 2 Create New World, 3 Cancel.
    [[nodiscard]] static std::vector<MenuButton> layoutButtons(int windowWidth, int windowHeight);
    // Rows visible in the list area for this window height.
    [[nodiscard]] static size_t visibleRows(int windowHeight) noexcept;

private:
    void clampScroll(int windowHeight) noexcept;

    std::vector<WorldSummary> m_worlds;
    std::optional<size_t> m_selected;
    size_t m_scroll{0}; // First row shown
};

// The Loading screen: a title, a status line and, when `progress` is set, Beta's
// grey and green progress bar (0..1).
void appendLoadingScreen(std::vector<render::UiQuadVertex>& outVerts, int windowWidth, int windowHeight,
                         std::string_view title, std::string_view status, std::optional<float> progress);

} // namespace betacraft::ui
