#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>

namespace betacraft::storage {

struct MigrationReport {
    size_t migrated{0}; // Written to a region
    size_t skipped{0};  // Already in a region (an interrupted run resumed)
    size_t corrupt{0};  // Unreadable .chk: left in chunks.migrated/, not migrated
    bool completed{false}; // chunks/ renamed (or absent); false: nothing renamed, retry later
};

// Beyond this many chunks the game shows the progress bar (Loading screen, S7.8).
inline constexpr size_t kMigrationProgressThreshold = 200;

// Number of chunks/c_X_Z.chk files a migration of `saveDir` would go through (0 without
// chunks/); above kMigrationProgressThreshold the Loading screen shows a progress bar.
[[nodiscard]] size_t countLegacyChunks(const std::filesystem::path& saveDir);

// S7.4 one-way conversion of chunks/c_X_Z.{chk,dec,be} into region bundles:
//   1. every chunk not yet in a region is read, checked (its blocks must deserialize) and
//      written as one bundle; chunks already present are skipped, so an interrupted run
//      resumes; a corrupt chunk is counted, never fatal;
//   2. every region is flushed;
//   3. chunks/ is renamed chunks.migrated/ (chunks.migrated.N/ if that exists already);
//   4. last, a V2 level.dat gets `storage_format 2` (a text edit: other keys are kept as
//      they are). A V1 or missing level.dat is left alone; the game writes the key on its
//      next save.
// Nothing is renamed after a failed write or flush. A level.dat that is too new or
// unreadable refuses the migration (read-only session, conventions §1.2). `progress` (may be
// empty) receives 0..1, and 1 only once the directory is renamed.
MigrationReport migrateLegacyChunks(const std::filesystem::path& saveDir, const std::function<void(float)>& progress);

} // namespace betacraft::storage
