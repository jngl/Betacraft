#include "game/Application.hpp"
#include "ecs/Components.hpp"
#include "ui/CreateWorldScreen.hpp"
#include "ui/WorldSelectScreen.hpp"
#include "test_support/GLTestContext.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <memory>

using namespace betacraft;
using namespace testing;

TEST(ApplicationE2ETest, RunMultipleFramesAndCleanShutdown) {
    if (!test::GLTestContext::isAvailable()) {
        GTEST_SKIP() << "Skipping E2E Application test: OpenGL 3.3 Core display/context not available.";
    }

    test::TempDir tempDir;
    core::EngineConfig config{};
    config.windowTitle = "BetaCraft E2E Test";
    config.windowWidth = 640;
    config.windowHeight = 480;
    config.vsync = false;
    config.worldSaveDir = tempDir.path().string();
    config.worldSeed = 123456ULL;
    config.renderDistanceChunks = 2; // small render distance for fast test execution
    config.autoEnterWorld = true; // S7.9: open worldSaveDir in initialize(), as --save-dir does

    game::Application app(config);
    ASSERT_TRUE(app.initialize());

    // Run 5 full frames (events, update, world render, hud, swap)
    const int exitCode = app.run(5);
    EXPECT_EQ(exitCode, 0);

    app.shutdown();

    // Verify level.dat was created on shutdown
    const auto levelDat = tempDir.path() / "level.dat";
    EXPECT_TRUE(std::filesystem::exists(levelDat));
}

// The in-game half of the frame loop: Application::update latches real SDL input and calls
// WorldSession::tick at 20 TPS. What the tick does (drops, fluids): test_world_session_tick.cpp.
class ApplicationInGameE2ETest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!test::GLTestContext::isAvailable()) {
            GTEST_SKIP() << "OpenGL 3.3 Core display/context not available.";
        }
        core::EngineConfig config{};
        config.windowTitle = "BetaCraft E2E In-Game Test";
        config.windowWidth = 320;
        config.windowHeight = 240;
        config.vsync = false;
        config.worldSaveDir = m_dir.path().string();
        config.worldSeed = 123456ULL;
        config.renderDistanceChunks = 2;
        config.autoEnterWorld = true;
        m_app = std::make_unique<game::Application>(config);
        ASSERT_TRUE(m_app->initialize());
        ASSERT_NE(m_app->getWorld(), nullptr);

        // A glass platform high above the spawn terrain, the player standing on it.
        world::World& w = *m_app->getWorld();
        const glm::vec3 spawn = m_app->getPlayer().getPosition();
        m_x0 = static_cast<int32_t>(std::floor(spawn.x));
        m_z0 = static_cast<int32_t>(std::floor(spawn.z));
        for (int32_t z = m_z0 - 10; z <= m_z0 + 10; ++z) {
            for (int32_t x = m_x0 - 10; x <= m_x0 + 10; ++x) {
                ASSERT_TRUE(w.setBlock(x, kFloorY, z, voxel::BlockId::Glass) ||
                            w.getBlock(x, kFloorY, z) == voxel::BlockId::Glass);
            }
        }
        m_app->getPlayer().setPosition({static_cast<float>(m_x0) + 0.5f, static_cast<float>(kFloorY + 1),
                                        static_cast<float>(m_z0) + 0.5f});
        m_app->getPlayer().resetInterpolationHistory();
        ASSERT_EQ(m_app->runFixedFrames(10, kTick), 0);
    }

    void TearDown() override {
        if (m_app) m_app->shutdown();
    }

    static void pushLeftButton(Uint32 type) {
        SDL_Event ev{};
        ev.type = type;
        ev.button.type = type;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
        ev.button.clicks = 1;
        ASSERT_EQ(SDL_PushEvent(&ev), 1);
    }

    static constexpr int32_t kFloorY = 114;
    static constexpr float kTick = 0.05f; // One 20 TPS game tick per frame
    test::TempDir m_dir;
    std::unique_ptr<game::Application> m_app;
    int32_t m_x0{0};
    int32_t m_z0{0};
};

// Hand-off: a click pressed and released between two game ticks is latched by the
// Application and still reaches WorldSession::tick as attackPressed on the crosshair target.
TEST_F(ApplicationInGameE2ETest, ATapOnAFlowerReleasedWithinOneTickBreaksIt) {
    world::World& w = *m_app->getWorld();
    const core::BlockPos flower{m_x0 + 2, kFloorY + 1, m_z0};
    ASSERT_TRUE(w.setBlock(flower, voxel::BlockId::Dandelion));
    // Aim at the flower's box centre from the eye (1.62 above the feet).
    m_app->getPlayer().setRotation(0.0f, glm::degrees(std::atan2(-1.32f, 2.0f)));
    ASSERT_EQ(m_app->runFixedFrames(2, kTick), 0); // The crosshair finds the flower

    pushLeftButton(SDL_MOUSEBUTTONDOWN);
    pushLeftButton(SDL_MOUSEBUTTONUP);
    ASSERT_EQ(m_app->runFixedFrames(2, kTick), 0);

    EXPECT_EQ(w.getBlock(flower), voxel::BlockId::Air);
}

// Hand-off: the frame loop's game ticks run the world phase (fluids and their drops).
TEST_F(ApplicationInGameE2ETest, WaterPlacedNextToAFlowerFlowsOverIt) {
    world::World& w = *m_app->getWorld();
    // More than Beta's 4-block flow search from every platform edge so the source spreads in
    // all four directions.
    const core::BlockPos source{m_x0 + 5, kFloorY + 1, m_z0 + 5};
    const core::BlockPos flower{source.x + 1, source.y, source.z};
    ASSERT_TRUE(w.setBlock(flower, voxel::BlockId::Rose));
    ASSERT_TRUE(w.setBlock(source, voxel::BlockId::Water));

    // Water spreads every 5 game ticks, but the update waits its turn behind the generated
    // terrain's own fluid work (256 updates per tick), so give it a generous bound.
    for (int frame = 0; frame < 600 && !voxel::isWaterBlock(w.getBlock(flower)); ++frame) {
        ASSERT_EQ(m_app->runFixedFrames(1, kTick), 0);
    }

    EXPECT_TRUE(voxel::isWaterBlock(w.getBlock(flower)));
}

namespace {

[[nodiscard]] core::EngineConfig sprint1Config(const test::TempDir& dir) {
    core::EngineConfig config{};
    config.windowTitle = "BetaCraft E2E Sprint 1";
    config.windowWidth = 320;
    config.windowHeight = 240;
    config.vsync = false;
    config.worldSaveDir = dir.path().string();
    config.worldSeed = 123456ULL;
    config.renderDistanceChunks = 2;
    config.autoEnterWorld = true;
    return config;
}

} // namespace

// S1.7 / S1.4 hand-off: entering a new world from the title starts it in the title's mode
// (Survival) and writes level.dat; shutdown closes the session so the inventory comes back.
// Details of the saved state: test_world_session_lifecycle.cpp.
TEST(ApplicationE2ETest, SurvivalWorldSavesInventoryOnShutdown) {
    if (!test::GLTestContext::isAvailable()) {
        GTEST_SKIP() << "OpenGL 3.3 Core display/context not available.";
    }
    test::TempDir dir;
    {
        game::Application app(sprint1Config(dir));
        ASSERT_TRUE(app.initialize());
        EXPECT_EQ(app.getScreenState(), ui::ScreenState::TitleScreen);
        ASSERT_EQ(app.runFixedFrames(3, 0.05f), 0);
        EXPECT_EQ(app.getScreenState(), ui::ScreenState::InGame);
        EXPECT_FALSE(app.getPlayer().isCreative());
        EXPECT_TRUE(std::filesystem::exists(dir.path() / "level.dat")) << "written on entering the new world";
        app.getPlayer().setInventorySlot(17, {voxel::BlockId::Cobblestone, 23});
        app.shutdown();
    }

    game::Application app(sprint1Config(dir));
    ASSERT_TRUE(app.initialize());
    ASSERT_EQ(app.runFixedFrames(2, 0.05f), 0);
    EXPECT_EQ(app.getScreenState(), ui::ScreenState::InGame);
    EXPECT_EQ(app.getPlayer().countItem(voxel::BlockId::Cobblestone), 23) << "saved, and no kit on top";
    app.shutdown();
}

// S1.4 hand-off: the title's mode (EngineConfig::defaultGameMode) starts the new world.
TEST(ApplicationE2ETest, NewCreativeWorldGetsTheCreativeKit) {
    if (!test::GLTestContext::isAvailable()) {
        GTEST_SKIP() << "OpenGL 3.3 Core display/context not available.";
    }
    test::TempDir dir;
    core::EngineConfig config = sprint1Config(dir);
    config.defaultGameMode = core::GameMode::Creative;
    game::Application app(config);
    ASSERT_TRUE(app.initialize());
    ASSERT_TRUE(app.getWorldSession()->awaitingGameMode());
    ASSERT_EQ(app.runFixedFrames(2, 0.05f), 0);
    EXPECT_EQ(app.getScreenState(), ui::ScreenState::InGame);
    EXPECT_FALSE(app.getWorldSession()->awaitingGameMode());
    EXPECT_TRUE(app.getPlayer().isCreative());
    app.shutdown();
}

// S1.6: dying opens the death screen with the inventory scattered; Respawn goes back in game
// alive. Where and how the player comes back: test_world_session_respawn (GL-free).
TEST(ApplicationE2ETest, DeathScreenThenRespawnAtSpawn) {
    if (!test::GLTestContext::isAvailable()) {
        GTEST_SKIP() << "OpenGL 3.3 Core display/context not available.";
    }
    test::TempDir dir;
    game::Application app(sprint1Config(dir));
    ASSERT_TRUE(app.initialize());
    ASSERT_EQ(app.runFixedFrames(2, 0.05f), 0);
    physics::Player& player = app.getPlayer();
    player.setInventorySlot(0, {voxel::BlockId::Torch, 10});
    const size_t itemsBefore = app.getSimulation().entities().count<ecs::ItemDropComp>();

    player.setPosition({8.5f, -70.0f, 8.5f});
    for (int i = 0; i < 200 && app.getScreenState() != ui::ScreenState::DeathScreen; ++i) {
        ASSERT_EQ(app.runFixedFrames(1, 0.05f), 0);
    }
    ASSERT_EQ(app.getScreenState(), ui::ScreenState::DeathScreen);
    EXPECT_TRUE(player.isDead());
    EXPECT_EQ(player.countItem(voxel::BlockId::Torch), 0);
    EXPECT_EQ(app.getSimulation().entities().count<ecs::ItemDropComp>(), itemsBefore + 1);

    app.respawnPlayer();
    EXPECT_EQ(app.getScreenState(), ui::ScreenState::InGame);
    EXPECT_FALSE(player.isDead());
    app.shutdown();
}

// S1.4 hand-off: quitting from the title screen of a new world writes no level.dat, so the
// next launch still offers the mode choice.
TEST(ApplicationE2ETest, NewWorldQuitAtTitleKeepsTheModeChoice) {
    if (!test::GLTestContext::isAvailable()) {
        GTEST_SKIP() << "OpenGL 3.3 Core display/context not available.";
    }
    test::TempDir dir;
    {
        game::Application app(sprint1Config(dir));
        ASSERT_TRUE(app.initialize());
        app.shutdown(); // Never left the title screen
    }
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "level.dat"));

    core::EngineConfig config = sprint1Config(dir);
    config.defaultGameMode = core::GameMode::Creative;
    game::Application app(config);
    ASSERT_TRUE(app.initialize());
    ASSERT_EQ(app.runFixedFrames(2, 0.05f), 0);
    EXPECT_TRUE(app.getPlayer().isCreative()) << "still a new world: the choice applies";
    app.shutdown();
}

// Conventions §1.2 hand-off: Application::isSaveDisabled forwards the session's flag, and
// playing then shutting down leaves a too new level.dat untouched.
TEST(ApplicationE2ETest, TooNewLevelDatIsNeverOverwritten) {
    if (!test::GLTestContext::isAvailable()) {
        GTEST_SKIP() << "OpenGL 3.3 Core display/context not available.";
    }
    test::TempDir dir;
    const std::string kV3 = "BETACRAFT_LEVEL_V3\nseed 42\n";
    {
        std::ofstream ofs(dir.path() / "level.dat", std::ios::binary);
        ofs << kV3;
    }
    game::Application app(sprint1Config(dir));
    ASSERT_TRUE(app.initialize());
    EXPECT_TRUE(app.isSaveDisabled());
    ASSERT_EQ(app.runFixedFrames(5, 0.05f), 0);
    EXPECT_EQ(app.getScreenState(), ui::ScreenState::InGame);
    app.shutdown();
    EXPECT_FALSE(app.isSaveDisabled()) << "no world open any more";

    std::ifstream ifs(dir.path() / "level.dat", std::ios::binary);
    const std::string onDisk((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_EQ(onDisk, kV3);
}

namespace {

// S7.9: no autoEnterWorld, worlds under <dir>/saves (the menu flow).
[[nodiscard]] core::EngineConfig menuConfig(const test::TempDir& dir) {
    core::EngineConfig config = sprint1Config(dir);
    config.autoEnterWorld = false;
    config.savesRoot = (dir.path() / "saves").string();
    return config;
}

void pushClick(int x, int y) {
    for (const Uint32 type : {static_cast<Uint32>(SDL_MOUSEBUTTONDOWN), static_cast<Uint32>(SDL_MOUSEBUTTONUP)}) {
        SDL_Event ev{};
        ev.type = type;
        ev.button.type = type;
        ev.button.button = SDL_BUTTON_LEFT;
        ev.button.state = type == SDL_MOUSEBUTTONDOWN ? SDL_PRESSED : SDL_RELEASED;
        ev.button.clicks = 1;
        ev.button.x = x;
        ev.button.y = y;
        ASSERT_EQ(SDL_PushEvent(&ev), 1);
    }
}

void pushKey(SDL_Keycode key) {
    SDL_Event ev{};
    ev.type = SDL_KEYDOWN;
    ev.key.type = SDL_KEYDOWN;
    ev.key.state = SDL_PRESSED;
    ev.key.keysym.sym = key;
    ASSERT_EQ(SDL_PushEvent(&ev), 1);
}

void pushText(const char* text) {
    SDL_Event ev{};
    ev.type = SDL_TEXTINPUT;
    ev.text.type = SDL_TEXTINPUT;
    std::snprintf(ev.text.text, sizeof(ev.text.text), "%s", text);
    ASSERT_EQ(SDL_PushEvent(&ev), 1);
}

[[nodiscard]] std::string readFile(const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
}

} // namespace

// S7.9: without autoEnterWorld the game starts on the title screen with no world, and nothing
// is written until a world is picked.
TEST(ApplicationWorldsE2ETest, StartsOnTitleWithoutWorldUnlessAutoEnter) {
    if (!test::GLTestContext::isAvailable()) {
        GTEST_SKIP() << "OpenGL 3.3 Core display/context not available.";
    }
    test::TempDir dir;
    game::Application app(menuConfig(dir));
    ASSERT_TRUE(app.initialize());
    EXPECT_EQ(app.getWorld(), nullptr);
    EXPECT_FALSE(app.hasWorld());
    EXPECT_EQ(app.getScreenState(), ui::ScreenState::TitleScreen);
    ASSERT_EQ(app.runFixedFrames(3, 0.05f), 0) << "frames on the title screen need no world";
    EXPECT_EQ(app.getScreenState(), ui::ScreenState::TitleScreen);
    app.leaveWorld(); // No-op without a world
    app.shutdown();
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "level.dat"));
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "saves"));
}

// S7.8 / S7.9 through real SDL events: Singleplayer -> World Select -> Create New World ->
// typed name and seed, Survival toggled to Creative -> in game in the new world's folder.
TEST(ApplicationWorldsE2ETest, MenusCreateAndEnterANewWorld) {
    if (!test::GLTestContext::isAvailable()) {
        GTEST_SKIP() << "OpenGL 3.3 Core display/context not available.";
    }
    test::TempDir dir;
    core::EngineConfig config = menuConfig(dir);
    config.windowWidth = 800;
    config.windowHeight = 600;
    game::Application app(config);
    ASSERT_TRUE(app.initialize());
    const core::EngineConfig& live = app.getConfig();
    ASSERT_EQ(live.windowWidth, 800);

    pushClick(400, 295); // Singleplayer (rows from y = 280 at 800x600)
    ASSERT_EQ(app.runFixedFrames(1, 0.05f), 0);
    ASSERT_EQ(app.getScreenState(), ui::ScreenState::WorldSelect);

    pushClick(500, 519); // Create New World, right of Play Selected World at y = 504
    ASSERT_EQ(app.runFixedFrames(1, 0.05f), 0);
    ASSERT_EQ(app.getScreenState(), ui::ScreenState::CreateWorld);

    for (int i = 0; i < 9; ++i) pushKey(SDLK_BACKSPACE); // "New World"
    pushText("Alpha/1");
    pushKey(SDLK_TAB);
    pushText("test");
    pushClick(400, 380); // Game Mode: Creative
    pushKey(SDLK_RETURN);
    ASSERT_EQ(app.runFixedFrames(1, 0.05f), 0);

    ASSERT_NE(app.getWorld(), nullptr);
    EXPECT_EQ(app.getScreenState(), ui::ScreenState::InGame);
    EXPECT_EQ(app.getWorld()->getSeed(), 3556498U) << "\"test\".hashCode()";
    EXPECT_TRUE(app.getPlayer().isCreative());
    const std::filesystem::path world = dir.path() / "saves" / "Alpha1";
    EXPECT_EQ(std::filesystem::path(app.getConfig().worldSaveDir), world) << "'/' never typed into the name";
    const std::string level = readFile(world / "level.dat");
    EXPECT_NE(level.find("name Alpha1\n"), std::string::npos) << level;
    EXPECT_NE(level.find("mode creative\n"), std::string::npos) << level;
    app.shutdown();

    const std::vector<ui::WorldSummary> worlds = ui::listWorlds(dir.path() / "saves");
    ASSERT_EQ(worlds.size(), 1U);
    EXPECT_EQ(worlds[0].name, "Alpha1");
    EXPECT_EQ(worlds[0].seed, 3556498U);
    EXPECT_EQ(worlds[0].mode, core::GameMode::Creative);
    EXPECT_FALSE(worlds[0].needsMigration);
}

// S7.9 hand-off: leaveWorld closes the session and returns to the title; entering B then A
// opens each in turn, and the World Select list orders them by last play.
// What each world keeps: test_world_session_lifecycle.cpp.
TEST(ApplicationWorldsE2ETest, LeaveWorldThenAnotherWorldKeepsNothingOfTheFirst) {
    if (!test::GLTestContext::isAvailable()) {
        GTEST_SKIP() << "OpenGL 3.3 Core display/context not available.";
    }
    test::TempDir dir;
    const std::filesystem::path a = dir.path() / "saves" / "A";
    const std::filesystem::path b = dir.path() / "saves" / "B";
    game::Application app(menuConfig(dir));
    ASSERT_TRUE(app.initialize());

    ASSERT_TRUE(app.enterWorld(a, core::NewWorldParams{"World A", 42, core::GameMode::Creative}));
    ASSERT_EQ(app.runFixedFrames(3, 0.05f), 0);
    EXPECT_EQ(app.getScreenState(), ui::ScreenState::InGame);
    EXPECT_EQ(app.getWorld()->getSeed(), 42U);
    app.getPlayer().setInventorySlot(5, {voxel::BlockId::Dirt, 7});

    app.leaveWorld();
    EXPECT_FALSE(app.hasWorld());
    EXPECT_EQ(app.getWorld(), nullptr);
    EXPECT_EQ(app.getWorldSession(), nullptr);
    EXPECT_EQ(app.getScreenState(), ui::ScreenState::TitleScreen);
    EXPECT_TRUE(std::filesystem::exists(a / "level.dat"));

    ASSERT_TRUE(app.enterWorld(b, core::NewWorldParams{"World B", 7, core::GameMode::Survival}));
    ASSERT_EQ(app.runFixedFrames(3, 0.05f), 0);
    EXPECT_EQ(app.getWorld()->getSeed(), 7U);
    EXPECT_FALSE(app.getPlayer().isCreative());
    EXPECT_EQ(app.getPlayer().countItem(voxel::BlockId::Dirt), 0) << "A's inventory stayed in A";

    ASSERT_TRUE(app.enterWorld(a, core::NewWorldParams{"ignored", 999, core::GameMode::Survival}));
    EXPECT_TRUE(std::filesystem::exists(b / "level.dat")) << "entering A first left B saved";
    ASSERT_EQ(app.runFixedFrames(2, 0.05f), 0);
    EXPECT_EQ(app.getWorld()->getSeed(), 42U);
    EXPECT_EQ(app.getPlayer().getInventorySlot(5), (physics::InventorySlot{voxel::BlockId::Dirt, 7}));
    app.shutdown();

    const std::vector<ui::WorldSummary> worlds = ui::listWorlds(dir.path() / "saves");
    ASSERT_EQ(worlds.size(), 2U);
    EXPECT_EQ(worlds[0].name, "World A") << "played last";
    EXPECT_EQ(worlds[1].name, "World B");
}

// Playtest: two worlds created with the seed "test" have the same terrain.
TEST(ApplicationWorldsE2ETest, SameSeedTextGivesSameTerrain) {
    if (!test::GLTestContext::isAvailable()) {
        GTEST_SKIP() << "OpenGL 3.3 Core display/context not available.";
    }
    test::TempDir dir;
    game::Application app(menuConfig(dir));
    ASSERT_TRUE(app.initialize());
    const uint64_t seed = ui::seedFromText("test", 1U);
    const auto sampleColumns = [&app]() {
        std::vector<voxel::BlockId> blocks;
        for (int32_t x = -8; x < 24; x += 3) {
            for (int32_t z = -8; z < 24; z += 5) {
                for (int32_t y = 40; y < 100; y += 4) blocks.push_back(app.getWorld()->getBlock(x, y, z));
            }
        }
        return blocks;
    };
    ASSERT_TRUE(app.enterWorld(dir.path() / "saves" / "one", core::NewWorldParams{"one", seed, core::GameMode::Survival}));
    const std::vector<voxel::BlockId> first = sampleColumns();
    const glm::vec3 spawnOne = app.getSpawnPoint();
    app.leaveWorld();
    ASSERT_TRUE(app.enterWorld(dir.path() / "saves" / "two", core::NewWorldParams{"two", seed, core::GameMode::Survival}));
    EXPECT_EQ(sampleColumns(), first);
    EXPECT_EQ(app.getSpawnPoint(), spawnOne);
    app.shutdown();
}

// S7.4 / S7.8 / S7.9 hand-off: a pre-Sprint 7 save is listed as "to migrate", entering it
// from the Application converts it, and it is no longer flagged.
// The migration itself: test_world_session_lifecycle.cpp.
TEST(ApplicationWorldsE2ETest, LegacySaveFlaggedThenMigratedOnEnter) {
    if (!test::GLTestContext::isAvailable()) {
        GTEST_SKIP() << "OpenGL 3.3 Core display/context not available.";
    }
    test::TempDir dir;
    const std::filesystem::path save = dir.path() / "saves" / "v1";
    std::filesystem::create_directories(save.parent_path());
    std::filesystem::copy(std::filesystem::path(BETACRAFT_TEST_DATA_DIR) / "saves" / "v1", save,
                          std::filesystem::copy_options::recursive);
    std::vector<ui::WorldSummary> worlds = ui::listWorlds(dir.path() / "saves");
    ASSERT_EQ(worlds.size(), 1U);
    EXPECT_TRUE(worlds[0].needsMigration);

    game::Application app(menuConfig(dir));
    ASSERT_TRUE(app.initialize());
    ASSERT_TRUE(app.enterWorld(save));
    EXPECT_EQ(app.getScreenState(), ui::ScreenState::TitleScreen) << "loaded, waiting to be entered";
    EXPECT_TRUE(std::filesystem::is_directory(save / "region"));
    ASSERT_EQ(app.runFixedFrames(2, 0.05f), 0);
    EXPECT_EQ(app.getScreenState(), ui::ScreenState::InGame);
    app.leaveWorld();
    app.shutdown();

    worlds = ui::listWorlds(dir.path() / "saves");
    ASSERT_EQ(worlds.size(), 1U);
    EXPECT_FALSE(worlds[0].needsMigration);
}
