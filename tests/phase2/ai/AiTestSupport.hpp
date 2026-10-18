#pragma once
// Shared helpers for the S8 A* tests: a GridNavQuery with a Solid floor at y = 0, mobs
// walking at y = 1.

#include "ai/GridNavQuery.hpp"
#include "ai/VoxelPathfinder.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <span>

namespace betacraft::test::ai {

using betacraft::ai::GridNavQuery;
using betacraft::ai::NavAgent;
using betacraft::ai::NavCell;

inline constexpr int32_t kWalkY = 1;
inline const NavAgent kZombie{{0.6f, 1.8f, 0.6f}, false};
inline const NavAgent kSpider{{1.4f, 0.9f, 1.4f}, false};

[[nodiscard]] inline GridNavQuery flatGrid(int32_t sx, int32_t sy, int32_t sz) {
    GridNavQuery grid(sx, sy, sz);
    grid.fill({0, 0, 0}, {sx - 1, 0, sz - 1}, NavCell::Solid);
    return grid;
}

// Sum of the move costs along a path (each consecutive pair must be a legal move).
[[nodiscard]] inline float pathCost(const GridNavQuery& grid, std::span<const glm::ivec3> path, const NavAgent& agent) {
    float cost = 0.0f;
    for (size_t i = 1; i < path.size(); ++i) {
        const auto c = betacraft::ai::moveCost(grid, path[i - 1], path[i], agent);
        if (!c) {
            return -1.0f;
        }
        cost += *c;
    }
    return cost;
}

} // namespace betacraft::test::ai
