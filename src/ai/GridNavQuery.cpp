#include "ai/GridNavQuery.hpp"

#include <algorithm>

namespace betacraft::ai {

GridNavQuery::GridNavQuery(int32_t sizeX, int32_t sizeY, int32_t sizeZ)
    : m_sx(std::max(sizeX, 0)),
      m_sy(std::max(sizeY, 0)),
      m_sz(std::max(sizeZ, 0)),
      m_cells(static_cast<size_t>(m_sx) * static_cast<size_t>(m_sy) * static_cast<size_t>(m_sz), NavCell::Passable),
      m_loaded(m_cells.size(), 1U) {}

bool GridNavQuery::isLoaded(core::BlockPos pos) const {
    return inBounds(pos) && m_loaded[indexOf(pos)] != 0U;
}

NavCell GridNavQuery::classify(core::BlockPos pos) const {
    ++m_queries;
    return inBounds(pos) ? m_cells[indexOf(pos)] : NavCell::Solid;
}

void GridNavQuery::set(core::BlockPos pos, NavCell cell) {
    if (inBounds(pos)) {
        m_cells[indexOf(pos)] = cell;
    }
}

template <typename Fn>
void GridNavQuery::forEachInBox(core::BlockPos from, core::BlockPos to, Fn&& fn) {
    const int32_t x0 = std::max(std::min(from.x, to.x), 0);
    const int32_t y0 = std::max(std::min(from.y, to.y), 0);
    const int32_t z0 = std::max(std::min(from.z, to.z), 0);
    const int32_t x1 = std::min(std::max(from.x, to.x), m_sx - 1);
    const int32_t y1 = std::min(std::max(from.y, to.y), m_sy - 1);
    const int32_t z1 = std::min(std::max(from.z, to.z), m_sz - 1);
    for (int32_t y = y0; y <= y1; ++y) {
        for (int32_t z = z0; z <= z1; ++z) {
            for (int32_t x = x0; x <= x1; ++x) {
                fn(indexOf({x, y, z}));
            }
        }
    }
}

void GridNavQuery::fill(core::BlockPos from, core::BlockPos to, NavCell cell) {
    forEachInBox(from, to, [&](size_t i) { m_cells[i] = cell; });
}

void GridNavQuery::setLoaded(core::BlockPos from, core::BlockPos to, bool loaded) {
    forEachInBox(from, to, [&](size_t i) { m_loaded[i] = loaded ? 1U : 0U; });
}

} // namespace betacraft::ai
