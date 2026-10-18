#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <filesystem>
#include <cmath>
#include <cassert>
#include <algorithm>
#include <limits>

#include "ui/InputManager.hpp"
#include "ui/OptionsMenu.hpp"
#include "ui/ScreenState.hpp"
#include "render/TextRenderer.hpp"
#include "audio/AudioEngine.hpp"

using namespace betacraft;
using namespace betacraft::ui;
using namespace betacraft::render;
using namespace betacraft::audio;

static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define STRESS_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "\nFAIL: " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            g_testsFailed++; \
            return false; \
        } \
    } while(0)

#define RUN_TEST(fn) \
    do { \
        std::cout << "[RUNNING] " << #fn << "..." << std::flush; \
        if (fn()) { \
            std::cout << " PASS" << std::endl; \
            g_testsPassed++; \
        } else { \
            std::cout << " FAILED" << std::endl; \
        } \
    } while(0)

// Helper to create and cleanup temporary test files
struct TempTestFile {
    std::filesystem::path path;
    explicit TempTestFile(const std::string& filename)
        : path(std::filesystem::temp_directory_path() / filename) {
        if (std::filesystem::exists(path)) {
            std::filesystem::remove(path);
        }
    }
    ~TempTestFile() {
        if (std::filesystem::exists(path)) {
            std::filesystem::remove(path);
        }
    }
};

// ============================================================================
// SUITE 1: options.txt Edge Cases
// ============================================================================

bool testOptionsMissingFile() {
    InputManager input;
    const std::filesystem::path missingPath = std::filesystem::temp_directory_path() / "definitely_non_existent_options_12345.txt";
    if (std::filesystem::exists(missingPath)) {
        std::filesystem::remove(missingPath);
    }

    // loadFromFile must return false and preserve defaults
    STRESS_ASSERT(!input.loadFromFile(missingPath), "loadFromFile on missing file must return false");
    STRESS_ASSERT(input.getPrimaryBinding(GameAction::MoveForward) == SDL_SCANCODE_W, "Forward key must remain W");
    STRESS_ASSERT(input.getFov() == 70.0f, "FOV must remain default 70");
    STRESS_ASSERT(input.getRenderDistance() == 8, "Render distance must remain default 8");
    STRESS_ASSERT(input.getGuiScalePreference() == GuiScalePreference::Auto, "GUI scale must remain Auto");
    return true;
}

bool testOptionsReadOnlyPath() {
    InputManager input;
    // Attempt saving to an unwriteable system path
    const std::filesystem::path unwriteablePath = "/proc/sys/vm/invalid_options_write.txt";
    STRESS_ASSERT(!input.saveToFile(unwriteablePath), "saveToFile to read-only/unwriteable path must return false without crash");

    // Also test read-only existing file
    TempTestFile tempFile("ro_test_options.txt");
    {
        std::ofstream out(tempFile.path);
        out << "fov:80.0\n";
    }
    // Set permissions to read-only (0444)
    std::filesystem::permissions(tempFile.path, std::filesystem::perms::owner_read | std::filesystem::perms::group_read | std::filesystem::perms::others_read);
    // Saving to read-only file must return false
    STRESS_ASSERT(!input.saveToFile(tempFile.path), "saveToFile to read-only file must return false");

    // Revert permissions for cleanup
    std::filesystem::permissions(tempFile.path, std::filesystem::perms::all);
    return true;
}

bool testOptionsCorruptedLinesAndTokens() {
    TempTestFile tempFile("corrupted_options.txt");
    {
        std::ofstream out(tempFile.path);
        out << "# A comment line\n";
        out << "\n";
        out << "line_without_colon\n";
        out << ":empty_key_with_colon\n";
        out << "::double_colon_prefix\n";
        out << "fov:82.5\n";
        out << "render_distance:not_a_number\n";
        out << "gui_scale:invalid_token\n";
        out << "key_forward:not_an_int\n";
        out << "key_backward:22\n"; // SDL_SCANCODE_S is 22
        out << "fov:multiple:colons:in:line:90.0\n";
        out << "unknown_future_setting:12345\n";
        out << "   \t  \r  \n"; // Whitespace only line
        out << "music_enabled:1\n";
    }

    InputManager input;
    STRESS_ASSERT(input.loadFromFile(tempFile.path), "loadFromFile must return true on partially corrupted file");
    STRESS_ASSERT(input.getFov() == 82.5f, "FOV should be loaded from the valid line");
    STRESS_ASSERT(input.getPrimaryBinding(GameAction::MoveBackward) == static_cast<SDL_Scancode>(22), "MoveBackward should be loaded");
    STRESS_ASSERT(input.getRenderDistance() == 8, "render_distance with malformed token must retain default");
    STRESS_ASSERT(input.isMusicEnabled(), "music_enabled:1 should be parsed as true");
    return true;
}

bool testOptionsEmptyValues() {
    TempTestFile tempFile("empty_values_options.txt");
    {
        std::ofstream out(tempFile.path);
        out << "fov:\n";
        out << "render_distance:\n";
        out << "gui_scale:\n";
        out << "key_forward:\n";
        out << "music_volume:\n";
        out << "sound_volume:\n";
        out << "music_enabled:\n";
        out << "key_jump:44\n"; // SDL_SCANCODE_SPACE is 44
    }

    InputManager input;
    STRESS_ASSERT(input.loadFromFile(tempFile.path), "loadFromFile with empty values must succeed");
    // All empty values must be gracefully skipped, defaults preserved
    STRESS_ASSERT(input.getFov() == 70.0f, "FOV should remain default");
    STRESS_ASSERT(input.getRenderDistance() == 8, "render_distance should remain default");
    STRESS_ASSERT(input.getPrimaryBinding(GameAction::Jump) == static_cast<SDL_Scancode>(44), "Jump should be loaded");
    return true;
}

bool testOptionsNumericOverflow() {
    TempTestFile tempFile("overflow_options.txt");
    {
        std::ofstream out(tempFile.path);
        // Numbers that exceed 32-bit signed integer or float ranges
        out << "key_forward:9999999999999999999999999999999999999999\n";
        out << "render_distance:9999999999999999999999999999999999999999\n";
        out << "fov:1e500\n"; // Exceeds IEEE 754 float
        out << "key_jump:44\n";
    }

    InputManager input;
    // Must catch std::out_of_range without crashing
    STRESS_ASSERT(input.loadFromFile(tempFile.path), "loadFromFile must handle numeric overflow gracefully");
    STRESS_ASSERT(input.getPrimaryBinding(GameAction::Jump) == static_cast<SDL_Scancode>(44), "Subsequent valid keys must still parse");
    STRESS_ASSERT(input.getPrimaryBinding(GameAction::MoveForward) == SDL_SCANCODE_W, "Overflow key should keep default");
    return true;
}

bool testOptionsOutOfRangeSliderValues() {
    TempTestFile tempFile("out_of_range_options.txt");
    {
        std::ofstream out(tempFile.path);
        out << "fov:1000.0\n";
        out << "render_distance:99999\n";
        out << "gui_scale:99\n"; // Beyond 0..4
        out << "music_volume:50.0\n";
        out << "sound_volume:-10.0\n";
    }

    InputManager input;
    STRESS_ASSERT(input.loadFromFile(tempFile.path), "loadFromFile must succeed");
    // gui_scale is explicitly clamped to 0..4
    STRESS_ASSERT(static_cast<int>(input.getGuiScalePreference()) <= 4, "GUI scale must be clamped <= 4");

    // Test how OptionsMenu handles extreme slider values when rendered and clicked
    OptionsMenu menu;
    std::vector<UiQuadVertex> verts;
    menu.render(800, 600, verts, 0, 0, ScreenState::OptionsMenu, input);
    STRESS_ASSERT(!verts.empty(), "OptionsMenu render with extreme values must produce vertices without crashing");

    // Click FOV button: it should increment and wrap gracefully
    ScreenState curr = ScreenState::OptionsMenu;
    ScreenState prev = ScreenState::TitleScreen;
    AudioEngine audio;

    // FOV button is id 1 at cx = (800 - 320)/2 = 240, startY = 600 * 0.3 = 180
    // Click FOV button
    menu.handleMouseClick(250.0f, 190.0f, 800, 600, curr, prev, input, audio);
    // Since input.getFov() was 1000.0f, +10 is > 110.0f, so it must wrap to 60.0f!
    STRESS_ASSERT(input.getFov() == 60.0f, "Clicking FOV with out-of-range value must wrap to 60.0f");

    // Click Render Distance button: id 2 at startY + 45 = 225
    menu.handleMouseClick(250.0f, 235.0f, 800, 600, curr, prev, input, audio);
    // Since renderDistance was 99999, +4 is > 16, so it must wrap to 4!
    STRESS_ASSERT(input.getRenderDistance() == 4, "Clicking Render Distance with extreme value must wrap to 4");

    return true;
}

// ============================================================================
// SUITE 2: Key Rebinding Edge Cases
// ============================================================================

bool testKeyRebindingDuplicateKeys() {
    InputManager input;

    // Rebind multiple actions to the same key (e.g. SPACE)
    input.setBinding(GameAction::MoveForward, SDL_SCANCODE_SPACE);
    input.setBinding(GameAction::Jump, SDL_SCANCODE_SPACE);
    input.setBinding(GameAction::Sprint, SDL_SCANCODE_SPACE);

    STRESS_ASSERT(input.getPrimaryBinding(GameAction::MoveForward) == SDL_SCANCODE_SPACE, "Forward is SPACE");
    STRESS_ASSERT(input.getPrimaryBinding(GameAction::Jump) == SDL_SCANCODE_SPACE, "Jump is SPACE");
    STRESS_ASSERT(input.getPrimaryBinding(GameAction::Sprint) == SDL_SCANCODE_SPACE, "Sprint is SPACE");

    // Simulate key down event
    SDL_Event ev{};
    ev.type = SDL_KEYDOWN;
    ev.key.repeat = 0;
    ev.key.keysym.scancode = SDL_SCANCODE_SPACE;
    input.handleEvent(ev);

    STRESS_ASSERT(input.isActionActive(GameAction::MoveForward), "Forward should activate");
    STRESS_ASSERT(input.isActionActive(GameAction::Jump), "Jump should activate");
    STRESS_ASSERT(input.isActionActive(GameAction::Sprint), "Sprint should activate");

    auto pi = input.buildPlayerInput();
    STRESS_ASSERT(pi.forward && pi.jump && pi.sprint, "PlayerInput must reflect all duplicate bound actions");

    // Simulate key up event
    ev.type = SDL_KEYUP;
    input.handleEvent(ev);

    STRESS_ASSERT(!input.isActionActive(GameAction::MoveForward), "Forward should deactivate");
    STRESS_ASSERT(!input.isActionActive(GameAction::Jump), "Jump should deactivate");
    STRESS_ASSERT(!input.isActionActive(GameAction::Sprint), "Sprint should deactivate");
    return true;
}

bool testKeyRebindingCancelViaEsc() {
    OptionsMenu menu;
    InputManager input;
    AudioEngine audio;

    ScreenState curr = ScreenState::KeybindingMenu;
    ScreenState prev = ScreenState::OptionsMenu;

    // Button 100 (Forward) is at cx = (800 - 340)/2 = 230, startY = 600 * 0.22 = 132
    // Click Forward to enter listening mode
    bool clicked = menu.handleMouseClick(250.0f, 140.0f, 800, 600, curr, prev, input, audio);
    STRESS_ASSERT(clicked, "Clicking Forward button should succeed");
    STRESS_ASSERT(menu.isListeningForKey(), "OptionsMenu should now be listening for key");

    // Press ESC to cancel rebinding
    bool handled = menu.handleKeyDown(SDL_SCANCODE_ESCAPE, input, audio);
    STRESS_ASSERT(handled, "handleKeyDown with ESC should handle event");
    STRESS_ASSERT(!menu.isListeningForKey(), "Listening mode must be cancelled after ESC");
    // Forward binding must NOT be set to ESC
    STRESS_ASSERT(input.getPrimaryBinding(GameAction::MoveForward) == SDL_SCANCODE_W, "Forward key must remain original W, not ESC");
    return true;
}

bool testKeyRebindingCancelViaClickOutside() {
    OptionsMenu menu;
    InputManager input;
    AudioEngine audio;

    ScreenState curr = ScreenState::KeybindingMenu;
    ScreenState prev = ScreenState::OptionsMenu;

    // Click Jump button (id 104) at startY + 4 * 38 = 132 + 152 = 284
    menu.handleMouseClick(250.0f, 290.0f, 800, 600, curr, prev, input, audio);
    STRESS_ASSERT(menu.isListeningForKey(), "Should be listening for Jump key");

    // Click in top-left corner (outside any button)
    menu.handleMouseClick(10.0f, 10.0f, 800, 600, curr, prev, input, audio);
    STRESS_ASSERT(!menu.isListeningForKey(), "Clicking outside must cancel listening mode");
    STRESS_ASSERT(input.getPrimaryBinding(GameAction::Jump) == SDL_SCANCODE_SPACE, "Jump key must remain SPACE");
    return true;
}

bool testSpecialNonAsciiScancodes() {
    InputManager input;
    // Test function keys
    input.setBinding(GameAction::MoveForward, SDL_SCANCODE_F1);
    STRESS_ASSERT(InputManager::getKeyDisplayName(SDL_SCANCODE_F1) == "F1", "F1 display name");

    input.setBinding(GameAction::MoveBackward, SDL_SCANCODE_F12);
    STRESS_ASSERT(InputManager::getKeyDisplayName(SDL_SCANCODE_F12) == "F12", "F12 display name");

    // Keypad keys
    input.setBinding(GameAction::Jump, SDL_SCANCODE_KP_ENTER);
    STRESS_ASSERT(!InputManager::getKeyDisplayName(SDL_SCANCODE_KP_ENTER).empty(), "KP_ENTER display name not empty");

    input.setBinding(GameAction::Sneak, SDL_SCANCODE_KP_PLUS);
    STRESS_ASSERT(!InputManager::getKeyDisplayName(SDL_SCANCODE_KP_PLUS).empty(), "KP_PLUS display name not empty");

    // Modifiers
    input.setBinding(GameAction::Sprint, SDL_SCANCODE_RCTRL);
    STRESS_ASSERT(!InputManager::getKeyDisplayName(SDL_SCANCODE_RCTRL).empty(), "RCTRL display name not empty");

    // International keys
    input.setBinding(GameAction::Inventory, SDL_SCANCODE_GRAVE);
    STRESS_ASSERT(!InputManager::getKeyDisplayName(SDL_SCANCODE_GRAVE).empty(), "GRAVE display name not empty");

    // SDL_SCANCODE_UNKNOWN (0)
    STRESS_ASSERT(InputManager::getKeyDisplayName(SDL_SCANCODE_UNKNOWN) == "NONE", "UNKNOWN display name must be NONE");

    // Boundary scancode: SDL_NUM_SCANCODES - 1 (511)
    const SDL_Scancode maxScancode = static_cast<SDL_Scancode>(SDL_NUM_SCANCODES - 1);
    input.setBinding(GameAction::DropItem, maxScancode);
    STRESS_ASSERT(input.getPrimaryBinding(GameAction::DropItem) == maxScancode, "Max valid scancode 511");

    return true;
}

bool testScancodeBoundaryAndBufferSafety() {
    InputManager input;
    // Test invalid / out-of-range scancodes
    const SDL_Scancode invalidHigh = static_cast<SDL_Scancode>(1000);
    const SDL_Scancode invalidNegative = static_cast<SDL_Scancode>(-1);

    // getKeyDisplayName must not crash on out-of-range scancodes
    STRESS_ASSERT(InputManager::getKeyDisplayName(invalidHigh) == "NONE", "Out-of-range high scancode must return NONE");
    STRESS_ASSERT(InputManager::getKeyDisplayName(invalidNegative) == "NONE", "Negative scancode must return NONE");

    // Test updateKeyboardState with valid keyboard array
    Uint8 keyState[SDL_NUM_SCANCODES] = {0};
    keyState[SDL_SCANCODE_F1] = 1;

    input.setBinding(GameAction::MoveForward, SDL_SCANCODE_F1);
    input.updateKeyboardState(keyState);
    STRESS_ASSERT(input.isActionActive(GameAction::MoveForward), "F1 action active");

    return true;
}

// ============================================================================
// SUITE 3: MSDF Text Rendering & Virtual GUI Scaling
// ============================================================================

bool testMsdfEmptyAndWhitespaceStrings() {
    // Empty string
    STRESS_ASSERT(TextRenderer::measureTextWidth("", 1.0f) == 0.0f, "measureTextWidth on empty string is 0.0");
    STRESS_ASSERT(TextRenderer::measureTextWidth("", 5.0f) == 0.0f, "measureTextWidth on empty string at scale 5 is 0.0");

    std::vector<UiQuadVertex> verts;
    TextRenderer::appendText(verts, "", 10.0f, 10.0f, 1.0f, glm::vec4(1.0f), false);
    STRESS_ASSERT(verts.empty(), "appendText on empty string must emit 0 vertices");

    TextRenderer::appendText(verts, "", 10.0f, 10.0f, 1.0f, glm::vec4(1.0f), true); // drop shadow
    STRESS_ASSERT(verts.empty(), "appendText on empty string with dropShadow must emit 0 vertices");

    // Spaces only
    const std::string spaces = "    "; // 4 spaces
    const float spaceWidth = TextRenderer::measureTextWidth(spaces, 1.0f);
    STRESS_ASSERT(spaceWidth == 4.0f * 6.0f, "4 spaces width should be 24.0");

    // Spaces emit NO quads into vertex buffer
    TextRenderer::appendText(verts, spaces, 10.0f, 10.0f, 1.0f, glm::vec4(1.0f), false);
    STRESS_ASSERT(verts.empty(), "Spaces must emit 0 vertices into the vertex stream");
    return true;
}

bool testMsdfAllByteCodepointsAndUnicode() {
    // Test all 256 byte values
    for (int i = 0; i < 256; ++i) {
        const char c = static_cast<char>(static_cast<unsigned char>(i));
        const std::string s(1, c);

        const float w = TextRenderer::measureTextWidth(s, 1.0f);
        STRESS_ASSERT(!std::isnan(w) && !std::isinf(w), "Width must not be NaN/inf for any byte");
        STRESS_ASSERT(w >= 0.0f, "Width must be non-negative");

        std::vector<UiQuadVertex> verts;
        TextRenderer::appendText(verts, s, 0.0f, 0.0f, 1.0f, glm::vec4(1.0f), false);

        if (c != ' ') {
            STRESS_ASSERT(verts.size() == 6u, "Each non-space byte emits exactly 1 quad (6 vertices)");
            // Verify UV coordinates are within [0.0, 1.0]
            for (const auto& v : verts) {
                STRESS_ASSERT(v.uv.x >= 0.0f && v.uv.x <= 1.0f, "UV.x must be within [0, 1]");
                STRESS_ASSERT(v.uv.y >= 0.0f && v.uv.y <= 1.0f, "UV.y must be within [0, 1]");
                STRESS_ASSERT(!std::isnan(v.pos.x) && !std::isnan(v.pos.y), "Position must not be NaN");
            }
        } else {
            STRESS_ASSERT(verts.empty(), "Space emits 0 vertices");
        }
    }

    // Test UTF-8 multi-byte sequence like French accents and degree symbol
    const std::string utf8Test = "FOV: 70\xc2\xb0 - Éléphant & Coeur";
    const float utf8Width = TextRenderer::measureTextWidth(utf8Test, 1.0f);
    STRESS_ASSERT(utf8Width > 0.0f, "UTF-8 string measurement must succeed");

    std::vector<UiQuadVertex> utf8Verts;
    TextRenderer::appendText(utf8Verts, utf8Test, 0.0f, 0.0f, 1.0f, glm::vec4(1.0f), true);
    // With dropShadow, each non-space character emits 12 vertices (2 quads)
    size_t nonSpace = 0;
    for (char c : utf8Test) if (c != ' ') ++nonSpace;
    STRESS_ASSERT(utf8Verts.size() == nonSpace * 12u, "Drop shadow must emit 12 vertices per glyph");
    return true;
}

bool testMsdfExtremeGuiScales() {
    const std::string text = "ExtremeScaleTest";
    const float baseW = TextRenderer::measureTextWidth(text, 1.0f);
    STRESS_ASSERT(baseW > 0.0f, "Base width must be positive");

    // Scale 0.1x
    const float w01 = TextRenderer::measureTextWidth(text, 0.1f);
    STRESS_ASSERT(std::abs(w01 - baseW * 0.1f) < 1e-4f, "0.1x scale linearity");

    // Scale 10.0x
    const float w10 = TextRenderer::measureTextWidth(text, 10.0f);
    STRESS_ASSERT(std::abs(w10 - baseW * 10.0f) < 1e-4f, "10.0x scale linearity");

    // Scale 0.0x
    const float w0 = TextRenderer::measureTextWidth(text, 0.0f);
    STRESS_ASSERT(w0 == 0.0f, "0.0x scale must yield 0 width");

    // Vertices at extreme scales
    std::vector<UiQuadVertex> verts01;
    TextRenderer::appendText(verts01, text, 100.0f, 100.0f, 0.1f, glm::vec4(1.0f));
    STRESS_ASSERT(verts01.size() == text.size() * 6u, "0.1x emits correct vertex count");
    for (const auto& v : verts01) {
        STRESS_ASSERT(!std::isnan(v.pos.x) && !std::isinf(v.pos.x), "No NaN at 0.1x");
    }

    std::vector<UiQuadVertex> verts10;
    TextRenderer::appendText(verts10, text, 100.0f, 100.0f, 10.0f, glm::vec4(1.0f));
    STRESS_ASSERT(verts10.size() == text.size() * 6u, "10.0x emits correct vertex count");
    for (const auto& v : verts10) {
        STRESS_ASSERT(!std::isnan(v.pos.x) && !std::isinf(v.pos.x), "No NaN at 10.0x");
    }

    // Negative scale (-1.0f): verify no crashes or NaNs
    std::vector<UiQuadVertex> vertsNeg;
    TextRenderer::appendText(vertsNeg, text, 100.0f, 100.0f, -1.0f, glm::vec4(1.0f));
    STRESS_ASSERT(vertsNeg.size() == text.size() * 6u, "Negative scale emits quads without crash");
    for (const auto& v : vertsNeg) {
        STRESS_ASSERT(!std::isnan(v.pos.x) && !std::isnan(v.pos.y), "No NaN on negative scale");
    }
    return true;
}

bool testVirtualGuiScaleComputationBounds() {
    // Window dimensions: zero or negative
    STRESS_ASSERT(TextRenderer::calculateGuiScale(0, 0, GuiScalePreference::Auto) == 1, "0x0 clamps to scale 1");
    STRESS_ASSERT(TextRenderer::calculateGuiScale(-800, -600, GuiScalePreference::Auto) == 1, "Negative dims clamp to scale 1");

    // Tiny window
    STRESS_ASSERT(TextRenderer::calculateGuiScale(10, 10, GuiScalePreference::Auto) == 1, "10x10 clamps to scale 1");
    STRESS_ASSERT(TextRenderer::calculateGuiScale(319, 239, GuiScalePreference::Auto) == 1, "319x239 clamps to scale 1");

    // Standard steps
    STRESS_ASSERT(TextRenderer::calculateGuiScale(640, 480, GuiScalePreference::Auto) == 2, "640x480 is scale 2");
    STRESS_ASSERT(TextRenderer::calculateGuiScale(1280, 720, GuiScalePreference::Auto) == 3, "1280x720 is scale 3");
    STRESS_ASSERT(TextRenderer::calculateGuiScale(1920, 1080, GuiScalePreference::Auto) == 4, "1920x1080 is scale 4");

    // Huge resolutions: 4K, 8K, 16K (must clamp to 4)
    STRESS_ASSERT(TextRenderer::calculateGuiScale(3840, 2160, GuiScalePreference::Auto) == 4, "4K clamps to scale 4");
    STRESS_ASSERT(TextRenderer::calculateGuiScale(7680, 4320, GuiScalePreference::Auto) == 4, "8K clamps to scale 4");
    STRESS_ASSERT(TextRenderer::calculateGuiScale(15360, 8640, GuiScalePreference::Auto) == 4, "16K clamps to scale 4");

    // Extreme aspect ratios (e.g. ultra-wide or ultra-tall)
    STRESS_ASSERT(TextRenderer::calculateGuiScale(10000, 200, GuiScalePreference::Auto) == 1, "Ultra wide but short window clamps to 1");
    STRESS_ASSERT(TextRenderer::calculateGuiScale(200, 10000, GuiScalePreference::Auto) == 1, "Ultra tall but narrow window clamps to 1");

    // Invalid enum preference values
    const auto invalidPref1 = static_cast<GuiScalePreference>(250);
    const auto invalidPref2 = static_cast<GuiScalePreference>(99);
    const int scaleInvalid1 = TextRenderer::calculateGuiScale(1920, 1080, invalidPref1);
    const int scaleInvalid2 = TextRenderer::calculateGuiScale(1920, 1080, invalidPref2);
    STRESS_ASSERT(scaleInvalid1 >= 1 && scaleInvalid1 <= 4, "Invalid enum pref 1 must clamp in [1, 4]");
    STRESS_ASSERT(scaleInvalid2 >= 1 && scaleInvalid2 <= 4, "Invalid enum pref 2 must clamp in [1, 4]");
    return true;
}

bool testMsdfMedianAndFragmentEvaluation() {
    // All 6 permutations of (1.0, 2.0, 3.0) must evaluate to 2.0
    STRESS_ASSERT(TextRenderer::msdfMedian(1.0f, 2.0f, 3.0f) == 2.0f, "Permutation 1,2,3");
    STRESS_ASSERT(TextRenderer::msdfMedian(1.0f, 3.0f, 2.0f) == 2.0f, "Permutation 1,3,2");
    STRESS_ASSERT(TextRenderer::msdfMedian(2.0f, 1.0f, 3.0f) == 2.0f, "Permutation 2,1,3");
    STRESS_ASSERT(TextRenderer::msdfMedian(2.0f, 3.0f, 1.0f) == 2.0f, "Permutation 2,3,1");
    STRESS_ASSERT(TextRenderer::msdfMedian(3.0f, 1.0f, 2.0f) == 2.0f, "Permutation 3,1,2");
    STRESS_ASSERT(TextRenderer::msdfMedian(3.0f, 2.0f, 1.0f) == 2.0f, "Permutation 3,2,1");

    // Identical values
    STRESS_ASSERT(TextRenderer::msdfMedian(0.7f, 0.7f, 0.7f) == 0.7f, "All equal");
    STRESS_ASSERT(TextRenderer::msdfMedian(0.1f, 0.9f, 0.9f) == 0.9f, "Two equal high");
    STRESS_ASSERT(TextRenderer::msdfMedian(0.1f, 0.1f, 0.9f) == 0.1f, "Two equal low");

    // Extreme float values
    STRESS_ASSERT(TextRenderer::msdfMedian(-1e10f, 0.0f, 1e10f) == 0.0f, "Extreme range median");

    // evaluateMsdfFragment monotonicity and clamping in [0.0, 1.0]
    float prevAlpha = -1.0f;
    for (float d = 0.0f; d <= 1.0f; d += 0.05f) {
        const float alpha = TextRenderer::evaluateMsdfFragment(glm::vec3(d), 4.0f);
        STRESS_ASSERT(alpha >= 0.0f && alpha <= 1.0f, "Fragment opacity must be in [0.0, 1.0]");
        STRESS_ASSERT(alpha >= prevAlpha, "Fragment opacity must be monotonically non-decreasing");
        prevAlpha = alpha;
    }

    // Distance exactly 0.5 (contour)
    const float contourAlpha = TextRenderer::evaluateMsdfFragment(glm::vec3(0.5f), 4.0f);
    STRESS_ASSERT(std::abs(contourAlpha - 0.5f) < 1e-4f, "0.5 distance must give exactly 0.5 alpha");

    // Extreme pxRange (e.g. 0.001f and 1000.0f)
    const float alphaTinyRange = TextRenderer::evaluateMsdfFragment(glm::vec3(0.8f), 0.001f);
    STRESS_ASSERT(alphaTinyRange >= 0.0f && alphaTinyRange <= 1.0f, "Tiny pxRange in bounds");

    const float alphaHugeRange = TextRenderer::evaluateMsdfFragment(glm::vec3(0.8f), 1000.0f);
    STRESS_ASSERT(alphaHugeRange == 1.0f, "Huge pxRange saturates to 1.0");
    return true;
}

bool testMsdfMassiveStringStress() {
    // 10,000 character string
    std::string hugeString;
    hugeString.reserve(10000);
    for (size_t i = 0; i < 10000; ++i) {
        hugeString.push_back(static_cast<char>(33 + (i % 94)));
    }

    const float hugeWidth = TextRenderer::measureTextWidth(hugeString, 1.0f);
    STRESS_ASSERT(hugeWidth == 10000.0f * 8.0f, "10k characters width must be 80,000.0");

    std::vector<UiQuadVertex> verts;
    verts.reserve(10000 * 6);
    TextRenderer::appendText(verts, hugeString, 0.0f, 0.0f, 1.0f, glm::vec4(1.0f), false);
    STRESS_ASSERT(verts.size() == 60000u, "10k characters must emit exactly 60,000 vertices");
    return true;
}

// ============================================================================
// SUITE 4: Options Menu State Transitions & Integration
// ============================================================================

bool testOptionsMenuStateNavigation() {
    OptionsMenu menu;
    InputManager input;
    AudioEngine audio;

    ScreenState current = ScreenState::TitleScreen;
    ScreenState previous = ScreenState::TitleScreen;

    // Transition Title -> Options
    current = ScreenState::OptionsMenu;
    STRESS_ASSERT(current == ScreenState::OptionsMenu, "State is OptionsMenu");

    // Click "Controls..." button (id 5 at startY + 180 = 600*0.3 + 180 = 360)
    // cx = (800 - 320)/2 = 240
    bool clickedControls = menu.handleMouseClick(250.0f, 365.0f, 800, 600, current, previous, input, audio);
    STRESS_ASSERT(clickedControls, "Click Controls button");
    STRESS_ASSERT(current == ScreenState::KeybindingMenu, "State transitioned to KeybindingMenu");

    // In KeybindingMenu, click "Reset Keys" (id 108 at startY + 315 = 600*0.22 + 315 = 447)
    // cx = (800 - 340)/2 = 230
    input.setBinding(GameAction::MoveForward, SDL_SCANCODE_Z);
    bool clickedReset = menu.handleMouseClick(250.0f, 450.0f, 800, 600, current, previous, input, audio);
    STRESS_ASSERT(clickedReset, "Click Reset Keys");
    STRESS_ASSERT(input.getPrimaryBinding(GameAction::MoveForward) == SDL_SCANCODE_W, "Reset Keys restored W");

    // In KeybindingMenu, click "Done" (id 109 at startY + 355 = 487)
    bool clickedDone = menu.handleMouseClick(250.0f, 490.0f, 800, 600, current, previous, input, audio);
    STRESS_ASSERT(clickedDone, "Click Done in KeybindingMenu");
    STRESS_ASSERT(current == ScreenState::OptionsMenu, "State returned to OptionsMenu");

    // In OptionsMenu, click "Done" (id 6 at startY + 235 = 180 + 235 = 415)
    bool clickedOptionsDone = menu.handleMouseClick(250.0f, 420.0f, 800, 600, current, previous, input, audio);
    STRESS_ASSERT(clickedOptionsDone, "Click Done in OptionsMenu");
    STRESS_ASSERT(current == ScreenState::TitleScreen, "State returned to TitleScreen (previous)");

    return true;
}

bool testOptionsMenuSliderCycling() {
    OptionsMenu menu;
    InputManager input;
    AudioEngine audio;

    ScreenState current = ScreenState::OptionsMenu;
    ScreenState previous = ScreenState::TitleScreen;

    // FOV button (id 1 at y = 180): clicks cycle 70 -> 80 -> 90 -> 100 -> 110 -> 60
    input.setFov(70.0f);
    menu.handleMouseClick(250.0f, 190.0f, 800, 600, current, previous, input, audio);
    STRESS_ASSERT(input.getFov() == 80.0f, "FOV stepped to 80");
    menu.handleMouseClick(250.0f, 190.0f, 800, 600, current, previous, input, audio);
    STRESS_ASSERT(input.getFov() == 90.0f, "FOV stepped to 90");
    menu.handleMouseClick(250.0f, 190.0f, 800, 600, current, previous, input, audio);
    STRESS_ASSERT(input.getFov() == 100.0f, "FOV stepped to 100");
    menu.handleMouseClick(250.0f, 190.0f, 800, 600, current, previous, input, audio);
    STRESS_ASSERT(input.getFov() == 110.0f, "FOV stepped to 110");
    menu.handleMouseClick(250.0f, 190.0f, 800, 600, current, previous, input, audio);
    STRESS_ASSERT(input.getFov() == 60.0f, "FOV wrapped to 60");

    // Render Distance button (id 2 at y = 225): clicks cycle 8 -> 12 -> 16 -> 4
    input.setRenderDistance(8);
    menu.handleMouseClick(250.0f, 235.0f, 800, 600, current, previous, input, audio);
    STRESS_ASSERT(input.getRenderDistance() == 12, "RD stepped to 12");
    menu.handleMouseClick(250.0f, 235.0f, 800, 600, current, previous, input, audio);
    STRESS_ASSERT(input.getRenderDistance() == 16, "RD stepped to 16");
    menu.handleMouseClick(250.0f, 235.0f, 800, 600, current, previous, input, audio);
    STRESS_ASSERT(input.getRenderDistance() == 4, "RD wrapped to 4");

    // GUI Scale button (id 3 at y = 270): clicks cycle Auto (0) -> 1x (1) -> 2x (2) -> 3x (3) -> 4x (4) -> Auto (0)
    input.setGuiScalePreference(GuiScalePreference::Auto);
    menu.handleMouseClick(250.0f, 280.0f, 800, 600, current, previous, input, audio);
    STRESS_ASSERT(input.getGuiScalePreference() == GuiScalePreference::Scale1x, "GUI scale stepped to 1x");
    menu.handleMouseClick(250.0f, 280.0f, 800, 600, current, previous, input, audio);
    STRESS_ASSERT(input.getGuiScalePreference() == GuiScalePreference::Scale2x, "GUI scale stepped to 2x");
    menu.handleMouseClick(250.0f, 280.0f, 800, 600, current, previous, input, audio);
    STRESS_ASSERT(input.getGuiScalePreference() == GuiScalePreference::Scale3x, "GUI scale stepped to 3x");
    menu.handleMouseClick(250.0f, 280.0f, 800, 600, current, previous, input, audio);
    STRESS_ASSERT(input.getGuiScalePreference() == GuiScalePreference::Scale4x, "GUI scale stepped to 4x");
    menu.handleMouseClick(250.0f, 280.0f, 800, 600, current, previous, input, audio);
    STRESS_ASSERT(input.getGuiScalePreference() == GuiScalePreference::Auto, "GUI scale wrapped to Auto");

    // Music toggle button (id 4 at y = 315)
    input.setMusicEnabled(true);
    menu.handleMouseClick(250.0f, 325.0f, 800, 600, current, previous, input, audio);
    STRESS_ASSERT(!input.isMusicEnabled(), "Music toggled to false");
    menu.handleMouseClick(250.0f, 325.0f, 800, 600, current, previous, input, audio);
    STRESS_ASSERT(input.isMusicEnabled(), "Music toggled back to true");

    return true;
}

// ============================================================================
// MAIN RUNNER
// ============================================================================

int main() {
    std::cout << "============================================================\n";
    std::cout << " ADVERSARIAL STRESS TEST SUITE: MILESTONE 3 (SPRINT 1.3)    \n";
    std::cout << " MSDF FONT RENDERING, UI & OPTIONS PERSISTENCE              \n";
    std::cout << "============================================================\n\n";

    std::cout << "--- SUITE 1: options.txt Edge Cases ---\n";
    RUN_TEST(testOptionsMissingFile);
    RUN_TEST(testOptionsReadOnlyPath);
    RUN_TEST(testOptionsCorruptedLinesAndTokens);
    RUN_TEST(testOptionsEmptyValues);
    RUN_TEST(testOptionsNumericOverflow);
    RUN_TEST(testOptionsOutOfRangeSliderValues);

    std::cout << "\n--- SUITE 2: Key Rebinding Edge Cases ---\n";
    RUN_TEST(testKeyRebindingDuplicateKeys);
    RUN_TEST(testKeyRebindingCancelViaEsc);
    RUN_TEST(testKeyRebindingCancelViaClickOutside);
    RUN_TEST(testSpecialNonAsciiScancodes);
    RUN_TEST(testScancodeBoundaryAndBufferSafety);

    std::cout << "\n--- SUITE 3: MSDF Text Rendering & Virtual GUI Scaling ---\n";
    RUN_TEST(testMsdfEmptyAndWhitespaceStrings);
    RUN_TEST(testMsdfAllByteCodepointsAndUnicode);
    RUN_TEST(testMsdfExtremeGuiScales);
    RUN_TEST(testVirtualGuiScaleComputationBounds);
    RUN_TEST(testMsdfMedianAndFragmentEvaluation);
    RUN_TEST(testMsdfMassiveStringStress);

    std::cout << "\n--- SUITE 4: Options Menu State Transitions & Integration ---\n";
    RUN_TEST(testOptionsMenuStateNavigation);
    RUN_TEST(testOptionsMenuSliderCycling);

    std::cout << "\n============================================================\n";
    std::cout << "SUMMARY: Passed: " << g_testsPassed << ", Failed: " << g_testsFailed << "\n";
    std::cout << "============================================================\n";

    return (g_testsFailed == 0) ? 0 : 1;
}
