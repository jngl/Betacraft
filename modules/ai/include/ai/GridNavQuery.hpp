#pragma once

#include "ai/NavVoxelQuery.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace betacraft::ai {

// Dense in-memory box [0,sx) x [0,sy) x [0,sz) of NavCells (all Passable at first), on the
// model of fluids::GridFluidVoxelAccess. Unit tests build their mazes with it; cells outside
// the box, or marked unloaded, report isLoaded() == false.
class GridNavQuery final : public INavVoxelQuery {
public:
    GridNavQuery(int32_t sizeX, int32_t sizeY, int32_t sizeZ);

    [[nodiscard]] int32_t sizeX() const noexcept { return m_sx; }
    [[nodiscard]] int32_t sizeY() const noexcept { return m_sy; }
    [[nodiscard]] int32_t sizeZ() const noexcept { return m_sz; }

    [[nodiscard]] bool isLoaded(core::BlockPos pos) const override;
    [[nodiscard]] NavCell classify(core::BlockPos pos) const override;

    // Out-of-range writes are ignored.
    void set(core::BlockPos pos, NavCell cell);
    // Inclusive box fill, clipped to the grid.
    void fill(core::BlockPos from, core::BlockPos to, NavCell cell);
    // Inclusive box (clipped) marked unloaded or loaded again; contents are kept.
    void setLoaded(core::BlockPos from, core::BlockPos to, bool loaded);

    // classify() calls so far, to observe how much of the grid a search touched.
    [[nodiscard]] uint64_t queryCount() const noexcept { return m_queries; }

private:
    [[nodiscard]] bool inBounds(core::BlockPos pos) const noexcept {
        return pos.x >= 0 && pos.y >= 0 && pos.z >= 0 && pos.x < m_sx && pos.y < m_sy && pos.z < m_sz;
    }
    [[nodiscard]] size_t indexOf(core::BlockPos pos) const noexcept {
        return (static_cast<size_t>(pos.y) * static_cast<size_t>(m_sz) + static_cast<size_t>(pos.z)) *
                   static_cast<size_t>(m_sx) +
               static_cast<size_t>(pos.x);
    }
    template <typename Fn>
    void forEachInBox(core::BlockPos from, core::BlockPos to, Fn&& fn);

    int32_t m_sx;
    int32_t m_sy;
    int32_t m_sz;
    std::vector<NavCell> m_cells;
    std::vector<uint8_t> m_loaded;
    mutable uint64_t m_queries{0};
};

} // namespace betacraft::ai
