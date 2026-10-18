#include "tests/test_helpers.hpp"
#include "game/InventoryCrafting.hpp"
#include "game/PlayerPersistence.hpp"
#include "gameplay/Armor.hpp"
#include "physics/Player.hpp"
#include "world/Block.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace betacraft;
using namespace testing;

TEST(PersistenceIntegrationTest, ModifySaveAndReloadWorld) {
    world::BlockRegistry::initialize();
    test::TempDir tempDir;

    // Phase 1: create world, load chunks, modify blocks
    {
        world::World world1(173173ULL, tempDir.path());
        world1.ensureChunkLoaded({0, 0});
        world1.ensureChunkLoaded({-1, 2});

        world1.setBlock(5, 75, 5, world::BlockId::TNT);
        world1.setBlock(5, 76, 5, world::BlockId::Torch);
        world1.setBlock(-10, 68, 35, world::BlockId::DiamondOre);

        const world::WorldLevelData saveLevel{
            .seed = 173173ULL,
            .playerPos = {5.5f, 77.0f, 5.5f},
            .playerYaw = 45.0f,
            .playerPitch = 10.0f,
            .timeOfDay = 0.35f
        };
        world1.saveModifiedChunks(saveLevel);
    }

    // Phase 2: create completely fresh World instance pointing to same directory
    {
        world::World world2(0ULL, tempDir.path()); // start with dummy seed
        world::WorldLevelData loadedLevel{};
        ASSERT_TRUE(world2.loadLevelData(loadedLevel));

        EXPECT_EQ(loadedLevel.seed, 173173ULL);
        EXPECT_FLOAT_EQ(loadedLevel.playerPos.x, 5.5f);
        EXPECT_FLOAT_EQ(loadedLevel.playerPos.y, 77.0f);
        EXPECT_FLOAT_EQ(loadedLevel.playerPos.z, 5.5f);
        EXPECT_FLOAT_EQ(loadedLevel.playerYaw, 45.0f);
        EXPECT_FLOAT_EQ(loadedLevel.playerPitch, 10.0f);
        EXPECT_FLOAT_EQ(loadedLevel.timeOfDay, 0.35f);

        // Load modified chunk and check blocks
        world2.ensureChunkLoaded({0, 0});
        EXPECT_EQ(world2.getBlock(5, 75, 5), world::BlockId::TNT);
        EXPECT_EQ(world2.getBlock(5, 76, 5), world::BlockId::Torch);

        world2.ensureChunkLoaded({-1, 2});
        EXPECT_EQ(world2.getBlock(-10, 68, 35), world::BlockId::DiamondOre);
    }
}

// S5.4: the metadata of the shaped blocks (door state, ladder side, crop stage) survives a
// save cycle through the chunk RLE (format v2 carries the nibble).
TEST(PersistenceIntegrationTest, ShapedBlockMetadataSurvivesRestart) {
    world::BlockRegistry::initialize();
    test::TempDir tempDir;
    constexpr uint8_t kOpenDoor = 0x4 | 0x1;
    {
        world::World world1(173173ULL, tempDir.path());
        world1.ensureChunkLoaded({0, 0});
        ASSERT_TRUE(world1.setBlockWithMetadata(3, 100, 3, world::BlockId::WoodDoor, kOpenDoor));
        ASSERT_TRUE(world1.setBlockWithMetadata(3, 101, 3, world::BlockId::WoodDoor, 0x8 | kOpenDoor));
        ASSERT_TRUE(world1.setBlock(6, 100, 3, world::BlockId::Stone));
        ASSERT_TRUE(world1.setBlockWithMetadata(5, 100, 3, world::BlockId::Ladder, 4));
        ASSERT_TRUE(world1.setBlock(8, 100, 3, world::BlockId::Farmland));
        ASSERT_TRUE(world1.setBlockWithMetadata(8, 101, 3, world::BlockId::Crops, 5));
        world1.saveModifiedChunks(world::WorldLevelData{.seed = 173173ULL});
    }
    {
        world::World world2(173173ULL, tempDir.path());
        world2.ensureChunkLoaded({0, 0});
        EXPECT_EQ(world2.getBlock(3, 100, 3), world::BlockId::WoodDoor);
        EXPECT_EQ(world2.getMetadata(3, 100, 3), kOpenDoor);
        EXPECT_EQ(world2.getBlock(3, 101, 3), world::BlockId::WoodDoor);
        EXPECT_EQ(world2.getMetadata(3, 101, 3), 0x8 | kOpenDoor);
        EXPECT_EQ(world2.getBlock(5, 100, 3), world::BlockId::Ladder);
        EXPECT_EQ(world2.getMetadata(5, 100, 3), 4);
        EXPECT_EQ(world2.getBlock(8, 101, 3), world::BlockId::Crops);
        EXPECT_EQ(world2.getMetadata(8, 101, 3), 5);
    }
}

// S5.8: the bed the player slept in (level.dat key "bed") and the spawn point next to it
// survive a restart; a level.dat without the key has no bed spawn.
TEST(PersistenceIntegrationTest, BedSpawnSurvivesRestart) {
    world::BlockRegistry::initialize();
    test::TempDir tempDir;
    {
        world::World world1(173173ULL, tempDir.path());
        world1.ensureChunkLoaded({0, 0});
        world::WorldLevelData level{.seed = 173173ULL};
        level.spawnPos = glm::vec3{4.5f, 71.0f, 6.5f};
        level.bedPos = core::BlockPos{5, 71, 6};
        world1.saveModifiedChunks(level);
    }
    world::World world2(0ULL, tempDir.path());
    world::WorldLevelData loaded{};
    ASSERT_EQ(world2.loadLevelDataStatus(loaded), world::LevelLoadStatus::Ok);
    ASSERT_TRUE(loaded.bedPos.has_value());
    EXPECT_EQ(*loaded.bedPos, (core::BlockPos{5, 71, 6}));
    ASSERT_TRUE(loaded.spawnPos.has_value());
    EXPECT_FLOAT_EQ(loaded.spawnPos->x, 4.5f);

    world::WorldLevelData noBed{};
    ASSERT_EQ(world::decodeLevelData(world::encodeLevelData(world::WorldLevelData{}), noBed), world::LevelLoadStatus::Ok);
    EXPECT_FALSE(noBed.bedPos.has_value());
}

// S1.7: the player's inventory (wear included), health, air, selection, mode and the spawn
// point survive a restart through level.dat V2, even saved with the crafting screen open.
TEST(PersistenceIntegrationTest, PlayerInventoryHealthSpawnSurviveRestart) {
    world::BlockRegistry::initialize();
    test::TempDir tempDir;
    const glm::vec3 spawn{8.5f, 70.01f, 8.5f};

    physics::Player before;
    before.setGameMode(core::GameMode::Survival);
    before.setInventorySlot(0, physics::InventorySlot{gameplay::items::StonePickaxe, 1, 37}); // Worn
    before.setInventorySlot(3, {world::BlockId::Cobblestone, 40});
    before.setInventorySlot(20, physics::InventorySlot{gameplay::items::IronIngot, 7});
    before.setSelectedSlot(3);
    before.setHealth(13);
    before.breath().air = 150;

    // The crafting screen is open: two planks in the grid and torches on the cursor.
    game::InventoryCrafting screen;
    before.setInventorySlot(30, {world::BlockId::OakPlanks, 2});
    before.setInventorySlot(31, {world::BlockId::Torch, 5});
    (void)screen.open(crafting::GridSize::Inventory2x2, before);
    ASSERT_TRUE(screen.clickInventorySlot(before, 30));
    ASSERT_TRUE(screen.clickGridSlot(0));
    ASSERT_TRUE(screen.clickInventorySlot(before, 31));
    ASSERT_EQ(before.countItem(world::BlockId::OakPlanks), 0);
    ASSERT_EQ(before.countItem(world::BlockId::Torch), 0);
    const std::vector<physics::InventorySlot> pending = screen.pendingStacks();
    ASSERT_EQ(pending.size(), 2U);

    {
        world::World world1(173173ULL, tempDir.path());
        world1.ensureChunkLoaded({0, 0});
        world::WorldLevelData level{};
        level.seed = 173173ULL;
        level.playerPos = {5.5f, 77.0f, 5.5f};
        level.spawnPos = spawn;
        level.player = game::capturePlayer(before, pending);
        world1.saveModifiedChunks(level);
    }

    world::World world2(0ULL, tempDir.path());
    world::WorldLevelData loaded{};
    ASSERT_EQ(world2.loadLevelDataStatus(loaded), world::LevelLoadStatus::Ok);
    ASSERT_TRUE(loaded.player.has_value());
    ASSERT_TRUE(loaded.spawnPos.has_value());
    EXPECT_FLOAT_EQ(loaded.spawnPos->x, spawn.x);
    EXPECT_FLOAT_EQ(loaded.spawnPos->y, spawn.y);
    EXPECT_FLOAT_EQ(loaded.spawnPos->z, spawn.z);

    physics::Player after;
    after.setGameMode(core::GameMode::Creative);
    game::restorePlayer(after, *loaded.player);
    EXPECT_EQ(after.gameMode(), core::GameMode::Survival);
    EXPECT_EQ(after.getHealth(), 13);
    EXPECT_EQ(after.breath().air, 150);
    EXPECT_EQ(after.getSelectedSlot(), 3);
    EXPECT_EQ(after.getInventorySlot(0), (physics::InventorySlot{gameplay::items::StonePickaxe, 1, 37}));
    EXPECT_EQ(after.getInventorySlot(3), (physics::InventorySlot{world::BlockId::Cobblestone, 40}));
    EXPECT_EQ(after.getInventorySlot(20), (physics::InventorySlot{gameplay::items::IronIngot, 7}));
    EXPECT_EQ(after.countItem(world::BlockId::OakPlanks), 2) << "the grid was saved";
    EXPECT_EQ(after.countItem(world::BlockId::Torch), 5) << "the cursor stack was saved";
    // The open screen itself was not touched by the capture.
    EXPECT_EQ(screen.pendingStacks().size(), 2U);
}

// restorePlayer never trusts the file: counts, health, air and selection are clamped and
// unknown ids empty their slot.
TEST(PersistenceIntegrationTest, RestorePlayerClampsSavedValues) {
    world::BlockRegistry::initialize();
    world::PlayerSaveData data{};
    data.health = 99;
    data.air = -500;
    data.selectedSlot = 42;
    data.inventory[0] = {static_cast<uint16_t>(world::BlockId::Cobblestone), 500, 0};
    data.inventory[1] = {gameplay::items::IronSword, 9, 12};
    data.inventory[2] = {1000, 3, 0};  // No such item
    data.inventory[3] = {static_cast<uint16_t>(world::BlockId::Dirt), 0, 0};

    physics::Player player;
    player.setInventorySlot(3, {world::BlockId::Stone, 1});
    game::restorePlayer(player, data);
    EXPECT_EQ(player.getHealth(), 20);
    EXPECT_EQ(player.breath().air, 0);
    EXPECT_EQ(player.getSelectedSlot(), 8);
    EXPECT_EQ(player.getInventorySlot(0).count, 64);
    EXPECT_EQ(player.getInventorySlot(1), (physics::InventorySlot{gameplay::items::IronSword, 1, 12}));
    EXPECT_TRUE(player.getInventorySlot(2).isEmpty());
    EXPECT_TRUE(player.getInventorySlot(3).isEmpty());

    data.health = -4;
    game::restorePlayer(player, data);
    EXPECT_EQ(player.getHealth(), 0);
}

// S6.8: worn armor round-trips through level.dat (`armor` keys, no version change), and a
// level.dat without them loads with the armor slots empty.
TEST(PersistenceIntegrationTest, ArmorSurvivesRestart) {
    world::BlockRegistry::initialize();
    test::TempDir tempDir;
    const gameplay::ItemId helmet = gameplay::armorItemId(gameplay::ArmorMaterial::Iron, gameplay::ArmorSlot::Helmet);
    const gameplay::ItemId boots = gameplay::armorItemId(gameplay::ArmorMaterial::Diamond, gameplay::ArmorSlot::Boots);

    physics::Player before;
    before.setArmorSlot(0, {helmet, 1, 41});
    before.setArmorSlot(3, {boots, 1});
    {
        world::World world1(173173ULL, tempDir.path());
        world1.ensureChunkLoaded({0, 0});
        world::WorldLevelData level{};
        level.seed = 173173ULL;
        level.player = game::capturePlayer(before);
        const std::string text = world::encodeLevelData(level);
        EXPECT_NE(text.find("BETACRAFT_LEVEL_V2"), std::string::npos) << "no version change";
        EXPECT_NE(text.find("armor 0 306 1 41"), std::string::npos) << text;
        EXPECT_NE(text.find("armor 3 313 1 0"), std::string::npos) << text;
        EXPECT_EQ(text.find("armor 1"), std::string::npos) << "empty slots are not written";
        world1.saveModifiedChunks(level);
    }

    world::World world2(0ULL, tempDir.path());
    world::WorldLevelData loaded{};
    ASSERT_EQ(world2.loadLevelDataStatus(loaded), world::LevelLoadStatus::Ok);
    ASSERT_TRUE(loaded.player.has_value());
    physics::Player after;
    after.setArmorSlot(1, {gameplay::armorItemId(gameplay::ArmorMaterial::Gold, gameplay::ArmorSlot::Chestplate), 1});
    game::restorePlayer(after, *loaded.player);
    EXPECT_EQ(after.getArmorSlot(0), (physics::InventorySlot{helmet, 1, 41}));
    EXPECT_TRUE(after.getArmorSlot(1).isEmpty()) << "a missing key leaves the slot empty";
    EXPECT_TRUE(after.getArmorSlot(2).isEmpty());
    EXPECT_EQ(after.getArmorSlot(3), (physics::InventorySlot{boots, 1}));
    EXPECT_EQ(after.armorValue(), before.armorValue());
}

TEST(PersistenceIntegrationTest, LevelDatWithoutArmorKeysLoads) {
    world::WorldLevelData data{};
    ASSERT_EQ(world::decodeLevelData("BETACRAFT_LEVEL_V2\nitems_scheme 2\nseed 5\nplayer.health 12\ninv 0 1 3 0\n", data),
              world::LevelLoadStatus::Ok);
    ASSERT_TRUE(data.player.has_value());
    for (const world::SavedStack& s : data.player->armor) EXPECT_EQ(s, world::SavedStack{});

    physics::Player player;
    player.setArmorSlot(2, {gameplay::armorItemId(gameplay::ArmorMaterial::Leather, gameplay::ArmorSlot::Leggings), 1});
    game::restorePlayer(player, *data.player);
    for (size_t i = 0; i < physics::Player::ARMOR_SLOT_COUNT; ++i) EXPECT_TRUE(player.getArmorSlot(i).isEmpty()) << i;
    EXPECT_EQ(player.getHealth(), 12);

    // Malformed or misplaced armor lines never put a wrong item in a slot.
    ASSERT_EQ(world::decodeLevelData("BETACRAFT_LEVEL_V2\narmor 7 306 1 0\narmor 0 1 1 0\narmor 1 306 1 0\n"
                                     "armor 2 312 1 9999\narmor x\n",
                                     data),
              world::LevelLoadStatus::Ok);
    game::restorePlayer(player, *data.player);
    EXPECT_TRUE(player.getArmorSlot(0).isEmpty()) << "stone is no helmet";
    EXPECT_TRUE(player.getArmorSlot(1).isEmpty()) << "a helmet is no chestplate";
    EXPECT_EQ(player.getArmorSlot(2).damage, 360) << "wear clamped to maxDamage";
}
