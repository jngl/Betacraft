// Sprint 7 integration (S7.4): the frozen pre-Sprint 7 save tests/data/saves/v1, migrated to
// region files, loads exactly like the same save read by the legacy code path: blocks,
// metadata, decoration (no tree pasted twice, the undecorated chunk keeps its plan), block
// entities and the player in level.dat.
#include "test_support/GLTestContext.hpp"

#include "tests/integration/support/WorldgenSupport.hpp"
#include "storage/SaveMigration.hpp"
#include "voxel/BlockEntityStore.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <filesystem>

using namespace betacraft;
using namespace betacraft::test::worldgen_support;
using voxel::Chunk;

namespace {

[[nodiscard]] std::filesystem::path copyReference(const test::TempDir& dir, const char* name) {
    const auto save = dir.path() / name;
    std::filesystem::copy(std::filesystem::path(BETACRAFT_TEST_DATA_DIR) / "saves" / "v1", save,
                          std::filesystem::copy_options::recursive);
    return save;
}

[[nodiscard]] int64_t firstMetadataDifference(const Chunk& a, const Chunk& b) noexcept {
    for (int32_t y = 0; y < Chunk::HEIGHT; ++y)
        for (int32_t z = 0; z < Chunk::DEPTH; ++z)
            for (int32_t x = 0; x < Chunk::WIDTH; ++x)
                if (a.getMetadata(x, y, z) != b.getMetadata(x, y, z)) return static_cast<int64_t>(Chunk::blockIndex(x, y, z));
    return -1;
}

} // namespace

TEST(StorageLegacyMigration, MigratedSaveLoadsIdenticalToLegacy) {
    voxel::BlockRegistry::initialize();
    test::TempDir dir("betacraft_s7_migration_");
    const auto legacySave = copyReference(dir, "legacy");
    const auto migratedSave = copyReference(dir, "migrated");

    const storage::MigrationReport report = storage::migrateLegacyChunks(migratedSave, {});
    ASSERT_TRUE(report.completed);
    ASSERT_EQ(report.migrated, 10U);

    // Legacy: the pre-Sprint 7 reader (region storage off, chunks/ files only).
    world::World legacy(kSeed, legacySave);
    legacy.setRegionStorageEnabled(false);
    world::World migrated(kSeed, migratedSave);
    ASSERT_FALSE(std::filesystem::exists(migratedSave / "chunks"));

    // Same load order (it decides which neighbour decorates first): 7x7 around the save.
    const auto area = squareRegion({0, 0}, 3);
    loadAll(legacy, area);
    loadAll(migrated, area);
    for (const core::ChunkCoord c : area) {
        const Chunk& a = *legacy.getChunk(c);
        const Chunk& b = *migrated.getChunk(c);
        EXPECT_EQ(firstBlockDifference(a, b), -1) << "chunk " << c.x << "," << c.z;
        EXPECT_EQ(firstMetadataDifference(a, b), -1) << "chunk " << c.x << "," << c.z;
        EXPECT_EQ(legacy.isChunkDecorated(c), migrated.isChunkDecorated(c)) << "chunk " << c.x << "," << c.z;
        EXPECT_EQ(countDecoration(a), countDecoration(b)) << "chunk " << c.x << "," << c.z;
    }
    EXPECT_EQ(migrated.getBlock(8, 120, 8), voxel::BlockId::Glass);
    EXPECT_EQ(migrated.getBlock(2 * 16 + 8, 120, 8), voxel::BlockId::Glass);

    ASSERT_EQ(migrated.blockEntities().size(), 2U);
    for (const core::BlockPos pos : {core::BlockPos{4, 121, 4}, core::BlockPos{6, 121, 4}}) {
        const voxel::BlockEntity* a = legacy.blockEntities().find(pos);
        const voxel::BlockEntity* b = migrated.blockEntities().find(pos);
        ASSERT_NE(a, nullptr);
        ASSERT_NE(b, nullptr);
        EXPECT_EQ(*a, *b);
    }
    EXPECT_EQ(std::get<voxel::ChestData>(*migrated.blockEntities().find({4, 121, 4})).slots[5],
              (voxel::StoredStack{265, 12, 0}));

    storage::WorldLevelData before;
    storage::WorldLevelData after;
    ASSERT_EQ(legacy.loadLevelDataStatus(before), storage::LevelLoadStatus::Ok);
    ASSERT_EQ(migrated.loadLevelDataStatus(after), storage::LevelLoadStatus::Ok);
    EXPECT_EQ(after.storageFormat, storage::kRegionStorageFormat);
    EXPECT_EQ(before.storageFormat, storage::kLegacyStorageFormat);
    ASSERT_TRUE(before.player.has_value());
    ASSERT_TRUE(after.player.has_value());
    EXPECT_EQ(after.player->inventory, before.player->inventory);
    EXPECT_EQ(after.player->health, 17);
    EXPECT_EQ(after.seed, before.seed);
}

// Without migration (it failed or has not run yet), the facade still reads the legacy files
// and writes edits to the regions, which take precedence from then on.
TEST(StorageLegacyMigration, UnmigratedSaveReadsThroughFacade) {
    voxel::BlockRegistry::initialize();
    test::TempDir dir("betacraft_s7_migration_");
    const auto save = copyReference(dir, "world");
    {
        world::World w(kSeed, save);
        loadAll(w, squareRegion({0, 0}, 2));
        EXPECT_EQ(w.getBlock(8, 120, 8), voxel::BlockId::Glass);
        EXPECT_EQ(w.blockEntities().size(), 2U);
        ASSERT_TRUE(w.setBlock(8, 121, 8, voxel::BlockId::Obsidian));
        w.saveModifiedChunks(storage::WorldLevelData{});
        EXPECT_EQ(w.storageFormat(), storage::kLegacyStorageFormat) << "chunks/ still to migrate";
        EXPECT_TRUE(w.needsStorageMigration(storage::kLegacyStorageFormat));
    }
    EXPECT_TRUE(std::filesystem::exists(save / "region" / "r.0.0.bcr"));
    const storage::MigrationReport r = storage::migrateLegacyChunks(save, {});
    ASSERT_TRUE(r.completed);
    EXPECT_EQ(r.skipped, 1U) << "the chunk saved to a region is newer than its .chk";
    EXPECT_EQ(r.migrated, 9U);

    world::World w(kSeed, save);
    loadAll(w, squareRegion({0, 0}, 2));
    EXPECT_EQ(w.getBlock(8, 121, 8), voxel::BlockId::Obsidian);
    EXPECT_EQ(w.getBlock(8, 120, 8), voxel::BlockId::Glass);
    EXPECT_EQ(w.blockEntities().size(), 2U);
}
