#include "storage/WorldStorage.hpp"
#include "test_support/TempDir.hpp"
#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

using namespace betacraft::storage;
using namespace betacraft::voxel;
using namespace testing;

TEST(WorldStorageTest, DirectoryAndLevelDataPersistence) {
    betacraft::test::TempDir tempDir;
    WorldStorage storage(tempDir.path());
    EXPECT_TRUE(storage.ensureDirectoryExists());

    WorldLevelData writeData{
        .seed = 987654321ULL,
        .playerPos = {12.5f, 75.0f, -8.25f},
        .playerYaw = 180.0f,
        .playerPitch = -15.0f,
        .timeOfDay = 0.65f
    };
    EXPECT_TRUE(storage.saveLevelData(writeData));

    WorldLevelData readData{};
    EXPECT_TRUE(storage.loadLevelData(readData));
    EXPECT_EQ(readData.seed, writeData.seed);
    EXPECT_FLOAT_EQ(readData.playerPos.x, writeData.playerPos.x);
    EXPECT_FLOAT_EQ(readData.playerPos.y, writeData.playerPos.y);
    EXPECT_FLOAT_EQ(readData.playerPos.z, writeData.playerPos.z);
    EXPECT_FLOAT_EQ(readData.playerYaw, writeData.playerYaw);
    EXPECT_FLOAT_EQ(readData.playerPitch, writeData.playerPitch);
    EXPECT_FLOAT_EQ(readData.timeOfDay, writeData.timeOfDay);
}

TEST(WorldStorageTest, ChunkPersistenceRoundtrip) {
    BlockRegistry::initialize();
    betacraft::test::TempDir tempDir;
    WorldStorage storage(tempDir.path());

    Chunk c1({-4, 7});
    c1.setBlock(3, 10, 3, BlockId::Obsidian);
    c1.setBlock(10, 70, 10, BlockId::Torch);
    EXPECT_TRUE(storage.saveChunk(c1));

    Chunk c2({-4, 7});
    EXPECT_TRUE(storage.loadChunk(c2));
    EXPECT_EQ(c2.getBlock(3, 10, 3), BlockId::Obsidian);
    EXPECT_EQ(c2.getBlock(10, 70, 10), BlockId::Torch);

    // Non-existent chunk
    Chunk c3({99, -99});
    EXPECT_FALSE(storage.loadChunk(c3));
}

// ---------------------------------------------------------------------------------------
// Sprint 1 (S1.7): level.dat V2, versioning, atomic writes and chunk quarantine.
// ---------------------------------------------------------------------------------------

namespace {

[[nodiscard]] std::string readAll(const std::filesystem::path& path) {
    std::ifstream ifs(path, std::ios::binary);
    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

void writeAll(const std::filesystem::path& path, std::string_view text) {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    ofs << text;
}

[[nodiscard]] WorldLevelData sampleV2() {
    WorldLevelData data{};
    data.seed = 987654321ULL;
    data.playerPos = {12.5f, 75.0f, -8.25f};
    data.playerYaw = 180.0f;
    data.playerPitch = -15.0f;
    data.timeOfDay = 0.65f;
    data.spawnPos = glm::vec3(8.5f, 70.01f, 8.5f);
    PlayerSaveData player{};
    player.mode = betacraft::core::GameMode::Creative;
    player.health = 13;
    player.air = 120;
    player.selectedSlot = 4;
    player.fallDistance = 2.5f;
    player.inventory[0] = {103, 1, 12};
    player.inventory[9] = {17, 32, 0};
    player.inventory[35] = {268, 1, 40};
    data.player = player;
    return data;
}

} // namespace

TEST(WorldStorageTest, LevelDataV2RoundTripsPlayerSpawnAndMode) {
    betacraft::test::TempDir tempDir;
    WorldStorage storage(tempDir.path());
    const WorldLevelData written = sampleV2();
    ASSERT_TRUE(storage.saveLevelData(written));
    EXPECT_EQ(readAll(tempDir.path() / "level.dat").rfind("BETACRAFT_LEVEL_V2\n", 0), 0U);

    WorldLevelData read{};
    ASSERT_EQ(storage.loadLevelDataStatus(read), LevelLoadStatus::Ok);
    EXPECT_EQ(read.seed, written.seed);
    EXPECT_FLOAT_EQ(read.playerPos.x, 12.5f);
    EXPECT_FLOAT_EQ(read.playerPos.y, 75.0f);
    EXPECT_FLOAT_EQ(read.playerPos.z, -8.25f);
    EXPECT_FLOAT_EQ(read.playerYaw, 180.0f);
    EXPECT_FLOAT_EQ(read.playerPitch, -15.0f);
    EXPECT_FLOAT_EQ(read.timeOfDay, 0.65f);
    EXPECT_EQ(read.itemsScheme, 1U);
    ASSERT_TRUE(read.spawnPos.has_value());
    EXPECT_FLOAT_EQ(read.spawnPos->y, 70.01f);
    ASSERT_TRUE(read.player.has_value());
    EXPECT_EQ(read.player->mode, betacraft::core::GameMode::Creative);
    EXPECT_EQ(read.player->health, 13);
    EXPECT_EQ(read.player->air, 120);
    EXPECT_EQ(read.player->selectedSlot, 4);
    EXPECT_FLOAT_EQ(read.player->fallDistance, 2.5f);
    EXPECT_EQ(read.player->inventory, written.player->inventory);

    // The bool overload kept for older callers reports success too.
    WorldLevelData again{};
    EXPECT_TRUE(storage.loadLevelData(again));
}

TEST(WorldStorageTest, LegacyV1LevelLoadsAsLegacy) {
    constexpr std::string_view kV1 = "BETACRAFT_LEVEL_V1\n173173\n5.5 77 5.5\n45 10\n0.35\n";
    WorldLevelData read{};
    ASSERT_EQ(decodeLevelData(kV1, read), LevelLoadStatus::LegacyV1);
    EXPECT_EQ(read.seed, 173173ULL);
    EXPECT_FLOAT_EQ(read.playerPos.y, 77.0f);
    EXPECT_FLOAT_EQ(read.playerYaw, 45.0f);
    EXPECT_FLOAT_EQ(read.playerPitch, 10.0f);
    EXPECT_FLOAT_EQ(read.timeOfDay, 0.35f);
    EXPECT_FALSE(read.player.has_value());
    EXPECT_FALSE(read.spawnPos.has_value());

    betacraft::test::TempDir tempDir;
    WorldStorage storage(tempDir.path());
    ASSERT_TRUE(storage.ensureDirectoryExists());
    writeAll(tempDir.path() / "level.dat", kV1);
    WorldLevelData fromDisk{};
    EXPECT_EQ(storage.loadLevelDataStatus(fromDisk), LevelLoadStatus::LegacyV1);
    EXPECT_TRUE(storage.loadLevelData(fromDisk));

    WorldLevelData truncated{};
    EXPECT_EQ(decodeLevelData("BETACRAFT_LEVEL_V1\n173173\n5.5 77\n", truncated), LevelLoadStatus::Corrupt);
    EXPECT_EQ(decodeLevelData("garbage", truncated), LevelLoadStatus::Corrupt);
    betacraft::test::TempDir empty;
    EXPECT_EQ(WorldStorage(empty.path()).loadLevelDataStatus(truncated), LevelLoadStatus::Missing);
}

TEST(WorldStorageTest, UnknownKeysAreIgnored) {
    const std::string text = encodeLevelData(sampleV2()) + "weather.rain 1\nfuture.key a b c\n";
    WorldLevelData read{};
    ASSERT_EQ(decodeLevelData("BETACRAFT_LEVEL_V2\nzzz 1\n" + text.substr(text.find('\n') + 1), read),
              LevelLoadStatus::Ok);
    EXPECT_EQ(read.seed, 987654321ULL);
    ASSERT_TRUE(read.player.has_value());
    EXPECT_EQ(read.player->health, 13);
    EXPECT_EQ(read.player->inventory[9], (SavedStack{17, 32, 0}));
}

TEST(WorldStorageTest, MissingKeysKeepDefaults) {
    WorldLevelData read{};
    ASSERT_EQ(decodeLevelData("BETACRAFT_LEVEL_V2\nseed 42\nplayer.health 7\n", read), LevelLoadStatus::Ok);
    const WorldLevelData defaults{};
    EXPECT_EQ(read.seed, 42ULL);
    EXPECT_FLOAT_EQ(read.timeOfDay, defaults.timeOfDay);
    EXPECT_FLOAT_EQ(read.playerPos.y, defaults.playerPos.y);
    EXPECT_FLOAT_EQ(read.playerYaw, defaults.playerYaw);
    EXPECT_EQ(read.itemsScheme, 1U);
    EXPECT_FALSE(read.spawnPos.has_value());
    ASSERT_TRUE(read.player.has_value());
    const PlayerSaveData playerDefaults{};
    EXPECT_EQ(read.player->health, 7);
    EXPECT_EQ(read.player->mode, playerDefaults.mode);
    EXPECT_EQ(read.player->air, playerDefaults.air);
    EXPECT_EQ(read.player->selectedSlot, 0);
    EXPECT_EQ(read.player->inventory, playerDefaults.inventory);

    WorldLevelData bare{};
    ASSERT_EQ(decodeLevelData("BETACRAFT_LEVEL_V2\n", bare), LevelLoadStatus::Ok);
    EXPECT_FALSE(bare.player.has_value());
    EXPECT_EQ(bare.seed, defaults.seed);
}

TEST(WorldStorageTest, TooNewVersionIsRefused) {
    betacraft::test::TempDir tempDir;
    WorldStorage storage(tempDir.path());
    ASSERT_TRUE(storage.ensureDirectoryExists());
    constexpr std::string_view kV3 = "BETACRAFT_LEVEL_V3\nseed 1\nwhatever changed meaning\n";
    writeAll(tempDir.path() / "level.dat", kV3);

    WorldLevelData read{};
    EXPECT_EQ(storage.loadLevelDataStatus(read), LevelLoadStatus::TooNew);
    EXPECT_FALSE(storage.loadLevelData(read));
    EXPECT_EQ(read.seed, WorldLevelData{}.seed) << "nothing is read from a newer format";
    EXPECT_EQ(readAll(tempDir.path() / "level.dat"), kV3) << "loading never rewrites the file";
}

TEST(WorldStorageTest, MalformedInventoryLineIsSkipped) {
    constexpr std::string_view kText =
        "BETACRAFT_LEVEL_V2\n"
        "inv 0 103 1 12\n"
        "inv 1 4\n"            // Too few fields
        "inv 2 4 x 0\n"        // Not a number
        "inv 36 4 1 0\n"       // Slot out of range
        "inv -1 4 1 0\n"
        "inv 3 70000 1 0\n"    // Id does not fit an ItemId
        "inv 4 4 1 0 extra\n"  // Trailing garbage
        "inv 5 5 64 0\n";
    WorldLevelData read{};
    ASSERT_EQ(decodeLevelData(kText, read), LevelLoadStatus::Ok);
    ASSERT_TRUE(read.player.has_value());
    EXPECT_EQ(read.player->inventory[0], (SavedStack{103, 1, 12}));
    for (size_t slot = 1; slot <= 4; ++slot) {
        EXPECT_EQ(read.player->inventory[slot], SavedStack{}) << "slot " << slot;
    }
    EXPECT_EQ(read.player->inventory[5], (SavedStack{5, 64, 0}));
}

TEST(WorldStorageTest, SaveWritesBackupAndIsAtomic) {
    betacraft::test::TempDir tempDir;
    WorldStorage storage(tempDir.path());
    ASSERT_TRUE(storage.ensureDirectoryExists());
    const auto dir = tempDir.path();
    constexpr std::string_view kV1 = "BETACRAFT_LEVEL_V1\n173173\n5.5 77 5.5\n45 10\n0.35\n";
    writeAll(dir / "level.dat", kV1);

    // First save over V1: the V1 file is kept once as level.dat.v1.bak.
    WorldLevelData first = sampleV2();
    ASSERT_TRUE(storage.saveLevelData(first));
    EXPECT_EQ(readAll(dir / "level.dat.v1.bak"), kV1);
    EXPECT_EQ(readAll(dir / "level.dat_old"), kV1);

    WorldLevelData second = sampleV2();
    second.seed = 1234ULL;
    ASSERT_TRUE(storage.saveLevelData(second));
    EXPECT_EQ(readAll(dir / "level.dat.v1.bak"), kV1) << "the V1 backup is written only once";
    EXPECT_EQ(readAll(dir / "level.dat_old"), encodeLevelData(first));
    EXPECT_EQ(readAll(dir / "level.dat"), encodeLevelData(second));
    EXPECT_FALSE(std::filesystem::exists(dir / "level.dat.tmp"));

    for (const auto& entry : std::filesystem::directory_iterator(dir)) {
        EXPECT_NE(entry.path().extension(), ".tmp") << entry.path();
    }
}

TEST(WorldStorageTest, UnreadableChunkIsQuarantined) {
    BlockRegistry::initialize();
    betacraft::test::TempDir tempDir;
    WorldStorage storage(tempDir.path());
    ASSERT_TRUE(storage.ensureDirectoryExists());
    const auto chunkFile = tempDir.path() / "chunks" / "c_3_-2.chk";
    writeAll(chunkFile, "definitely not RLE");

    Chunk c({3, -2});
    EXPECT_FALSE(storage.loadChunk(c));
    EXPECT_FALSE(std::filesystem::exists(chunkFile));
    const auto quarantined = tempDir.path() / "chunks" / "c_3_-2.chk.corrupt";
    ASSERT_TRUE(std::filesystem::exists(quarantined));
    EXPECT_EQ(readAll(quarantined), "definitely not RLE");

    // A chunk without a file is simply absent: nothing is quarantined.
    Chunk missing({50, 50});
    EXPECT_FALSE(storage.loadChunk(missing));
    EXPECT_FALSE(std::filesystem::exists(tempDir.path() / "chunks" / "c_50_50.chk.corrupt"));
}

// Conventions §1.2: a session that refused level.dat (too new / unreadable) is read-only.
// Nothing is written, and a chunk it cannot read (maybe a newer format) is not quarantined.
TEST(WorldStorageTest, ReadOnlyStorageNeverWritesNorQuarantines) {
    BlockRegistry::initialize();
    betacraft::test::TempDir tempDir;
    WorldStorage storage(tempDir.path());
    ASSERT_TRUE(storage.ensureDirectoryExists());
    const auto chunkFile = tempDir.path() / "chunks" / "c_1_1.chk";
    writeAll(chunkFile, "a chunk from the future");
    storage.setReadOnly(true);
    ASSERT_TRUE(storage.isReadOnly());

    Chunk c({1, 1});
    EXPECT_FALSE(storage.loadChunk(c));
    EXPECT_EQ(readAll(chunkFile), "a chunk from the future") << "left in place";
    EXPECT_FALSE(std::filesystem::exists(tempDir.path() / "chunks" / "c_1_1.chk.corrupt"));

    Chunk other({2, 2});
    EXPECT_FALSE(storage.saveChunk(other));
    EXPECT_FALSE(storage.saveLevelData(WorldLevelData{}));
    EXPECT_FALSE(std::filesystem::exists(tempDir.path() / "level.dat"));
    EXPECT_FALSE(std::filesystem::exists(tempDir.path() / "chunks" / "c_2_2.chk"));
}

// S2.4: the .be block entity codec (conventions §1, kBlockEntityFormatVersion = 1).
TEST(WorldStorageTest, BlockEntityCodecRoundTrip) {
    const betacraft::core::ChunkCoord coord{-2, 3};
    FurnaceData furnace{};
    furnace.input = {15, 7, 0};
    furnace.fuel = {101, 3, 0};
    furnace.output = {265, 1, 0};
    furnace.burnTicks = 1234;
    furnace.burnTicksTotal = 1600;
    furnace.cookTicks = 77;
    ChestData chest{};
    chest.slots[0] = {4, 64, 0};
    chest.slots[13] = {104, 1, 42};
    chest.slots[26] = {264, 5, 0};
    const std::vector<BlockEntityEntry> entries{
        {{-32 + 5, 64, 48 + 9}, furnace},
        {{-32 + 15, 0, 48 + 0}, chest},
        {{-32 + 0, 127, 48 + 15}, FurnaceData{}},
    };
    const std::vector<uint8_t> bytes = encodeBlockEntities(entries);
    const auto decoded = decodeBlockEntities(coord, bytes);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), entries.size());
    for (size_t i = 0; i < entries.size(); ++i) {
        EXPECT_EQ((*decoded)[i].first, entries[i].first) << i;
        EXPECT_EQ((*decoded)[i].second, entries[i].second) << i;
    }
    const auto none = decodeBlockEntities(coord, encodeBlockEntities({}));
    ASSERT_TRUE(none.has_value());
    EXPECT_TRUE(none->empty());
}

TEST(WorldStorageTest, BlockEntityCodecRejectsTruncatedAndBadMagic) {
    const betacraft::core::ChunkCoord coord{0, 0};
    ChestData chest{};
    chest.slots[3] = {5, 10, 0};
    const std::vector<BlockEntityEntry> entries{{{1, 2, 3}, chest}, {{4, 5, 6}, FurnaceData{}}};
    const std::vector<uint8_t> bytes = encodeBlockEntities(entries);
    ASSERT_TRUE(decodeBlockEntities(coord, bytes).has_value());
    for (size_t len = 0; len < bytes.size(); ++len) {
        EXPECT_FALSE(decodeBlockEntities(coord, std::span(bytes.data(), len)).has_value()) << len;
    }
    std::vector<uint8_t> badMagic = bytes;
    badMagic[0] = 'X';
    EXPECT_FALSE(decodeBlockEntities(coord, badMagic).has_value());
    std::vector<uint8_t> tooNew = bytes;
    tooNew[4] = static_cast<uint8_t>(kBlockEntityFormatVersion + 1);
    EXPECT_FALSE(decodeBlockEntities(coord, tooNew).has_value());
    std::vector<uint8_t> badKind = bytes;
    badKind[7 + 3] = 9; // First entry's kind byte
    EXPECT_FALSE(decodeBlockEntities(coord, badKind).has_value());
    std::vector<uint8_t> trailing = bytes;
    trailing.push_back(0);
    EXPECT_FALSE(decodeBlockEntities(coord, trailing).has_value());
}

TEST(WorldStorageTest, BlockEntityV1GoldenBytesDecode) {
    // Frozen v1 file (conventions §1.8): one furnace at local (2, 70, 9) of chunk (1, -1)
    // with 5 iron ore, 1 coal, 3 iron ingots, burn 900/1600, cook 150.
    const std::vector<uint8_t> golden{
        'B', 'C', 'B', 'E', 0x01, 0x01, 0x00, // magic, version 1, 1 entry
        0x02, 0x46, 0x09, 0x01,               // lx 2, y 70, lz 9, kind furnace
        0x0F, 0x00, 0x05, 0x00, 0x00,         // input: id 15 x5
        0x65, 0x00, 0x01, 0x00, 0x00,         // fuel: id 101 x1
        0x09, 0x01, 0x03, 0x00, 0x00,         // output: id 265 x3
        0x84, 0x03, 0x40, 0x06, 0x96, 0x00,   // burn 900, total 1600, cook 150
    };
    const auto decoded = decodeBlockEntities({1, -1}, golden);
    ASSERT_TRUE(decoded.has_value());
    ASSERT_EQ(decoded->size(), 1U);
    EXPECT_EQ((*decoded)[0].first, (betacraft::core::BlockPos{16 + 2, 70, -16 + 9}));
    const auto* f = std::get_if<FurnaceData>(&(*decoded)[0].second);
    ASSERT_NE(f, nullptr);
    EXPECT_EQ(f->input, (StoredStack{15, 5, 0}));
    EXPECT_EQ(f->fuel, (StoredStack{101, 1, 0}));
    EXPECT_EQ(f->output, (StoredStack{265, 3, 0}));
    EXPECT_EQ(f->burnTicks, 900);
    EXPECT_EQ(f->burnTicksTotal, 1600);
    EXPECT_EQ(f->cookTicks, 150);
    // The encoder still writes exactly these bytes.
    EXPECT_EQ(encodeBlockEntities(*decoded), golden);
}
