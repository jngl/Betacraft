#pragma once

#include "worldgen/DecorationFeatures.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace betacraft::worldgen {

// What World persists next to a saved chunk for the two-pass decoration: whether pass 2 already ran on it and
// the plan it was registered with (DecorationPipeline::findPlan). The plan is required for
// an undecorated chunk whose neighbour already consumed it (re-planning edited terrain would
// cut that neighbour's trees) and lets a decorated chunk feed neighbours still waiting.
struct DecorationRecord {
    bool decorated{false};
    FeaturePlan plan;
};

// Layout (little-endian): "BCDR", version u8, flags u8 (bit 0 = decorated), count u32,
// then `count` records of type u8, x/y/z i32, height u8, variant u32, condition u8.
inline constexpr size_t FEATURE_RECORD_HEADER_BYTES = 10;
inline constexpr size_t FEATURE_RECORD_BYTES = 19;

[[nodiscard]] std::vector<uint8_t> encodeDecorationRecord(const DecorationRecord& record);
// nullopt for anything malformed (bad magic/version, unknown enum values or flag bits,
// size not matching the count) - the caller then treats the chunk as having no record.
[[nodiscard]] std::optional<DecorationRecord> decodeDecorationRecord(std::span<const uint8_t> bytes);

} // namespace betacraft::worldgen
