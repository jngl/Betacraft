#pragma once

#include "core/Types.hpp"
#include "world/Block.hpp"
#include "world/BlockEntity.hpp"

#include <cstdint>
#include <optional>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace betacraft::world {

// Blocks that carry a block entity (Beta BlockContainer).
[[nodiscard]] constexpr bool hasBlockEntity(BlockId id) noexcept {
    return id == BlockId::Furnace || id == BlockId::LitFurnace || id == BlockId::Chest;
}

// Whether the entity of `oldId` survives an edit to `newId`: only the furnace <-> lit
// furnace swap keeps it (Beta BlockFurnace.updateFurnaceBlockState keepFurnaceInventory).
[[nodiscard]] constexpr bool keepsBlockEntity(BlockId oldId, BlockId newId) noexcept {
    const auto isFurnace = [](BlockId b) { return b == BlockId::Furnace || b == BlockId::LitFurnace; };
    return oldId == newId || (isFurnace(oldId) && isFurnace(newId));
}

// Whether `entity` is the payload the block `id` carries.
[[nodiscard]] bool blockEntityMatches(BlockId id, const BlockEntity& entity) noexcept;

using BlockEntityEntry = std::pair<core::BlockPos, BlockEntity>;

// Block entities of the resident chunks, keyed by chunk then by the cell inside it. World
// owns one; chunks hand theirs over on unload (extractChunk) and get them back from the
// .be file on load (insertChunk).
class BlockEntityStore {
public:
    [[nodiscard]] BlockEntity* find(core::BlockPos pos) noexcept;
    [[nodiscard]] const BlockEntity* find(core::BlockPos pos) const noexcept;
    // The entity at `pos`, created empty for `kind` (Furnace / LitFurnace: FurnaceData,
    // Chest: ChestData) when missing or of the wrong type.
    BlockEntity& getOrCreate(core::BlockPos pos, BlockId kind);
    // Removes and returns the entity at `pos`.
    std::optional<BlockEntity> take(core::BlockPos pos);
    // Removes every entity of the chunk and returns them (unload).
    [[nodiscard]] std::vector<BlockEntityEntry> extractChunk(core::ChunkCoord coord);
    // Copy of the chunk's entities (save), in no particular order.
    [[nodiscard]] std::vector<BlockEntityEntry> chunkEntries(core::ChunkCoord coord) const;
    // Adds the entities (load); positions outside `coord` or outside the height are ignored.
    void insertChunk(core::ChunkCoord coord, std::vector<BlockEntityEntry> entries);
    [[nodiscard]] size_t size() const noexcept;

    // fn(core::BlockPos, BlockEntity&) for every entity.
    template <class Fn>
    void forEach(Fn&& fn) {
        for (auto& [coord, cells] : m_byChunk) {
            for (auto& [key, entity] : cells) {
                fn(posOf(coord, key), entity);
            }
        }
    }

    [[nodiscard]] static uint16_t localKey(core::BlockPos pos) noexcept;
    [[nodiscard]] static core::BlockPos posOf(core::ChunkCoord coord, uint16_t key) noexcept;

private:
    std::unordered_map<core::ChunkCoord, std::unordered_map<uint16_t /*lx|lz<<4|y<<8*/, BlockEntity>> m_byChunk;
};

// .be file (conventions §1): "BCBE", u8 version (kBlockEntityFormatVersion), u16 entry count,
// then per entry u8 lx, u8 y, u8 lz, u8 kind (1 = furnace, 2 = chest) and its slots as
// u16 id, u8 count, u16 damage (furnace: input, fuel, output, then i16 burnTicks,
// burnTicksTotal, cookTicks; chest: 27 slots). Little-endian.
[[nodiscard]] std::vector<uint8_t> encodeBlockEntities(std::span<const BlockEntityEntry> entries);
// nullopt for a bad magic, an unknown (newer) version, an unknown kind, a cell outside the
// chunk, or truncated / trailing bytes. Positions are world positions inside `coord`.
[[nodiscard]] std::optional<std::vector<BlockEntityEntry>> decodeBlockEntities(core::ChunkCoord coord,
                                                                               std::span<const uint8_t> bytes);

} // namespace betacraft::world
