// GL-free WorldSession lifecycle (spec #9, ticket #12): open, start, save and close a real
// world on disk. Equivalents of the Application e2e tests that check saves and inventory.

#include "WorldSessionSupport.hpp"

#include "crafting/CraftingGrid.hpp"
#include "gameplay/ItemIds.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace betacraft::test::worldsession {
namespace {

using game::WorldSession;

class WorldSessionLifecycle : public ::testing::Test {
protected:
    void SetUp() override { world::BlockRegistry::initialize(); }

    [[nodiscard]] std::unique_ptr<WorldSession> open() { return WorldSession::open(openOptions(m_dir.path())); }
    [[nodiscard]] bool levelDatExists() const { return std::filesystem::exists(m_dir.path() / "level.dat"); }

    TempDir m_dir{"betacraft_world_session_"};
};

// S1.4: a new world waits for its mode; choosing Creative gives the creative kit and writes
// the first level.dat at once.
TEST_F(WorldSessionLifecycle, NewCreativeWorldGetsTheCreativeKit) {
    std::unique_ptr<WorldSession> session = open();
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->levelStatus(), storage::LevelLoadStatus::Missing);
    EXPECT_TRUE(session->awaitingGameMode());
    EXPECT_FALSE(levelDatExists()) << "nothing written before the mode is chosen";

    session->startNewWorld(core::GameMode::Creative);
    EXPECT_FALSE(session->awaitingGameMode());
    EXPECT_TRUE(session->player().isCreative());
    EXPECT_GT(session->player().countItem(world::BlockId::Cobblestone), 0);
    EXPECT_TRUE(levelDatExists()) << "the first level.dat is written with the mode";
}

// S1.7 / S1.4: a new Survival world starts empty, and what the player holds, its health and
// its selection come back when the world is reopened, with no kit on top.
TEST_F(WorldSessionLifecycle, SurvivalWorldSavesInventoryOnShutdown) {
    glm::vec3 spawn{};
    {
        std::unique_ptr<WorldSession> session = open();
        session->startNewWorld(core::GameMode::Survival);
        physics::Player& player = session->player();
        EXPECT_FALSE(player.isCreative());
        for (size_t i = 0; i < physics::Player::TOTAL_INVENTORY_SIZE; ++i) {
            ASSERT_TRUE(player.getInventorySlot(i).isEmpty()) << "Survival starts empty-handed";
        }
        EXPECT_TRUE(levelDatExists());

        player.setInventorySlot(2, physics::InventorySlot{gameplay::items::IronPickaxe, 1, 99});
        player.setInventorySlot(17, {BlockId::Cobblestone, 23});
        player.setSelectedSlot(2);
        player.setHealth(15);
        spawn = session->spawnPoint();
        EXPECT_TRUE(session->close());
    }

    std::unique_ptr<WorldSession> session = open();
    EXPECT_EQ(session->levelStatus(), storage::LevelLoadStatus::Ok);
    EXPECT_FALSE(session->awaitingGameMode());
    const physics::Player& player = session->player();
    EXPECT_FALSE(player.isCreative());
    EXPECT_EQ(player.getInventorySlot(2), (physics::InventorySlot{gameplay::items::IronPickaxe, 1, 99}));
    EXPECT_EQ(player.getInventorySlot(17), (physics::InventorySlot{BlockId::Cobblestone, 23}));
    EXPECT_EQ(player.countItem(BlockId::Cobblestone), 23) << "no starter kit on a saved world";
    EXPECT_EQ(player.getSelectedSlot(), 2);
    EXPECT_EQ(player.getHealth(), 15);
    EXPECT_EQ(session->spawnPoint(), spawn);
}

// S1.4: a new world closed before its mode is chosen writes nothing, so the next open still
// waits for the mode and gives the chosen mode's starter kit.
TEST_F(WorldSessionLifecycle, NewWorldQuitAtTitleKeepsTheModeChoice) {
    {
        std::unique_ptr<WorldSession> session = open();
        ASSERT_TRUE(session->awaitingGameMode());
        (void)session->save();
        EXPECT_TRUE(session->close());
    }
    EXPECT_FALSE(levelDatExists());

    std::unique_ptr<WorldSession> session = open();
    EXPECT_EQ(session->levelStatus(), storage::LevelLoadStatus::Missing);
    ASSERT_TRUE(session->awaitingGameMode()) << "still a new world: the choice applies";
    session->startNewWorld(core::GameMode::Creative);
    EXPECT_TRUE(session->player().isCreative());
    EXPECT_GT(session->player().countItem(BlockId::Cobblestone), 0);
}

// Conventions §1.2: a level.dat from a newer version is refused and never overwritten; the
// session plays in the default mode and writes nothing at all into the save directory.
TEST_F(WorldSessionLifecycle, TooNewLevelDatIsNeverOverwritten) {
    const std::string kV3 = "BETACRAFT_LEVEL_V3\nseed 42\n";
    {
        std::ofstream ofs(m_dir.path() / "level.dat", std::ios::binary);
        ofs << kV3;
    }
    WorldSession::OpenOptions options = openOptions(m_dir.path());
    options.defaultGameMode = core::GameMode::Creative;
    std::unique_ptr<WorldSession> session = WorldSession::open(options);
    ASSERT_NE(session, nullptr) << "a too new save is still playable";
    EXPECT_EQ(session->levelStatus(), storage::LevelLoadStatus::TooNew);
    EXPECT_TRUE(session->isSaveDisabled());
    EXPECT_TRUE(session->world().isSaveDisabled()) << "the World holds the flag";
    EXPECT_FALSE(session->awaitingGameMode());
    EXPECT_TRUE(session->player().isCreative()) << "the default mode, with its kit";
    EXPECT_GT(session->player().countItem(BlockId::Cobblestone), 0);

    ASSERT_TRUE(session->world().setBlock(2, 100, 2, BlockId::Glowstone));
    session->player().setInventorySlot(0, {BlockId::Dirt, 3});
    (void)session->save();
    (void)session->close();
    session.reset();

    std::ifstream ifs(m_dir.path() / "level.dat", std::ios::binary);
    const std::string onDisk((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    EXPECT_EQ(onDisk, kV3);
    EXPECT_FALSE(std::filesystem::exists(m_dir.path() / "level.dat_old"));
    size_t written = 0;
    for (const auto& entry : std::filesystem::directory_iterator(m_dir.path())) {
        written += entry.path().filename() == "level.dat" ? 0U : 1U;
    }
    EXPECT_EQ(written, 0U) << "no chunk, region or temporary file written either";
}

// S7.4: opening a pre-Sprint 7 save converts it to region files (chunks.migrated/ kept),
// reports its progress from 0, and brings its inventory back; the next open has nothing left
// to convert.
TEST_F(WorldSessionLifecycle, LegacySaveFlaggedThenMigratedOnEnter) {
    const std::filesystem::path save = m_dir.path() / "v1";
    std::filesystem::copy(std::filesystem::path(BETACRAFT_TEST_DATA_DIR) / "saves" / "v1", save,
                          std::filesystem::copy_options::recursive);
    std::vector<game::MigrationProgress> progress;
    WorldSession::OpenOptions options = openOptions(save);
    options.onMigrationProgress = [&](const game::MigrationProgress& p) { progress.push_back(p); };
    {
        std::unique_ptr<WorldSession> session = WorldSession::open(options);
        ASSERT_NE(session, nullptr);
        ASSERT_FALSE(progress.empty());
        EXPECT_GT(progress.front().legacyChunks, 0U);
        EXPECT_FLOAT_EQ(progress.front().fraction, 0.0f) << "reported before the first chunk";
        EXPECT_FLOAT_EQ(progress.back().fraction, 1.0f);
        EXPECT_TRUE(std::filesystem::is_directory(save / "region"));
        EXPECT_TRUE(std::filesystem::is_directory(save / "chunks.migrated"));
        EXPECT_FALSE(std::filesystem::exists(save / "chunks"));
        size_t items = 0;
        for (size_t i = 0; i < physics::Player::TOTAL_INVENTORY_SIZE; ++i) {
            items += session->player().getInventorySlot(i).isEmpty() ? 0U : 1U;
        }
        EXPECT_GT(items, 0U) << "the saved inventory came back";
        EXPECT_TRUE(session->close());
    }

    progress.clear();
    std::unique_ptr<WorldSession> session = WorldSession::open(options);
    ASSERT_NE(session, nullptr);
    EXPECT_TRUE(progress.empty()) << "nothing left to migrate";
}

// S7.9: closing world A saves it completely; world B opened next starts clean; A reopens as
// it was left, the new-world parameters of an existing world being ignored.
TEST_F(WorldSessionLifecycle, LeaveWorldThenAnotherWorldKeepsNothingOfTheFirst) {
    const std::filesystem::path a = m_dir.path() / "A";
    const std::filesystem::path b = m_dir.path() / "B";
    const auto openWith = [](const std::filesystem::path& folder, core::NewWorldParams params) {
        WorldSession::OpenOptions options = openOptions(folder);
        options.newWorld = std::move(params);
        return WorldSession::open(options);
    };

    std::unique_ptr<WorldSession> session = openWith(a, {"World A", 42, core::GameMode::Creative});
    ASSERT_NE(session, nullptr);
    EXPECT_FALSE(session->awaitingGameMode()) << "Create World chose the mode: started at once";
    EXPECT_EQ(session->world().getSeed(), 42U);
    EXPECT_TRUE(session->player().isCreative());
    EXPECT_EQ(session->name(), "World A");
    const glm::vec3 spawnA = session->player().getPosition();
    const core::BlockPos marker{static_cast<int32_t>(std::floor(spawnA.x)) + 1, 120,
                                static_cast<int32_t>(std::floor(spawnA.z))};
    ASSERT_TRUE(session->world().setBlock(marker, BlockId::Glowstone));
    session->player().setInventorySlot(5, {BlockId::Dirt, 7});
    (void)session->simulation().spawnItem(gameplay::ItemStack{static_cast<gameplay::ItemId>(BlockId::Sand), 1},
                                          spawnA + glm::vec3(3.0f, 1.0f, 0.0f));
    ASSERT_GT(session->simulation().entities().count<ecs::ItemDropComp>(), 0U);
    EXPECT_TRUE(session->close());
    session.reset();
    EXPECT_TRUE(std::filesystem::exists(a / "level.dat"));
    EXPECT_TRUE(std::filesystem::is_directory(a / "region")) << "every queued chunk flushed to the regions";
    EXPECT_FALSE(std::filesystem::exists(a / "chunks"));

    session = openWith(b, {"World B", 7, core::GameMode::Survival});
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->world().getSeed(), 7U);
    EXPECT_FALSE(session->player().isCreative());
    EXPECT_EQ(session->player().countItem(BlockId::Dirt), 0) << "A's inventory stayed in A";
    EXPECT_EQ(session->simulation().entities().count<ecs::ItemDropComp>(), 0U);
    EXPECT_NE(session->world().getBlock(marker), BlockId::Glowstone) << "A's blocks are not in B";
    session.reset(); // The destructor closes B
    EXPECT_TRUE(std::filesystem::exists(b / "level.dat"));
    EXPECT_FALSE(std::filesystem::exists(b / "level.dat.tmp"));

    session = openWith(a, {"ignored", 999, core::GameMode::Survival});
    ASSERT_NE(session, nullptr);
    EXPECT_EQ(session->world().getSeed(), 42U);
    EXPECT_EQ(session->name(), "World A");
    EXPECT_TRUE(session->player().isCreative());
    EXPECT_EQ(session->player().getInventorySlot(5), (physics::InventorySlot{BlockId::Dirt, 7}));
    EXPECT_EQ(session->world().getBlock(marker), BlockId::Glowstone);
}

// The only failure of open: a folder that cannot be created.
TEST_F(WorldSessionLifecycle, OpenFailsOnlyWhenTheFolderCannotBeCreated) {
    const std::filesystem::path file = m_dir.path() / "not_a_folder";
    std::ofstream(file) << "x";
    WorldSession::OpenOptions options = openOptions(file / "world");
    options.newWorld = core::NewWorldParams{"W", 1, core::GameMode::Survival};
    EXPECT_EQ(WorldSession::open(options), nullptr);
}

// close() can run more than once; only the first writes.
TEST_F(WorldSessionLifecycle, CloseCanBeCalledMoreThanOnce) {
    std::unique_ptr<WorldSession> session = open();
    session->startNewWorld(core::GameMode::Survival);
    EXPECT_TRUE(session->close());
    std::filesystem::remove(m_dir.path() / "level.dat");
    EXPECT_TRUE(session->close());
    EXPECT_FALSE(levelDatExists()) << "the second close writes nothing";
}

// Spec #9 user story 24: close() reports a chunk that could not be written, even though the
// save() inside it only queued the chunk. A file where the region folder goes makes every
// region write fail.
TEST_F(WorldSessionLifecycle, CloseReportsChunksThatCouldNotBeWritten) {
    std::ofstream(m_dir.path() / "region") << "not a folder";
    std::unique_ptr<WorldSession> session = open();
    ASSERT_NE(session, nullptr);
    session->startNewWorld(core::GameMode::Survival);
    ASSERT_TRUE(session->world().setBlock(2, 100, 2, BlockId::Glowstone));

    EXPECT_FALSE(session->close());
    EXPECT_FALSE(session->close()) << "later calls return the first result";
}

} // namespace

// The fixture opens, starts, saves and closes a world with no GL.
class WorldSessionFixture : public WorldSessionSupport {};

TEST_F(WorldSessionFixture, OpensAStartedWorldWithThePlayerOnTheFloor) {
    EXPECT_EQ(m_session->world().getSeed(), p2worldgen::kSeed);
    EXPECT_FALSE(m_session->awaitingGameMode());
    EXPECT_EQ(world().getBlock(2, kFloorY, 2), BlockId::Glass);
    EXPECT_TRUE(player().isOnGround());
    EXPECT_FALSE(m_session->save().closedCrafting);
    EXPECT_TRUE(m_session->close());
    EXPECT_TRUE(std::filesystem::exists(m_dir.path() / "level.dat"));
}

// Spec #9 user story 22: saving never loses or duplicates the grid and cursor stacks. When
// the inventory cannot take them, the session closes the grid and throws them in front of
// the player.
TEST_F(WorldSessionFixture, SaveClosesTheGridWhenItsStacksDoNotFit) {
    for (size_t i = 0; i < physics::Player::TOTAL_INVENTORY_SIZE; ++i) {
        player().setInventorySlot(i, {BlockId::Stone, 64});
    }
    player().setInventorySlot(0, {BlockId::OakLog, 1});
    (void)m_session->crafting().open(crafting::GridSize::Inventory2x2, player());
    ASSERT_TRUE(m_session->crafting().clickInventorySlot(player(), 0)); // Log on the cursor
    ASSERT_TRUE(m_session->crafting().clickGridSlot(0));                // ... into the grid
    player().setInventorySlot(0, {BlockId::Stone, 64});                 // Now nothing fits
    const size_t before = itemEntityCount();

    EXPECT_TRUE(m_session->save().closedCrafting);
    EXPECT_TRUE(m_session->crafting().pendingStacks().empty());
    EXPECT_EQ(itemEntityCount(), before + 1) << "the log thrown in front of the player";
    EXPECT_EQ(player().countItem(BlockId::OakLog), 0) << "not duplicated into the inventory";
    EXPECT_FALSE(m_session->save().closedCrafting) << "nothing left to close";
}

// A grid whose stacks fit stays open: the save folds them into the saved inventory.
TEST_F(WorldSessionFixture, SaveKeepsTheGridOpenWhenItsStacksFit) {
    player().setInventorySlot(0, {BlockId::OakLog, 1});
    (void)m_session->crafting().open(crafting::GridSize::Inventory2x2, player());
    ASSERT_TRUE(m_session->crafting().clickInventorySlot(player(), 0));
    ASSERT_TRUE(m_session->crafting().clickGridSlot(0));
    const size_t before = itemEntityCount();

    EXPECT_FALSE(m_session->save().closedCrafting);
    EXPECT_EQ(m_session->crafting().pendingStacks().size(), 1U);
    EXPECT_EQ(itemEntityCount(), before);
}

} // namespace betacraft::test::worldsession
