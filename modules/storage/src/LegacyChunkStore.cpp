#include "storage/LegacyChunkStore.hpp"

#include "core/Logger.hpp"

#include <charconv>
#include <fstream>
#include <sstream>
#include <string>

namespace betacraft::storage {

namespace {

constexpr std::streamsize kMaxChunkFileBytes = 16 * 1024 * 1024;
constexpr std::streamsize kMaxSideFileBytes = 1024 * 1024;

[[nodiscard]] std::optional<std::vector<uint8_t>> readFile(const std::filesystem::path& path, std::streamsize maxSize) {
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if (!ifs.is_open()) {
        return std::nullopt;
    }
    const std::streamsize size = ifs.tellg();
    if (size <= 0 || size > maxSize) {
        return std::nullopt;
    }
    ifs.seekg(0, std::ios::beg);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    if (!ifs.read(reinterpret_cast<char*>(bytes.data()), size)) {
        return std::nullopt;
    }
    return bytes;
}

[[nodiscard]] bool parseInt(std::string_view text, int32_t& out) {
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), out);
    return ec == std::errc{} && ptr == text.data() + text.size();
}

} // namespace

LegacyChunkStore::LegacyChunkStore(std::filesystem::path saveDir) : m_saveDir(std::move(saveDir)) {}

std::filesystem::path LegacyChunkStore::chunkFilePath(core::ChunkCoord coord) const {
    std::ostringstream oss;
    oss << "c_" << coord.x << "_" << coord.z << ".chk";
    return chunksDirectory() / oss.str();
}

std::filesystem::path LegacyChunkStore::decorationFilePath(core::ChunkCoord coord) const {
    std::filesystem::path path = chunkFilePath(coord);
    path.replace_extension(".dec");
    return path;
}

std::filesystem::path LegacyChunkStore::blockEntityFilePath(core::ChunkCoord coord) const {
    std::filesystem::path path = chunkFilePath(coord);
    path.replace_extension(".be");
    return path;
}

std::optional<worldgen::DecorationRecord> LegacyChunkStore::readDecoration(core::ChunkCoord coord) const {
    const auto bytes = readFile(decorationFilePath(coord), kMaxSideFileBytes);
    if (!bytes) {
        return std::nullopt;
    }
    auto record = worldgen::decodeDecorationRecord(*bytes);
    if (!record) {
        core::Logger::warn("WorldStorage", "Ignoring corrupt decoration record ", decorationFilePath(coord).string());
    }
    return record;
}

std::vector<uint8_t> LegacyChunkStore::readBlockEntityBytes(core::ChunkCoord coord) const {
    const auto path = blockEntityFilePath(coord);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return {};
    }
    auto bytes = readFile(path, kMaxSideFileBytes);
    // Present but unreadable: one byte no decoder accepts, so the caller warns and quarantines.
    return bytes ? std::move(*bytes) : std::vector<uint8_t>{0};
}

std::optional<ChunkBundle> LegacyChunkStore::readBundle(core::ChunkCoord coord) const {
    const auto path = chunkFilePath(coord);
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return std::nullopt;
    }
    ChunkBundle bundle;
    auto blocks = readFile(path, kMaxChunkFileBytes);
    // Unreadable whole: a single zero byte, which Chunk::deserializeRLE rejects (quarantine).
    bundle.blocks = blocks ? std::move(*blocks) : std::vector<uint8_t>{0};
    bundle.decoration = readDecoration(coord);
    bundle.blockEntities = readBlockEntityBytes(coord);
    return bundle;
}

std::vector<core::ChunkCoord> LegacyChunkStore::listChunks() const {
    std::vector<core::ChunkCoord> out;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(chunksDirectory(), ec)) {
        const std::string name = entry.path().filename().string();
        if (name.size() < 8 || name.rfind("c_", 0) != 0 || name.substr(name.size() - 4) != ".chk") continue;
        const std::string_view body = std::string_view(name).substr(2, name.size() - 6);
        // Coordinates may be negative: split on the '_' that follows the first number.
        const size_t sep = body.find('_', body.empty() || body[0] != '-' ? 0 : 1);
        int32_t x = 0;
        int32_t z = 0;
        if (sep == std::string_view::npos || !parseInt(body.substr(0, sep), x) || !parseInt(body.substr(sep + 1), z)) {
            continue;
        }
        out.push_back({x, z});
    }
    return out;
}

std::optional<std::vector<uint8_t>> LegacyChunkStore::read(core::ChunkCoord coord) {
    auto bundle = readBundle(coord);
    if (!bundle) return std::nullopt;
    return encodeBundle(*bundle);
}

bool LegacyChunkStore::contains(core::ChunkCoord coord) {
    std::error_code ec;
    return std::filesystem::exists(chunkFilePath(coord), ec);
}

bool LegacyChunkStore::quarantineChunk(core::ChunkCoord coord) const {
    const auto path = chunkFilePath(coord);
    auto quarantined = path;
    quarantined += ".corrupt";
    std::error_code ec;
    std::filesystem::rename(path, quarantined, ec);
    core::Logger::warn("WorldStorage", "Unreadable chunk file ", path.string(), ec ? " (could not quarantine)" : " moved to ",
                       ec ? std::string() : quarantined.filename().string());
    return !ec;
}

bool LegacyChunkStore::quarantineBlockEntities(core::ChunkCoord coord) const {
    const auto path = blockEntityFilePath(coord);
    auto quarantined = path;
    quarantined += ".corrupt";
    std::error_code ec;
    std::filesystem::rename(path, quarantined, ec);
    core::Logger::warn("WorldStorage", "Ignoring unreadable block entity file ", path.string());
    return !ec;
}

} // namespace betacraft::storage
