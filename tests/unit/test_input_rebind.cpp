#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "ui/ScreenState.hpp"
#include "ui/InputManager.hpp"
#include "ui/OptionsMenu.hpp"
#include "audio/AudioEngine.hpp"
#include "world/World.hpp"
#include "core/Types.hpp"
#include "tests/test_helpers.hpp"

#include <filesystem>
#include <fstream>

using namespace betacraft::ui;

TEST(InputRebindTest, ScreenStateTransitions) {
    ScreenState state = ScreenState::TitleScreen;
    EXPECT_EQ(state, ScreenState::TitleScreen);

    // TitleScreen -> OptionsMenu
    state = ScreenState::OptionsMenu;
    EXPECT_EQ(state, ScreenState::OptionsMenu);

    // OptionsMenu -> KeybindingMenu
    state = ScreenState::KeybindingMenu;
    EXPECT_EQ(state, ScreenState::KeybindingMenu);

    // KeybindingMenu -> OptionsMenu
    state = ScreenState::OptionsMenu;
    EXPECT_EQ(state, ScreenState::OptionsMenu);

    // OptionsMenu -> InGame
    state = ScreenState::InGame;
    EXPECT_EQ(state, ScreenState::InGame);

    // InGame -> PauseMenu
    state = ScreenState::PauseMenu;
    EXPECT_EQ(state, ScreenState::PauseMenu);

    // PauseMenu -> TitleScreen
    state = ScreenState::TitleScreen;
    EXPECT_EQ(state, ScreenState::TitleScreen);
}

TEST(InputRebindTest, InputManagerDefaults) {
    InputManager input;

    // Check defaults (WASD, Space, Shift, Ctrl, E)
    EXPECT_EQ(input.getPrimaryBinding(GameAction::MoveForward), SDL_SCANCODE_W);
    EXPECT_EQ(input.getPrimaryBinding(GameAction::MoveBackward), SDL_SCANCODE_S);
    EXPECT_EQ(input.getPrimaryBinding(GameAction::MoveLeft), SDL_SCANCODE_A);
    EXPECT_EQ(input.getPrimaryBinding(GameAction::MoveRight), SDL_SCANCODE_D);
    EXPECT_EQ(input.getPrimaryBinding(GameAction::Jump), SDL_SCANCODE_SPACE);
    EXPECT_EQ(input.getPrimaryBinding(GameAction::Sneak), SDL_SCANCODE_LSHIFT);
    EXPECT_EQ(input.getPrimaryBinding(GameAction::Sprint), SDL_SCANCODE_LCTRL);
    EXPECT_EQ(input.getPrimaryBinding(GameAction::Inventory), SDL_SCANCODE_E);

    // Secondary bindings for arrows
    EXPECT_EQ(input.getSecondaryBinding(GameAction::MoveForward), SDL_SCANCODE_UP);
    EXPECT_EQ(input.getSecondaryBinding(GameAction::MoveBackward), SDL_SCANCODE_DOWN);
    EXPECT_EQ(input.getSecondaryBinding(GameAction::MoveLeft), SDL_SCANCODE_LEFT);
    EXPECT_EQ(input.getSecondaryBinding(GameAction::MoveRight), SDL_SCANCODE_RIGHT);
}

TEST(InputRebindTest, DynamicRebinding) {
    InputManager input;

    // Rebind Forward to Z (French AZERTY layout)
    input.setBinding(GameAction::MoveForward, SDL_SCANCODE_Z);
    EXPECT_EQ(input.getPrimaryBinding(GameAction::MoveForward), SDL_SCANCODE_Z);

    // Simulated keyboard state
    Uint8 keyState[SDL_NUM_SCANCODES] = {0};
    keyState[SDL_SCANCODE_Z] = 1;

    input.updateKeyboardState(keyState);
    EXPECT_TRUE(input.isActionActive(GameAction::MoveForward));
    EXPECT_FALSE(input.isActionActive(GameAction::MoveBackward));

    auto playerInput = input.buildPlayerInput();
    EXPECT_TRUE(playerInput.forward);
    EXPECT_FALSE(playerInput.backward);
}

TEST(InputRebindTest, DisplayNames) {
    EXPECT_EQ(InputManager::getKeyDisplayName(SDL_SCANCODE_W), "W");
    EXPECT_EQ(InputManager::getKeyDisplayName(SDL_SCANCODE_SPACE), "SPACE");
    EXPECT_EQ(InputManager::getActionDisplayName(GameAction::MoveForward), "Forward");
    EXPECT_EQ(InputManager::getActionDisplayName(GameAction::Jump), "Jump");
}

TEST(InputRebindTest, OptionsFilePersistenceAndRecovery) {
    // A unique directory: UnitTests and AllTests both run this test in parallel.
    const betacraft::test::TempDir tempDir("betacraft_options_");
    const std::filesystem::path tempPath = tempDir.path() / "test_options.txt";

    InputManager originalInput;
    originalInput.setBinding(GameAction::MoveForward, SDL_SCANCODE_Z);
    originalInput.setBinding(GameAction::MoveLeft, SDL_SCANCODE_Q);
    originalInput.setFov(85.0f);
    originalInput.setRenderDistance(12);
    originalInput.setGuiScalePreference(betacraft::render::GuiScalePreference::Scale3x);
    originalInput.setMusicVolume(0.5f);
    originalInput.setSoundVolume(0.8f);

    EXPECT_TRUE(originalInput.saveToFile(tempPath));
    EXPECT_TRUE(std::filesystem::exists(tempPath));

    InputManager loadedInput;
    EXPECT_TRUE(loadedInput.loadFromFile(tempPath));

    EXPECT_EQ(loadedInput.getPrimaryBinding(GameAction::MoveForward), SDL_SCANCODE_Z);
    EXPECT_EQ(loadedInput.getPrimaryBinding(GameAction::MoveLeft), SDL_SCANCODE_Q);
    EXPECT_FLOAT_EQ(loadedInput.getFov(), 85.0f);
    EXPECT_EQ(loadedInput.getRenderDistance(), 12);
    EXPECT_EQ(loadedInput.getGuiScalePreference(), betacraft::render::GuiScalePreference::Scale3x);
    EXPECT_FLOAT_EQ(loadedInput.getMusicVolume(), 0.5f);
    EXPECT_FLOAT_EQ(loadedInput.getSoundVolume(), 0.8f);

    // Missing file fallback
    InputManager missingInput;
    EXPECT_FALSE(missingInput.loadFromFile(tempDir.path() / "non_existent_file_options.txt"));
    // Still has default forward W
    EXPECT_EQ(missingInput.getPrimaryBinding(GameAction::MoveForward), SDL_SCANCODE_W);

    // Corrupted file tolerance
    {
        std::ofstream out(tempPath);
        out << "corrupted string without colon\n";
        out << ":::invalid:::\n";
        out << "fov:95.0\n";
    }

    InputManager corruptedInput;
    EXPECT_TRUE(corruptedInput.loadFromFile(tempPath));
    EXPECT_FLOAT_EQ(corruptedInput.getFov(), 95.0f);
    EXPECT_EQ(corruptedInput.getPrimaryBinding(GameAction::MoveForward), SDL_SCANCODE_W);

    std::filesystem::remove(tempPath);
}

TEST(InputRebindTest, OptionsMenuRenderDistanceCyclingAndImmediatePersistence) {
    const betacraft::test::TempDir tempDir("betacraft_options_menu_");
    OptionsMenu menu;
    menu.setOptionsPath(tempDir.path() / "options.txt");
    InputManager input;
    betacraft::audio::AudioEngine audio;

    ScreenState curState = ScreenState::OptionsMenu;
    ScreenState prevState = ScreenState::InGame;

    EXPECT_EQ(input.getRenderDistance(), 8);

    const int w = 800;
    const int h = 600;
    const float cx = (800.0f - 320.0f) * 0.5f; // 240.0f
    const float startY = 600.0f * 0.30f;       // 180.0f
    // Button 2 (Render Distance) is at: x = cx, y = startY + 45.0f (225.0f), w = 320.0f, h = 36.0f
    const float clickX = cx + 50.0f;
    const float clickY = startY + 45.0f + 10.0f;

    // Click 1: 8 -> 12
    EXPECT_TRUE(menu.handleMouseClick(clickX, clickY, w, h, curState, prevState, input, audio));
    EXPECT_EQ(input.getRenderDistance(), 12);

    // Click 2: 12 -> 16
    EXPECT_TRUE(menu.handleMouseClick(clickX, clickY, w, h, curState, prevState, input, audio));
    EXPECT_EQ(input.getRenderDistance(), 16);

    // Click 3: 16 -> 4
    EXPECT_TRUE(menu.handleMouseClick(clickX, clickY, w, h, curState, prevState, input, audio));
    EXPECT_EQ(input.getRenderDistance(), 4);

    // Click 4: 4 -> 8
    EXPECT_TRUE(menu.handleMouseClick(clickX, clickY, w, h, curState, prevState, input, audio));
    EXPECT_EQ(input.getRenderDistance(), 8);

    // Verify it automatically persisted to the menu's options file
    InputManager verifyInput;
    EXPECT_TRUE(verifyInput.loadFromFile(menu.optionsPath()));
    EXPECT_EQ(verifyInput.getRenderDistance(), 8);
}

TEST(InputRebindTest, WorldDynamicRenderDistanceLoadingAndUnloading) {
    const betacraft::test::TempDir tempDir("betacraft_test_rd_world_");
    const std::filesystem::path& tempWorldDir = tempDir.path();

    betacraft::world::World world(42ULL, tempWorldDir);
    const glm::vec3 playerPos(0.0f, 64.0f, 0.0f);

    // Initial load with radius 4 chunks
    world.updateChunksAround(playerPos, 4, 100);
    const size_t chunksAtRadius4 = world.getChunks().size();
    EXPECT_GT(chunksAtRadius4, 0u);

    // Expand render distance immediately to 8 chunks
    world.updateChunksAround(playerPos, 8, 100);
    const size_t chunksAtRadius8 = world.getChunks().size();
    EXPECT_GT(chunksAtRadius8, chunksAtRadius4);

    // Immediately shrink render distance back to 4 chunks
    world.updateChunksAround(playerPos, 4, 100);
    const size_t chunksAfterShrink = world.getChunks().size();
    EXPECT_LE(chunksAfterShrink, chunksAtRadius8);

    // All chunks beyond radius + 3 (4 + 3 = 7 chunks) MUST be unloaded immediately
    const int32_t maxDistSq = (4 + 3) * (4 + 3);
    for (const auto& [coord, chunk] : world.getChunks()) {
        const int32_t distSq = coord.x * coord.x + coord.z * coord.z;
        EXPECT_LE(distSq, maxDistSq);
    }
}


// S3.8: mouse sensitivity, persisted under mouse_sensitivity and set in 10 % steps.
TEST(InputRebindTest, SensitivityPersists) {
    // A unique directory: UnitTests and AllTests both run this test in parallel.
    const betacraft::test::TempDir tempDir("betacraft_sensitivity_");
    const std::filesystem::path tempPath = tempDir.path() / "options.txt";
    InputManager input;
    EXPECT_FLOAT_EQ(input.getMouseSensitivity(), 0.5f);
    input.setMouseSensitivity(0.2f);
    ASSERT_TRUE(input.saveToFile(tempPath));

    InputManager loaded;
    ASSERT_TRUE(loaded.loadFromFile(tempPath));
    EXPECT_FLOAT_EQ(loaded.getMouseSensitivity(), 0.2f);

    // Out-of-range values are clamped; a missing key keeps the default.
    {
        std::ofstream out(tempPath);
        out << "mouse_sensitivity:7\n";
    }
    InputManager clamped;
    ASSERT_TRUE(clamped.loadFromFile(tempPath));
    EXPECT_FLOAT_EQ(clamped.getMouseSensitivity(), 1.0f);
    {
        std::ofstream out(tempPath);
        out << "fov:80\n";
    }
    InputManager missing;
    ASSERT_TRUE(missing.loadFromFile(tempPath));
    EXPECT_FLOAT_EQ(missing.getMouseSensitivity(), 0.5f);
    std::filesystem::remove(tempPath);

    // The options row next to GUI Scale steps by 10 % and wraps after 100 %.
    OptionsMenu menu;
    menu.setOptionsPath(tempPath);
    betacraft::audio::AudioEngine audio;
    ScreenState cur = ScreenState::OptionsMenu;
    const ScreenState prev = ScreenState::InGame;
    // Right half of the GUI Scale row: x in [405, 560], y in [270, 306] at 800x600.
    const float x = 480.0f;
    const float y = 288.0f;
    input.setMouseSensitivity(0.2f);
    EXPECT_TRUE(menu.handleMouseClick(x, y, 800, 600, cur, prev, input, audio));
    EXPECT_NEAR(input.getMouseSensitivity(), 0.3f, 1e-6f);
    for (int i = 0; i < 7; ++i) (void)menu.handleMouseClick(x, y, 800, 600, cur, prev, input, audio);
    EXPECT_NEAR(input.getMouseSensitivity(), 1.0f, 1e-6f);
    (void)menu.handleMouseClick(x, y, 800, 600, cur, prev, input, audio);
    EXPECT_NEAR(input.getMouseSensitivity(), 0.0f, 1e-6f);
    // Each click saved the menu's options file at once.
    InputManager saved;
    ASSERT_TRUE(saved.loadFromFile(tempPath));
    EXPECT_NEAR(saved.getMouseSensitivity(), 0.0f, 1e-6f);
}

// Beta EntityRenderer: f = 0.6s + 0.2, degrees per pixel = f^3 * 8 * 0.15.
TEST(InputRebindTest, BetaSensitivityCurve) {
    EXPECT_NEAR(InputManager::degreesPerPixel(0.5f), 0.15f, 1e-6f);
    EXPECT_NEAR(InputManager::degreesPerPixel(0.0f), 0.0096f, 1e-6f);
    EXPECT_NEAR(InputManager::degreesPerPixel(1.0f), 0.6144f, 1e-5f);
    EXPECT_LT(InputManager::degreesPerPixel(0.2f), InputManager::degreesPerPixel(0.5f));
    InputManager input;
    EXPECT_NEAR(input.getDegreesPerPixel(), 0.15f, 1e-6f);
}
