#include "ui/WorldSelectScreen.hpp"
#include "render/TextRenderer.hpp"
#include "ui/TextField.hpp"

#include <algorithm>
#include <chrono>
#include <ctime>
#include <fstream>
#include <sstream>

namespace betacraft::ui {

namespace {

constexpr float kListTop = 56.0f;
constexpr float kButtonsHeight = 110.0f; // Room under the list for the two button rows

[[nodiscard]] int64_t unixSeconds(std::filesystem::file_time_type time) {
    const auto sys = std::chrono::file_clock::to_sys(time);
    return std::chrono::duration_cast<std::chrono::seconds>(sys.time_since_epoch()).count();
}

[[nodiscard]] float listX(int windowWidth) noexcept {
    return (static_cast<float>(windowWidth) - WorldSelectScreen::kListWidth) * 0.5f;
}

} // namespace

std::vector<WorldSummary> listWorlds(const std::filesystem::path& savesRoot) {
    std::vector<WorldSummary> worlds;
    std::error_code ec;
    if (!std::filesystem::is_directory(savesRoot, ec)) {
        return worlds;
    }
    for (std::filesystem::directory_iterator it(savesRoot, ec), end; !ec && it != end; it.increment(ec)) {
        const std::filesystem::path dir = it->path();
        const std::string folder = dir.filename().string();
        std::error_code entryEc;
        if (folder.empty() || folder.front() == '.' || !it->is_directory(entryEc)) {
            continue;
        }
        const std::filesystem::path levelPath = dir / "level.dat";
        const bool hasLevel = std::filesystem::is_regular_file(levelPath, entryEc);
        const bool hasChunks = std::filesystem::is_directory(dir / "chunks", entryEc);
        const bool hasRegions = std::filesystem::is_directory(dir / "region", entryEc);
        if (!hasLevel && !hasChunks && !hasRegions) {
            continue;
        }

        WorldSummary summary;
        summary.path = dir;
        summary.name = folder;
        storage::WorldLevelData level{};
        if (hasLevel) {
            std::ifstream in(levelPath, std::ios::binary);
            std::ostringstream text;
            text << in.rdbuf();
            summary.status = in.is_open() ? storage::decodeLevelData(text.str(), level) : storage::LevelLoadStatus::Corrupt;
        }
        const bool readable =
            summary.status == storage::LevelLoadStatus::Ok || summary.status == storage::LevelLoadStatus::LegacyV1;
        if (readable) {
            summary.seed = level.seed;
            if (!level.name.empty()) summary.name = level.name;
            // A V1 level.dat predates game modes: those worlds load as Creative.
            summary.mode = summary.status == storage::LevelLoadStatus::LegacyV1 ? core::GameMode::Creative
                           : level.player                                     ? level.player->mode
                                                                              : core::GameMode::Survival;
        }
        summary.needsMigration = (readable || summary.status == storage::LevelLoadStatus::Missing) && hasChunks &&
                                 level.storageFormat < storage::kRegionStorageFormat;
        const auto written = std::filesystem::last_write_time(hasLevel ? levelPath : dir, entryEc);
        if (!entryEc) summary.lastPlayed = unixSeconds(written);
        worlds.push_back(std::move(summary));
    }
    std::sort(worlds.begin(), worlds.end(), [](const WorldSummary& a, const WorldSummary& b) {
        if (a.lastPlayed != b.lastPlayed) return a.lastPlayed > b.lastPlayed;
        return a.path.filename().string() < b.path.filename().string();
    });
    return worlds;
}

std::string formatLastPlayed(int64_t unixSeconds) {
    const std::time_t t = static_cast<std::time_t>(unixSeconds);
    std::tm local{};
    if (localtime_r(&t, &local) == nullptr) {
        return "?";
    }
    char buffer[32];
    const size_t n = std::strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M", &local);
    return std::string(buffer, n);
}

void WorldSelectScreen::refresh(const std::filesystem::path& savesRoot) {
    setWorlds(listWorlds(savesRoot));
}

void WorldSelectScreen::setWorlds(std::vector<WorldSummary> worlds) {
    m_worlds = std::move(worlds);
    m_selected.reset();
    m_scroll = 0;
}

const WorldSummary* WorldSelectScreen::selectedWorld() const noexcept {
    return m_selected && *m_selected < m_worlds.size() ? &m_worlds[*m_selected] : nullptr;
}

std::vector<MenuButton> WorldSelectScreen::layoutButtons(int windowWidth, int windowHeight) {
    const float x = listX(windowWidth);
    const float sh = static_cast<float>(windowHeight);
    const float half = (kListWidth - 10.0f) * 0.5f;
    return {
        {x, sh - 96.0f, half, 38.0f, "Play Selected World", 1},
        {x + kListWidth - half, sh - 96.0f, half, 38.0f, "Create New World", 2},
        {x, sh - 50.0f, kListWidth, 38.0f, "Cancel", 3},
    };
}

size_t WorldSelectScreen::visibleRows(int windowHeight) noexcept {
    const float area = static_cast<float>(windowHeight) - kButtonsHeight - kListTop;
    return area < kRowHeight ? 1U : static_cast<size_t>(area / kRowHeight);
}

void WorldSelectScreen::clampScroll(int windowHeight) noexcept {
    const size_t rows = visibleRows(windowHeight);
    const size_t maxScroll = m_worlds.size() > rows ? m_worlds.size() - rows : 0U;
    m_scroll = std::min(m_scroll, maxScroll);
}

void WorldSelectScreen::scroll(int wheelY, int windowHeight) noexcept {
    if (wheelY > 0) {
        m_scroll = m_scroll > static_cast<size_t>(wheelY) ? m_scroll - static_cast<size_t>(wheelY) : 0U;
    } else if (wheelY < 0) {
        m_scroll += static_cast<size_t>(-wheelY);
    }
    clampScroll(windowHeight);
}

void WorldSelectScreen::render(int windowWidth, int windowHeight, std::vector<render::UiQuadVertex>& outVerts, float mouseX,
                               float mouseY) const {
    const float sw = static_cast<float>(windowWidth);
    const float sh = static_cast<float>(windowHeight);
    appendSolidQuad(outVerts, 0.0f, 0.0f, sw, sh, glm::vec4(0.12f, 0.08f, 0.05f, 1.0f));
    // Beta draws the list over a darker band.
    appendSolidQuad(outVerts, 0.0f, kListTop - 6.0f, sw, sh - kButtonsHeight + 2.0f, glm::vec4(0.05f, 0.03f, 0.02f, 1.0f));

    const std::string title = "Select World";
    const float titleW = render::TextRenderer::measureTextWidth(title, 2.0f);
    render::TextRenderer::appendText(outVerts, title, (sw - titleW) * 0.5f, 20.0f, 2.0f, glm::vec4(1.0f), true);

    const float x = listX(windowWidth);
    const size_t rows = visibleRows(windowHeight);
    if (m_worlds.empty()) {
        const std::string none = "No worlds yet";
        const float w = render::TextRenderer::measureTextWidth(none, 1.4f);
        render::TextRenderer::appendText(outVerts, none, (sw - w) * 0.5f, kListTop + 20.0f, 1.4f,
                                         glm::vec4(0.6f, 0.6f, 0.6f, 1.0f), true);
    }
    for (size_t i = m_scroll; i < m_worlds.size() && i < m_scroll + rows; ++i) {
        const WorldSummary& world = m_worlds[i];
        const float y = kListTop + static_cast<float>(i - m_scroll) * kRowHeight;
        const bool selected = m_selected && *m_selected == i;
        const bool hovered = mouseX >= x && mouseX <= x + kListWidth && mouseY >= y && mouseY < y + kRowHeight;
        if (selected) {
            // Beta: a grey frame around the selected world.
            appendSolidQuad(outVerts, x - 2.0f, y - 2.0f, x + kListWidth + 2.0f, y + kRowHeight - 2.0f,
                            glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
            appendSolidQuad(outVerts, x - 1.0f, y - 1.0f, x + kListWidth + 1.0f, y + kRowHeight - 3.0f,
                            glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
        } else if (hovered) {
            appendSolidQuad(outVerts, x, y, x + kListWidth, y + kRowHeight - 4.0f, glm::vec4(1.0f, 1.0f, 1.0f, 0.06f));
        }
        render::TextRenderer::appendText(outVerts, world.name, x + 6.0f, y + 2.0f, 1.4f, glm::vec4(1.0f), true);
        const std::string detail = world.path.filename().string() + " (" + formatLastPlayed(world.lastPlayed) + ")";
        render::TextRenderer::appendText(outVerts, detail, x + 6.0f, y + 16.0f, 1.0f,
                                         glm::vec4(0.63f, 0.63f, 0.63f, 1.0f), true);
        std::string info;
        glm::vec4 infoColour{0.63f, 0.63f, 0.63f, 1.0f};
        if (world.status == storage::LevelLoadStatus::TooNew || world.status == storage::LevelLoadStatus::Corrupt) {
            info = world.status == storage::LevelLoadStatus::TooNew ? "Newer version: read only" : "Unreadable: read only";
            infoColour = glm::vec4(1.0f, 0.35f, 0.35f, 1.0f);
        } else {
            info = std::string(world.mode == core::GameMode::Creative ? "Creative Mode" : "Survival Mode");
            if (world.status != storage::LevelLoadStatus::Missing) info += ", seed " + std::to_string(world.seed);
            if (world.needsMigration) {
                info += " - Must be converted!";
                infoColour = glm::vec4(1.0f, 0.75f, 0.3f, 1.0f);
            }
        }
        render::TextRenderer::appendText(outVerts, info, x + 6.0f, y + 28.0f, 1.0f, infoColour, true);
    }
    if (m_worlds.size() > rows) {
        // Scroll position along the right edge of the list.
        const float areaH = static_cast<float>(rows) * kRowHeight;
        const float barH = std::max(12.0f, areaH * static_cast<float>(rows) / static_cast<float>(m_worlds.size()));
        const float barY = kListTop + (areaH - barH) * static_cast<float>(m_scroll) /
                                          static_cast<float>(m_worlds.size() - rows);
        appendSolidQuad(outVerts, x + kListWidth + 6.0f, barY, x + kListWidth + 10.0f, barY + barH,
                        glm::vec4(0.6f, 0.6f, 0.6f, 1.0f));
    }

    for (const MenuButton& button : layoutButtons(windowWidth, windowHeight)) {
        const bool enabled = button.id != 1 || selectedWorld() != nullptr;
        appendMenuButton(outVerts, button, button.label, isInside(button, mouseX, mouseY), enabled);
    }
}

WorldSelectChoice WorldSelectScreen::handleMouseClick(float mouseX, float mouseY, int windowWidth, int windowHeight,
                                                      audio::AudioEngine& audio) {
    for (const MenuButton& button : layoutButtons(windowWidth, windowHeight)) {
        if (!isInside(button, mouseX, mouseY)) continue;
        if (button.id == 1 && selectedWorld() == nullptr) return WorldSelectChoice::None;
        audio.playUiClick();
        switch (button.id) {
            case 1: return WorldSelectChoice::Play;
            case 2: return WorldSelectChoice::Create;
            default: return WorldSelectChoice::Cancel;
        }
    }
    const float x = listX(windowWidth);
    if (mouseX < x || mouseX > x + kListWidth || mouseY < kListTop) {
        return WorldSelectChoice::None;
    }
    const size_t row = static_cast<size_t>((mouseY - kListTop) / kRowHeight);
    const size_t index = m_scroll + row;
    if (row >= visibleRows(windowHeight) || index >= m_worlds.size()) {
        return WorldSelectChoice::None;
    }
    if (m_selected && *m_selected == index) {
        audio.playUiClick();
        return WorldSelectChoice::Play; // Beta: a double click on a world plays it
    }
    m_selected = index;
    return WorldSelectChoice::None;
}

WorldSelectChoice WorldSelectScreen::handleKeyDown(SDL_Keycode key, audio::AudioEngine& audio) {
    switch (key) {
        case SDLK_ESCAPE:
            audio.playUiClick();
            return WorldSelectChoice::Cancel;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            if (selectedWorld() == nullptr) return WorldSelectChoice::None;
            audio.playUiClick();
            return WorldSelectChoice::Play;
        case SDLK_UP:
            if (!m_worlds.empty()) m_selected = m_selected && *m_selected > 0 ? *m_selected - 1 : 0U;
            if (m_selected && *m_selected < m_scroll) m_scroll = *m_selected;
            return WorldSelectChoice::None;
        case SDLK_DOWN:
            if (!m_worlds.empty()) m_selected = m_selected ? std::min(*m_selected + 1, m_worlds.size() - 1) : 0U;
            return WorldSelectChoice::None;
        default:
            return WorldSelectChoice::None;
    }
}

void appendLoadingScreen(std::vector<render::UiQuadVertex>& outVerts, int windowWidth, int windowHeight,
                         std::string_view title, std::string_view status, std::optional<float> progress) {
    const float sw = static_cast<float>(windowWidth);
    const float sh = static_cast<float>(windowHeight);
    appendSolidQuad(outVerts, 0.0f, 0.0f, sw, sh, glm::vec4(0.12f, 0.08f, 0.05f, 1.0f));
    const float titleW = render::TextRenderer::measureTextWidth(title, 1.8f);
    render::TextRenderer::appendText(outVerts, title, (sw - titleW) * 0.5f, sh * 0.5f - 40.0f, 1.8f, glm::vec4(1.0f),
                                     true);
    const float statusW = render::TextRenderer::measureTextWidth(status, 1.4f);
    render::TextRenderer::appendText(outVerts, status, (sw - statusW) * 0.5f, sh * 0.5f - 10.0f, 1.4f,
                                     glm::vec4(1.0f), true);
    if (progress) {
        // Beta LoadingScreenRenderer: 100 x 2 px, grey with the done part green (here x2).
        const float barW = 200.0f;
        const float x = (sw - barW) * 0.5f;
        const float y = sh * 0.5f + 16.0f;
        const float done = std::clamp(*progress, 0.0f, 1.0f) * barW;
        appendSolidQuad(outVerts, x, y, x + barW, y + 4.0f, glm::vec4(0.5f, 0.5f, 0.5f, 1.0f));
        appendSolidQuad(outVerts, x, y, x + done, y + 4.0f, glm::vec4(0.5f, 1.0f, 0.5f, 1.0f));
    }
}

} // namespace betacraft::ui
