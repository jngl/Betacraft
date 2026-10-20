#include "fluids/GridFluidVoxelAccess.hpp"

#include <algorithm>

namespace betacraft::fluids {

GridFluidVoxelAccess::GridFluidVoxelAccess(int32_t sizeX, int32_t sizeY, int32_t sizeZ)
    : m_sx(std::max(sizeX, 0)),
      m_sy(std::max(sizeY, 0)),
      m_sz(std::max(sizeZ, 0)),
      m_cells(static_cast<size_t>(m_sx) * static_cast<size_t>(m_sy) * static_cast<size_t>(m_sz)),
      m_loaded(m_cells.size(), 1U) {}

bool GridFluidVoxelAccess::isLoaded(const core::BlockPos& pos) const noexcept {
    return pos.x >= 0 && pos.y >= 0 && pos.z >= 0 && pos.x < m_sx && pos.y < m_sy && pos.z < m_sz &&
           m_loaded[indexOf(pos)] != 0U;
}

voxel::BlockId GridFluidVoxelAccess::getBlock(const core::BlockPos& pos) const noexcept {
    return isLoaded(pos) ? m_cells[indexOf(pos)].block : voxel::BlockId::Air;
}

FluidState GridFluidVoxelAccess::getFluid(const core::BlockPos& pos) const noexcept {
    return isLoaded(pos) ? m_cells[indexOf(pos)].fluid : FluidState{};
}

void GridFluidVoxelAccess::setBlock(const core::BlockPos& pos, voxel::BlockId id) {
    if (!isLoaded(pos)) {
        return;
    }
    m_cells[indexOf(pos)] = Cell{id, FluidState{}};
    ++m_writes;
}

void GridFluidVoxelAccess::setFluid(const core::BlockPos& pos, FluidState state) {
    if (!isLoaded(pos)) {
        return;
    }
    m_cells[indexOf(pos)] = Cell{voxel::BlockId::Air, state.isEmpty() ? FluidState{} : state};
    ++m_writes;
}

template <typename Fn>
void GridFluidVoxelAccess::forEachInBox(const core::BlockPos& from, const core::BlockPos& to, Fn&& fn) {
    const int32_t x0 = std::max(std::min(from.x, to.x), 0);
    const int32_t y0 = std::max(std::min(from.y, to.y), 0);
    const int32_t z0 = std::max(std::min(from.z, to.z), 0);
    const int32_t x1 = std::min(std::max(from.x, to.x), m_sx - 1);
    const int32_t y1 = std::min(std::max(from.y, to.y), m_sy - 1);
    const int32_t z1 = std::min(std::max(from.z, to.z), m_sz - 1);
    for (int32_t y = y0; y <= y1; ++y) {
        for (int32_t z = z0; z <= z1; ++z) {
            for (int32_t x = x0; x <= x1; ++x) {
                fn(core::BlockPos{x, y, z});
            }
        }
    }
}

void GridFluidVoxelAccess::fill(const core::BlockPos& from, const core::BlockPos& to, voxel::BlockId id) {
    forEachInBox(from, to, [&](const core::BlockPos& p) { setBlock(p, id); });
}

void GridFluidVoxelAccess::setLoaded(const core::BlockPos& from, const core::BlockPos& to, bool loaded) {
    forEachInBox(from, to, [&](const core::BlockPos& p) { m_loaded[indexOf(p)] = loaded ? 1U : 0U; });
}

size_t GridFluidVoxelAccess::countFluid(FluidType type) const noexcept {
    return static_cast<size_t>(std::count_if(m_cells.begin(), m_cells.end(), [type](const Cell& c) {
        return !c.fluid.isEmpty() && (type == FluidType::None || c.fluid.type == type);
    }));
}

bool GridFluidVoxelAccess::sameContents(const GridFluidVoxelAccess& other) const noexcept {
    return m_sx == other.m_sx && m_sy == other.m_sy && m_sz == other.m_sz && m_cells == other.m_cells;
}

std::unique_ptr<GridFluidVoxelAccess> GridFluidVoxelAccess::clone() const {
    auto copy = std::make_unique<GridFluidVoxelAccess>(0, 0, 0);
    copy->m_sx = m_sx;
    copy->m_sy = m_sy;
    copy->m_sz = m_sz;
    copy->m_cells = m_cells;
    copy->m_loaded = m_loaded;
    copy->m_writes = m_writes;
    return copy;
}

} // namespace betacraft::fluids
