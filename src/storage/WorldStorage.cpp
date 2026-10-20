#include "storage/WorldStorage.hpp"
#include "core/Logger.hpp"
#include "storage/LegacyChunkStore.hpp"
#include "storage/RegionChunkStore.hpp"
#include "storage/SaveMigration.hpp"

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <sstream>
#include <vector>

namespace betacraft::storage {

WorldStorage::WorldStorage(std::filesystem::path saveDir)
    : m_saveDir(std::move(saveDir)),
      m_regions(std::make_unique<RegionChunkStore>(m_saveDir / "region")),
      m_legacy(std::make_unique<LegacyChunkStore>(m_saveDir)) {}

WorldStorage::~WorldStorage() = default; // The region store flushes on destruction
WorldStorage::WorldStorage(WorldStorage&&) noexcept = default;
WorldStorage& WorldStorage::operator=(WorldStorage&&) noexcept = default;

void WorldStorage::setReadOnly(bool readOnly) noexcept {
    m_readOnly = readOnly;
    m_regions->setReadOnly(readOnly);
}

void WorldStorage::setRegionStorageEnabled(bool enabled) {
    std::error_code ec;
    m_regionWrites = enabled || std::filesystem::exists(m_saveDir / "region", ec);
}

bool WorldStorage::ensureSaveRoot() const {
    std::error_code ec;
    std::filesystem::create_directories(m_saveDir, ec);
    if (ec) {
        core::Logger::warn("WorldStorage", "Failed to create save directory: ", ec.message());
        return false;
    }
    return true;
}

bool WorldStorage::ensureDirectoryExists() const {
    std::error_code ec;
    std::filesystem::create_directories(m_saveDir / "chunks", ec);
    if (ec) {
        core::Logger::warn("WorldStorage", "Failed to create save directory: ", ec.message());
        return false;
    }
    return true;
}

namespace {

constexpr std::string_view kLevelHeaderPrefix = "BETACRAFT_LEVEL_V";

[[nodiscard]] std::string_view modeName(core::GameMode mode) noexcept {
    return mode == core::GameMode::Creative ? "creative" : "survival";
}

// Reads exactly `values...` from the rest of a line; false (nothing to trust) otherwise.
template <typename... T>
[[nodiscard]] bool parseFields(std::istringstream& in, T&... values) {
    ((in >> values), ...);
    if (in.fail()) return false;
    std::string extra;
    return !(in >> extra);
}

[[nodiscard]] LevelLoadStatus decodeLegacyV1(std::istringstream& in, WorldLevelData& out) {
    WorldLevelData data{};
    in >> data.seed;
    in >> data.playerPos.x >> data.playerPos.y >> data.playerPos.z;
    in >> data.playerYaw >> data.playerPitch;
    in >> data.timeOfDay;
    if (in.fail()) {
        return LevelLoadStatus::Corrupt;
    }
    out = data;
    return LevelLoadStatus::LegacyV1;
}

void decodeV2Line(const std::string& key, std::istringstream& in, WorldLevelData& out) {
    const auto player = [&out]() -> PlayerSaveData& {
        if (!out.player) out.player.emplace();
        return *out.player;
    };
    if (key == "items_scheme") {
        uint32_t v = 0;
        if (parseFields(in, v)) out.itemsScheme = v;
    } else if (key == "seed") {
        uint64_t v = 0;
        if (parseFields(in, v)) out.seed = v;
    } else if (key == "time") {
        float v = 0.0f;
        if (parseFields(in, v)) out.timeOfDay = v;
    } else if (key == "mode") {
        std::string v;
        if (parseFields(in, v)) {
            if (const auto mode = core::parseGameMode(v)) player().mode = *mode;
        }
    } else if (key == "spawn") {
        glm::vec3 v{};
        if (parseFields(in, v.x, v.y, v.z)) out.spawnPos = v;
    } else if (key == "storage_format") {
        uint32_t v = 0;
        if (parseFields(in, v) && v >= kLegacyStorageFormat) out.storageFormat = v;
    } else if (key == "name") {
        std::string v;
        std::getline(in, v);
        const size_t first = v.find_first_not_of(" \t");
        const size_t last = v.find_last_not_of(" \t\r");
        out.name = first == std::string::npos ? std::string{} : v.substr(first, last - first + 1);
    } else if (key == "weather.raining") {
        int32_t v = 0;
        if (parseFields(in, v)) out.weather.raining = v != 0;
    } else if (key == "weather.thundering") {
        int32_t v = 0;
        if (parseFields(in, v)) out.weather.thundering = v != 0;
    } else if (key == "weather.rainTime") {
        int32_t v = 0;
        if (parseFields(in, v)) out.weather.rainTime = std::max(0, v);
    } else if (key == "weather.thunderTime") {
        int32_t v = 0;
        if (parseFields(in, v)) out.weather.thunderTime = std::max(0, v);
    } else if (key == "weather.rainStrength") {
        float v = 0.0f;
        if (parseFields(in, v) && std::isfinite(v)) {
            out.weather.rainStrength = std::clamp(v, 0.0f, 1.0f);
            out.weather.prevRainStrength = out.weather.rainStrength;
        }
    } else if (key == "weather.thunderStrength") {
        float v = 0.0f;
        if (parseFields(in, v) && std::isfinite(v)) {
            out.weather.thunderStrength = std::clamp(v, 0.0f, 1.0f);
            out.weather.prevThunderStrength = out.weather.thunderStrength;
        }
    } else if (key == "bed") {
        int32_t x = 0;
        int32_t y = 0;
        int32_t z = 0;
        if (parseFields(in, x, y, z)) out.bedPos = core::BlockPos{x, y, z};
    } else if (key == "player.pos") {
        glm::vec3 v{};
        if (parseFields(in, v.x, v.y, v.z)) out.playerPos = v;
    } else if (key == "player.rot") {
        float yaw = 0.0f;
        float pitch = 0.0f;
        if (parseFields(in, yaw, pitch)) {
            out.playerYaw = yaw;
            out.playerPitch = pitch;
        }
    } else if (key == "player.health") {
        int32_t v = 0;
        if (parseFields(in, v)) player().health = v;
    } else if (key == "player.air") {
        int32_t v = 0;
        if (parseFields(in, v)) player().air = v;
    } else if (key == "player.selected") {
        int32_t v = 0;
        if (parseFields(in, v)) player().selectedSlot = v;
    } else if (key == "player.fall") {
        float v = 0.0f;
        if (parseFields(in, v)) player().fallDistance = v;
    } else if (key == "player.fire") {
        int32_t v = 0;
        if (parseFields(in, v)) player().fireTicks = v;
    } else if (key == "inv") {
        int32_t slot = -1;
        uint32_t id = 0;
        int32_t count = 0;
        int32_t damage = 0;
        if (parseFields(in, slot, id, count, damage) && slot >= 0 &&
            slot < static_cast<int32_t>(PlayerSaveData{}.inventory.size()) && id <= 0xFFFFU) {
            player().inventory[static_cast<size_t>(slot)] = SavedStack{static_cast<uint16_t>(id), count, damage};
        } else {
            core::Logger::warn("WorldStorage", "Skipping malformed level.dat inventory line");
        }
    } else if (key == "armor") {
        int32_t slot = -1;
        uint32_t id = 0;
        int32_t count = 0;
        int32_t damage = 0;
        if (parseFields(in, slot, id, count, damage) && slot >= 0 &&
            slot < static_cast<int32_t>(PlayerSaveData{}.armor.size()) && id <= 0xFFFFU) {
            player().armor[static_cast<size_t>(slot)] = SavedStack{static_cast<uint16_t>(id), count, damage};
        } else {
            core::Logger::warn("WorldStorage", "Skipping malformed level.dat armor line");
        }
    }
    // Unknown keys are ignored (conventions §1.3): newer minor additions stay readable.
}

[[nodiscard]] bool readTextFile(const std::filesystem::path& path, std::string& out) {
    std::ifstream ifs(path, std::ios::in | std::ios::binary);
    if (!ifs.is_open()) {
        return false;
    }
    std::ostringstream oss;
    oss << ifs.rdbuf();
    out = oss.str();
    return true;
}

} // namespace

std::string encodeLevelData(const WorldLevelData& data) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(std::numeric_limits<float>::max_digits10);
    out << kLevelHeaderPrefix << kLevelFormatVersion << '\n';
    out << "items_scheme " << data.itemsScheme << '\n';
    out << "seed " << data.seed << '\n';
    out << "time " << data.timeOfDay << '\n';
    if (data.storageFormat >= kRegionStorageFormat) {
        out << "storage_format " << data.storageFormat << '\n';
    }
    if (!data.name.empty()) {
        std::string name = data.name;
        std::replace_if(name.begin(), name.end(), [](char c) { return c == '\n' || c == '\r'; }, ' ');
        out << "name " << name << '\n';
    }
    if (data.player) {
        out << "mode " << modeName(data.player->mode) << '\n';
    }
    if (data.spawnPos) {
        out << "spawn " << data.spawnPos->x << ' ' << data.spawnPos->y << ' ' << data.spawnPos->z << '\n';
    }
    if (data.bedPos) {
        out << "bed " << data.bedPos->x << ' ' << data.bedPos->y << ' ' << data.bedPos->z << '\n';
    }
    const WeatherState& w = data.weather;
    out << "weather.raining " << (w.raining ? 1 : 0) << '\n';
    out << "weather.thundering " << (w.thundering ? 1 : 0) << '\n';
    out << "weather.rainTime " << w.rainTime << '\n';
    out << "weather.thunderTime " << w.thunderTime << '\n';
    out << "weather.rainStrength " << w.rainStrength << '\n';
    out << "weather.thunderStrength " << w.thunderStrength << '\n';
    out << "player.pos " << data.playerPos.x << ' ' << data.playerPos.y << ' ' << data.playerPos.z << '\n';
    out << "player.rot " << data.playerYaw << ' ' << data.playerPitch << '\n';
    if (data.player) {
        const PlayerSaveData& p = *data.player;
        out << "player.health " << p.health << '\n';
        out << "player.air " << p.air << '\n';
        out << "player.selected " << p.selectedSlot << '\n';
        out << "player.fall " << p.fallDistance << '\n';
        if (p.fireTicks != 0) {
            out << "player.fire " << p.fireTicks << '\n';
        }
        for (size_t i = 0; i < p.inventory.size(); ++i) {
            const SavedStack& s = p.inventory[i];
            if (s.id != 0 && s.count > 0) {
                out << "inv " << i << ' ' << s.id << ' ' << s.count << ' ' << s.damage << '\n';
            }
        }
        for (size_t i = 0; i < p.armor.size(); ++i) {
            const SavedStack& s = p.armor[i];
            if (s.id != 0 && s.count > 0) {
                out << "armor " << i << ' ' << s.id << ' ' << s.count << ' ' << s.damage << '\n';
            }
        }
    }
    return out.str();
}

LevelLoadStatus decodeLevelData(std::string_view text, WorldLevelData& out) {
    std::istringstream in{std::string(text)};
    in.imbue(std::locale::classic());
    std::string header;
    if (!std::getline(in, header)) {
        return LevelLoadStatus::Corrupt;
    }
    if (!header.empty() && header.back() == '\r') header.pop_back();
    if (header.rfind(kLevelHeaderPrefix, 0) != 0) {
        return LevelLoadStatus::Corrupt;
    }
    uint32_t version = 0;
    {
        std::istringstream v(header.substr(kLevelHeaderPrefix.size()));
        if (!parseFields(v, version) || version == 0) {
            return LevelLoadStatus::Corrupt;
        }
    }
    if (version > kLevelFormatVersion) {
        return LevelLoadStatus::TooNew;
    }
    if (version == 1) {
        return decodeLegacyV1(in, out);
    }

    WorldLevelData data{};
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::istringstream fields(line);
        fields.imbue(std::locale::classic());
        std::string key;
        if (!(fields >> key)) continue;
        decodeV2Line(key, fields, data);
    }
    out = std::move(data);
    return LevelLoadStatus::Ok;
}

bool WorldStorage::saveLevelData(const WorldLevelData& data) const {
    if (m_readOnly || !ensureSaveRoot()) {
        return false;
    }
    const auto path = m_saveDir / "level.dat";
    const auto tmpPath = m_saveDir / "level.dat.tmp";
    const auto oldPath = m_saveDir / "level.dat_old";
    const auto v1BackupPath = m_saveDir / "level.dat.v1.bak";
    {
        std::ofstream ofs(tmpPath, std::ios::out | std::ios::trunc | std::ios::binary);
        if (!ofs.is_open()) {
            return false;
        }
        ofs << encodeLevelData(data);
        ofs.flush();
        if (!ofs.good()) {
            std::error_code ec;
            std::filesystem::remove(tmpPath, ec);
            return false;
        }
    }

    std::error_code ec;
    if (std::filesystem::exists(path, ec)) {
        // First save over a V1 world: keep a copy older binaries can still open.
        std::string previous;
        if (!std::filesystem::exists(v1BackupPath, ec) && readTextFile(path, previous) &&
            previous.rfind("BETACRAFT_LEVEL_V1", 0) == 0) {
            std::filesystem::copy_file(path, v1BackupPath, std::filesystem::copy_options::overwrite_existing, ec);
            if (ec) {
                core::Logger::warn("WorldStorage", "Failed to back up the V1 level.dat: ", ec.message());
            }
        }
        std::filesystem::rename(path, oldPath, ec);
        if (ec) {
            core::Logger::warn("WorldStorage", "Failed to keep level.dat_old: ", ec.message());
        }
    }
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        core::Logger::warn("WorldStorage", "Failed to replace level.dat: ", ec.message());
        return false;
    }
    return true;
}

LevelLoadStatus WorldStorage::loadLevelDataStatus(WorldLevelData& outData) const {
    const auto path = m_saveDir / "level.dat";
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        return LevelLoadStatus::Missing;
    }
    std::string text;
    if (!readTextFile(path, text)) {
        return LevelLoadStatus::Corrupt;
    }
    const LevelLoadStatus status = decodeLevelData(text, outData);
    if (status == LevelLoadStatus::Corrupt) {
        core::Logger::warn("WorldStorage", "Unreadable level.dat in ", m_saveDir.string());
    } else if (status == LevelLoadStatus::TooNew) {
        core::Logger::warn("WorldStorage", "level.dat in ", m_saveDir.string(), " is from a newer version");
    }
    return status;
}

bool WorldStorage::loadLevelData(WorldLevelData& outData) const {
    const LevelLoadStatus status = loadLevelDataStatus(outData);
    return status == LevelLoadStatus::Ok || status == LevelLoadStatus::LegacyV1;
}

namespace {

[[nodiscard]] bool writeFile(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) {
        return false;
    }
    ofs.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return ofs.good();
}

} // namespace

// --- Legacy writers (region storage disabled) --------------------------------------------

bool WorldStorage::saveLegacyChunk(const world::Chunk& chunk) const {
    if (!ensureDirectoryExists()) return false;
    return writeFile(m_legacy->chunkFilePath(chunk.getCoord()), chunk.serializeRLE());
}

bool WorldStorage::saveLegacyDecoration(core::ChunkCoord coord, const worldgen::DecorationRecord& record) const {
    if (!ensureDirectoryExists()) return false;
    return writeFile(m_legacy->decorationFilePath(coord), worldgen::encodeDecorationRecord(record));
}

bool WorldStorage::saveLegacyBlockEntities(core::ChunkCoord coord, std::span<const uint8_t> encoded) const {
    if (!ensureDirectoryExists()) return false;
    const auto path = m_legacy->blockEntityFilePath(coord);
    std::error_code ec;
    if (encoded.empty()) {
        std::filesystem::remove(path, ec);
        return !ec;
    }
    auto tmpPath = path;
    tmpPath += ".tmp";
    if (!writeFile(tmpPath, encoded)) {
        std::filesystem::remove(tmpPath, ec);
        return false;
    }
    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        core::Logger::warn("WorldStorage", "Failed to replace ", path.string(), ": ", ec.message());
        return false;
    }
    return true;
}

// --- Bundles -------------------------------------------------------------------------------

std::optional<ChunkBundle> WorldStorage::loadChunkBundle(core::ChunkCoord coord) const {
    if (m_regions->contains(coord)) {
        if (auto bytes = m_regions->read(coord)) {
            if (auto bundle = decodeBundle(*bytes)) {
                return bundle;
            }
        }
        if (m_readOnly) {
            core::Logger::warn("WorldStorage", "Unreadable region chunk ", coord.x, ",", coord.z,
                               " left untouched (read-only session)");
        } else {
            (void)m_regions->quarantine(coord);
        }
    }
    return m_legacy->readBundle(coord);
}

bool WorldStorage::saveChunkBundle(core::ChunkCoord coord, const ChunkBundle& bundle) {
    if (m_readOnly) {
        return false;
    }
    if (m_regionWrites) {
        return ensureSaveRoot() && m_regions->write(coord, encodeBundle(bundle));
    }
    // Pre-Sprint 7 order: record first (a chunk file without one would load as a pre-P3.4
    // decorated save), block entities next, so a chunk file never refers to newer contents.
    if (bundle.decoration && !saveLegacyDecoration(coord, *bundle.decoration)) return false;
    if (!saveLegacyBlockEntities(coord, bundle.blockEntities)) return false;
    if (!ensureDirectoryExists()) return false;
    return writeFile(m_legacy->chunkFilePath(coord), bundle.blocks);
}

std::optional<std::vector<uint8_t>> WorldStorage::loadChunkBundleBytes(core::ChunkCoord coord) const {
    std::optional<ChunkBundle> bundle = loadChunkBundle(coord);
    if (!bundle) return std::nullopt;
    return encodeBundle(*bundle);
}

bool WorldStorage::saveChunkBundleBytes(core::ChunkCoord coord, std::span<const uint8_t> bytes) {
    if (m_readOnly) {
        return false;
    }
    if (m_regionWrites) {
        return ensureSaveRoot() && m_regions->write(coord, bytes);
    }
    const std::optional<ChunkBundle> bundle = decodeBundle(bytes);
    return bundle && saveChunkBundle(coord, *bundle);
}

void WorldStorage::quarantineChunk(core::ChunkCoord coord) const {
    if (m_readOnly) {
        core::Logger::warn("WorldStorage", "Unreadable chunk ", coord.x, ",", coord.z, " left untouched (read-only session)");
        return;
    }
    if (m_regions->contains(coord)) {
        (void)m_regions->quarantine(coord);
    } else if (m_legacy->contains(coord)) {
        (void)m_legacy->quarantineChunk(coord);
    }
}

std::vector<world::BlockEntityEntry> WorldStorage::decodeBundleBlockEntities(core::ChunkCoord coord,
                                                                             const ChunkBundle& bundle) const {
    if (bundle.blockEntities.empty()) {
        return {};
    }
    auto entries = world::decodeBlockEntities(coord, bundle.blockEntities);
    if (entries) {
        return std::move(*entries);
    }
    // The chunk still loads, without its entities. A legacy .be is kept aside (conventions
    // §1.5); in a region the bytes stay until the chunk is next saved.
    if (!m_readOnly && !m_regions->contains(coord)) {
        std::error_code ec;
        if (std::filesystem::exists(m_legacy->blockEntityFilePath(coord), ec)) {
            (void)m_legacy->quarantineBlockEntities(coord);
            return {};
        }
    }
    core::Logger::warn("WorldStorage", "Ignoring unreadable block entities of chunk ", coord.x, ",", coord.z);
    return {};
}

bool WorldStorage::flush() const {
    return m_regions->flush();
}

bool WorldStorage::closeRegions() const {
    return m_regions->closeAll();
}

size_t WorldStorage::openRegionCount() const {
    return m_regions->openRegionCount();
}

uint64_t WorldStorage::openRegionBytes() const {
    return m_regions->openRegionBytes();
}

uint32_t WorldStorage::storageFormat() const {
    std::error_code ec;
    return m_regionWrites && !std::filesystem::exists(m_legacy->chunksDirectory(), ec) ? kRegionStorageFormat
                                                                                       : kLegacyStorageFormat;
}

bool WorldStorage::needsMigration(uint32_t levelStorageFormat) const {
    std::error_code ec;
    return m_regionWrites && !m_readOnly && levelStorageFormat < kRegionStorageFormat &&
           std::filesystem::is_directory(m_legacy->chunksDirectory(), ec);
}

MigrationReport WorldStorage::migrateLegacyChunks(const std::function<void(float)>& progress) {
    if (m_readOnly || !m_regionWrites) {
        return {};
    }
    (void)m_regions->closeAll(); // The migration opens the same region files
    return storage::migrateLegacyChunks(m_saveDir, progress);
}

// --- Pre-Sprint 7 wrappers -----------------------------------------------------------------

template <typename Edit>
bool WorldStorage::editBundle(core::ChunkCoord coord, Edit&& edit) const {
    ChunkBundle bundle;
    if (m_regions->contains(coord)) {
        auto bytes = m_regions->read(coord);
        auto decoded = bytes ? decodeBundle(*bytes) : std::nullopt;
        if (!decoded) {
            (void)m_regions->quarantine(coord); // About to be replaced: keep the bytes
        } else {
            bundle = std::move(*decoded);
        }
    }
    if (!m_regions->contains(coord)) {
        // First region write of a chunk still in chunks/: carry its other parts over.
        if (auto legacy = m_legacy->readBundle(coord)) {
            bundle = std::move(*legacy);
        }
    }
    edit(bundle);
    return ensureSaveRoot() && m_regions->write(coord, encodeBundle(bundle)) && m_regions->flush();
}

bool WorldStorage::saveDecorationRecord(core::ChunkCoord coord, const worldgen::DecorationRecord& record) const {
    if (m_readOnly) {
        return false;
    }
    if (!m_regionWrites) {
        return saveLegacyDecoration(coord, record);
    }
    return editBundle(coord, [&record](ChunkBundle& b) { b.decoration = record; });
}

std::optional<worldgen::DecorationRecord> WorldStorage::loadDecorationRecord(core::ChunkCoord coord) const {
    if (m_regions->contains(coord)) {
        if (auto bundle = loadChunkBundle(coord)) {
            return bundle->decoration;
        }
    }
    return m_legacy->readDecoration(coord);
}

bool WorldStorage::saveBlockEntities(core::ChunkCoord coord, std::span<const world::BlockEntityEntry> entries) const {
    if (m_readOnly) {
        return false;
    }
    std::vector<uint8_t> encoded;
    if (!entries.empty()) {
        encoded = world::encodeBlockEntities(entries);
    }
    if (!m_regionWrites) {
        return saveLegacyBlockEntities(coord, encoded);
    }
    return editBundle(coord, [&encoded](ChunkBundle& b) { b.blockEntities = std::move(encoded); });
}

std::vector<world::BlockEntityEntry> WorldStorage::loadBlockEntities(core::ChunkCoord coord) const {
    if (m_regions->contains(coord)) {
        if (auto bundle = loadChunkBundle(coord)) {
            return decodeBundleBlockEntities(coord, *bundle);
        }
    }
    ChunkBundle legacy;
    legacy.blockEntities = m_legacy->readBlockEntityBytes(coord);
    return decodeBundleBlockEntities(coord, legacy);
}

bool WorldStorage::saveChunk(const world::Chunk& chunk) const {
    if (m_readOnly) {
        return false;
    }
    if (!m_regionWrites) {
        return saveLegacyChunk(chunk);
    }
    return editBundle(chunk.getCoord(), [&chunk](ChunkBundle& b) { b.blocks = chunk.serializeRLE(); });
}

bool WorldStorage::loadChunk(world::Chunk& chunk) const {
    const auto bundle = loadChunkBundle(chunk.getCoord());
    if (!bundle || bundle->blocks.empty()) {
        return false;
    }
    if (chunk.deserializeRLE(bundle->blocks)) {
        return true;
    }
    quarantineChunk(chunk.getCoord());
    return false;
}

} // namespace betacraft::storage
