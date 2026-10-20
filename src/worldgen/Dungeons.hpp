#pragma once

#include "core/Types.hpp"
#include "gameplay/ItemIds.hpp"
#include "voxel/BlockEntity.hpp"
#include "worldgen/CaveCarver.hpp"
#include "worldgen/SeededRandom.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace betacraft::world { class Chunk; }

namespace betacraft::worldgen {

// S6.10 Beta 1.7.3 dungeons (WorldGenDungeons), placed in pass 1 after the ore veins. Unlike
// Beta, whose rooms may straddle chunks, a room stays inside its chunk so pass 1 remains a
// function of the chunk alone and DecorationPipeline / FeaturePlanCodec are untouched.

// Surface of the pass-1 columns (ColumnContext::surfaceHeight), read before the caves.
using HeightSampler = ColumnSampler;

inline constexpr int32_t kDungeonAttempts = 8;     // Beta: 8 tries per chunk
inline constexpr int32_t kDungeonInnerHeight = 3;  // Air from y to y + 2, ceiling at y + 4
inline constexpr int32_t kDungeonMinOrigin = 4;    // Local origin in [4, 11]: walls stay inside
inline constexpr int32_t kDungeonMaxOrigin = 11;
inline constexpr int32_t kDungeonSurfaceClearance = 3; // Ceiling at most surface - 3
inline constexpr int32_t kDungeonMinOpenings = 1;
inline constexpr int32_t kDungeonMaxOpenings = 5;
inline constexpr int32_t kDungeonChests = 2;
inline constexpr int32_t kDungeonChestTries = 3;
inline constexpr int32_t kDungeonChestDraws = 8; // pickCheckLootItem draws per chest
inline constexpr uint64_t kDungeonSalt = 0xD0D6E0115EEDULL;

// A Chest block with this metadata still has its loot to draw (on first access).
inline constexpr uint8_t kChestLootPendingMeta = 1;

struct DungeonInfo {
    core::BlockPos spawner{}; // World position of the MobSpawner (the room's floor centre)
    int32_t radiusX{2};       // Inner half-size: the walls are at +-(radius + 1)
    int32_t radiusZ{2};
    int32_t openings{0};      // Wall cells open on two levels before the room was built
    std::vector<core::BlockPos> chests;
};

// Beta WorldGenDungeons.generate, 8 tries: a room of 5..7 x 3 x 5..7 air (walls 7..9 x 5 x
// 7..9) whose floor and ceiling are solid and whose walls open 1..5 times onto air; walls of
// cobblestone, a floor of mossy cobblestone 3 times in 4, a MobSpawner in the middle and up to
// 2 chests (Chest, meta kChestLootPendingMeta) against a wall. Returns the rooms placed.
std::vector<DungeonInfo> placeDungeons(world::Chunk& chunk, uint64_t seed, const HeightSampler& heights);

struct LootStack {
    gameplay::ItemId id{gameplay::kNoItem};
    int32_t count{1};
};

// Beta WorldGenDungeons.pickCheckLootItem: one draw out of 11; the saddle, the records and the
// cocoa beans are not in the game and give nothing (their random draws still happen).
[[nodiscard]] std::optional<LootStack> pickCheckLootItem(SeededRandom& rng) noexcept;

// The 27 slots of the dungeon chest at `pos`: 8 pickCheckLootItem draws, each into a random
// slot, seeded by hash(worldSeed, pos). The same chest always gets the same loot.
[[nodiscard]] std::array<world::StoredStack, world::kChestSlots> rollDungeonChestLoot(uint64_t worldSeed,
                                                                                     core::BlockPos pos) noexcept;

} // namespace betacraft::worldgen
