#include "storage/RegionChunkStore.hpp"

#include "core/Logger.hpp"
#include "core/Types.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <string>

namespace betacraft::storage {

namespace {

[[nodiscard]] uint32_t nowSeconds() noexcept {
    const auto now = std::chrono::system_clock::now().time_since_epoch();
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::seconds>(now).count());
}

[[nodiscard]] std::pair<int32_t, int32_t> localOf(core::ChunkCoord coord) noexcept {
    return {core::floorMod(coord.x, RegionFile::kSize), core::floorMod(coord.z, RegionFile::kSize)};
}

} // namespace

RegionChunkStore::RegionChunkStore(std::filesystem::path regionDir, bool readOnly)
    : m_dir(std::move(regionDir)), m_readOnly(readOnly) {}

RegionChunkStore::~RegionChunkStore() {
    (void)closeAll();
}

std::pair<int32_t, int32_t> RegionChunkStore::regionOf(core::ChunkCoord coord) noexcept {
    return {core::floorDiv(coord.x, RegionFile::kSize), core::floorDiv(coord.z, RegionFile::kSize)};
}

std::filesystem::path RegionChunkStore::regionPath(const std::filesystem::path& regionDir, int32_t rx, int32_t rz) {
    return regionDir / ("r." + std::to_string(rx) + "." + std::to_string(rz) + ".bcr");
}

RegionFile* RegionChunkStore::region(core::ChunkCoord coord, bool create) {
    const Key key = regionOf(coord);
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it) {
        if (it->first == key) {
            if (it != m_cache.begin()) {
                m_cache.splice(m_cache.begin(), m_cache, it);
            }
            return m_cache.front().second.get();
        }
    }
    if (m_readOnly) {
        create = false;
    }
    if (create) {
        std::error_code ec;
        std::filesystem::create_directories(m_dir, ec);
        if (ec) {
            core::Logger::warn("RegionChunkStore", "Cannot create ", m_dir.string(), ": ", ec.message());
            return nullptr;
        }
    }
    auto file = RegionFile::open(regionPath(m_dir, key.first, key.second), create, m_readOnly);
    if (!file) {
        return nullptr;
    }
    if (m_cache.size() >= kCacheSize) {
        if (!m_cache.back().second->flush()) {
            core::Logger::warn("RegionChunkStore", "Flush of an evicted region failed");
        }
        m_cache.pop_back();
    }
    m_cache.emplace_front(key, std::move(file));
    return m_cache.front().second.get();
}

std::optional<std::vector<uint8_t>> RegionChunkStore::read(core::ChunkCoord coord) {
    const std::lock_guard lock(m_mutex);
    RegionFile* file = region(coord, false);
    if (!file) return std::nullopt;
    const auto [lx, lz] = localOf(coord);
    return file->read(lx, lz);
}

bool RegionChunkStore::contains(core::ChunkCoord coord) {
    const std::lock_guard lock(m_mutex);
    RegionFile* file = region(coord, false);
    if (!file) return false;
    const auto [lx, lz] = localOf(coord);
    return file->contains(lx, lz);
}

bool RegionChunkStore::write(core::ChunkCoord coord, std::span<const uint8_t> bundleBytes) {
    const std::lock_guard lock(m_mutex);
    if (m_readOnly) return false;
    RegionFile* file = region(coord, true);
    if (!file) return false;
    const auto [lx, lz] = localOf(coord);
    return file->write(lx, lz, bundleBytes, nowSeconds());
}

bool RegionChunkStore::erase(core::ChunkCoord coord) {
    const std::lock_guard lock(m_mutex);
    if (m_readOnly) return false;
    RegionFile* file = region(coord, false);
    if (!file) return true;
    const auto [lx, lz] = localOf(coord);
    return file->erase(lx, lz);
}

bool RegionChunkStore::quarantine(core::ChunkCoord coord) {
    const std::lock_guard lock(m_mutex);
    if (m_readOnly) return false;
    RegionFile* file = region(coord, false);
    if (!file) return false;
    const auto [lx, lz] = localOf(coord);
    const RegionFile::Entry e = file->entry(lx, lz);
    if (e.sectorCount == 0) return false;
    // Raw copy of the sectors, straight from the file: whatever made them unreadable is kept.
    std::vector<char> raw(static_cast<size_t>(e.sectorCount) * RegionFile::kSectorBytes);
    {
        const Key key = regionOf(coord);
        std::ifstream in(regionPath(m_dir, key.first, key.second), std::ios::binary);
        in.seekg(static_cast<std::streamoff>(e.sectorOffset) * static_cast<std::streamoff>(RegionFile::kSectorBytes));
        in.read(raw.data(), static_cast<std::streamsize>(raw.size()));
        raw.resize(static_cast<size_t>(std::max<std::streamsize>(in.gcount(), 0)));
    }
    const std::filesystem::path dir = m_dir / "quarantine";
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    const auto path = dir / ("c_" + std::to_string(coord.x) + "_" + std::to_string(coord.z) + "." +
                             std::to_string(nowSeconds()) + ".bin");
    {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        out.write(raw.data(), static_cast<std::streamsize>(raw.size()));
        if (!out.good()) {
            core::Logger::warn("RegionChunkStore", "Cannot quarantine chunk ", coord.x, ",", coord.z);
            return false; // Keep the entry: the bytes exist nowhere else
        }
    }
    core::Logger::warn("RegionChunkStore", "Unreadable chunk ", coord.x, ",", coord.z, " moved to ", path.string());
    return file->erase(lx, lz) && file->flush();
}

bool RegionChunkStore::flushLocked() {
    bool ok = true;
    for (auto& [key, file] : m_cache) {
        ok = file->flush() && ok;
    }
    return ok;
}

bool RegionChunkStore::flush() {
    const std::lock_guard lock(m_mutex);
    return flushLocked();
}

bool RegionChunkStore::closeAll() {
    const std::lock_guard lock(m_mutex);
    const bool ok = flushLocked();
    m_cache.clear();
    return ok;
}

void RegionChunkStore::setReadOnly(bool readOnly) {
    const std::lock_guard lock(m_mutex);
    if (readOnly == m_readOnly) return;
    (void)flushLocked();
    m_cache.clear(); // Reopened in the new mode
    m_readOnly = readOnly;
}

bool RegionChunkStore::isReadOnly() const {
    const std::lock_guard lock(m_mutex);
    return m_readOnly;
}

size_t RegionChunkStore::openRegionCount() const {
    const std::lock_guard lock(m_mutex);
    return m_cache.size();
}

uint64_t RegionChunkStore::openRegionBytes() const {
    const std::lock_guard lock(m_mutex);
    uint64_t total = 0;
    for (const auto& [key, file] : m_cache) {
        total += file->fileSizeBytes();
    }
    return total;
}

} // namespace betacraft::storage
