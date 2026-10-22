#include "ui/CreateWorldScreen.hpp"
#include "render/TextRenderer.hpp"

#include <array>
#include <optional>
#include <limits>

namespace betacraft::ui {

namespace {

constexpr float kFieldWidth = 400.0f;
constexpr float kFieldHeight = 28.0f;

[[nodiscard]] float columnX(int windowWidth) noexcept {
    return (static_cast<float>(windowWidth) - kFieldWidth) * 0.5f;
}

// Java 6 Long.parseLong: an optional '-', then at least one decimal digit, within int64.
[[nodiscard]] std::optional<int64_t> parseJavaLong(std::string_view text) noexcept {
    bool negative = false;
    size_t i = 0;
    if (!text.empty() && text[0] == '-') {
        negative = true;
        i = 1;
    }
    if (i >= text.size()) {
        return std::nullopt;
    }
    // Accumulated as a negative number, like Java, so that INT64_MIN fits.
    int64_t value = 0;
    const int64_t limit = negative ? std::numeric_limits<int64_t>::min() : -std::numeric_limits<int64_t>::max();
    for (; i < text.size(); ++i) {
        const char c = text[i];
        if (c < '0' || c > '9') {
            return std::nullopt;
        }
        const int digit = c - '0';
        if (value < limit / 10) return std::nullopt;
        value *= 10;
        if (value < limit + digit) return std::nullopt;
        value -= digit;
    }
    return negative ? value : -value;
}

[[nodiscard]] std::string upperAscii(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') c = static_cast<char>(c - 'a' + 'A');
    }
    return out;
}

} // namespace

int32_t javaStringHashCode(std::string_view text) noexcept {
    uint32_t hash = 0; // Unsigned: Java int arithmetic wraps
    for (const char c : text) {
        hash = hash * 31U + static_cast<uint32_t>(static_cast<unsigned char>(c));
    }
    return static_cast<int32_t>(hash);
}

uint64_t seedFromText(std::string_view text, uint64_t randomSeed) noexcept {
    if (text.empty()) {
        return randomSeed;
    }
    if (const std::optional<int64_t> number = parseJavaLong(text)) {
        return *number != 0 ? static_cast<uint64_t>(*number) : randomSeed;
    }
    return static_cast<uint64_t>(static_cast<int64_t>(javaStringHashCode(text)));
}

std::string sanitizeFolderName(std::string_view name) {
    const size_t first = name.find_first_not_of(' ');
    const size_t last = name.find_last_not_of(' ');
    std::string out = first == std::string_view::npos ? std::string{} : std::string(name.substr(first, last - first + 1));
    for (char& c : out) {
        if (!TextField::isAllowedChar(c, kForbiddenFileNameChars)) c = '_';
    }
    if (out.empty()) {
        return "World";
    }
    if (out.front() == '.') {
        out.front() = '_';
    }
    static constexpr std::array<std::string_view, 22> kDeviceNames{
        "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4", "COM5", "COM6", "COM7",
        "COM8", "COM9", "LPT1", "LPT2", "LPT3", "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
    const std::string upper = upperAscii(out.substr(0, out.find('.')));
    for (const std::string_view device : kDeviceNames) {
        if (upper == device) {
            out += '_';
            break;
        }
    }
    return out;
}

std::string uniqueFolderName(const std::filesystem::path& savesRoot, std::string_view name) {
    std::string folder = sanitizeFolderName(name);
    std::error_code ec;
    while (std::filesystem::exists(savesRoot / folder, ec)) {
        folder += '-';
    }
    return folder;
}

CreateWorldScreen::CreateWorldScreen() {
    reset();
}

void CreateWorldScreen::reset(core::GameMode mode) {
    m_name.setText("New World");
    m_seed.clear();
    m_mode = mode;
    m_name.setFocused(true);
    m_seed.setFocused(false);
}

core::NewWorldParams CreateWorldScreen::params(uint64_t randomSeed) const {
    core::NewWorldParams out;
    const size_t first = m_name.text().find_first_not_of(' ');
    const size_t last = m_name.text().find_last_not_of(' ');
    out.name = first == std::string::npos ? std::string("World") : m_name.text().substr(first, last - first + 1);
    out.seed = seedFromText(m_seed.text(), randomSeed);
    out.mode = m_mode;
    return out;
}

std::filesystem::path CreateWorldScreen::folder(const std::filesystem::path& savesRoot) const {
    return savesRoot / uniqueFolderName(savesRoot, m_name.text());
}

MenuButton CreateWorldScreen::fieldBox(int index, int windowWidth, int windowHeight) noexcept {
    const float sh = static_cast<float>(windowHeight);
    const float y = index == 0 ? sh * 0.24f : sh * 0.44f;
    return MenuButton{columnX(windowWidth), y, kFieldWidth, kFieldHeight, "", index};
}

std::vector<MenuButton> CreateWorldScreen::layoutButtons(int windowWidth, int windowHeight) const {
    const float x = columnX(windowWidth);
    const float sh = static_cast<float>(windowHeight);
    const float half = (kFieldWidth - 10.0f) * 0.5f;
    return {
        {x, sh * 0.62f, kFieldWidth, 38.0f,
         m_mode == core::GameMode::Creative ? "Game Mode: Creative" : "Game Mode: Survival", 1},
        {x, sh - 70.0f, half, 38.0f, "Create New World", 2},
        {x + kFieldWidth - half, sh - 70.0f, half, 38.0f, "Cancel", 3},
    };
}

void CreateWorldScreen::render(int windowWidth, int windowHeight, std::vector<render::UiQuadVertex>& outVerts, float mouseX,
                               float mouseY, bool caretVisible) const {
    const float sw = static_cast<float>(windowWidth);
    const float sh = static_cast<float>(windowHeight);
    appendSolidQuad(outVerts, 0.0f, 0.0f, sw, sh, glm::vec4(0.12f, 0.08f, 0.05f, 1.0f));

    const std::string title = "Create New World";
    const float titleW = render::TextRenderer::measureTextWidth(title, 2.0f);
    render::TextRenderer::appendText(outVerts, title, (sw - titleW) * 0.5f, sh * 0.1f, 2.0f, glm::vec4(1.0f), true);

    const glm::vec4 grey{0.63f, 0.63f, 0.63f, 1.0f};
    const MenuButton nameBox = fieldBox(0, windowWidth, windowHeight);
    const MenuButton seedBox = fieldBox(1, windowWidth, windowHeight);
    render::TextRenderer::appendText(outVerts, "World Name", nameBox.x, nameBox.y - 18.0f, 1.3f, grey, true);
    m_name.render(outVerts, nameBox.x, nameBox.y, nameBox.width, nameBox.height, caretVisible);
    const std::string savedIn = "Will be saved in: " + uniqueFolderName(m_savesRoot, m_name.text());
    render::TextRenderer::appendText(outVerts, savedIn, nameBox.x, nameBox.y + kFieldHeight + 6.0f, 1.1f, grey, true);

    render::TextRenderer::appendText(outVerts, "Seed for the World Generator", seedBox.x, seedBox.y - 18.0f, 1.3f,
                                     grey, true);
    m_seed.render(outVerts, seedBox.x, seedBox.y, seedBox.width, seedBox.height, caretVisible);
    render::TextRenderer::appendText(outVerts, "Leave blank for a random seed", seedBox.x,
                                     seedBox.y + kFieldHeight + 6.0f, 1.1f, grey, true);

    for (const MenuButton& button : layoutButtons(windowWidth, windowHeight)) {
        appendMenuButton(outVerts, button, button.label, isInside(button, mouseX, mouseY));
    }
}

CreateWorldChoice CreateWorldScreen::handleMouseClick(float mouseX, float mouseY, int windowWidth, int windowHeight,
                                                      audio::AudioEngine& audio) {
    for (int i = 0; i < 2; ++i) {
        if (isInside(fieldBox(i, windowWidth, windowHeight), mouseX, mouseY)) {
            m_name.setFocused(i == 0);
            m_seed.setFocused(i == 1);
            return CreateWorldChoice::None;
        }
    }
    for (const MenuButton& button : layoutButtons(windowWidth, windowHeight)) {
        if (!isInside(button, mouseX, mouseY)) continue;
        audio.playUiClick();
        if (button.id == 1) {
            m_mode = m_mode == core::GameMode::Creative ? core::GameMode::Survival : core::GameMode::Creative;
            return CreateWorldChoice::None;
        }
        return button.id == 2 ? CreateWorldChoice::Create : CreateWorldChoice::Cancel;
    }
    return CreateWorldChoice::None;
}

void CreateWorldScreen::handleTextInput(std::string_view utf8) {
    if (m_name.isFocused()) {
        (void)m_name.insertText(utf8);
    } else if (m_seed.isFocused()) {
        (void)m_seed.insertText(utf8);
    }
}

CreateWorldChoice CreateWorldScreen::handleKeyDown(SDL_Keycode key, audio::AudioEngine& audio) {
    switch (key) {
        case SDLK_BACKSPACE:
            if (m_name.isFocused()) (void)m_name.backspace();
            else if (m_seed.isFocused()) (void)m_seed.backspace();
            return CreateWorldChoice::None;
        case SDLK_TAB: {
            const bool nameNext = !m_name.isFocused();
            m_name.setFocused(nameNext);
            m_seed.setFocused(!nameNext);
            return CreateWorldChoice::None;
        }
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            audio.playUiClick();
            return CreateWorldChoice::Create;
        case SDLK_ESCAPE:
            audio.playUiClick();
            return CreateWorldChoice::Cancel;
        default:
            return CreateWorldChoice::None;
    }
}

} // namespace betacraft::ui
