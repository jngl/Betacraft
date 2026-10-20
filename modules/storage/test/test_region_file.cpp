// S7.1 RegionFile: 32x32 chunks in 4 KiB sectors, an 8 KiB header (offsets + timestamps)
// rewritten only by flush(), after the data.
#include "StorageTestHelpers.hpp"
#include "storage/RegionFile.hpp"
#include "test_support/TempDir.hpp"

#include <gtest/gtest.h>

#include <filesystem>

using namespace betacraft;
using storage::RegionFile;
using test::storage::noise;

namespace {

constexpr size_t kSector = RegionFile::kSectorBytes;

// Overwrites header entry (localX, localZ) of the file on disk.
void patchEntry(const std::filesystem::path& path, int32_t lx, int32_t lz, uint32_t offset, uint32_t count) {
    std::vector<uint8_t> bytes = test::storage::readBytes(path);
    const size_t at = static_cast<size_t>(lx + lz * RegionFile::kSize) * 4;
    const uint32_t packed = (offset << 8) | count;
    bytes[at] = static_cast<uint8_t>(packed >> 24);
    bytes[at + 1] = static_cast<uint8_t>(packed >> 16);
    bytes[at + 2] = static_cast<uint8_t>(packed >> 8);
    bytes[at + 3] = static_cast<uint8_t>(packed);
    test::storage::writeBytes(path, bytes);
}

} // namespace

TEST(RegionFile, WriteReadRoundtripAllCorners) {
    test::TempDir dir("betacraft_region_");
    const auto path = dir.path() / "r.0.0.bcr";
    const std::pair<int32_t, int32_t> corners[] = {{0, 0}, {31, 0}, {0, 31}, {31, 31}};
    {
        auto file = RegionFile::open(path, true);
        ASSERT_NE(file, nullptr);
        EXPECT_FALSE(file->isReadOnly());
        uint32_t seed = 1;
        for (const auto& [x, z] : corners) {
            ASSERT_TRUE(file->write(x, z, noise(3000 + seed * 2500, seed), 100 + seed));
            ++seed;
        }
        seed = 1;
        for (const auto& [x, z] : corners) {
            EXPECT_EQ(file->read(x, z), noise(3000 + seed * 2500, seed)) << x << "," << z;
            ++seed;
        }
        EXPECT_FALSE(file->write(32, 0, noise(10, 9), 1));
        EXPECT_FALSE(file->write(0, -1, noise(10, 9), 1));
        EXPECT_FALSE(file->read(-1, 0).has_value());
        EXPECT_FALSE(file->read(5, 5).has_value());
        ASSERT_TRUE(file->flush());
    }
    EXPECT_EQ(std::filesystem::file_size(path) % kSector, 0U);

    auto again = RegionFile::open(path, false);
    ASSERT_NE(again, nullptr);
    uint32_t seed = 1;
    for (const auto& [x, z] : corners) {
        EXPECT_EQ(again->read(x, z), noise(3000 + seed * 2500, seed)) << x << "," << z;
        ++seed;
    }
    EXPECT_FALSE(again->contains(5, 5));
    EXPECT_EQ(RegionFile::open(dir.path() / "missing.bcr", false), nullptr) << "never created without `create`";
    EXPECT_FALSE(std::filesystem::exists(dir.path() / "missing.bcr"));
}

TEST(RegionFile, RewriteSmallerReusesSectors) {
    test::TempDir dir("betacraft_region_");
    const auto path = dir.path() / "r.0.0.bcr";
    auto file = RegionFile::open(path, true);
    ASSERT_NE(file, nullptr);
    ASSERT_TRUE(file->write(3, 4, noise(3 * kSector, 7), 1));
    const RegionFile::Entry big = file->entry(3, 4);
    EXPECT_EQ(big.sectorCount, 4U); // 12288 bytes + the 5-byte chunk header
    const uint32_t sectors = file->fileSectorCount();

    ASSERT_TRUE(file->write(3, 4, noise(100, 8), 2));
    const RegionFile::Entry small = file->entry(3, 4);
    EXPECT_EQ(small.sectorOffset, big.sectorOffset) << "rewritten in place";
    EXPECT_EQ(small.sectorCount, 1U);
    EXPECT_EQ(file->fileSectorCount(), sectors) << "the file did not grow";
    EXPECT_EQ(file->read(3, 4), noise(100, 8));

    ASSERT_TRUE(file->flush());
    file.reset();
    auto again = RegionFile::open(path, false);
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(again->read(3, 4), noise(100, 8));
    EXPECT_EQ(again->entry(3, 4), small);
}

TEST(RegionFile, RewriteLargerRelocatesAndOldDataStillReadableUntilHeaderUpdate) {
    test::TempDir dir("betacraft_region_");
    const auto path = dir.path() / "r.0.0.bcr";
    auto file = RegionFile::open(path, true);
    ASSERT_NE(file, nullptr);
    const auto oldData = noise(1000, 1);
    ASSERT_TRUE(file->write(1, 1, oldData, 1));
    ASSERT_TRUE(file->write(2, 1, noise(1000, 2), 1)); // Right after it: no room to grow
    ASSERT_TRUE(file->flush());
    const RegionFile::Entry before = file->entry(1, 1);

    const auto newData = noise(2 * kSector, 3);
    ASSERT_TRUE(file->write(1, 1, newData, 2));
    const RegionFile::Entry after = file->entry(1, 1);
    EXPECT_NE(after.sectorOffset, before.sectorOffset) << "relocated";
    EXPECT_EQ(after.sectorCount, 3U);
    EXPECT_EQ(file->read(1, 1), newData);
    // Another chunk written before the flush must not land on the old sectors either.
    ASSERT_TRUE(file->write(9, 9, noise(500, 4), 2));
    EXPECT_NE(file->entry(9, 9).sectorOffset, before.sectorOffset);

    {
        // What a crash right now would leave: the on-disk header still points at the old data.
        auto crashed = RegionFile::open(path, false, true);
        ASSERT_NE(crashed, nullptr);
        EXPECT_EQ(crashed->read(1, 1), oldData);
        EXPECT_FALSE(crashed->contains(9, 9));
    }
    ASSERT_TRUE(file->flush());
    auto updated = RegionFile::open(path, false, true);
    ASSERT_NE(updated, nullptr);
    EXPECT_EQ(updated->read(1, 1), newData);
    EXPECT_EQ(updated->read(9, 9), noise(500, 4));
    EXPECT_EQ(updated->read(2, 1), noise(1000, 2));
}

TEST(RegionFile, EraseFreesSectors) {
    test::TempDir dir("betacraft_region_");
    const auto path = dir.path() / "r.0.0.bcr";
    auto file = RegionFile::open(path, true);
    ASSERT_NE(file, nullptr);
    ASSERT_TRUE(file->write(0, 0, noise(2000, 1), 1));
    ASSERT_TRUE(file->write(1, 0, noise(2000, 2), 1));
    ASSERT_TRUE(file->flush());
    const RegionFile::Entry erased = file->entry(0, 0);

    ASSERT_TRUE(file->erase(0, 0));
    EXPECT_FALSE(file->contains(0, 0));
    EXPECT_FALSE(file->read(0, 0).has_value());
    EXPECT_EQ(file->entry(0, 0), RegionFile::Entry{});
    // Still reserved until the header saying so is on disk.
    ASSERT_TRUE(file->write(2, 0, noise(2000, 3), 1));
    EXPECT_NE(file->entry(2, 0).sectorOffset, erased.sectorOffset);

    ASSERT_TRUE(file->flush());
    const uint32_t sectors = file->fileSectorCount();
    ASSERT_TRUE(file->write(3, 0, noise(2000, 4), 1));
    EXPECT_EQ(file->entry(3, 0).sectorOffset, erased.sectorOffset) << "freed sector reused";
    EXPECT_EQ(file->fileSectorCount(), sectors);
    ASSERT_TRUE(file->flush());
    file.reset();

    auto again = RegionFile::open(path, false);
    ASSERT_NE(again, nullptr);
    EXPECT_FALSE(again->contains(0, 0));
    EXPECT_EQ(again->read(1, 0), noise(2000, 2));
    EXPECT_EQ(again->read(2, 0), noise(2000, 3));
    EXPECT_EQ(again->read(3, 0), noise(2000, 4));
}

TEST(RegionFile, CorruptHeaderEntryOutOfBoundsReturnsNullopt) {
    test::TempDir dir("betacraft_region_");
    const auto path = dir.path() / "r.0.0.bcr";
    {
        auto file = RegionFile::open(path, true);
        ASSERT_NE(file, nullptr);
        ASSERT_TRUE(file->write(0, 0, noise(1000, 1), 1));
        ASSERT_TRUE(file->flush());
    }
    patchEntry(path, 5, 5, 1000, 3); // Past the end of the file
    patchEntry(path, 6, 6, 0, 1);    // Inside the header
    patchEntry(path, 7, 7, 2, 1);    // Overlaps chunk (0, 0)

    auto file = RegionFile::open(path, false);
    ASSERT_NE(file, nullptr);
    EXPECT_FALSE(file->read(5, 5).has_value());
    EXPECT_FALSE(file->contains(5, 5));
    EXPECT_FALSE(file->read(6, 6).has_value());
    EXPECT_FALSE(file->read(7, 7).has_value());
    EXPECT_EQ(file->read(0, 0), noise(1000, 1)) << "the earlier entry keeps its sectors";

    // The broken entries can be written again, without touching chunk (0, 0).
    ASSERT_TRUE(file->write(5, 5, noise(300, 2), 2));
    ASSERT_TRUE(file->write(7, 7, noise(300, 3), 2));
    EXPECT_EQ(file->read(5, 5), noise(300, 2));
    EXPECT_EQ(file->read(7, 7), noise(300, 3));
    EXPECT_EQ(file->read(0, 0), noise(1000, 1));
}

TEST(RegionFile, TruncatedFileOpensReadOnlyGracefully) {
    test::TempDir dir("betacraft_region_");
    const auto path = dir.path() / "r.0.0.bcr";
    test::storage::writeBytes(path, std::vector<uint8_t>(100, 0x7F));

    auto file = RegionFile::open(path, true);
    ASSERT_NE(file, nullptr);
    EXPECT_TRUE(file->isReadOnly());
    for (int32_t z = 0; z < RegionFile::kSize; ++z) {
        for (int32_t x = 0; x < RegionFile::kSize; ++x) {
            ASSERT_FALSE(file->read(x, z).has_value());
        }
    }
    EXPECT_FALSE(file->write(0, 0, noise(10, 1), 1));
    EXPECT_FALSE(file->erase(0, 0));
    EXPECT_TRUE(file->flush());
    file.reset();
    EXPECT_EQ(test::storage::readBytes(path), std::vector<uint8_t>(100, 0x7F)) << "left exactly as it was";

    // A whole header whose last chunk lost its tail: that chunk reads as absent, the file
    // stays writable.
    const auto other = dir.path() / "r.1.0.bcr";
    {
        auto f = RegionFile::open(other, true);
        ASSERT_NE(f, nullptr);
        ASSERT_TRUE(f->write(0, 0, noise(500, 2), 1));
        ASSERT_TRUE(f->write(1, 0, noise(3000, 3), 1));
        ASSERT_TRUE(f->flush());
    }
    std::filesystem::resize_file(other, 3 * kSector + 100);
    auto cut = RegionFile::open(other, false);
    ASSERT_NE(cut, nullptr);
    EXPECT_FALSE(cut->isReadOnly());
    EXPECT_EQ(cut->read(0, 0), noise(500, 2));
    EXPECT_FALSE(cut->read(1, 0).has_value());
    ASSERT_TRUE(cut->write(1, 0, noise(3000, 4), 2));
    EXPECT_EQ(cut->read(1, 0), noise(3000, 4));
}

TEST(RegionFile, TimestampUpdated) {
    test::TempDir dir("betacraft_region_");
    const auto path = dir.path() / "r.0.0.bcr";
    auto file = RegionFile::open(path, true);
    ASSERT_NE(file, nullptr);
    ASSERT_TRUE(file->write(4, 2, noise(100, 1), 1000));
    EXPECT_EQ(file->timestamp(4, 2), 1000U);
    ASSERT_TRUE(file->write(4, 2, noise(100, 2), 2000));
    EXPECT_EQ(file->timestamp(4, 2), 2000U);
    EXPECT_EQ(file->timestamp(5, 2), 0U);
    ASSERT_TRUE(file->flush());
    file.reset();

    auto again = RegionFile::open(path, false);
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(again->timestamp(4, 2), 2000U);
    ASSERT_TRUE(again->erase(4, 2));
    EXPECT_EQ(again->timestamp(4, 2), 0U);
}
