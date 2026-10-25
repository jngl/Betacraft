#pragma once

#include "storage/IChunkStore.hpp"
#include "storage/RegionFile.hpp"

#include <cstddef>
#include <filesystem>
#include <list>
#include <memory>
#include <mutex>
#include <utility>

namespace betacraft::storage {

// Chunk bundles in region files under `regionDir` (r.<rx>.<rz>.bcr, rx = floorDiv(x, 32)),
// with the kCacheSize most recently used RegionFiles kept open (an evicted one is flushed).
// Thread-safe: one mutex serialises every call (the I/O worker writes from its thread).
class RegionChunkStore final : public IChunkStore {
public:
    static constexpr size_t kCacheSize = 8;

    explicit RegionChunkStore(std::filesystem::path regionDir, bool readOnly = false);
    ~RegionChunkStore() override; // Flushes every open region

    RegionChunkStore(const RegionChunkStore&) = delete;
    RegionChunkStore& operator=(const RegionChunkStore&) = delete;

    [[nodiscard]] std::optional<std::vector<uint8_t>> read(core::ChunkCoord coord) override;
    [[nodiscard]] bool contains(core::ChunkCoord coord) override;
    bool write(core::ChunkCoord coord, std::span<const uint8_t> bundleBytes) override;
    bool flush() override;
    bool erase(core::ChunkCoord coord);

    // Conventions §1.5 for regions: the raw sectors of the chunk's entry are copied to
    // quarantine/c_X_Z.<seconds>.bin under the region directory, then the entry is erased so
    // the regenerated chunk does not read it again. False (nothing done) when read-only.
    bool quarantine(core::ChunkCoord coord);

    // Read-only: regions are opened O_RDONLY, never created, and every write fails.
    void setReadOnly(bool readOnly);
    [[nodiscard]] bool isReadOnly() const;
    // Flushes and closes every open region (before another process or store touches them).
    bool closeAll();

    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return m_dir; }
    [[nodiscard]] size_t openRegionCount() const;
    [[nodiscard]] uint64_t openRegionBytes() const;

    [[nodiscard]] static std::pair<int32_t, int32_t> regionOf(core::ChunkCoord coord) noexcept;
    [[nodiscard]] static std::filesystem::path regionPath(const std::filesystem::path& regionDir, int32_t rx, int32_t rz);

private:
    using Key = std::pair<int32_t, int32_t>;
    // The open region holding `coord` (moved to the front of the cache); nullptr when it does
    // not exist and `create` is false, or cannot be opened.
    RegionFile* region(core::ChunkCoord coord, bool create);
    bool flushLocked();

    std::filesystem::path m_dir;
    bool m_readOnly{false};
    mutable std::mutex m_mutex;
    std::list<std::pair<Key, std::unique_ptr<RegionFile>>> m_cache; // Most recent first
};

} // namespace betacraft::storage
