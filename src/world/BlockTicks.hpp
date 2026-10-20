#pragma once

#include "core/Types.hpp"
#include "gameplay/SplitMix64.hpp"
#include "voxel/Block.hpp"

#include <array>
#include <cstdint>

namespace betacraft::world {

class World;

// S5.1 random block ticks (Beta Block.updateTick reached from World.updateBlocksAndPlayCaveSounds):
// World::tickRandomBlocks picks kRandomTicksPerChunk random cells in every nearby chunk each
// game tick and calls the handler of the block found there, with its metadata.
using RandomTickFn = void (*)(World&, const core::BlockPos&, uint8_t meta, gameplay::SplitMix64&);

// Beta: 80 cells per 16x128x16 chunk and game tick, i.e. 10 per 16^3 section.
inline constexpr int32_t kRandomTicksPerChunk = 80;
// Chunks within this many chunks of the player (Chebyshev) receive random ticks.
inline constexpr int32_t kRandomTickRadiusChunks = 8;

// Leaves metadata (Beta BlockLeaves): bits 0-1 the leaf kind, bit 2 set on player-placed
// leaves (they never decay; the bit Beta 1.8+ uses, Beta 1.7.3 had none), bit 3 "check decay",
// set on the leaves around a log or leaf that went away.
inline constexpr uint8_t kLeavesPlayerPlacedBit = 0x4;
inline constexpr uint8_t kLeavesCheckDecayBit = 0x8;
// Beta BlockLeaves: a leaf survives when a log is reachable in at most this many steps
// through leaves.
inline constexpr int32_t kLeavesSupportRange = 4;
// Beta BlockSapling: bit 3 marks a sapling one growth step away from a tree.
inline constexpr uint8_t kSaplingReadyBit = 0x8;
// Beta BlockCactus / BlockReed: columns grow up to this many blocks, one block every 16 ticks
// of the top block's age (metadata 0..15).
inline constexpr int32_t kMaxPlantColumnHeight = 3;

// Handler per block id; nullptr for blocks without random ticks.
extern const std::array<RandomTickFn, 256> kRandomTick;

[[nodiscard]] inline bool hasRandomTick(BlockId id) noexcept {
    return kRandomTick[static_cast<size_t>(id)] != nullptr;
}

[[nodiscard]] constexpr bool isLogBlock(BlockId id) noexcept {
    return id == BlockId::OakLog || id == BlockId::BirchLog || id == BlockId::PineLog;
}

// Beta BlockSapling.growTree with WorldGenTrees: an oak 4..6 logs high grows from the sapling
// at `pos`, over grass or dirt, writing its logs and leaves only into replaceable non-liquid
// cells or leaves. Returns false, changing nothing, when the trunk has no room.
bool growTreeFromSapling(World& world, const core::BlockPos& pos, gameplay::SplitMix64& rng);

// S5.7 (Beta BlockFarmland.isWaterNearby): water within x/z +-4 and y 0..+1 of `pos`.
[[nodiscard]] bool farmlandHasWaterNearby(const World& world, const core::BlockPos& pos);

// S5.7: gameplay::cropGrowthRate of the crop at `pos`, read from the world.
[[nodiscard]] float cropGrowthRate(const World& world, const core::BlockPos& pos);

// Beta BlockLeaves.updateTick search: a log reachable from `pos` in at most
// kLeavesSupportRange steps through leaves.
[[nodiscard]] bool leavesHaveLogSupport(const World& world, const core::BlockPos& pos);

} // namespace betacraft::world
