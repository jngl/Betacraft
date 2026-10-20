#include "storage/ChunkBundleCodec.hpp"

#include "core/Logger.hpp"

namespace betacraft::storage {

namespace {

void appendRecord(std::vector<uint8_t>& out, uint8_t tag, std::span<const uint8_t> bytes) {
    out.push_back(tag);
    const auto len = static_cast<uint32_t>(bytes.size());
    for (int shift = 0; shift < 32; shift += 8) {
        out.push_back(static_cast<uint8_t>(len >> shift));
    }
    out.insert(out.end(), bytes.begin(), bytes.end());
}

} // namespace

std::vector<uint8_t> encodeBundle(const ChunkBundle& bundle) {
    std::vector<uint8_t> out(1, BUNDLE_V1);
    out.reserve(1 + 3 * 5 + bundle.blocks.size() + bundle.blockEntities.size() + 64);
    if (!bundle.blocks.empty()) {
        appendRecord(out, kBundleTagBlocks, bundle.blocks);
    }
    if (bundle.decoration) {
        appendRecord(out, kBundleTagDecoration, worldgen::encodeDecorationRecord(*bundle.decoration));
    }
    if (!bundle.blockEntities.empty()) {
        appendRecord(out, kBundleTagBlockEntities, bundle.blockEntities);
    }
    return out;
}

std::optional<ChunkBundle> decodeBundle(std::span<const uint8_t> bytes) {
    if (bytes.empty() || bytes[0] == 0 || bytes[0] > BUNDLE_V1) {
        return std::nullopt;
    }
    ChunkBundle bundle;
    size_t pos = 1;
    while (pos < bytes.size()) {
        if (bytes.size() - pos < 5) {
            return std::nullopt;
        }
        const uint8_t tag = bytes[pos];
        const uint32_t len = static_cast<uint32_t>(bytes[pos + 1]) | (static_cast<uint32_t>(bytes[pos + 2]) << 8) |
                             (static_cast<uint32_t>(bytes[pos + 3]) << 16) | (static_cast<uint32_t>(bytes[pos + 4]) << 24);
        pos += 5;
        if (len > bytes.size() - pos) {
            return std::nullopt;
        }
        const std::span<const uint8_t> body = bytes.subspan(pos, len);
        pos += len;
        switch (tag) {
        case kBundleTagBlocks:
            bundle.blocks.assign(body.begin(), body.end());
            break;
        case kBundleTagDecoration:
            bundle.decoration = worldgen::decodeDecorationRecord(body);
            if (!bundle.decoration) {
                core::Logger::warn("ChunkBundle", "Ignoring corrupt decoration record");
            }
            break;
        case kBundleTagBlockEntities:
            bundle.blockEntities.assign(body.begin(), body.end());
            break;
        default:
            break; // Written by a newer version: skipped
        }
    }
    return bundle;
}

} // namespace betacraft::storage
