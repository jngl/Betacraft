#include "world/BlockTicks.hpp"

#include "gameplay/Crops.hpp"
#include "world/World.hpp"
#include "worldgen/DecorationFeatures.hpp"

#include <algorithm>
#include <array>

namespace betacraft::world {

namespace {

// Beta BlockGrass.updateTick: dark grass under a light-blocking block reverts to dirt (1 in 4),
// bright grass converts one random dirt cell around it (x +-1, y -3..+1, z +-1) whose top is
// lit to 4 and lets light through.
void tickGrass(World& world, const core::BlockPos& pos, uint8_t /*meta*/, gameplay::SplitMix64& rng) {
    const core::BlockPos above = pos.offset(0, 1, 0);
    const uint8_t light = world.getEffectiveLight(above);
    if (light < 4 && BlockRegistry::get(world.getBlock(above)).lightOpacity > 2) {
        if (rng.nextInt(4) == 0) {
            (void)world.setBlockWithMetadata(pos.x, pos.y, pos.z, BlockId::Dirt, 0);
        }
        return;
    }
    if (light < 9) {
        return;
    }
    const core::BlockPos target = pos.offset(rng.nextInt(3) - 1, rng.nextInt(5) - 3, rng.nextInt(3) - 1);
    const core::BlockPos targetAbove = target.offset(0, 1, 0);
    if (world.getBlock(target) == BlockId::Dirt && world.getEffectiveLight(targetAbove) >= 4 &&
        BlockRegistry::get(world.getBlock(targetAbove)).lightOpacity <= 2) {
        (void)world.setBlockWithMetadata(target.x, target.y, target.z, BlockId::Grass, 0);
    }
}

// Beta BlockSapling.updateTick: in light 9+ (the cell above), 1 in 30 ticks advance one step;
// the first step only sets the ready bit, the second grows the tree.
void tickSapling(World& world, const core::BlockPos& pos, uint8_t meta, gameplay::SplitMix64& rng) {
    if (world.getEffectiveLight(pos.offset(0, 1, 0)) < 9 || rng.nextInt(30) != 0) {
        return;
    }
    if ((meta & kSaplingReadyBit) == 0) {
        (void)world.setMetadata(pos.x, pos.y, pos.z, static_cast<uint8_t>(meta | kSaplingReadyBit));
    } else {
        (void)growTreeFromSapling(world, pos, rng);
    }
}

// Beta BlockCactus / BlockReed.updateTick: with air above and a column shorter than 3, the
// top block ages; at 15 a new block grows on it.
void tickColumnPlant(World& world, const core::BlockPos& pos, uint8_t meta, gameplay::SplitMix64& /*rng*/) {
    const BlockId self = world.getBlock(pos);
    const core::BlockPos above = pos.offset(0, 1, 0);
    if (above.y >= Chunk::HEIGHT || world.getBlock(above) != BlockId::Air) {
        return;
    }
    int32_t height = 1;
    while (height < kMaxPlantColumnHeight && world.getBlock(pos.offset(0, -height, 0)) == self) {
        ++height;
    }
    if (height >= kMaxPlantColumnHeight) {
        return;
    }
    if (meta >= 15) {
        (void)world.setBlockWithMetadata(above.x, above.y, above.z, self, 0);
        (void)world.setMetadata(pos.x, pos.y, pos.z, 0);
    } else {
        (void)world.setMetadata(pos.x, pos.y, pos.z, static_cast<uint8_t>(meta + 1));
    }
}

// Beta BlockLeaves.updateTick: only leaves flagged by a removal nearby check for a log; with
// one they clear the flag, without one they decay (dropping as if broken: a sapling 1 in 20).
// Player-placed leaves never decay.
void tickLeaves(World& world, const core::BlockPos& pos, uint8_t meta, gameplay::SplitMix64& /*rng*/) {
    if ((meta & kLeavesPlayerPlacedBit) != 0 || (meta & kLeavesCheckDecayBit) == 0) {
        return;
    }
    // Beta checkChunksExist(+-5): an unloaded neighbour could hold the supporting log.
    constexpr int32_t kReach = kLeavesSupportRange + 1;
    for (const int32_t dz : {-kReach, kReach}) {
        for (const int32_t dx : {-kReach, kReach}) {
            const core::ChunkCoord c{core::floorDiv(pos.x + dx, Chunk::WIDTH), core::floorDiv(pos.z + dz, Chunk::DEPTH)};
            if (world.getChunk(c) == nullptr) {
                return;
            }
        }
    }
    if (leavesHaveLogSupport(world, pos)) {
        (void)world.setMetadata(pos.x, pos.y, pos.z, static_cast<uint8_t>(meta & ~kLeavesCheckDecayBit));
    } else {
        (void)world.breakBlockNaturally(pos);
    }
}

// Beta BlockFarmland.updateTick: 1 tick in 5, water within 4 cells keeps it wet (moisture 7);
// otherwise it dries one step, and dry farmland without crops on it turns back to dirt.
void tickFarmland(World& world, const core::BlockPos& pos, uint8_t meta, gameplay::SplitMix64& rng) {
    if (rng.nextInt(gameplay::kFarmlandUpdateOneIn) != 0) {
        return;
    }
    if (farmlandHasWaterNearby(world, pos)) {
        (void)world.setMetadata(pos.x, pos.y, pos.z, gameplay::kFarmlandWetMoisture);
    } else if (meta > 0) {
        (void)world.setMetadata(pos.x, pos.y, pos.z, static_cast<uint8_t>(meta - 1));
    } else if (world.getBlock(pos.offset(0, 1, 0)) != BlockId::Crops) {
        (void)world.setBlockWithMetadata(pos.x, pos.y, pos.z, BlockId::Dirt, 0);
    }
}

// Beta BlockCrops.updateTick: crops off farmland or in the dark (below 8 without open sky)
// break; in light 9+ they grow one stage when rand((int)(100 / growth rate)) == 0.
void tickCrops(World& world, const core::BlockPos& pos, uint8_t meta, gameplay::SplitMix64& rng) {
    const uint8_t sky = world.getSkyLight(pos.x, pos.y, pos.z);
    const uint8_t full = std::max(sky, world.getBlockLight(pos.x, pos.y, pos.z));
    if (world.getBlock(pos.offset(0, -1, 0)) != BlockId::Farmland ||
        (full < gameplay::kCropsMinStayLight && sky < 15)) {
        (void)world.breakBlockNaturally(pos);
        return;
    }
    if (world.getEffectiveLight(pos.offset(0, 1, 0)) < gameplay::kCropsMinGrowLight || meta >= gameplay::kCropsRipeStage) {
        return;
    }
    if (rng.nextInt(gameplay::cropGrowthBound(cropGrowthRate(world, pos))) == 0) {
        (void)world.setMetadata(pos.x, pos.y, pos.z, static_cast<uint8_t>(meta + 1));
    }
}

[[nodiscard]] std::array<RandomTickFn, 256> buildRandomTickTable() noexcept {
    std::array<RandomTickFn, 256> table{};
    table[static_cast<size_t>(BlockId::Grass)] = &tickGrass;
    table[static_cast<size_t>(BlockId::Sapling)] = &tickSapling;
    table[static_cast<size_t>(BlockId::Cactus)] = &tickColumnPlant;
    table[static_cast<size_t>(BlockId::SugarCane)] = &tickColumnPlant;
    table[static_cast<size_t>(BlockId::Leaves)] = &tickLeaves;
    table[static_cast<size_t>(BlockId::Farmland)] = &tickFarmland;
    table[static_cast<size_t>(BlockId::Crops)] = &tickCrops;
    return table;
}

// Cells a growing tree may take: air, flora and leaves, never liquids or terrain.
[[nodiscard]] bool treeCanReplace(BlockId id) noexcept {
    return id == BlockId::Leaves || (BlockRegistry::get(id).replaceable && !BlockRegistry::isLiquid(id));
}

} // namespace

const std::array<RandomTickFn, 256> kRandomTick = buildRandomTickTable();

bool farmlandHasWaterNearby(const World& world, const core::BlockPos& pos) {
    constexpr int32_t r = gameplay::kFarmlandWaterReach;
    for (int32_t dy = 0; dy <= 1; ++dy) {
        for (int32_t dz = -r; dz <= r; ++dz) {
            for (int32_t dx = -r; dx <= r; ++dx) {
                if (isWaterBlock(world.getBlock(pos.x + dx, pos.y + dy, pos.z + dz))) {
                    return true;
                }
            }
        }
    }
    return false;
}

float cropGrowthRate(const World& world, const core::BlockPos& pos) {
    gameplay::CropNeighbourhood cells{};
    for (int32_t dx = -1; dx <= 1; ++dx) {
        for (int32_t dz = -1; dz <= 1; ++dz) {
            const auto ix = static_cast<size_t>(dx + 1);
            const auto iz = static_cast<size_t>(dz + 1);
            cells.level[ix][iz] = world.getBlock(pos.x + dx, pos.y, pos.z + dz);
            cells.below[ix][iz] = world.getBlock(pos.x + dx, pos.y - 1, pos.z + dz);
            cells.belowMeta[ix][iz] = world.getMetadata(pos.x + dx, pos.y - 1, pos.z + dz);
        }
    }
    return gameplay::cropGrowthRate(cells);
}

bool growTreeFromSapling(World& world, const core::BlockPos& pos, gameplay::SplitMix64& rng) {
    const auto height = static_cast<uint8_t>(4 + rng.nextInt(3));
    const worldgen::Feature tree{.type = worldgen::FeatureType::OakTree,
                                 .origin = pos,
                                 .height = height,
                                 .variant = static_cast<uint32_t>(rng.next())};
    // Beta WorldGenTrees: room up to the crown inside the world, soil under the trunk.
    if (pos.y < 1 || pos.y + height + 1 >= Chunk::HEIGHT) {
        return false;
    }
    const BlockId soil = world.getBlock(pos.offset(0, -1, 0));
    if (soil != BlockId::Grass && soil != BlockId::Dirt) {
        return false;
    }
    for (int32_t ty = 1; ty < height; ++ty) {
        if (!treeCanReplace(world.getBlock(pos.offset(0, ty, 0)))) {
            return false;
        }
    }
    worldgen::forEachFeatureBlock(tree, [&](core::BlockPos p, BlockId proposed) {
        const BlockId existing = world.getBlock(p);
        if (proposed == BlockId::Dirt) {
            if (existing == BlockId::Grass) {
                (void)world.setBlockWithMetadata(p.x, p.y, p.z, BlockId::Dirt, 0);
            }
            return;
        }
        if (p == pos || (treeCanReplace(existing) && existing != proposed)) {
            // Generated leaves (meta 0) only start checking for decay after a removal nearby.
            (void)world.setBlockWithMetadata(p.x, p.y, p.z, proposed, 0);
        }
    });
    return true;
}

bool leavesHaveLogSupport(const World& world, const core::BlockPos& pos) {
    // Breadth-first through leaves, as Beta's distance propagation in a 9^3 box around the leaf.
    constexpr int32_t kSide = 2 * kLeavesSupportRange + 1;
    std::array<bool, static_cast<size_t>(kSide * kSide * kSide)> visited{};
    const auto index = [&](const core::BlockPos& p) {
        return static_cast<size_t>(((p.y - pos.y + kLeavesSupportRange) * kSide + (p.z - pos.z + kLeavesSupportRange)) *
                                       kSide +
                                   (p.x - pos.x + kLeavesSupportRange));
    };
    static constexpr std::array<glm::ivec3, 6> kSteps{{{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}}};
    std::array<core::BlockPos, static_cast<size_t>(kSide * kSide * kSide)> frontier{};
    size_t head = 0;
    size_t tail = 0;
    frontier[tail++] = pos;
    visited[index(pos)] = true;
    for (int32_t depth = 1; depth <= kLeavesSupportRange && head < tail; ++depth) {
        const size_t levelEnd = tail;
        for (; head < levelEnd; ++head) {
            const core::BlockPos from = frontier[head];
            for (const glm::ivec3& s : kSteps) {
                const core::BlockPos p = from.offset(s.x, s.y, s.z);
                if (visited[index(p)]) continue;
                visited[index(p)] = true;
                const BlockId id = world.getBlock(p);
                if (isLogBlock(id)) return true;
                if (id == BlockId::Leaves && depth < kLeavesSupportRange) frontier[tail++] = p;
            }
        }
    }
    return false;
}

} // namespace betacraft::world
