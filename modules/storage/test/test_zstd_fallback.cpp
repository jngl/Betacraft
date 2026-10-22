// S7.1 / conventions §4: a build without libzstd (or with BETACRAFT_WITH_ZSTD=OFF) stores
// chunks with compression byte 0, and every build reads them.
#include "StorageTestHelpers.hpp"
#include "storage/RegionFile.hpp"
#include "test_support/TempDir.hpp"

#include <gtest/gtest.h>

using namespace betacraft;
using storage::RegionFile;

TEST(ZstdFallback, UncompressedChunkReadableWhenBuiltWithoutZstd) {
    test::TempDir dir("betacraft_zstd_");
    const auto path = dir.path() / "r.0.0.bcr";
    // Very compressible: zstd would shrink it a lot if it were used.
    const std::vector<uint8_t> payload(20000, 0x42);
    {
        auto file = RegionFile::open(path, true);
        ASSERT_NE(file, nullptr);
        file->setCompression(RegionFile::Compression::None); // What a zstd-less build writes
        ASSERT_TRUE(file->write(7, 3, payload, 1));
        EXPECT_EQ(file->entry(7, 3).sectorCount, 5U) << "stored raw";
        ASSERT_TRUE(file->flush());
    }
    // On disk: u32 length then compression byte 0.
    const std::vector<uint8_t> bytes = test::storage::readBytes(path);
    auto reopened = RegionFile::open(path, false);
    ASSERT_NE(reopened, nullptr);
    const size_t at = static_cast<size_t>(reopened->entry(7, 3).sectorOffset) * RegionFile::kSectorBytes;
    ASSERT_GT(bytes.size(), at + 5);
    EXPECT_EQ((uint32_t{bytes[at]} << 24) | (uint32_t{bytes[at + 1]} << 16) | (uint32_t{bytes[at + 2]} << 8) | bytes[at + 3],
              payload.size());
    EXPECT_EQ(bytes[at + 4], 0U);
    EXPECT_EQ(reopened->read(7, 3), payload);

    if (RegionFile::kZstdAvailable) {
        // The default compresses, and both kinds coexist in one file.
        ASSERT_TRUE(reopened->write(8, 3, payload, 2));
        EXPECT_EQ(reopened->entry(8, 3).sectorCount, 1U);
        EXPECT_EQ(reopened->read(8, 3), payload);
        EXPECT_EQ(reopened->read(7, 3), payload);
    } else {
        ASSERT_TRUE(reopened->write(8, 3, payload, 2));
        EXPECT_EQ(reopened->read(8, 3), payload);
    }
}
