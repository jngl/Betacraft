#include "worldgen/FeaturePlanCodec.hpp"

#include <array>

namespace betacraft::worldgen {

namespace {

constexpr std::array<uint8_t, 4> MAGIC{'B', 'C', 'D', 'R'};
constexpr uint8_t VERSION = 1;
constexpr uint8_t FLAG_DECORATED = 0x01U;
// Far above any real plan (one feature per column at most, plus conditional duplicates);
// guards the allocation against a corrupt count.
constexpr uint32_t MAX_FEATURES = 4096;

void putU32(std::vector<uint8_t>& out, uint32_t v) {
    for (int i = 0; i < 4; ++i) out.push_back(static_cast<uint8_t>(v >> (8 * i)));
}

[[nodiscard]] uint32_t getU32(std::span<const uint8_t> in, size_t pos) noexcept {
    uint32_t v = 0;
    for (size_t i = 0; i < 4; ++i) v |= static_cast<uint32_t>(in[pos + i]) << (8 * i);
    return v;
}

} // namespace

std::vector<uint8_t> encodeDecorationRecord(const DecorationRecord& record) {
    std::vector<uint8_t> out;
    out.reserve(FEATURE_RECORD_HEADER_BYTES + record.plan.size() * FEATURE_RECORD_BYTES);
    out.insert(out.end(), MAGIC.begin(), MAGIC.end());
    out.push_back(VERSION);
    out.push_back(record.decorated ? FLAG_DECORATED : 0U);
    putU32(out, static_cast<uint32_t>(record.plan.size()));
    for (const Feature& f : record.plan) {
        out.push_back(static_cast<uint8_t>(f.type));
        putU32(out, static_cast<uint32_t>(f.origin.x));
        putU32(out, static_cast<uint32_t>(f.origin.y));
        putU32(out, static_cast<uint32_t>(f.origin.z));
        out.push_back(f.height);
        putU32(out, f.variant);
        out.push_back(static_cast<uint8_t>(f.condition));
    }
    return out;
}

std::optional<DecorationRecord> decodeDecorationRecord(std::span<const uint8_t> bytes) {
    if (bytes.size() < FEATURE_RECORD_HEADER_BYTES) return std::nullopt;
    for (size_t i = 0; i < MAGIC.size(); ++i) {
        if (bytes[i] != MAGIC[i]) return std::nullopt;
    }
    if (bytes[4] != VERSION || (bytes[5] & ~FLAG_DECORATED) != 0U) return std::nullopt;
    const uint32_t count = getU32(bytes, 6);
    if (count > MAX_FEATURES || bytes.size() != FEATURE_RECORD_HEADER_BYTES + count * FEATURE_RECORD_BYTES) {
        return std::nullopt;
    }

    DecorationRecord record;
    record.decorated = (bytes[5] & FLAG_DECORATED) != 0U;
    record.plan.reserve(count);
    for (size_t pos = FEATURE_RECORD_HEADER_BYTES; pos < bytes.size(); pos += FEATURE_RECORD_BYTES) {
        const uint8_t type = bytes[pos];
        const uint8_t condition = bytes[pos + 18];
        if (type > static_cast<uint8_t>(FeatureType::SugarCane) ||
            condition > static_cast<uint8_t>(FeatureCondition::NoAdjacentWater)) {
            return std::nullopt;
        }
        Feature f;
        f.type = static_cast<FeatureType>(type);
        f.origin = core::BlockPos{static_cast<int32_t>(getU32(bytes, pos + 1)), static_cast<int32_t>(getU32(bytes, pos + 5)),
                                  static_cast<int32_t>(getU32(bytes, pos + 9))};
        f.height = bytes[pos + 13];
        f.variant = getU32(bytes, pos + 14);
        f.condition = static_cast<FeatureCondition>(condition);
        record.plan.push_back(f);
    }
    return record;
}

} // namespace betacraft::worldgen
