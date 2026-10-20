#include "storage/SaveMigration.hpp"

#include "core/Logger.hpp"
#include "storage/LegacyChunkStore.hpp"
#include "storage/RegionChunkStore.hpp"
#include "storage/WorldStorage.hpp"
#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>

namespace betacraft::storage {

namespace {

[[nodiscard]] std::filesystem::path migratedDirectory(const std::filesystem::path& saveDir) {
    std::filesystem::path target = saveDir / "chunks.migrated";
    std::error_code ec;
    for (int n = 1; std::filesystem::exists(target, ec); ++n) {
        target = saveDir / ("chunks.migrated." + std::to_string(n));
    }
    return target;
}

// Appends (or replaces) the storage_format line of a V2 level.dat, keeping every other line
// byte for byte, with the atomic tmp / level.dat_old / rename sequence of saveLevelData.
[[nodiscard]] bool writeStorageFormat(const std::filesystem::path& saveDir) {
    const auto path = saveDir / "level.dat";
    std::string text;
    {
        std::ifstream in(path, std::ios::binary);
        if (!in.is_open()) return false;
        std::ostringstream oss;
        oss << in.rdbuf();
        text = oss.str();
    }
    std::istringstream lines(text);
    std::string line;
    std::string out;
    while (std::getline(lines, line)) {
        std::string_view key(line);
        if (key.rfind("storage_format", 0) == 0 && (key.size() == 14 || key[14] == ' ' || key[14] == '\t')) {
            continue;
        }
        out += line;
        out += '\n';
    }
    out += "storage_format " + std::to_string(storage::kRegionStorageFormat) + "\n";

    const auto tmpPath = saveDir / "level.dat.tmp";
    {
        std::ofstream ofs(tmpPath, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) return false;
        ofs << out;
        ofs.flush();
        if (!ofs.good()) {
            std::error_code ec;
            std::filesystem::remove(tmpPath, ec);
            return false;
        }
    }
    std::error_code ec;
    std::filesystem::rename(path, saveDir / "level.dat_old", ec);
    std::filesystem::rename(tmpPath, path, ec);
    return !ec;
}

} // namespace

size_t countLegacyChunks(const std::filesystem::path& saveDir) {
    const LegacyChunkStore legacy(saveDir);
    std::error_code ec;
    if (!std::filesystem::is_directory(legacy.chunksDirectory(), ec)) {
        return 0;
    }
    return legacy.listChunks().size();
}

MigrationReport migrateLegacyChunks(const std::filesystem::path& saveDir, const std::function<void(float)>& progress) {
    MigrationReport report;
    const auto report01 = [&progress](float v) {
        if (progress) progress(v);
    };

    storage::WorldLevelData level;
    const storage::WorldStorage levelStorage(saveDir);
    const storage::LevelLoadStatus status = levelStorage.loadLevelDataStatus(level);
    if (status == storage::LevelLoadStatus::TooNew || status == storage::LevelLoadStatus::Corrupt) {
        core::Logger::warn("SaveMigration", "level.dat of ", saveDir.string(), " is not readable: migration refused");
        return report;
    }

    const LegacyChunkStore legacy(saveDir);
    std::error_code ec;
    if (!std::filesystem::is_directory(legacy.chunksDirectory(), ec)) {
        report.completed = true; // Nothing (left) to migrate
        return report;
    }

    std::vector<core::ChunkCoord> coords = legacy.listChunks();
    std::sort(coords.begin(), coords.end(), [](core::ChunkCoord a, core::ChunkCoord b) {
        return a.x != b.x ? a.x < b.x : a.z < b.z;
    });
    world::BlockRegistry::initialize();
    {
        RegionChunkStore regions(saveDir / "region");
        for (size_t i = 0; i < coords.size(); ++i) {
            report01(static_cast<float>(i) / static_cast<float>(coords.size() + 1));
            const core::ChunkCoord coord = coords[i];
            if (regions.contains(coord)) {
                ++report.skipped;
                continue;
            }
            std::optional<ChunkBundle> bundle = legacy.readBundle(coord);
            world::Chunk check(coord);
            if (!bundle || !check.deserializeRLE(bundle->blocks)) {
                core::Logger::warn("SaveMigration", "Chunk ", coord.x, ",", coord.z, " is unreadable: not migrated");
                ++report.corrupt;
                continue;
            }
            // The raw bytes, not a re-serialisation: nothing the reader dropped is lost.
            if (!regions.write(coord, encodeBundle(*bundle))) {
                core::Logger::error("SaveMigration", "Writing chunk ", coord.x, ",", coord.z, " failed: migration stopped");
                return report;
            }
            ++report.migrated;
        }
        if (!regions.closeAll()) {
            core::Logger::error("SaveMigration", "Flushing the regions failed: migration stopped");
            return report;
        }
    }

    const std::filesystem::path target = migratedDirectory(saveDir);
    std::filesystem::rename(legacy.chunksDirectory(), target, ec);
    if (ec) {
        core::Logger::error("SaveMigration", "Cannot rename chunks/ to ", target.string(), ": ", ec.message());
        return report;
    }
    report.completed = true;
    report01(1.0f);

    if (status == storage::LevelLoadStatus::Ok && !writeStorageFormat(saveDir)) {
        core::Logger::warn("SaveMigration", "Could not record storage_format 2 in level.dat (written at next save)");
    }
    core::Logger::info("SaveMigration", "Migrated ", report.migrated, " chunks (", report.skipped, " already done, ",
                       report.corrupt, " corrupt) to ", (saveDir / "region").string());
    return report;
}

} // namespace betacraft::storage
