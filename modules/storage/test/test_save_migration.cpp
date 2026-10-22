// S7.4 migration of chunks/c_X_Z.{chk,dec,be} into region bundles, on the frozen
// pre-Sprint 7 save tests/data/saves/v1 (10 chunks, a chest and a furnace in chunk (0, 0)).
#include "StorageTestHelpers.hpp"
#include "storage/ChunkBundleCodec.hpp"
#include "storage/LegacyChunkStore.hpp"
#include "storage/RegionChunkStore.hpp"
#include "storage/SaveMigration.hpp"
#include "storage/WorldStorage.hpp"
#include "test_support/TempDir.hpp"

#include <gtest/gtest.h>

#include <map>
#include <stdexcept>

using namespace betacraft;
using storage::MigrationReport;

namespace {

constexpr size_t kReferenceChunks = 10;

[[nodiscard]] std::filesystem::path referenceSave() {
    return std::filesystem::path(BETACRAFT_TEST_DATA_DIR) / "saves" / "v1";
}

// A writable copy of the reference save.
[[nodiscard]] std::filesystem::path copyReference(const test::TempDir& dir) {
    const auto save = dir.path() / "world";
    std::filesystem::copy(referenceSave(), save, std::filesystem::copy_options::recursive);
    return save;
}

struct CoordLess {
    bool operator()(core::ChunkCoord a, core::ChunkCoord b) const noexcept {
        return a.x != b.x ? a.x < b.x : a.z < b.z;
    }
};
using BundleMap = std::map<core::ChunkCoord, storage::ChunkBundle, CoordLess>;

[[nodiscard]] BundleMap legacyBundles(const std::filesystem::path& save) {
    BundleMap out;
    const storage::LegacyChunkStore legacy(save);
    for (const core::ChunkCoord c : legacy.listChunks()) {
        out[c] = *legacy.readBundle(c);
    }
    return out;
}

void expectSameBundle(const storage::ChunkBundle& a, const storage::ChunkBundle& b, core::ChunkCoord c) {
    EXPECT_EQ(a.blocks, b.blocks) << c.x << "," << c.z;
    EXPECT_EQ(a.blockEntities, b.blockEntities) << c.x << "," << c.z;
    ASSERT_EQ(a.decoration.has_value(), b.decoration.has_value()) << c.x << "," << c.z;
    if (a.decoration) {
        EXPECT_EQ(a.decoration->decorated, b.decoration->decorated) << c.x << "," << c.z;
        EXPECT_EQ(a.decoration->plan, b.decoration->plan) << c.x << "," << c.z;
    }
}

[[nodiscard]] size_t regionChunkCount(const std::filesystem::path& save, const BundleMap& coords) {
    storage::RegionChunkStore regions(save / "region", true);
    size_t n = 0;
    for (const auto& [c, b] : coords) n += regions.contains(c) ? 1U : 0U;
    return n;
}

} // namespace

TEST(SaveMigration, MigratesChkDecAndBlockEntities) {
    test::TempDir dir("betacraft_migration_");
    const auto save = copyReference(dir);
    const BundleMap before = legacyBundles(save);
    ASSERT_EQ(before.size(), kReferenceChunks);
    ASSERT_FALSE(before.at({0, 0}).blockEntities.empty()) << "the reference save has a .be";
    ASSERT_TRUE(before.at({2, 0}).decoration.has_value());
    ASSERT_FALSE(before.at({2, 0}).decoration->decorated) << "and an undecorated chunk with its plan";

    std::vector<float> progress;
    const MigrationReport r = storage::migrateLegacyChunks(save, [&progress](float p) { progress.push_back(p); });
    EXPECT_TRUE(r.completed);
    EXPECT_EQ(r.migrated, kReferenceChunks);
    EXPECT_EQ(r.skipped, 0U);
    EXPECT_EQ(r.corrupt, 0U);
    ASSERT_FALSE(progress.empty());
    EXPECT_FLOAT_EQ(progress.back(), 1.0f);
    EXPECT_TRUE(std::is_sorted(progress.begin(), progress.end()));

    storage::RegionChunkStore regions(save / "region", true);
    for (const auto& [c, legacy] : before) {
        const auto bytes = regions.read(c);
        ASSERT_TRUE(bytes.has_value()) << c.x << "," << c.z;
        const auto bundle = storage::decodeBundle(*bytes);
        ASSERT_TRUE(bundle.has_value());
        expectSameBundle(*bundle, legacy, c);
    }
    // The old files are kept, byte for byte.
    for (const auto& entry : std::filesystem::directory_iterator(referenceSave() / "chunks")) {
        const auto kept = save / "chunks.migrated" / entry.path().filename();
        ASSERT_TRUE(std::filesystem::exists(kept)) << kept;
        EXPECT_EQ(test::storage::readBytes(kept), test::storage::readBytes(entry.path()));
    }
    EXPECT_FALSE(std::filesystem::exists(save / "chunks"));

    // WorldStorage reads them back from the regions.
    const storage::WorldStorage storage(save);
    const auto entities = storage.loadBlockEntities({0, 0});
    EXPECT_EQ(entities.size(), 2U) << "chest and furnace";
}

TEST(SaveMigration, ResumesAfterInterruption) {
    test::TempDir dir("betacraft_migration_");
    const auto save = copyReference(dir);
    const BundleMap before = legacyBundles(save);

    // Killed (here: an exception) after four chunks were written.
    int calls = 0;
    EXPECT_THROW((void)storage::migrateLegacyChunks(save,
                                                    [&calls](float) {
                                                        if (++calls == 5) throw std::runtime_error("interrupted");
                                                    }),
                 std::runtime_error);
    EXPECT_TRUE(std::filesystem::exists(save / "chunks")) << "nothing renamed";
    EXPECT_FALSE(std::filesystem::exists(save / "chunks.migrated"));
    EXPECT_EQ(regionChunkCount(save, before), 4U);
    EXPECT_EQ(test::storage::readText(save / "level.dat").find("storage_format"), std::string::npos);

    const MigrationReport r = storage::migrateLegacyChunks(save, {});
    EXPECT_TRUE(r.completed);
    EXPECT_EQ(r.skipped, 4U);
    EXPECT_EQ(r.migrated, kReferenceChunks - 4);
    EXPECT_EQ(regionChunkCount(save, before), kReferenceChunks);
    storage::RegionChunkStore regions(save / "region", true);
    for (const auto& [c, legacy] : before) {
        const auto bundle = storage::decodeBundle(*regions.read(c));
        ASSERT_TRUE(bundle.has_value());
        expectSameBundle(*bundle, legacy, c);
    }
}

TEST(SaveMigration, CorruptLegacyChunkCountedNotFatal) {
    test::TempDir dir("betacraft_migration_");
    const auto save = copyReference(dir);
    test::storage::writeText(save / "chunks" / "c_1_1.chk", "not a chunk at all");
    const BundleMap before = legacyBundles(save);

    const MigrationReport r = storage::migrateLegacyChunks(save, {});
    EXPECT_TRUE(r.completed);
    EXPECT_EQ(r.corrupt, 1U);
    EXPECT_EQ(r.migrated, kReferenceChunks - 1);
    storage::RegionChunkStore regions(save / "region", true);
    EXPECT_FALSE(regions.contains({1, 1}));
    EXPECT_TRUE(regions.contains({0, 0}));
    EXPECT_EQ(test::storage::readText(save / "chunks.migrated" / "c_1_1.chk"), "not a chunk at all")
        << "its bytes stay with the old files";
    (void)before;
}

TEST(SaveMigration, LegacyDirRenamedAndStorageFormatWrittenLast) {
    test::TempDir dir("betacraft_migration_");
    const auto save = copyReference(dir);
    const std::string original = test::storage::readText(save / "level.dat");

    bool sawFinalStep = false;
    const MigrationReport r = storage::migrateLegacyChunks(save, [&](float p) {
        if (p < 1.0f) {
            EXPECT_TRUE(std::filesystem::exists(save / "chunks")) << "renamed only after every chunk is flushed";
            return;
        }
        sawFinalStep = true;
        EXPECT_FALSE(std::filesystem::exists(save / "chunks"));
        EXPECT_TRUE(std::filesystem::is_directory(save / "chunks.migrated"));
        EXPECT_TRUE(std::filesystem::exists(save / "region" / "r.0.0.bcr"));
        EXPECT_TRUE(std::filesystem::exists(save / "region" / "r.-1.-1.bcr"));
        EXPECT_EQ(test::storage::readText(save / "level.dat"), original) << "storage_format comes last";
    });
    ASSERT_TRUE(r.completed);
    EXPECT_TRUE(sawFinalStep);

    const std::string updated = test::storage::readText(save / "level.dat");
    EXPECT_EQ(updated, original + "storage_format 2\n") << "every other key kept as it was";
    EXPECT_EQ(test::storage::readText(save / "level.dat_old"), original);
    storage::WorldLevelData level;
    ASSERT_EQ(storage::decodeLevelData(updated, level), storage::LevelLoadStatus::Ok);
    EXPECT_EQ(level.storageFormat, storage::kRegionStorageFormat);
    EXPECT_EQ(level.seed, 173173173ULL);
    ASSERT_TRUE(level.player.has_value());
    EXPECT_EQ(level.player->inventory[0], (storage::SavedStack{274, 1, 17}));

    // A level.dat from a newer engine refuses the migration and nothing changes.
    test::TempDir other("betacraft_migration_");
    const auto future = copyReference(other);
    test::storage::writeText(future / "level.dat", "BETACRAFT_LEVEL_V3\nseed 1\n");
    const MigrationReport refused = storage::migrateLegacyChunks(future, {});
    EXPECT_FALSE(refused.completed);
    EXPECT_EQ(refused.migrated, 0U);
    EXPECT_TRUE(std::filesystem::exists(future / "chunks"));
    EXPECT_FALSE(std::filesystem::exists(future / "region"));
    EXPECT_EQ(test::storage::readText(future / "level.dat"), "BETACRAFT_LEVEL_V3\nseed 1\n");
}

TEST(SaveMigration, IdempotentSecondRun) {
    test::TempDir dir("betacraft_migration_");
    const auto save = copyReference(dir);
    ASSERT_TRUE(storage::migrateLegacyChunks(save, {}).completed);
    const auto region = test::storage::readBytes(save / "region" / "r.0.0.bcr");
    const std::string level = test::storage::readText(save / "level.dat");

    const MigrationReport again = storage::migrateLegacyChunks(save, {});
    EXPECT_TRUE(again.completed);
    EXPECT_EQ(again.migrated + again.skipped + again.corrupt, 0U);
    EXPECT_EQ(test::storage::readBytes(save / "region" / "r.0.0.bcr"), region);
    EXPECT_EQ(test::storage::readText(save / "level.dat"), level);
    EXPECT_FALSE(std::filesystem::exists(save / "chunks.migrated.1"));

    // The facade sees nothing left to do either.
    storage::WorldStorage storage(save);
    EXPECT_FALSE(storage.needsMigration(storage::kLegacyStorageFormat));
    EXPECT_EQ(storage.storageFormat(), storage::kRegionStorageFormat);

    // A V1 level.dat is migrated too but not rewritten (the game writes V2 on its next save).
    test::TempDir v1dir("betacraft_migration_");
    const auto v1 = copyReference(v1dir);
    const std::string kV1 = "BETACRAFT_LEVEL_V1\n173173173\n8.5 122 8.5\n-90 0\n0.25\n";
    test::storage::writeText(v1 / "level.dat", kV1);
    const MigrationReport r = storage::migrateLegacyChunks(v1, {});
    EXPECT_TRUE(r.completed);
    EXPECT_EQ(r.migrated, kReferenceChunks);
    EXPECT_EQ(test::storage::readText(v1 / "level.dat"), kV1);
}
