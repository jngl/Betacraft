#pragma once

#include "ai/NavVoxelQuery.hpp"
#include "voxel/Block.hpp"

#include <cstdint>

namespace betacraft::world { class World; }

namespace betacraft::ai {

// How a block reads for navigation: water is Liquid, lava and cactus are Hazards, a wooden
// door (wooden or iron) is open or closed by its metadata, fences are too high to step on, and anything else
// is Solid when it has a collision box (BlockRegistry::isSolid), Passable otherwise.
[[nodiscard]] NavCell classifyBlock(voxel::BlockId id, uint8_t meta) noexcept;

// INavVoxelQuery over the live World (main thread). A cell is loaded when it lies in
// [0, Chunk::HEIGHT) and its chunk is resident.
class WorldNavQuery final : public INavVoxelQuery {
public:
    explicit WorldNavQuery(const world::World& world) noexcept : m_world(world) {}

    [[nodiscard]] bool isLoaded(core::BlockPos pos) const override;
    [[nodiscard]] NavCell classify(core::BlockPos pos) const override;

private:
    const world::World& m_world;
};

} // namespace betacraft::ai
