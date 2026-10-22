#include "storage/RegionFile.hpp"

#include "core/Logger.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#if defined(BETACRAFT_HAS_ZSTD)
#include <zstd.h>
#endif

namespace betacraft::storage {

namespace {

constexpr size_t kHeaderBytes = RegionFile::kHeaderSectors * RegionFile::kSectorBytes;
constexpr int kZstdLevel = 3;
// A chunk bundle is ~100 KiB raw at worst; anything claiming more than this is corrupt.
constexpr unsigned long long kMaxDecompressedBytes = 64ULL * 1024ULL * 1024ULL;

[[nodiscard]] uint32_t readBE32(const uint8_t* p) noexcept {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

void writeBE32(uint8_t* p, uint32_t v) noexcept {
    p[0] = static_cast<uint8_t>(v >> 24);
    p[1] = static_cast<uint8_t>(v >> 16);
    p[2] = static_cast<uint8_t>(v >> 8);
    p[3] = static_cast<uint8_t>(v);
}

[[nodiscard]] bool preadAll(int fd, uint8_t* out, size_t size, uint64_t offset) noexcept {
    size_t done = 0;
    while (done < size) {
        const ssize_t n = ::pread(fd, out + done, size - done, static_cast<off_t>(offset + done));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        done += static_cast<size_t>(n);
    }
    return true;
}

[[nodiscard]] uint32_t sectorsFor(uint64_t bytes) noexcept {
    return static_cast<uint32_t>((bytes + RegionFile::kSectorBytes - 1) / RegionFile::kSectorBytes);
}

} // namespace

RegionFile::RegionFile(int fd, bool readOnly) noexcept
    : m_fd(fd),
      m_readOnly(readOnly),
      m_entries(kEntries),
      m_timestamps(kEntries, 0),
      m_valid(kEntries, false) {}

RegionFile::~RegionFile() {
    if (m_headerDirty && !m_readOnly) {
        (void)flush();
    }
    if (m_fd >= 0) {
        ::close(m_fd);
    }
}

std::unique_ptr<RegionFile> RegionFile::open(const std::filesystem::path& path, bool create) {
    return open(path, create, false);
}

std::unique_ptr<RegionFile> RegionFile::open(const std::filesystem::path& path, bool create, bool readOnly) {
    int fd = -1;
    if (!readOnly) {
        fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC | (create ? O_CREAT : 0), 0644);
        if (fd < 0 && (errno == EACCES || errno == EROFS)) {
            readOnly = true; // Still readable: never try to write it
        } else if (fd < 0) {
            if (errno != ENOENT) {
                core::Logger::warn("RegionFile", "Cannot open ", path.string(), ": ", std::strerror(errno));
            }
            return nullptr;
        }
    }
    if (fd < 0) {
        fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) {
            return nullptr;
        }
    }
    std::unique_ptr<RegionFile> file(new RegionFile(fd, readOnly));
    if (!file->loadHeader(create)) {
        return nullptr;
    }
    if (file->m_readOnly && !readOnly) {
        core::Logger::warn("RegionFile", path.string(), " is truncated or not writable: opened read-only");
    }
    return file;
}

bool RegionFile::loadHeader(bool create) {
    struct stat st {};
    if (::fstat(m_fd, &st) != 0) {
        return false;
    }
    m_fileSize = static_cast<uint64_t>(st.st_size);
    std::vector<uint8_t> header(kHeaderBytes, 0);
    if (m_fileSize < kHeaderBytes) {
        if (m_fileSize == 0 && create && !m_readOnly) {
            if (!writeAt(0, header) || ::fsync(m_fd) != 0) {
                return false;
            }
            m_fileSize = kHeaderBytes;
        } else {
            // Truncated: read what is there, never rewrite the header over it.
            m_readOnly = true;
            if (m_fileSize > 0 && !preadAll(m_fd, header.data(), static_cast<size_t>(m_fileSize), 0)) {
                return false;
            }
        }
    } else if (!preadAll(m_fd, header.data(), kHeaderBytes, 0)) {
        return false;
    }

    const uint32_t fileSectors = sectorsFor(m_fileSize);
    m_used.assign(std::max(fileSectors, kHeaderSectors), false);
    for (uint32_t s = 0; s < kHeaderSectors; ++s) m_used[s] = true;
    for (size_t i = 0; i < static_cast<size_t>(kEntries); ++i) {
        const uint32_t packed = readBE32(header.data() + i * 4);
        m_entries[i] = Entry{packed >> 8, packed & 0xFFU};
        m_timestamps[i] = readBE32(header.data() + kSectorBytes + i * 4);
        const Entry e = m_entries[i];
        if (e.sectorCount == 0 && e.sectorOffset == 0) continue;
        bool valid = e.sectorCount > 0 && e.sectorOffset >= kHeaderSectors &&
                     static_cast<uint64_t>(e.sectorOffset) + e.sectorCount <= fileSectors;
        // Whatever part lies in the file is reserved, valid or not, so a bad entry's bytes
        // are never overwritten by another chunk.
        for (uint32_t s = e.sectorOffset; s < e.sectorOffset + e.sectorCount && s < fileSectors; ++s) {
            if (s < kHeaderSectors) continue;
            if (m_used[s]) valid = false; // Overlaps an earlier entry
            m_used[s] = true;
        }
        m_valid[i] = valid;
        if (!valid) {
            core::Logger::warn("RegionFile", "Ignoring invalid header entry ", i, " (sector ", e.sectorOffset, " x",
                               e.sectorCount, ")");
        }
    }
    return true;
}

RegionFile::Entry RegionFile::entry(int32_t localX, int32_t localZ) const noexcept {
    return inRange(localX, localZ) ? m_entries[indexOf(localX, localZ)] : Entry{};
}

uint32_t RegionFile::timestamp(int32_t localX, int32_t localZ) const noexcept {
    return inRange(localX, localZ) ? m_timestamps[indexOf(localX, localZ)] : 0U;
}

bool RegionFile::contains(int32_t localX, int32_t localZ) const noexcept {
    if (!inRange(localX, localZ)) return false;
    const size_t i = indexOf(localX, localZ);
    return m_valid[i] && m_entries[i].sectorCount > 0;
}

std::optional<std::vector<uint8_t>> RegionFile::read(int32_t localX, int32_t localZ) const {
    if (!contains(localX, localZ)) {
        return std::nullopt;
    }
    const Entry e = m_entries[indexOf(localX, localZ)];
    const uint64_t start = static_cast<uint64_t>(e.sectorOffset) * kSectorBytes;
    uint8_t head[kChunkHeaderBytes];
    if (!preadAll(m_fd, head, kChunkHeaderBytes, start)) {
        return std::nullopt;
    }
    const uint32_t length = readBE32(head);
    const uint8_t compression = head[4];
    if (static_cast<uint64_t>(length) + kChunkHeaderBytes > static_cast<uint64_t>(e.sectorCount) * kSectorBytes ||
        start + kChunkHeaderBytes + length > m_fileSize) {
        core::Logger::warn("RegionFile", "Chunk ", localX, ",", localZ, " overruns its sectors");
        return std::nullopt;
    }
    std::vector<uint8_t> data(length);
    if (length > 0 && !preadAll(m_fd, data.data(), length, start + kChunkHeaderBytes)) {
        return std::nullopt;
    }
    if (compression == static_cast<uint8_t>(Compression::None)) {
        return data;
    }
    if (compression == static_cast<uint8_t>(Compression::Zstd)) {
#if defined(BETACRAFT_HAS_ZSTD)
        const unsigned long long size = ZSTD_getFrameContentSize(data.data(), data.size());
        if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN || size > kMaxDecompressedBytes) {
            core::Logger::warn("RegionFile", "Chunk ", localX, ",", localZ, " has a bad zstd frame");
            return std::nullopt;
        }
        std::vector<uint8_t> out(static_cast<size_t>(size));
        const size_t got = ZSTD_decompress(out.data(), out.size(), data.data(), data.size());
        if (ZSTD_isError(got) || got != out.size()) {
            core::Logger::warn("RegionFile", "Chunk ", localX, ",", localZ, " failed to decompress");
            return std::nullopt;
        }
        return out;
#else
        core::Logger::warn("RegionFile", "Chunk ", localX, ",", localZ, " is zstd-compressed but this build has no zstd");
        return std::nullopt;
#endif
    }
    core::Logger::warn("RegionFile", "Chunk ", localX, ",", localZ, " has unknown compression ", int{compression});
    return std::nullopt;
}

uint32_t RegionFile::allocate(uint32_t count) const {
    uint32_t runStart = kHeaderSectors;
    uint32_t runLength = 0;
    for (uint32_t s = kHeaderSectors; s < m_used.size(); ++s) {
        if (m_used[s]) {
            runStart = s + 1;
            runLength = 0;
            continue;
        }
        if (++runLength == count) {
            return runStart;
        }
    }
    // A free run touching the end of the file grows into new sectors.
    return runStart;
}

void RegionFile::markUsed(uint32_t offset, uint32_t count, bool used) {
    if (offset + count > m_used.size()) {
        m_used.resize(offset + count, false);
    }
    for (uint32_t s = offset; s < offset + count; ++s) {
        if (s >= kHeaderSectors) m_used[s] = used;
    }
}

void RegionFile::releaseLater(uint32_t offset, uint32_t count) {
    if (count > 0) {
        m_pendingFree.emplace_back(offset, count);
    }
}

bool RegionFile::writeAt(uint64_t offset, std::span<const uint8_t> bytes) {
    size_t done = 0;
    while (done < bytes.size()) {
        const ssize_t n = ::pwrite(m_fd, bytes.data() + done, bytes.size() - done, static_cast<off_t>(offset + done));
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            core::Logger::warn("RegionFile", "Write failed: ", std::strerror(errno));
            return false;
        }
        done += static_cast<size_t>(n);
    }
    m_fileSize = std::max<uint64_t>(m_fileSize, offset + bytes.size());
    return true;
}

bool RegionFile::write(int32_t localX, int32_t localZ, std::span<const uint8_t> payload, uint32_t timestamp) {
    if (m_readOnly || !inRange(localX, localZ)) {
        return false;
    }
    std::span<const uint8_t> data = payload;
    uint8_t compression = static_cast<uint8_t>(Compression::None);
#if defined(BETACRAFT_HAS_ZSTD)
    std::vector<uint8_t> compressed;
    if (m_compression == Compression::Zstd && !payload.empty()) {
        compressed.resize(ZSTD_compressBound(payload.size()));
        const size_t n = ZSTD_compress(compressed.data(), compressed.size(), payload.data(), payload.size(), kZstdLevel);
        if (!ZSTD_isError(n) && n < payload.size()) {
            compressed.resize(n);
            data = compressed;
            compression = static_cast<uint8_t>(Compression::Zstd);
        }
    }
#endif
    const uint64_t total = kChunkHeaderBytes + data.size();
    const uint32_t needed = sectorsFor(total);
    if (needed > kMaxSectorsPerChunk || data.size() > 0xFFFFFFFFULL) {
        core::Logger::warn("RegionFile", "Chunk ", localX, ",", localZ, " too large (", total, " bytes)");
        return false;
    }
    std::vector<uint8_t> buffer(static_cast<size_t>(needed) * kSectorBytes, 0);
    writeBE32(buffer.data(), static_cast<uint32_t>(data.size()));
    buffer[4] = compression;
    if (!data.empty()) {
        std::memcpy(buffer.data() + kChunkHeaderBytes, data.data(), data.size());
    }

    const size_t i = indexOf(localX, localZ);
    const Entry old = m_entries[i];
    if (m_valid[i] && old.sectorCount >= needed) {
        // Fits: rewrite in place, keep the spare tail reserved until the header says so.
        if (!writeAt(static_cast<uint64_t>(old.sectorOffset) * kSectorBytes, buffer)) {
            return false;
        }
        releaseLater(old.sectorOffset + needed, old.sectorCount - needed);
        m_entries[i] = Entry{old.sectorOffset, needed};
    } else {
        const uint32_t offset = allocate(needed);
        if (offset + needed > 0xFFFFFFU) {
            core::Logger::warn("RegionFile", "Region file full");
            return false;
        }
        if (!writeAt(static_cast<uint64_t>(offset) * kSectorBytes, buffer)) {
            return false;
        }
        markUsed(offset, needed, true);
        if (m_valid[i]) {
            releaseLater(old.sectorOffset, old.sectorCount);
        }
        m_entries[i] = Entry{offset, needed};
        m_valid[i] = true;
    }
    m_timestamps[i] = timestamp;
    m_headerDirty = true;
    return true;
}

bool RegionFile::erase(int32_t localX, int32_t localZ) {
    if (m_readOnly || !inRange(localX, localZ)) {
        return false;
    }
    const size_t i = indexOf(localX, localZ);
    const Entry old = m_entries[i];
    if (old == Entry{} && m_timestamps[i] == 0) {
        return true;
    }
    if (m_valid[i]) {
        releaseLater(old.sectorOffset, old.sectorCount);
    }
    m_entries[i] = Entry{};
    m_timestamps[i] = 0;
    m_valid[i] = false;
    m_headerDirty = true;
    return true;
}

bool RegionFile::flush() {
    if (m_readOnly || !m_headerDirty) {
        return true;
    }
    // Data first: the header must never point at sectors that are not on disk yet.
    if (::fsync(m_fd) != 0) {
        core::Logger::warn("RegionFile", "fsync failed: ", std::strerror(errno));
        return false;
    }
    std::vector<uint8_t> header(kHeaderBytes, 0);
    for (size_t i = 0; i < static_cast<size_t>(kEntries); ++i) {
        const Entry e = m_entries[i];
        writeBE32(header.data() + i * 4, (e.sectorOffset << 8) | (e.sectorCount & 0xFFU));
        writeBE32(header.data() + kSectorBytes + i * 4, m_timestamps[i]);
    }
    if (!writeAt(0, header) || ::fsync(m_fd) != 0) {
        return false;
    }
    for (const auto& [offset, count] : m_pendingFree) {
        markUsed(offset, count, false);
    }
    m_pendingFree.clear();
    m_headerDirty = false;
    return true;
}

} // namespace betacraft::storage
