#pragma once

#include "storage/ChunkBundleCodec.hpp"
#include "storage/IChunkStore.hpp"

#include <filesystem>
#include <optional>
#include <vector>

namespace betacraft::storage {

// The pre-region layout, read-only: chunks/c_X_Z.chk (Chunk::serializeRLE), .dec
// (decoration record) and .be (block entities) beside it. Only the quarantine
// helpers touch the directory (renames, conventions §1.5); nothing is ever written.
class LegacyChunkStore final : public IChunkStore {
public:
    explicit LegacyChunkStore(std::filesystem::path saveDir);

    // The three files as one bundle; nullopt when there is no .chk. A .chk that cannot be
    // read whole (empty, over 16 MiB, I/O error) gives blocks that fail to deserialize, so the
    // caller quarantines it. An unreadable .dec is dropped with a warning (legacy-decorated);
    // the .be bytes are passed through undecoded.
    [[nodiscard]] std::optional<ChunkBundle> readBundle(core::ChunkCoord coord) const;
    [[nodiscard]] std::optional<worldgen::DecorationRecord> readDecoration(core::ChunkCoord coord) const;
    [[nodiscard]] std::vector<uint8_t> readBlockEntityBytes(core::ChunkCoord coord) const;
    // Every c_X_Z.chk of chunks/ (quarantined and temporary files excluded), unordered.
    [[nodiscard]] std::vector<core::ChunkCoord> listChunks() const;

    [[nodiscard]] std::optional<std::vector<uint8_t>> read(core::ChunkCoord coord) override;
    [[nodiscard]] bool contains(core::ChunkCoord coord) override;
    bool write(core::ChunkCoord, std::span<const uint8_t>) override { return false; }
    bool flush() override { return true; }

    // .chk -> .chk.corrupt and .be -> .be.corrupt, with a warning.
    bool quarantineChunk(core::ChunkCoord coord) const;
    bool quarantineBlockEntities(core::ChunkCoord coord) const;

    [[nodiscard]] std::filesystem::path chunksDirectory() const { return m_saveDir / "chunks"; }
    [[nodiscard]] std::filesystem::path chunkFilePath(core::ChunkCoord coord) const;
    [[nodiscard]] std::filesystem::path decorationFilePath(core::ChunkCoord coord) const;
    [[nodiscard]] std::filesystem::path blockEntityFilePath(core::ChunkCoord coord) const;

private:
    std::filesystem::path m_saveDir;
};

} // namespace betacraft::storage
