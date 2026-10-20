#include "worldgen/Dungeons.hpp"

#include "voxel/Block.hpp"
#include "voxel/Chunk.hpp"

#include <algorithm>

namespace betacraft::worldgen {

namespace {

using voxel::BlockId;
using voxel::Chunk;

// Beta Material.isSolid: water, lava, air, plants and torches are not.
[[nodiscard]] bool solidAt(const Chunk& chunk, int32_t x, int32_t y, int32_t z) noexcept {
    if (y < 0 || y >= Chunk::HEIGHT) return false;
    return voxel::BlockRegistry::isSolid(chunk.getBlock(x, y, z));
}

[[nodiscard]] bool airAt(const Chunk& chunk, int32_t x, int32_t y, int32_t z) noexcept {
    return y >= 0 && y < Chunk::HEIGHT && chunk.getBlock(x, y, z) == BlockId::Air;
}

void put(Chunk& chunk, int32_t x, int32_t y, int32_t z, BlockId id, uint8_t meta = 0) noexcept {
    chunk.setBlock(x, y, z, id, false);
    chunk.setMetadata(x, y, z, meta, false);
}

} // namespace

std::vector<DungeonInfo> placeDungeons(Chunk& chunk, uint64_t seed, const HeightSampler& heights) {
    std::vector<DungeonInfo> placed;
    const core::ChunkCoord coord = chunk.getCoord();
    const int32_t baseX = coord.x * Chunk::WIDTH;
    const int32_t baseZ = coord.z * Chunk::DEPTH;
    SeededRandom rng(chunkSeed(seed ^ kDungeonSalt, coord));
    constexpr int32_t h = kDungeonInnerHeight;

    for (int32_t attempt = 0; attempt < kDungeonAttempts; ++attempt) {
        const auto originSpan = static_cast<uint32_t>(kDungeonMaxOrigin - kDungeonMinOrigin + 1);
        const int32_t x = kDungeonMinOrigin + static_cast<int32_t>(rng.nextBounded(originSpan));
        const int32_t y = static_cast<int32_t>(rng.nextBounded(static_cast<uint32_t>(Chunk::HEIGHT)));
        const int32_t z = kDungeonMinOrigin + static_cast<int32_t>(rng.nextBounded(originSpan));
        const int32_t rx = static_cast<int32_t>(rng.nextBounded(2U)) + 2;
        const int32_t rz = static_cast<int32_t>(rng.nextBounded(2U)) + 2;
        if (y - 1 < 1 || y + h + 1 >= Chunk::HEIGHT) {
            continue;
        }

        // Ceiling under every column's surface - 3, then Beta's floor / ceiling / openings rule.
        bool ok = true;
        int32_t openings = 0;
        for (int32_t xx = x - rx - 1; ok && xx <= x + rx + 1; ++xx) {
            for (int32_t zz = z - rz - 1; ok && zz <= z + rz + 1; ++zz) {
                const int32_t surface = heights.sampleColumn(baseX + xx, baseZ + zz).surfaceHeight;
                if (y + h + 1 > surface - kDungeonSurfaceClearance || !solidAt(chunk, xx, y - 1, zz) ||
                    !solidAt(chunk, xx, y + h + 1, zz)) {
                    ok = false;
                    break;
                }
                const bool wall = xx == x - rx - 1 || xx == x + rx + 1 || zz == z - rz - 1 || zz == z + rz + 1;
                if (wall && airAt(chunk, xx, y, zz) && airAt(chunk, xx, y + 1, zz)) {
                    ++openings;
                }
            }
        }
        if (!ok || openings < kDungeonMinOpenings || openings > kDungeonMaxOpenings) {
            continue;
        }

        // Top-down like Beta, so "nothing solid below" reads the cell under before it changes.
        for (int32_t xx = x - rx - 1; xx <= x + rx + 1; ++xx) {
            for (int32_t yy = y + h; yy >= y - 1; --yy) {
                for (int32_t zz = z - rz - 1; zz <= z + rz + 1; ++zz) {
                    const bool shell = xx == x - rx - 1 || yy == y - 1 || zz == z - rz - 1 || xx == x + rx + 1 ||
                                       zz == z + rz + 1;
                    if (!shell) {
                        put(chunk, xx, yy, zz, BlockId::Air);
                    } else if (!solidAt(chunk, xx, yy - 1, zz)) {
                        put(chunk, xx, yy, zz, BlockId::Air);
                    } else if (solidAt(chunk, xx, yy, zz)) {
                        const bool mossy = yy == y - 1 && rng.nextBounded(4U) != 0U;
                        put(chunk, xx, yy, zz, mossy ? BlockId::MossyCobble : BlockId::Cobblestone);
                    }
                }
            }
        }

        DungeonInfo info{{baseX + x, y, baseZ + z}, rx, rz, openings, {}};
        // Chests against exactly one wall (Beta: 2 chests, 3 tries each).
        for (int32_t c = 0; c < kDungeonChests; ++c) {
            for (int32_t t = 0; t < kDungeonChestTries; ++t) {
                const int32_t cx = x + static_cast<int32_t>(rng.nextBounded(static_cast<uint32_t>(rx * 2 + 1))) - rx;
                const int32_t cz = z + static_cast<int32_t>(rng.nextBounded(static_cast<uint32_t>(rz * 2 + 1))) - rz;
                if (!airAt(chunk, cx, y, cz)) {
                    continue;
                }
                const int32_t walls = (solidAt(chunk, cx - 1, y, cz) ? 1 : 0) + (solidAt(chunk, cx + 1, y, cz) ? 1 : 0) +
                                      (solidAt(chunk, cx, y, cz - 1) ? 1 : 0) + (solidAt(chunk, cx, y, cz + 1) ? 1 : 0);
                if (walls == 1) {
                    put(chunk, cx, y, cz, BlockId::Chest, kChestLootPendingMeta);
                    info.chests.push_back({baseX + cx, y, baseZ + cz});
                    break;
                }
            }
        }
        put(chunk, x, y, z, BlockId::MobSpawner);
        placed.push_back(std::move(info));
    }
    return placed;
}

std::optional<LootStack> pickCheckLootItem(SeededRandom& rng) noexcept {
    namespace items = gameplay::items;
    const auto upTo4 = [&rng]() { return static_cast<int32_t>(rng.nextBounded(4U)) + 1; };
    switch (rng.nextBounded(11U)) {
        case 0: return std::nullopt; // Saddle (not in the game)
        case 1: return LootStack{items::IronIngot, upTo4()};
        case 2: return LootStack{items::Bread, 1};
        case 3: return LootStack{items::Wheat, upTo4()};
        case 4: return LootStack{items::Gunpowder, upTo4()};
        case 5: return LootStack{items::String, upTo4()};
        case 6: return LootStack{items::Bucket, 1};
        case 7:
            if (rng.nextBounded(100U) == 0U) return LootStack{items::GoldenApple, 1};
            return std::nullopt;
        case 8:
            if (rng.nextBounded(2U) == 0U) return LootStack{items::Redstone, upTo4()};
            return std::nullopt;
        case 9:
            if (rng.nextBounded(10U) == 0U) (void)rng.nextBounded(2U); // A record: not in the game
            return std::nullopt;
        default: return std::nullopt; // 10: cocoa beans (dye 351:3), not in the game
    }
}

std::array<voxel::StoredStack, voxel::kChestSlots> rollDungeonChestLoot(uint64_t worldSeed, core::BlockPos pos) noexcept {
    std::array<voxel::StoredStack, voxel::kChestSlots> slots{};
    const uint64_t ux = static_cast<uint32_t>(pos.x);
    const uint64_t uy = static_cast<uint32_t>(pos.y);
    const uint64_t uz = static_cast<uint32_t>(pos.z);
    SeededRandom rng(mix64(mix64(mix64(worldSeed ^ kDungeonSalt ^ (ux * 0xD1B54A32D192ED03ULL)) ^
                                 (uy * 0x8CB92BA72F3D8DD7ULL)) ^
                           (uz * 0xABC98388FB8FAC03ULL)));
    for (int32_t i = 0; i < kDungeonChestDraws; ++i) {
        if (const std::optional<LootStack> loot = pickCheckLootItem(rng)) {
            const auto slot = static_cast<size_t>(rng.nextBounded(static_cast<uint32_t>(voxel::kChestSlots)));
            slots[slot] = voxel::StoredStack{loot->id, static_cast<uint8_t>(std::clamp(loot->count, 1, 64)), 0};
        }
    }
    return slots;
}

} // namespace betacraft::worldgen
