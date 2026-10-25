#pragma once

#include "core/Types.hpp"

#include <cstdint>

namespace betacraft::ai {

// What a voxel means to a walking mob. Grows only at the end.
//   Solid       a floor to stand on and a wall to go around;
//   Passable    air and walk-through decorations (flowers, torches, crops...);
//   Liquid      water: swum through at a cost;
//   Hazard      never entered nor stood on (lava, cactus);
//   DoorOpen / DoorClosed  a door, passable when open (a closed one only for canOpenDoors);
//   Ladder      climbable, also a floor for the cell above it;
//   Fence       a wall too high to step onto (Beta fences are 1.5 blocks tall): no floor.
enum class NavCell : uint8_t { Solid, Passable, Liquid, Hazard, DoorOpen, DoorClosed, Ladder, Fence };

// Minimal voxel view the pathfinder needs. Like fluids::IFluidVoxelAccess it does not depend
// on World, so the search can be unit-tested on a small in-memory grid (GridNavQuery); the
// game passes a World-backed adapter (WorldNavQuery). Cells that are not loaded are never
// entered: classify() of such a cell is unspecified.
class INavVoxelQuery {
public:
    INavVoxelQuery() = default;
    virtual ~INavVoxelQuery() = default;

    INavVoxelQuery(const INavVoxelQuery&) = delete;
    INavVoxelQuery& operator=(const INavVoxelQuery&) = delete;

    [[nodiscard]] virtual bool isLoaded(core::BlockPos pos) const = 0;
    [[nodiscard]] virtual NavCell classify(core::BlockPos pos) const = 0;

protected:
    // Movable for concrete grids built by helper functions; never copied through the base.
    INavVoxelQuery(INavVoxelQuery&&) noexcept = default;
    INavVoxelQuery& operator=(INavVoxelQuery&&) noexcept = default;
};

} // namespace betacraft::ai
