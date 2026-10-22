#pragma once

#include "core/Types.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace betacraft::storage {

// S7.3 where chunk bundles (ChunkBundleCodec) live: RegionChunkStore (region/r.X.Z.bcr, the
// current format) or LegacyChunkStore (chunks/c_X_Z.{chk,dec,be}, read-only). WorldStorage is
// the facade the game uses; the migration (SaveMigration) copies one into the other.
class IChunkStore {
public:
    virtual ~IChunkStore() = default;

    // Encoded bundle bytes; nullopt when the chunk is absent or its entry unreadable.
    [[nodiscard]] virtual std::optional<std::vector<uint8_t>> read(core::ChunkCoord coord) = 0;
    [[nodiscard]] virtual bool contains(core::ChunkCoord coord) = 0;
    // False when the store is read-only or the write failed (the previous bundle then stays).
    virtual bool write(core::ChunkCoord coord, std::span<const uint8_t> bundleBytes) = 0;
    // Makes every write so far durable.
    virtual bool flush() = 0;
};

} // namespace betacraft::storage
