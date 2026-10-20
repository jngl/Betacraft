#pragma once

#include "worldgen/FeaturePlanCodec.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace betacraft::storage {

// S7.2 everything persisted for one chunk, written as a single region entry so that blocks,
// decoration record and block entities can never disagree after a crash (it replaces the
// ".dec, then .be, then .chk" write order).
//
// Layout: version byte BUNDLE_V1, then sub-records {uint8 tag, uint32 len (little-endian),
// len bytes}:
//   'B' Chunk::serializeRLE()                 (absent: no chunk stored)
//   'D' worldgen::encodeDecorationRecord      (absent: saved before P3.4, treated as decorated)
//   'E' voxel::encodeBlockEntities (S2.4)     (absent: no block entity)
// Unknown tags are skipped, so later additions stay readable by this version.
inline constexpr uint8_t BUNDLE_V1 = 1;
inline constexpr uint8_t kBundleTagBlocks = 'B';
inline constexpr uint8_t kBundleTagDecoration = 'D';
inline constexpr uint8_t kBundleTagBlockEntities = 'E';

struct ChunkBundle {
    std::vector<uint8_t> blocks;
    std::optional<worldgen::DecorationRecord> decoration;
    std::vector<uint8_t> blockEntities;
};

[[nodiscard]] std::vector<uint8_t> encodeBundle(const ChunkBundle& bundle);
// nullopt for a wrong version byte or a sub-record overrunning the buffer. A 'D' record that
// fails to decode is dropped with a warning (the chunk then loads as legacy-decorated, as an
// unreadable .dec did) rather than losing the whole chunk.
[[nodiscard]] std::optional<ChunkBundle> decodeBundle(std::span<const uint8_t> bytes);

} // namespace betacraft::storage
