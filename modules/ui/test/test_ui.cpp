#include "audio/AudioEngine.hpp"
#include "physics/Player.hpp"
#include "render/BitmapFont.hpp"
#include "storage/WorldStorage.hpp"
#include "test_support/GLTestContext.hpp"
#include "ui/CraftingSystem.hpp"
#include "ui/CreateWorldScreen.hpp"
#include "ui/DeathScreen.hpp"
#include "ui/HudRenderer.hpp"
#include "ui/ScreenState.hpp"
#include "ui/TextField.hpp"
#include "ui/TitleScreen.hpp"
#include "ui/WorldSelectScreen.hpp"
#include "voxel/Block.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <filesystem>
#include <fstream>
#include <limits>

using namespace betacraft;
using namespace betacraft::ui;
using namespace betacraft::render;
using namespace testing;

TEST(BitmapFontTest, MeasurementAndGlyphVertices) {
    const float wEmpty = BitmapFont::measureTextWidth("", 1.0f);
    EXPECT_FLOAT_EQ(wEmpty, 0.0f);

    const float wText = BitmapFont::measureTextWidth("Minecraft", 2.0f);
    EXPECT_GT(wText, 0.0f);

    std::vector<UiQuadVertex> vertsNoShadow;
    BitmapFont::appendText(vertsNoShadow, "TEST", 10.0f, 20.0f, 1.0f, glm::vec4(1.0f), false);
    EXPECT_FALSE(vertsNoShadow.empty());

    std::vector<UiQuadVertex> vertsWithShadow;
    BitmapFont::appendText(vertsWithShadow, "TEST", 10.0f, 20.0f, 1.0f, glm::vec4(1.0f), true);
    // With dropshadow enabled, every glyph pixel is duplicated with shadow quad
    EXPECT_EQ(vertsWithShadow.size(), 2 * vertsNoShadow.size());
}

TEST(CraftingSystemTest, RecipesAndPaletteCompleteness) {
    voxel::BlockRegistry::initialize();

    const auto recipes = CraftingSystem::getRecipes();
    EXPECT_GE(recipes.size(), 14U);

    const auto palette = CraftingSystem::getCreativePalette();
    EXPECT_EQ(palette.size(), 36U);
}

TEST(CraftingSystemTest, CraftingExecution) {
    physics::Player player;

    // Clear player inventory to isolate test
    for (size_t i = 0; i < physics::Player::TOTAL_INVENTORY_SIZE; ++i) {
        player.setInventorySlot(i, {});
    }

    const auto recipes = CraftingSystem::getRecipes();
    const auto& planksRecipe = recipes[0]; // 1 Oak Log -> 4 Planks

    EXPECT_FALSE(CraftingSystem::canCraft(planksRecipe, player));
    EXPECT_FALSE(CraftingSystem::craft(planksRecipe, player));

    player.addItem(voxel::BlockId::OakLog, 2);
    EXPECT_TRUE(CraftingSystem::canCraft(planksRecipe, player));
    EXPECT_TRUE(CraftingSystem::craft(planksRecipe, player));

    EXPECT_EQ(player.countItem(voxel::BlockId::OakLog), 1);
    EXPECT_EQ(player.countItem(voxel::BlockId::OakPlanks), 4);
}

TEST(HudRendererTest, InventoryClickPaletteItemSelection) {
    voxel::BlockRegistry::initialize();
    physics::Player player;
    HudRenderer hud;

    const int32_t screenW = 1280;
    const int32_t screenH = 720;
    constexpr float panelW = 720.0f;
    constexpr float panelH = 460.0f;
    const float px = (static_cast<float>(screenW) - panelW) * 0.5f;
    const float py = (static_cast<float>(screenH) - panelH) * 0.5f - 15.0f;
    const float palStartX = px + 340.0f;
    const float palStartY = py + 36.0f;

    // Click palette slot 0 (Stone)
    const int32_t clickX = static_cast<int32_t>(palStartX + 10.0f);
    const int32_t clickY = static_cast<int32_t>(palStartY + 10.0f);

    player.setSelectedSlot(0);
    EXPECT_TRUE(hud.handleInventoryClick(clickX, clickY, screenW, screenH, player));
    EXPECT_EQ(player.getHeldItem().id, voxel::BlockId::Stone);
    EXPECT_EQ(player.getHeldItem().count, 64);
}

// The provisional "Game Mode" button (id 4) only exists for a new world, under
// Singleplayer, and alternates Survival / Creative without leaving the title screen.
TEST(TitleScreenTest, GameModeButtonOnlyForNewWorlds) {
    audio::AudioEngine audio;
    TitleScreen title;
    title.setSelectedMode(core::GameMode::Survival);
    // 800x600: buttons are 280 wide from x = 260, rows every 50 px from y = 280.
    ScreenState state = ScreenState::TitleScreen;
    bool quit = false;
    EXPECT_TRUE(title.handleMouseClick(400.0f, 345.0f, 800, 600, state, quit, audio));
    EXPECT_EQ(state, ScreenState::OptionsMenu) << "an existing world has no mode button";
    EXPECT_EQ(title.selectedMode(), core::GameMode::Survival);

    title.setNewWorld(true);
    state = ScreenState::TitleScreen;
    EXPECT_TRUE(title.handleMouseClick(400.0f, 345.0f, 800, 600, state, quit, audio));
    EXPECT_EQ(state, ScreenState::TitleScreen);
    EXPECT_EQ(title.selectedMode(), core::GameMode::Creative);
    EXPECT_TRUE(title.handleMouseClick(400.0f, 345.0f, 800, 600, state, quit, audio));
    EXPECT_EQ(title.selectedMode(), core::GameMode::Survival);
    // Options moved down one row; Singleplayer stayed.
    EXPECT_TRUE(title.handleMouseClick(400.0f, 395.0f, 800, 600, state, quit, audio));
    EXPECT_EQ(state, ScreenState::OptionsMenu);
    state = ScreenState::TitleScreen;
    EXPECT_TRUE(title.handleMouseClick(400.0f, 295.0f, 800, 600, state, quit, audio));
    EXPECT_EQ(state, ScreenState::InGame);
    EXPECT_FALSE(quit);
}

// DeathScreen comes after the five original states, and its two buttons.
TEST(DeathScreenTest, ButtonsAndEnumPlacement) {
    EXPECT_EQ(static_cast<int>(ScreenState::KeybindingMenu), 4);
    EXPECT_EQ(static_cast<int>(ScreenState::DeathScreen), 5);
    EXPECT_EQ(static_cast<int>(ScreenState::Sleeping), 6) << "appended after DeathScreen";

    audio::AudioEngine audio;
    const DeathScreen screen;
    std::vector<UiQuadVertex> verts;
    screen.render(800, 600, verts, 0.0f, 0.0f);
    EXPECT_FALSE(verts.empty());
    // 800x600: Respawn at y = 300, Title menu at y = 350.
    EXPECT_EQ(screen.handleMouseClick(400.0f, 315.0f, 800, 600, audio), DeathChoice::Respawn);
    EXPECT_EQ(screen.handleMouseClick(400.0f, 365.0f, 800, 600, audio), DeathChoice::TitleMenu);
    EXPECT_EQ(screen.handleMouseClick(10.0f, 10.0f, 800, 600, audio), DeathChoice::None);
}

// The F3 overlay lists the mobs against their caps and the effective light at the feet.
TEST(HudMobDebugLine, ShowsCountsCapsAndFeetLight) {
    const HudMobStats mobs{.passive = 3, .hostile = 7, .passiveCap = 4, .hostileCap = 22};
    EXPECT_EQ(mobDebugLine(mobs, 11), "Mobs: 3/4 passive  7/22 hostile  Feet light: 11");
    EXPECT_EQ(mobDebugLine({}, 0), "Mobs: 0/0 passive  0/0 hostile  Feet light: 0");
    const HudDebugContext ctx{};
    EXPECT_EQ(ctx.mobs.hostileCap, 0);
}

// F3 counts the chunk bundles written, and those with no player edit around them
// (bug 8 regression detector, must stay at 0).
TEST(HudStorageDebugLine, ShowsBundlesAndUnsolicitedWrites) {
    EXPECT_EQ(storageDebugLine({.bundlesWritten = 12, .unsolicitedWrites = 0}),
              "Storage: 12 bundles written  0 without player edit");
    EXPECT_EQ(HudDebugContext{}.storage.unsolicitedWrites, 0U);
}

// F3 shows the chunks at each streaming stage, the per-frame integration time (last
// and slowest recently) and the open region files.
TEST(HudStorageDebugLine, StreamingLineShowsStagesIntegrationAndRegions) {
    HudStorageStats stats{};
    stats.generating = 14;
    stats.integrating = 3;
    stats.writing = 2;
    stats.lastIntegrationMs = 1.234f;
    stats.maxIntegrationMs = 6.5f;
    stats.openRegions = 4;
    stats.openRegionBytes = 300 * 1024 + 1;
    stats.loading = 7;
    EXPECT_EQ(streamingDebugLine(stats),
              "Chunks: 7 read  14 gen  3 integrate  2 write  Integration: 1.23/6.50 ms  Regions: 4 (301 KiB)");
    EXPECT_EQ(streamingDebugLine({}),
              "Chunks: 0 read  0 gen  0 integrate  0 write  Integration: 0.00/0.00 ms  Regions: 0 (0 KiB)");
    EXPECT_LE(stats.maxIntegrationMs, kIntegrationHitchMs);
}

// The screens come after DeathScreen and Sleeping, in the planned order.
TEST(ScreenStateTest, WorldScreensAppendedAtTheEnd) {
    EXPECT_EQ(static_cast<int>(ScreenState::Sleeping), 6);
    EXPECT_EQ(static_cast<int>(ScreenState::WorldSelect), 7);
    EXPECT_EQ(static_cast<int>(ScreenState::CreateWorld), 8);
    EXPECT_EQ(static_cast<int>(ScreenState::Loading), 9);
}

// With no world loaded, "Singleplayer" opens the world list, and the mode button
// is gone from the title (it lives on the Create World screen).
TEST(TitleScreenTest, SingleplayerOpensWorldSelectWithoutLoadedWorld) {
    audio::AudioEngine audio;
    TitleScreen title;
    title.setWorldLoaded(false);
    title.setNewWorld(true);
    ScreenState state = ScreenState::TitleScreen;
    bool quit = false;
    // 800x600: Singleplayer at y = 280, Options right under it (no mode button).
    EXPECT_TRUE(title.handleMouseClick(400.0f, 295.0f, 800, 600, state, quit, audio));
    EXPECT_EQ(state, ScreenState::WorldSelect);
    state = ScreenState::TitleScreen;
    EXPECT_TRUE(title.handleMouseClick(400.0f, 345.0f, 800, 600, state, quit, audio));
    EXPECT_EQ(state, ScreenState::OptionsMenu);
    EXPECT_FALSE(quit);
}

// Printable ASCII only (UTF-8 sequences dropped whole), minus the field's forbidden
// characters, at most 32 characters; Backspace removes the last one.
TEST(TextFieldTest, TextFieldFiltersForbiddenChars) {
    TextField name(TextField::kDefaultMaxLength, kForbiddenFileNameChars);
    EXPECT_EQ(name.insertText("My/World:*?\\"), 7U);
    EXPECT_EQ(name.text(), "MyWorld");
    EXPECT_EQ(name.insertText("\t\n\r\x7F<>|\"`"), 0U);
    EXPECT_EQ(name.text(), "MyWorld");
    EXPECT_EQ(name.insertText(" \xC3\xA9t\xE2\x82\xAC!"), 3U) << "é and € dropped, ' ', 't' and '!' kept";
    EXPECT_EQ(name.text(), "MyWorld t!");
    EXPECT_TRUE(name.backspace());
    EXPECT_EQ(name.text(), "MyWorld t");

    TextField seed;
    EXPECT_EQ(seed.insertText("a:b/c"), 5U) << "a seed field forbids nothing printable";
    seed.setText(std::string(40, 'x'));
    EXPECT_EQ(seed.text().size(), 32U) << "32 characters at most";
    EXPECT_EQ(seed.insertText("y"), 0U);
    seed.clear();
    EXPECT_FALSE(seed.backspace());
    EXPECT_FALSE(TextField::isAllowedChar('\0', {}));
    EXPECT_TRUE(TextField::isAllowedChar('~', {}));
}

// Beta GuiCreateWorld seeds. Reference values from Java ("x".hashCode(), Long.parseLong).
TEST(CreateWorldTest, SeedStringHashMatchesBeta) {
    EXPECT_EQ(javaStringHashCode(""), 0);
    EXPECT_EQ(javaStringHashCode("a"), 97);
    EXPECT_EQ(javaStringHashCode("test"), 3556498);
    EXPECT_EQ(javaStringHashCode("Hello"), 69609650);
    EXPECT_EQ(javaStringHashCode("hello world"), 1794106052);
    EXPECT_EQ(javaStringHashCode("Aa"), javaStringHashCode("BB"));
    EXPECT_EQ(javaStringHashCode("polygenelubricants"), std::numeric_limits<int32_t>::min());

    constexpr uint64_t kRandom = 0xDEADBEEFULL;
    EXPECT_EQ(seedFromText("", kRandom), kRandom) << "empty: random";
    EXPECT_EQ(seedFromText("0", kRandom), kRandom) << "Beta keeps the random seed for 0";
    EXPECT_EQ(seedFromText("404", kRandom), 404U) << "a number is taken as is";
    EXPECT_EQ(seedFromText("-1", kRandom), std::numeric_limits<uint64_t>::max());
    EXPECT_EQ(seedFromText("9223372036854775807", kRandom), 9223372036854775807ULL);
    EXPECT_EQ(seedFromText("-9223372036854775808", kRandom), 0x8000000000000000ULL);
    EXPECT_EQ(seedFromText("test", kRandom), 3556498U);
    // Not a Java long: hashed, and a negative hash is sign-extended like Java's int -> long.
    EXPECT_EQ(seedFromText("9223372036854775808", kRandom),
              static_cast<uint64_t>(static_cast<int64_t>(javaStringHashCode("9223372036854775808"))));
    EXPECT_EQ(seedFromText("polygenelubricants", kRandom), 0xFFFFFFFF80000000ULL);
    EXPECT_EQ(seedFromText("+5", kRandom), static_cast<uint64_t>(javaStringHashCode("+5"))) << "Java 6: no '+'";
    EXPECT_EQ(seedFromText(" 12", kRandom), static_cast<uint64_t>(javaStringHashCode(" 12")));
}

// Folder names are sanitised (Beta's forbidden characters, "World" when empty, no
// hidden or device names) and made unique by appending '-' like Beta.
TEST(CreateWorldTest, UniqueFolderName) {
    EXPECT_EQ(sanitizeFolderName("  My World  "), "My World");
    EXPECT_EQ(sanitizeFolderName("a/b\\c:d*e?f\"g<h>i|j`k"), "a_b_c_d_e_f_g_h_i_j_k");
    EXPECT_EQ(sanitizeFolderName("   "), "World");
    EXPECT_EQ(sanitizeFolderName(""), "World");
    EXPECT_EQ(sanitizeFolderName(".."), "_.");
    EXPECT_EQ(sanitizeFolderName(".hidden"), "_hidden");
    EXPECT_EQ(sanitizeFolderName("con"), "con_");
    EXPECT_EQ(sanitizeFolderName("NUL.txt"), "NUL.txt_");
    EXPECT_EQ(sanitizeFolderName("Console"), "Console");

    test::TempDir root;
    EXPECT_EQ(uniqueFolderName(root.path(), "New World"), "New World");
    std::filesystem::create_directories(root.path() / "New World");
    EXPECT_EQ(uniqueFolderName(root.path(), "New World"), "New World-");
    std::filesystem::create_directories(root.path() / "New World-");
    EXPECT_EQ(uniqueFolderName(root.path(), "New World"), "New World--");
    std::ofstream(root.path() / "World") << "a file takes the name too";
    EXPECT_EQ(uniqueFolderName(root.path(), ""), "World-");
}

// The Create World form gives the trimmed name, the seed and the mode toggled by its
// button; Enter creates, Escape cancels.
TEST(CreateWorldTest, ScreenProducesNewWorldParams) {
    audio::AudioEngine audio;
    CreateWorldScreen screen;
    EXPECT_EQ(screen.nameField().text(), "New World");
    EXPECT_TRUE(screen.nameField().isFocused());
    for (int i = 0; i < 9; ++i) (void)screen.handleKeyDown(SDLK_BACKSPACE, audio);
    screen.handleTextInput("  Alpha: Base ");
    EXPECT_EQ(screen.nameField().text(), "  Alpha Base ") << "':' refused in a name";
    (void)screen.handleKeyDown(SDLK_TAB, audio);
    EXPECT_TRUE(screen.seedField().isFocused());
    screen.handleTextInput("test");

    // 800x600: the mode button at y = 372, 400 wide from x = 200.
    EXPECT_EQ(screen.handleMouseClick(400.0f, 380.0f, 800, 600, audio), CreateWorldChoice::None);
    EXPECT_EQ(screen.selectedMode(), core::GameMode::Creative);
    const core::NewWorldParams params = screen.params(1234U);
    EXPECT_EQ(params.name, "Alpha Base");
    EXPECT_EQ(params.seed, 3556498U);
    EXPECT_EQ(params.mode, core::GameMode::Creative);

    test::TempDir root;
    EXPECT_EQ(screen.folder(root.path()), root.path() / "Alpha Base");
    EXPECT_EQ(screen.handleKeyDown(SDLK_RETURN, audio), CreateWorldChoice::Create);
    EXPECT_EQ(screen.handleKeyDown(SDLK_ESCAPE, audio), CreateWorldChoice::Cancel);
    // The buttons row at y = 530: Create on the left, Cancel on the right.
    EXPECT_EQ(screen.handleMouseClick(250.0f, 545.0f, 800, 600, audio), CreateWorldChoice::Create);
    EXPECT_EQ(screen.handleMouseClick(550.0f, 545.0f, 800, 600, audio), CreateWorldChoice::Cancel);
    screen.reset();
    EXPECT_EQ(screen.selectedMode(), core::GameMode::Survival);
    EXPECT_TRUE(screen.seedField().text().empty());
}

// ListWorlds reads every world's level.dat (name, seed, mode), flags pre-region saves
// still to migrate, skips what is not a world and lists the most recent first.
TEST(WorldSelectTest, ListWorldsReadsSummariesAndMigrationFlag) {
    test::TempDir root;
    const auto writeLevel = [](const std::filesystem::path& dir, const storage::WorldLevelData& data) {
        std::filesystem::create_directories(dir);
        std::ofstream(dir / "level.dat", std::ios::binary) << storage::encodeLevelData(data);
    };
    storage::WorldLevelData legacy{};
    legacy.seed = 42;
    legacy.player.emplace();
    legacy.player->mode = core::GameMode::Creative;
    writeLevel(root.path() / "old", legacy);
    std::filesystem::create_directories(root.path() / "old" / "chunks");

    storage::WorldLevelData migrated{};
    migrated.seed = 7;
    migrated.name = "My Base: v2";
    migrated.storageFormat = storage::kRegionStorageFormat;
    migrated.player.emplace();
    writeLevel(root.path() / "new", migrated);
    std::filesystem::create_directories(root.path() / "new" / "region");

    std::filesystem::create_directories(root.path() / "empty");      // Not a world
    std::filesystem::create_directories(root.path() / ".hidden" / "region");
    std::ofstream(root.path() / "stray.txt") << "not a directory";
    std::filesystem::create_directories(root.path() / "toonew");
    std::ofstream(root.path() / "toonew" / "level.dat") << "BETACRAFT_LEVEL_V9\nseed 1\n";

    const auto now = std::filesystem::file_time_type::clock::now();
    std::filesystem::last_write_time(root.path() / "old" / "level.dat", now - std::chrono::hours(48));
    std::filesystem::last_write_time(root.path() / "new" / "level.dat", now);
    std::filesystem::last_write_time(root.path() / "toonew" / "level.dat", now - std::chrono::hours(1));

    const std::vector<WorldSummary> worlds = listWorlds(root.path());
    ASSERT_EQ(worlds.size(), 3U);
    EXPECT_EQ(worlds[0].path, root.path() / "new");
    EXPECT_EQ(worlds[0].name, "My Base: v2");
    EXPECT_EQ(worlds[0].seed, 7U);
    EXPECT_EQ(worlds[0].mode, core::GameMode::Survival);
    EXPECT_FALSE(worlds[0].needsMigration);
    EXPECT_EQ(worlds[0].status, storage::LevelLoadStatus::Ok);

    EXPECT_EQ(worlds[1].path, root.path() / "toonew");
    EXPECT_EQ(worlds[1].status, storage::LevelLoadStatus::TooNew);
    EXPECT_FALSE(worlds[1].needsMigration);

    EXPECT_EQ(worlds[2].name, "old") << "no name key: the folder name";
    EXPECT_EQ(worlds[2].seed, 42U);
    EXPECT_EQ(worlds[2].mode, core::GameMode::Creative);
    EXPECT_TRUE(worlds[2].needsMigration) << "chunks/ and no storage_format 2";
    EXPECT_GT(worlds[0].lastPlayed, worlds[2].lastPlayed);

    EXPECT_TRUE(listWorlds(root.path() / "missing").empty());
    EXPECT_FALSE(std::filesystem::exists(root.path() / "missing")) << "listing creates nothing";

    WorldSelectScreen screen;
    screen.setWorlds(worlds);
    EXPECT_EQ(screen.selectedWorld(), nullptr);
    audio::AudioEngine audio;
    // 800x600: Play is disabled until a row is picked; rows start at y = 56, 44 px high.
    EXPECT_EQ(screen.handleMouseClick(300.0f, 519.0f, 800, 600, audio), WorldSelectChoice::None);
    EXPECT_EQ(screen.handleMouseClick(400.0f, 56.0f + 44.0f + 10.0f, 800, 600, audio), WorldSelectChoice::None);
    ASSERT_NE(screen.selectedWorld(), nullptr);
    EXPECT_EQ(screen.selectedWorld()->path, root.path() / "toonew");
    EXPECT_EQ(screen.handleMouseClick(300.0f, 519.0f, 800, 600, audio), WorldSelectChoice::Play);
    EXPECT_EQ(screen.handleMouseClick(400.0f, 56.0f + 44.0f + 10.0f, 800, 600, audio), WorldSelectChoice::Play)
        << "a second click on the selected world plays it";
    EXPECT_EQ(screen.handleMouseClick(500.0f, 519.0f, 800, 600, audio), WorldSelectChoice::Create);
    EXPECT_EQ(screen.handleMouseClick(400.0f, 565.0f, 800, 600, audio), WorldSelectChoice::Cancel);
    EXPECT_EQ(screen.handleKeyDown(SDLK_ESCAPE, audio), WorldSelectChoice::Cancel);
    std::vector<UiQuadVertex> verts;
    screen.render(800, 600, verts, 0.0f, 0.0f);
    EXPECT_FALSE(verts.empty());
}

// Level.dat keeps the world's display name (key "name", spaces included).
TEST(WorldSelectTest, LevelDatNameRoundTrips) {
    storage::WorldLevelData data{};
    data.name = "Two  words: ok";
    storage::WorldLevelData read{};
    ASSERT_EQ(storage::decodeLevelData(storage::encodeLevelData(data), read), storage::LevelLoadStatus::Ok);
    EXPECT_EQ(read.name, "Two  words: ok");
    EXPECT_EQ(storage::encodeLevelData(storage::WorldLevelData{}).find("name"), std::string::npos)
        << "no key for an unnamed world";
}
