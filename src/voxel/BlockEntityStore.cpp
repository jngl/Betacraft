#include "voxel/BlockEntityStore.hpp"

#include "voxel/Chunk.hpp"

#include <algorithm>
#include <array>

namespace betacraft::world {

namespace {

constexpr std::array<uint8_t, 4> kMagic{'B', 'C', 'B', 'E'};
constexpr uint8_t kKindFurnace = 1;
constexpr uint8_t kKindChest = 2;

[[nodiscard]] core::ChunkCoord chunkOfPos(core::BlockPos pos) noexcept {
    return {core::floorDiv(pos.x, Chunk::WIDTH), core::floorDiv(pos.z, Chunk::DEPTH)};
}

void putU16(std::vector<uint8_t>& out, uint16_t v) {
    out.push_back(static_cast<uint8_t>(v & 0xFFU));
    out.push_back(static_cast<uint8_t>(v >> 8U));
}

void putStack(std::vector<uint8_t>& out, const StoredStack& s) {
    const StoredStack v = s.empty() ? StoredStack{} : s;
    putU16(out, v.id);
    out.push_back(v.count);
    putU16(out, v.damage);
}

class Reader {
public:
    explicit Reader(std::span<const uint8_t> bytes) noexcept : m_bytes(bytes) {}

    [[nodiscard]] bool u8(uint8_t& v) noexcept {
        if (m_pos + 1 > m_bytes.size()) return false;
        v = m_bytes[m_pos++];
        return true;
    }
    [[nodiscard]] bool u16(uint16_t& v) noexcept {
        if (m_pos + 2 > m_bytes.size()) return false;
        v = static_cast<uint16_t>(m_bytes[m_pos] | (m_bytes[m_pos + 1] << 8U));
        m_pos += 2;
        return true;
    }
    [[nodiscard]] bool i16(int16_t& v) noexcept {
        uint16_t u = 0;
        if (!u16(u)) return false;
        v = static_cast<int16_t>(u);
        return true;
    }
    [[nodiscard]] bool stack(StoredStack& s) noexcept {
        if (!u16(s.id) || !u8(s.count) || !u16(s.damage)) return false;
        if (s.empty()) s = {};
        return true;
    }
    [[nodiscard]] bool atEnd() const noexcept { return m_pos == m_bytes.size(); }

private:
    std::span<const uint8_t> m_bytes;
    size_t m_pos{0};
};

} // namespace

bool blockEntityMatches(BlockId id, const BlockEntity& entity) noexcept {
    if (id == BlockId::Chest) {
        return std::holds_alternative<ChestData>(entity);
    }
    if (id == BlockId::Furnace || id == BlockId::LitFurnace) {
        return std::holds_alternative<FurnaceData>(entity);
    }
    return false;
}

uint16_t BlockEntityStore::localKey(core::BlockPos pos) noexcept {
    const auto lx = static_cast<uint16_t>(core::floorMod(pos.x, Chunk::WIDTH));
    const auto lz = static_cast<uint16_t>(core::floorMod(pos.z, Chunk::DEPTH));
    const auto y = static_cast<uint16_t>(pos.y & 0xFF);
    return static_cast<uint16_t>(lx | (lz << 4U) | (y << 8U));
}

core::BlockPos BlockEntityStore::posOf(core::ChunkCoord coord, uint16_t key) noexcept {
    return {coord.x * Chunk::WIDTH + (key & 0x0F), key >> 8U, coord.z * Chunk::DEPTH + ((key >> 4U) & 0x0F)};
}

BlockEntity* BlockEntityStore::find(core::BlockPos pos) noexcept {
    const auto chunkIt = m_byChunk.find(chunkOfPos(pos));
    if (chunkIt == m_byChunk.end()) return nullptr;
    const auto it = chunkIt->second.find(localKey(pos));
    return it != chunkIt->second.end() ? &it->second : nullptr;
}

const BlockEntity* BlockEntityStore::find(core::BlockPos pos) const noexcept {
    return const_cast<BlockEntityStore*>(this)->find(pos);
}

BlockEntity& BlockEntityStore::getOrCreate(core::BlockPos pos, BlockId kind) {
    BlockEntity& slot = m_byChunk[chunkOfPos(pos)][localKey(pos)];
    const bool wantChest = kind == BlockId::Chest;
    if (wantChest && !std::holds_alternative<ChestData>(slot)) {
        slot = ChestData{};
    } else if (!wantChest && !std::holds_alternative<FurnaceData>(slot)) {
        slot = FurnaceData{};
    }
    return slot;
}

std::optional<BlockEntity> BlockEntityStore::take(core::BlockPos pos) {
    const auto chunkIt = m_byChunk.find(chunkOfPos(pos));
    if (chunkIt == m_byChunk.end()) return std::nullopt;
    const auto it = chunkIt->second.find(localKey(pos));
    if (it == chunkIt->second.end()) return std::nullopt;
    std::optional<BlockEntity> out{std::move(it->second)};
    chunkIt->second.erase(it);
    if (chunkIt->second.empty()) {
        m_byChunk.erase(chunkIt);
    }
    return out;
}

std::vector<BlockEntityEntry> BlockEntityStore::extractChunk(core::ChunkCoord coord) {
    std::vector<BlockEntityEntry> out = chunkEntries(coord);
    m_byChunk.erase(coord);
    return out;
}

std::vector<BlockEntityEntry> BlockEntityStore::chunkEntries(core::ChunkCoord coord) const {
    std::vector<BlockEntityEntry> out;
    const auto it = m_byChunk.find(coord);
    if (it == m_byChunk.end()) return out;
    out.reserve(it->second.size());
    for (const auto& [key, entity] : it->second) {
        out.emplace_back(posOf(coord, key), entity);
    }
    return out;
}

void BlockEntityStore::insertChunk(core::ChunkCoord coord, std::vector<BlockEntityEntry> entries) {
    for (auto& [pos, entity] : entries) {
        if (!(chunkOfPos(pos) == coord) || pos.y < 0 || pos.y >= Chunk::HEIGHT) {
            continue;
        }
        m_byChunk[coord][localKey(pos)] = std::move(entity);
    }
}

size_t BlockEntityStore::size() const noexcept {
    size_t n = 0;
    for (const auto& [coord, cells] : m_byChunk) {
        n += cells.size();
    }
    return n;
}

std::vector<uint8_t> encodeBlockEntities(std::span<const BlockEntityEntry> entries) {
    std::vector<uint8_t> out(kMagic.begin(), kMagic.end());
    out.push_back(static_cast<uint8_t>(kBlockEntityFormatVersion));
    const size_t count = std::min<size_t>(entries.size(), 0xFFFFU); // One chunk holds far fewer
    putU16(out, static_cast<uint16_t>(count));
    for (size_t i = 0; i < count; ++i) {
        const auto& [pos, entity] = entries[i];
        out.push_back(static_cast<uint8_t>(core::floorMod(pos.x, Chunk::WIDTH)));
        out.push_back(static_cast<uint8_t>(pos.y));
        out.push_back(static_cast<uint8_t>(core::floorMod(pos.z, Chunk::DEPTH)));
        if (const auto* furnace = std::get_if<FurnaceData>(&entity)) {
            out.push_back(kKindFurnace);
            putStack(out, furnace->input);
            putStack(out, furnace->fuel);
            putStack(out, furnace->output);
            putU16(out, static_cast<uint16_t>(furnace->burnTicks));
            putU16(out, static_cast<uint16_t>(furnace->burnTicksTotal));
            putU16(out, static_cast<uint16_t>(furnace->cookTicks));
        } else {
            out.push_back(kKindChest);
            for (const StoredStack& s : std::get<ChestData>(entity).slots) {
                putStack(out, s);
            }
        }
    }
    return out;
}

std::optional<std::vector<BlockEntityEntry>> decodeBlockEntities(core::ChunkCoord coord,
                                                                 std::span<const uint8_t> bytes) {
    if (bytes.size() < kMagic.size() + 3 || !std::equal(kMagic.begin(), kMagic.end(), bytes.begin())) {
        return std::nullopt;
    }
    Reader in(bytes.subspan(kMagic.size()));
    uint8_t version = 0;
    uint16_t count = 0;
    if (!in.u8(version) || version == 0 || version > kBlockEntityFormatVersion || !in.u16(count)) {
        return std::nullopt;
    }
    std::vector<BlockEntityEntry> out;
    out.reserve(count);
    for (uint16_t i = 0; i < count; ++i) {
        uint8_t lx = 0;
        uint8_t y = 0;
        uint8_t lz = 0;
        uint8_t kind = 0;
        if (!in.u8(lx) || !in.u8(y) || !in.u8(lz) || !in.u8(kind)) {
            return std::nullopt;
        }
        if (lx >= Chunk::WIDTH || lz >= Chunk::DEPTH || y >= Chunk::HEIGHT) {
            return std::nullopt;
        }
        const core::BlockPos pos{coord.x * Chunk::WIDTH + lx, y, coord.z * Chunk::DEPTH + lz};
        if (kind == kKindFurnace) {
            FurnaceData f{};
            if (!in.stack(f.input) || !in.stack(f.fuel) || !in.stack(f.output) || !in.i16(f.burnTicks) ||
                !in.i16(f.burnTicksTotal) || !in.i16(f.cookTicks)) {
                return std::nullopt;
            }
            out.emplace_back(pos, f);
        } else if (kind == kKindChest) {
            ChestData c{};
            for (StoredStack& s : c.slots) {
                if (!in.stack(s)) {
                    return std::nullopt;
                }
            }
            out.emplace_back(pos, c);
        } else {
            return std::nullopt;
        }
    }
    if (!in.atEnd()) {
        return std::nullopt;
    }
    return out;
}

} // namespace betacraft::world
