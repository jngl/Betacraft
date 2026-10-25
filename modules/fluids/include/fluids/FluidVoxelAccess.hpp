#pragma once

#include "core/Types.hpp"
#include "fluids/FluidState.hpp"
#include "voxel/Block.hpp"

namespace betacraft::fluids {

// Beta's blockBlocksFlow(): fluids wash away non-solid decorations (plants, torches)
// but are stopped by everything else, including sugar cane (reeds). Kept constexpr and
// registry-free so it works before BlockRegistry::initialize() and in unit tests.
[[nodiscard]] constexpr bool defaultBlocksFlow(voxel::BlockId id) noexcept {
    using voxel::BlockId;
    switch (id) {
        case BlockId::Air:
        case BlockId::Water:
        case BlockId::Sapling:
        case BlockId::TallGrass:
        case BlockId::DeadBush:
        case BlockId::Dandelion:
        case BlockId::Rose:
        case BlockId::Torch:
        // Material.circuits: wire, lever, button and redstone torches wash away too.
        case BlockId::RedstoneWire:
        case BlockId::Lever:
        case BlockId::StoneButton:
        case BlockId::RedstoneTorchOff:
        case BlockId::RedstoneTorchOn:
            return false;
        default:
            return true;
    }
}

// Minimal voxel view the simulator needs. It deliberately does not depend on World so
// the cellular automaton can be unit-tested on a small in-memory grid; the integration
// step provides a World-backed adapter (block id + per-voxel fluid nibble).
//
// Contract:
//  - A cell holds either a fluid (getFluid().type != None) or a regular block. For fluid
//    cells getBlock() is unspecified; the simulator always queries getFluid() first.
//  - setFluid() replaces whatever block was there; setFluid(pos, {}) leaves Air.
//  - setBlock() writes a regular block and clears any fluid.
//  - Cells that are not loaded (outside the world, unloaded chunks) report isLoaded() ==
//    false; the simulator treats them as walls and never writes to them, and does not
//    count them as a solid floor for the infinite-source rule. When a chunk loads, call
//    FluidSimulator::notifyRegionLoaded() so fluid stopped at its border resumes.
//  - Storage encoding for an adapter: the fluid *type* (and hence wet vs dry) comes from
//    the block id (water / lava, still or flowing). In memory, FluidState::toNibble() level
//    0 is the thinnest *flowing* level (Beta decay 7, the 7th ring of a water spread), NOT
//    dry: "0 = dry" cannot coexist with its 7-block reach (source + 7
//    flowing levels + dry = 9 states for 3 bits). World's per-voxel metadata stores Beta's
//    native value instead (FluidState::betaDecay() / fromBetaDecay(), 0 = source), because
//    every water cell written before the nibble existed reads 0 and must stay a source.
class IFluidVoxelAccess {
public:
    IFluidVoxelAccess() = default;
    virtual ~IFluidVoxelAccess() = default;

    IFluidVoxelAccess(const IFluidVoxelAccess&) = delete;
    IFluidVoxelAccess& operator=(const IFluidVoxelAccess&) = delete;

    [[nodiscard]] virtual bool isLoaded(const core::BlockPos& pos) const noexcept = 0;
    [[nodiscard]] virtual voxel::BlockId getBlock(const core::BlockPos& pos) const noexcept = 0;
    [[nodiscard]] virtual FluidState getFluid(const core::BlockPos& pos) const noexcept = 0;
    virtual void setBlock(const core::BlockPos& pos, voxel::BlockId id) = 0;
    virtual void setFluid(const core::BlockPos& pos, FluidState state) = 0;

    // Whether the non-fluid content of the cell stops fluid. Overridable so a World
    // adapter can consult BlockRegistry for blocks added later.
    [[nodiscard]] virtual bool blocksFlow(const core::BlockPos& pos) const noexcept {
        return !isLoaded(pos) || defaultBlocksFlow(getBlock(pos));
    }
};

} // namespace betacraft::fluids
