// S7.3 WorldStorage as a facade over RegionChunkStore (writes) and LegacyChunkStore (reads
// of saves not migrated yet): the pre-Sprint 7 wrappers share one bundle per chunk, broken
// region entries are quarantined, and a read-only session touches nothing.
#include "StorageTestHelpers.hpp"
#include "storage/RegionChunkStore.hpp"
#include "storage/RegionFile.hpp"
#include "storage/WorldStorage.hpp"
#include "tests/test_helpers.hpp"

#include <gtest/gtest.h>

using namespace betacraft;
using world::BlockId;
using world::Chunk;
using storage::WorldStorage;

namespace {

[[nodiscard]] Chunk sampleChunk(core::ChunkCoord c) {
    world::BlockRegistry::initialize();
    Chunk chunk(c);
    chunk.setBlock(2, 40, 3, BlockId::Obsidian);
    return chunk;
}

// Replaces the stored entry of `c` with bytes that are no bundle.
void breakRegionEntry(const std::filesystem::path& save, core::ChunkCoord c) {
    const auto [rx, rz] = storage::RegionChunkStore::regionOf(c);
    auto file = storage::RegionFile::open(storage::RegionChunkStore::regionPath(save / "region", rx, rz), false);
    ASSERT_NE(file, nullptr);
    ASSERT_TRUE(file->write(core::floorMod(c.x, 32), core::floorMod(c.z, 32), std::vector<uint8_t>{0x09, 0xFF, 0xFF}, 1));
    ASSERT_TRUE(file->flush());
}

} // namespace

TEST(WorldStorageFacade, WrappersShareOneBundle) {
    test::TempDir dir("betacraft_facade_");
    const core::ChunkCoord c{-33, 70}; // Region (-2, 2)
    world::ChestData chest{};
    chest.slots[1] = {265, 5, 0};
    const std::vector<world::BlockEntityEntry> entities{{core::BlockPos{-33 * 16 + 1, 50, 70 * 16 + 1}, chest}};
    {
        const storage::WorldStorage storage(dir.path());
        ASSERT_TRUE(storage.saveChunk(sampleChunk(c)));
        ASSERT_TRUE(storage.saveDecorationRecord(c, worldgen::DecorationRecord{false, {}}));
        ASSERT_TRUE(storage.saveBlockEntities(c, entities));
        // Durable at once, as the separate files were: another reader sees all three now.
        const storage::WorldStorage reader(dir.path());
        Chunk loaded(c);
        EXPECT_TRUE(reader.loadChunk(loaded));
        EXPECT_EQ(loaded.getBlock(2, 40, 3), BlockId::Obsidian);
    }
    EXPECT_TRUE(std::filesystem::exists(dir.path() / "region" / "r.-2.2.bcr"));
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "chunks"));

    const storage::WorldStorage storage(dir.path());
    const auto bundle = storage.loadChunkBundle(c);
    ASSERT_TRUE(bundle.has_value());
    EXPECT_FALSE(bundle->blocks.empty());
    ASSERT_TRUE(bundle->decoration.has_value());
    EXPECT_FALSE(bundle->decoration->decorated);
    const auto loadedEntities = storage.loadBlockEntities(c);
    ASSERT_EQ(loadedEntities.size(), 1U);
    EXPECT_EQ(std::get<world::ChestData>(loadedEntities.front().second), chest);

    // Saving the blocks again keeps the other two parts.
    ASSERT_TRUE(storage.saveChunk(sampleChunk(c)));
    EXPECT_EQ(storage.loadBlockEntities(c).size(), 1U);
    EXPECT_TRUE(storage.loadDecorationRecord(c).has_value());
    // No entities: the record goes.
    ASSERT_TRUE(storage.saveBlockEntities(c, {}));
    EXPECT_TRUE(storage.loadChunkBundle(c)->blockEntities.empty());
}

TEST(WorldStorageFacade, CorruptRegionEntryQuarantinedThenLegacyRead) {
    test::TempDir dir("betacraft_facade_");
    const core::ChunkCoord c{1, 2};
    {
        storage::WorldStorage storage(dir.path());
        ASSERT_TRUE(storage.saveChunk(sampleChunk(c)));
        // An older copy still in chunks/ (save not migrated yet).
        storage.setRegionStorageEnabled(false);
        EXPECT_TRUE(storage.isRegionStorageActive()) << "region/ exists: never back to legacy writes";
    }
    {
        storage::WorldStorage legacyWriter(dir.path() / "legacy_only");
        legacyWriter.setRegionStorageEnabled(false);
        ASSERT_FALSE(legacyWriter.isRegionStorageActive());
        Chunk older = sampleChunk(c);
        older.setBlock(5, 5, 5, BlockId::Glass);
        ASSERT_TRUE(legacyWriter.saveChunk(older));
        EXPECT_TRUE(std::filesystem::exists(dir.path() / "legacy_only" / "chunks" / "c_1_2.chk"));
        EXPECT_FALSE(std::filesystem::exists(dir.path() / "legacy_only" / "region"));
        std::filesystem::create_directories(dir.path() / "chunks");
        std::filesystem::copy_file(dir.path() / "legacy_only" / "chunks" / "c_1_2.chk", dir.path() / "chunks" / "c_1_2.chk");
    }
    breakRegionEntry(dir.path(), c);

    const storage::WorldStorage storage(dir.path());
    Chunk loaded(c);
    ASSERT_TRUE(storage.loadChunk(loaded)) << "falls back to the legacy copy";
    EXPECT_EQ(loaded.getBlock(5, 5, 5), BlockId::Glass);
    size_t quarantined = 0;
    for (const auto& e : std::filesystem::directory_iterator(dir.path() / "region" / "quarantine")) {
        (void)e;
        ++quarantined;
    }
    EXPECT_EQ(quarantined, 1U) << "the broken entry's bytes are kept";
    storage::RegionChunkStore regions(dir.path() / "region", true);
    EXPECT_FALSE(regions.contains(c)) << "and the entry is gone, so it is not read again";
}

TEST(WorldStorageFacade, ReadOnlyFacadeNeverTouchesRegions) {
    test::TempDir dir("betacraft_facade_");
    const core::ChunkCoord good{0, 0};
    const core::ChunkCoord bad{3, 0};
    {
        const storage::WorldStorage storage(dir.path());
        ASSERT_TRUE(storage.saveChunk(sampleChunk(good)));
        ASSERT_TRUE(storage.saveChunk(sampleChunk(bad)));
    }
    breakRegionEntry(dir.path(), bad);
    const auto regionPath = dir.path() / "region" / "r.0.0.bcr";
    const auto before = test::storage::readBytes(regionPath);

    storage::WorldStorage storage(dir.path());
    storage.setReadOnly(true);
    Chunk ok(good);
    EXPECT_TRUE(storage.loadChunk(ok));
    Chunk broken(bad);
    EXPECT_FALSE(storage.loadChunk(broken));
    EXPECT_FALSE(storage.saveChunk(sampleChunk({5, 5})));
    EXPECT_FALSE(storage.saveChunkBundle({6, 6}, storage::ChunkBundle{sampleChunk({6, 6}).serializeRLE(), {}, {}}));
    EXPECT_FALSE(storage.saveDecorationRecord(good, worldgen::DecorationRecord{}));
    EXPECT_FALSE(storage.saveBlockEntities(good, {}));
    EXPECT_FALSE(storage.saveChunkBundle({40, 40}, storage::ChunkBundle{}));
    EXPECT_TRUE(storage.flush());
    EXPECT_TRUE(storage.closeRegions());
    EXPECT_EQ(test::storage::readBytes(regionPath), before);
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "region" / "quarantine"));
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "region" / "r.1.1.bcr"));
    EXPECT_FALSE(storage.needsMigration(storage::kLegacyStorageFormat));
    EXPECT_FALSE(storage.migrateLegacyChunks({}).completed);
}

TEST(WorldStorageFacade, StorageFormatKeyRoundTrips) {
    storage::WorldLevelData data{};
    EXPECT_EQ(storage::encodeLevelData(data).find("storage_format"), std::string::npos)
        << "format 1 is the absence of the key";
    data.storageFormat = storage::kRegionStorageFormat;
    storage::WorldLevelData out{};
    ASSERT_EQ(storage::decodeLevelData(storage::encodeLevelData(data), out), storage::LevelLoadStatus::Ok);
    EXPECT_EQ(out.storageFormat, storage::kRegionStorageFormat);
    ASSERT_EQ(storage::decodeLevelData("BETACRAFT_LEVEL_V2\nseed 5\n", out), storage::LevelLoadStatus::Ok);
    EXPECT_EQ(out.storageFormat, storage::kLegacyStorageFormat);
}
