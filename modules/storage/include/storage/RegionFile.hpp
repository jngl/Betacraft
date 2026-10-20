#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace betacraft::storage {

// S7.1 region file `region/r.X.Z.bcr`: 32x32 chunks in 4 KiB sectors.
//
// Layout (big-endian):
//   sector 0: 1024 entries {uint24 sectorOffset, uint8 sectorCount}, index localX + 32 * localZ
//   sector 1: 1024 uint32 timestamps
//   then chunks, each starting on a sector: uint32 length (bytes after the compression byte),
//   uint8 compression (0 = none, 1 = zstd level 3), payload.
//
// Crash safety: a chunk that still fits its sectors is rewritten in place; otherwise it goes
// to free sectors (or the end of the file) and only the in-memory header changes. The
// on-disk header is rewritten by flush(), after the data is fsynced, so a crash before it
// leaves the previous version readable. Sectors a rewrite or an erase released stay reserved
// until that flush (the on-disk header may still point at them). The free-sector bitmap is
// rebuilt from the header on open; header entries pointing outside the file, or overlapping an
// earlier entry, read as absent and their sectors are never reused.
//
// Not thread-safe: RegionChunkStore serialises access.
class RegionFile {
public:
    static constexpr int32_t kSize = 32; // Chunks per side
    static constexpr int32_t kEntries = kSize * kSize;
    static constexpr size_t kSectorBytes = 4096;
    static constexpr uint32_t kHeaderSectors = 2;
    static constexpr size_t kChunkHeaderBytes = 5; // uint32 length + uint8 compression
    static constexpr uint32_t kMaxSectorsPerChunk = 255;

    enum class Compression : uint8_t { None = 0, Zstd = 1 };
    // Whether this build can write and read compression 1.
    static constexpr bool kZstdAvailable =
#if defined(BETACRAFT_HAS_ZSTD)
        true;
#else
        false;
#endif

    struct Entry {
        uint32_t sectorOffset{0};
        uint32_t sectorCount{0};
        constexpr bool operator==(const Entry&) const noexcept = default;
    };

    // nullptr when the file cannot be opened (or does not exist and `create` is false). A file
    // too short to hold its header opens read-only (writes fail) instead of being rewritten;
    // `readOnly` forces that mode (nothing is ever written, not even a new header).
    static std::unique_ptr<RegionFile> open(const std::filesystem::path& path, bool create);
    static std::unique_ptr<RegionFile> open(const std::filesystem::path& path, bool create, bool readOnly);

    ~RegionFile();
    RegionFile(const RegionFile&) = delete;
    RegionFile& operator=(const RegionFile&) = delete;

    // The stored payload (decompressed); nullopt when absent, out of range or unreadable.
    [[nodiscard]] std::optional<std::vector<uint8_t>> read(int32_t localX, int32_t localZ) const;
    [[nodiscard]] bool contains(int32_t localX, int32_t localZ) const noexcept;
    bool write(int32_t localX, int32_t localZ, std::span<const uint8_t> payload, uint32_t timestamp);
    bool erase(int32_t localX, int32_t localZ);
    bool flush(); // fsync data, then header (then fsync again)

    // New chunks are compressed with zstd when the build has it (default) and this is on.
    void setCompression(Compression c) noexcept { m_compression = c; }
    [[nodiscard]] bool isReadOnly() const noexcept { return m_readOnly; }
    [[nodiscard]] bool hasUnflushedChanges() const noexcept { return m_headerDirty; }

    // Introspection (tests, F3).
    [[nodiscard]] Entry entry(int32_t localX, int32_t localZ) const noexcept;
    [[nodiscard]] uint32_t timestamp(int32_t localX, int32_t localZ) const noexcept;
    [[nodiscard]] uint32_t fileSectorCount() const noexcept { return static_cast<uint32_t>(m_used.size()); }
    [[nodiscard]] uint64_t fileSizeBytes() const noexcept { return m_fileSize; }

private:
    RegionFile(int fd, bool readOnly) noexcept;
    [[nodiscard]] static bool inRange(int32_t localX, int32_t localZ) noexcept {
        return localX >= 0 && localX < kSize && localZ >= 0 && localZ < kSize;
    }
    [[nodiscard]] static size_t indexOf(int32_t localX, int32_t localZ) noexcept {
        return static_cast<size_t>(localX + localZ * kSize);
    }
    bool loadHeader(bool create);
    // First run of `count` free sectors, or the end of the file.
    [[nodiscard]] uint32_t allocate(uint32_t count) const;
    void markUsed(uint32_t offset, uint32_t count, bool used);
    void releaseLater(uint32_t offset, uint32_t count);
    [[nodiscard]] bool writeAt(uint64_t offset, std::span<const uint8_t> bytes);

    int m_fd{-1};
    bool m_readOnly{false};
    bool m_headerDirty{false};
    Compression m_compression{kZstdAvailable ? Compression::Zstd : Compression::None};
    uint64_t m_fileSize{0};
    std::vector<Entry> m_entries;      // Live (in-memory) header
    std::vector<uint32_t> m_timestamps;
    std::vector<bool> m_valid;         // Entry readable (in bounds, no overlap)
    std::vector<bool> m_used;          // Per sector of the file
    std::vector<std::pair<uint32_t, uint32_t>> m_pendingFree; // Released since the last flush
};

} // namespace betacraft::storage
