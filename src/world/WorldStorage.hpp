#pragma once

#include "core/Config.hpp"
#include "core/Types.hpp"
#include "storage/ChunkBundleCodec.hpp"
#include "storage/SaveMigration.hpp"
#include "world/BlockEntityStore.hpp"
#include "world/Chunk.hpp"
#include "world/Weather.hpp"
#include "worldgen/FeaturePlanCodec.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace betacraft::storage {
class LegacyChunkStore;
class RegionChunkStore;
}

namespace betacraft::world {

// Save format versions (roadmap conventions §1): a reader accepts any version up to its own
// and refuses newer ones without overwriting them.
inline constexpr uint32_t kLevelFormatVersion = 2;
inline constexpr uint32_t kBlockEntityFormatVersion = 1;
// level.dat `items_scheme` (conventions §1.6): 1 = legacy ids 100..105 for stick, coal and
// four pickaxes; 2 (S2.10, always written) = Beta item numbers only. A file without the key
// is scheme 1; the loaders fold the legacy ids whatever the scheme.
inline constexpr uint32_t kLegacyItemsScheme = 1;
inline constexpr uint32_t kCurrentItemsScheme = 2;
// level.dat `storage_format` (S7.4, conventions §1): 1 (key absent) = chunks/c_X_Z.{chk,dec,be};
// 2 = region/r.X.Z.bcr bundles, written once the legacy chunks are migrated.
inline constexpr uint32_t kLegacyStorageFormat = 1;
inline constexpr uint32_t kRegionStorageFormat = 2;

// One inventory stack as level.dat stores it (ids in the `items_scheme` of the file).
struct SavedStack {
    uint16_t id{0};
    int32_t count{0};
    int32_t damage{0};

    constexpr bool operator==(const SavedStack&) const noexcept = default;
};

// Everything about the player that survives a restart, besides position and look.
struct PlayerSaveData {
    core::GameMode mode{core::GameMode::Survival};
    int32_t health{20};
    int32_t air{300};
    int32_t selectedSlot{0};
    int32_t fireTicks{0};
    float fallDistance{0.0f};
    std::array<SavedStack, 36> inventory{};
    // S6.8 key `armor <slot> <id> <count> <damage>` (slot = gameplay::ArmorSlot: helmet 0 ..
    // boots 3), written only for worn pieces; a missing key leaves the slot empty.
    std::array<SavedStack, 4> armor{};
};

struct WorldLevelData {
    uint64_t seed{173173173ULL};
    glm::vec3 playerPos{0.0f, 80.0f, 0.0f};
    float playerYaw{-90.0f};
    float playerPitch{0.0f};
    float timeOfDay{0.25f}; // 0.0=sunrise, 0.25=noon, 0.5=sunset, 0.75=midnight
    uint32_t itemsScheme{kLegacyItemsScheme}; // Of the file; the game writes kCurrentItemsScheme
    std::optional<glm::vec3> spawnPos;
    std::optional<PlayerSaveData> player; // Absent in V1 saves
    // S5.8 key "bed": head cell of the bed the player last slept in; spawnPos is then next to
    // it. Absent when the spawn is the world's.
    std::optional<core::BlockPos> bedPos;
    // S7.4 key "storage_format", written only when >= 2 (absent = kLegacyStorageFormat).
    uint32_t storageFormat{kLegacyStorageFormat};
    // S7.8 key "name": the world's display name (the rest of the line; empty = absent, the
    // World Select screen then shows the folder name).
    std::string name;
    // S8.9 keys "weather.raining", "weather.thundering", "weather.rainTime",
    // "weather.thunderTime", "weather.rainStrength" and "weather.thunderStrength" (no version
    // change). Missing keys keep the clear default.
    WeatherState weather{};
};

enum class LevelLoadStatus : uint8_t {
    Missing,  // No level.dat: a new world
    Ok,       // V2
    LegacyV1, // Positional V1: no player data
    Corrupt,  // Unreadable header or V1 body
    TooNew    // Written by a newer engine: never overwritten
};

// level.dat V2 text ("key value" lines under the BETACRAFT_LEVEL_V2 header). The decoder
// also reads V1, ignores unknown keys and malformed lines, and keeps defaults for missing
// keys; `out` is only meaningful for Ok and LegacyV1.
[[nodiscard]] std::string encodeLevelData(const WorldLevelData& data);
[[nodiscard]] LevelLoadStatus decodeLevelData(std::string_view text, WorldLevelData& out);

// The save directory: level.dat plus the chunk stores (S7.3 facade). Chunks are written as
// one storage::ChunkBundle per chunk into region files (region/r.X.Z.bcr); reads look in the
// regions first, then in the legacy chunks/ files of a save not migrated yet (read-only), so
// an old save loads losslessly with or without migration. With region storage disabled
// (FeatureFlags::regionStorage, --no-region-storage) and no region/ directory yet, the legacy
// files are written as before Sprint 7.
class WorldStorage {
public:
    explicit WorldStorage(std::filesystem::path saveDir);
    ~WorldStorage();
    WorldStorage(WorldStorage&&) noexcept;
    WorldStorage& operator=(WorldStorage&&) noexcept;

    // Creates the save directory and its legacy chunks/ folder (kept for old callers; the
    // game's own saves only create what they write).
    [[nodiscard]] bool ensureDirectoryExists() const;

    // Read-only session (level.dat too new or unreadable, conventions §1.2): every save
    // fails and an unreadable chunk is skipped without being quarantined, so nothing in the
    // save directory is renamed or written.
    void setReadOnly(bool readOnly) noexcept;
    [[nodiscard]] bool isReadOnly() const noexcept { return m_readOnly; }

    // Region writes (the default). Turning them off has no effect once region/ exists: the
    // regions would shadow anything written to chunks/.
    void setRegionStorageEnabled(bool enabled);
    [[nodiscard]] bool isRegionStorageActive() const noexcept { return m_regionWrites; }
    // What level.dat should record: kRegionStorageFormat once chunks are written to regions
    // and no legacy chunks/ directory is left to migrate, kLegacyStorageFormat otherwise.
    [[nodiscard]] uint32_t storageFormat() const;
    // S7.4 trigger: region storage active, storage_format below 2 and a chunks/ directory.
    [[nodiscard]] bool needsMigration(uint32_t levelStorageFormat) const;
    // Runs storage::migrateLegacyChunks on this save (regions are closed first). Refused
    // (report not completed) for a read-only session.
    storage::MigrationReport migrateLegacyChunks(const std::function<void(float)>& progress);

    // Always writes V2, atomically: level.dat.tmp, then the old file becomes level.dat_old
    // and the .tmp is renamed over level.dat. A V1 file is first copied once to
    // level.dat.v1.bak (binaries predating V2 cannot read the new one).
    [[nodiscard]] bool saveLevelData(const WorldLevelData& data) const;
    [[nodiscard]] LevelLoadStatus loadLevelDataStatus(WorldLevelData& outData) const;
    // True for Ok and LegacyV1.
    [[nodiscard]] bool loadLevelData(WorldLevelData& outData) const;

    // S7.3 one chunk's bundle (region first, then legacy files). A region entry that cannot be
    // read or decoded is quarantined (outside read-only sessions) and the legacy files are
    // tried next. `blocks` is empty when only side records were saved for the coordinate.
    [[nodiscard]] std::optional<storage::ChunkBundle> loadChunkBundle(core::ChunkCoord coord) const;
    // One atomic write of the whole chunk (not durable until flush()).
    [[nodiscard]] bool saveChunkBundle(core::ChunkCoord coord, const storage::ChunkBundle& bundle);
    [[nodiscard]] std::vector<uint8_t> encodeChunkBundle(const storage::ChunkBundle& bundle) const {
        return storage::encodeBundle(bundle);
    }
    // S7.6 the same two operations on encoded bundles, for the I/O worker thread (safe against
    // the main thread's own loads and saves of other chunks). Reading a legacy chunk re-encodes
    // its three files as one bundle; with region storage off the bytes are decoded and written
    // as legacy files.
    [[nodiscard]] std::optional<std::vector<uint8_t>> loadChunkBundleBytes(core::ChunkCoord coord) const;
    [[nodiscard]] bool saveChunkBundleBytes(core::ChunkCoord coord, std::span<const uint8_t> bytes);
    // Blocks of the chunk's bundle failed to deserialize: keep the bytes aside (conventions
    // §1.5) so the regenerated chunk does not overwrite them. Nothing happens when read-only.
    void quarantineChunk(core::ChunkCoord coord) const;
    // The bundle's block entities; an unreadable record is quarantined like a .be was.
    [[nodiscard]] std::vector<BlockEntityEntry> decodeBundleBlockEntities(core::ChunkCoord coord,
                                                                         const storage::ChunkBundle& bundle) const;
    // Makes every region write durable (data fsynced, then headers).
    bool flush() const;
    // Flushes and closes the open region files.
    bool closeRegions() const;
    [[nodiscard]] size_t openRegionCount() const;
    [[nodiscard]] uint64_t openRegionBytes() const;

    // Pre-Sprint 7 entry points, kept as wrappers over the bundle of the chunk: each one
    // reads the current bundle, replaces its part and writes it back (durably).
    [[nodiscard]] bool saveChunk(const Chunk& chunk) const;
    // False when there is no chunk. An unreadable one is quarantined with a warning
    // (.chk.corrupt for a legacy file) so the regenerated chunk does not silently overwrite it.
    [[nodiscard]] bool loadChunk(Chunk& chunk) const;

    // P3.4 decoration record kept with the chunk. Saves predating it have none.
    [[nodiscard]] bool saveDecorationRecord(core::ChunkCoord coord, const worldgen::DecorationRecord& record) const;
    [[nodiscard]] std::optional<worldgen::DecorationRecord> loadDecorationRecord(core::ChunkCoord coord) const;

    // S2.4 block entities (furnaces, chests) kept with the chunk; legacy files are written
    // atomically (.be.tmp renamed over .be). No entities: none stored. Saves predating it have none.
    [[nodiscard]] bool saveBlockEntities(core::ChunkCoord coord, std::span<const BlockEntityEntry> entries) const;
    // Empty when there are none; an unreadable record is ignored with a warning.
    [[nodiscard]] std::vector<BlockEntityEntry> loadBlockEntities(core::ChunkCoord coord) const;

    [[nodiscard]] const std::filesystem::path& directory() const noexcept { return m_saveDir; }

private:
    [[nodiscard]] bool ensureSaveRoot() const;
    // Read-modify-write of one chunk's bundle through `edit`, flushed (wrappers).
    template <typename Edit>
    [[nodiscard]] bool editBundle(core::ChunkCoord coord, Edit&& edit) const;
    // Pre-Sprint 7 writers (region storage disabled).
    [[nodiscard]] bool saveLegacyChunk(const Chunk& chunk) const;
    [[nodiscard]] bool saveLegacyDecoration(core::ChunkCoord coord, const worldgen::DecorationRecord& record) const;
    [[nodiscard]] bool saveLegacyBlockEntities(core::ChunkCoord coord, std::span<const uint8_t> encoded) const;

    std::filesystem::path m_saveDir;
    bool m_readOnly{false};
    bool m_regionWrites{true};
    std::unique_ptr<storage::RegionChunkStore> m_regions;
    std::unique_ptr<storage::LegacyChunkStore> m_legacy;
};

} // namespace betacraft::world
