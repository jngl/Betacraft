#pragma once

#include "fluids/FluidVoxelAccess.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace betacraft::fluids {

// Dense in-memory voxel box [0,sx) x [0,sy) x [0,sz) implementing the fluid access
// contract. Used by unit tests and as the reference behaviour for the World adapter.
class GridFluidVoxelAccess final : public IFluidVoxelAccess {
public:
    GridFluidVoxelAccess(int32_t sizeX, int32_t sizeY, int32_t sizeZ);

    [[nodiscard]] int32_t sizeX() const noexcept { return m_sx; }
    [[nodiscard]] int32_t sizeY() const noexcept { return m_sy; }
    [[nodiscard]] int32_t sizeZ() const noexcept { return m_sz; }

    [[nodiscard]] bool isLoaded(const core::BlockPos& pos) const noexcept override;
    [[nodiscard]] voxel::BlockId getBlock(const core::BlockPos& pos) const noexcept override;
    [[nodiscard]] FluidState getFluid(const core::BlockPos& pos) const noexcept override;
    void setBlock(const core::BlockPos& pos, voxel::BlockId id) override;
    void setFluid(const core::BlockPos& pos, FluidState state) override;

    // Inclusive box fill with a regular block (clears fluid). Out-of-range parts are clipped.
    void fill(const core::BlockPos& from, const core::BlockPos& to, voxel::BlockId id);

    // Marks an inclusive box (clipped) as unloaded or loaded again, like a chunk being
    // saved and reloaded: unloaded cells read as Air/dry, ignore writes and keep their
    // contents for when they are loaded again.
    void setLoaded(const core::BlockPos& from, const core::BlockPos& to, bool loaded);

    // Number of fluid cells of the given type (None = any fluid).
    [[nodiscard]] size_t countFluid(FluidType type = FluidType::None) const noexcept;

    [[nodiscard]] bool sameContents(const GridFluidVoxelAccess& other) const noexcept;

    // Deep copy for snapshots (the access interface itself is non-copyable so adapters
    // over a live World cannot be duplicated by accident).
    [[nodiscard]] std::unique_ptr<GridFluidVoxelAccess> clone() const;

    // Number of in-bounds writes performed, to observe simulator write volume.
    [[nodiscard]] uint64_t writeCount() const noexcept { return m_writes; }

private:
    struct Cell {
        voxel::BlockId block{voxel::BlockId::Air};
        FluidState fluid{};

        bool operator==(const Cell& other) const noexcept = default;
    };

    // Visits the in-bounds part of an inclusive box, whatever its load state.
    template <typename Fn>
    void forEachInBox(const core::BlockPos& from, const core::BlockPos& to, Fn&& fn);

    [[nodiscard]] size_t indexOf(const core::BlockPos& pos) const noexcept {
        return (static_cast<size_t>(pos.y) * static_cast<size_t>(m_sz) + static_cast<size_t>(pos.z)) *
                   static_cast<size_t>(m_sx) +
               static_cast<size_t>(pos.x);
    }

    int32_t m_sx;
    int32_t m_sy;
    int32_t m_sz;
    std::vector<Cell> m_cells;
    std::vector<uint8_t> m_loaded; // per cell, 1 = loaded (kept apart from contents)
    uint64_t m_writes{0};
};

} // namespace betacraft::fluids
