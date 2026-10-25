#pragma once

#include "core/Types.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <limits>

namespace betacraft::acoustics {

// Maximal audibility radius R_max. measureOcclusion() flags sources beyond it
// (OcclusionSample::beyondAudibleRange) so the mixer can cull them; the DDA itself is bounded by the
// derived step budget below.
inline constexpr int32_t MAX_AUDIBLE_RANGE = 48;
// A 6-connected traversal of a 48^3 box needs at most |dx|+|dy|+|dz| = 144 steps (spec bound).
inline constexpr uint32_t MAX_DDA_STEPS = 3u * static_cast<uint32_t>(MAX_AUDIBLE_RANGE);

struct TraversalResult {
    uint32_t visitedCount{0}; // Voxels handed to the visitor (start voxel included)
    bool reachedEnd{false};   // The voxel containing `to` was visited
    bool truncated{false};    // The step budget ran out before reaching the end voxel
};

namespace detail {
// Coordinates beyond this cannot be floored into int32 safely; the world is far smaller anyway.
inline constexpr double MAX_ABS_COORD = 1.0e9;
} // namespace detail

// Amanatides & Woo (1987) 3D-DDA from `from` to `to`, in world voxel units (voxel = floor(p)).
//
// The visitor is called as `bool visit(const core::BlockPos& voxel, float tEnter, float tExit)` where
// [tEnter, tExit] is the parametric sub-interval of the segment (0 = from, 1 = to) inside that voxel.
// Returning false stops the traversal. The visited path is 6-connected: exactly one axis moves per
// step, so a ray from voxel A to voxel B visits exactly 1 + |dx| + |dy| + |dz| voxels. When a ray
// crosses an edge or corner exactly, ties are broken X before Y before Z, which keeps the result
// deterministic; the intermediate voxel then gets a zero-length interval (tEnter == tExit), which
// measureOcclusion() resolves direction-independently.
//
// Stepping is driven by the remaining per-axis voxel counts instead of comparing t against 1, so
// floating-point drift can never overshoot the end voxel. O(steps) time, O(1) space, no allocation.
template <typename Visitor>
TraversalResult traverseVoxels(const glm::vec3& from, const glm::vec3& to, Visitor&& visit,
                               uint32_t maxSteps = MAX_DDA_STEPS) noexcept {
    TraversalResult result;

    const std::array<double, 3> a{from.x, from.y, from.z};
    const std::array<double, 3> b{to.x, to.y, to.z};
    for (size_t i = 0; i < 3; ++i) {
        if (!std::isfinite(a[i]) || !std::isfinite(b[i]) || std::fabs(a[i]) > detail::MAX_ABS_COORD ||
            std::fabs(b[i]) > detail::MAX_ABS_COORD) {
            return result;
        }
    }

    std::array<int32_t, 3> cur{};
    std::array<int32_t, 3> step{};
    std::array<uint32_t, 3> remaining{};
    std::array<double, 3> tMax{};
    std::array<double, 3> tDelta{};
    constexpr double INF = std::numeric_limits<double>::infinity();

    uint64_t totalSteps = 0;
    for (size_t i = 0; i < 3; ++i) {
        cur[i] = static_cast<int32_t>(std::floor(a[i]));
        const int32_t end = static_cast<int32_t>(std::floor(b[i]));
        const double d = b[i] - a[i];
        remaining[i] = static_cast<uint32_t>(std::abs(end - cur[i]));
        totalSteps += remaining[i];
        if (end > cur[i]) {
            step[i] = 1;
            tDelta[i] = 1.0 / d;
            tMax[i] = (static_cast<double>(cur[i]) + 1.0 - a[i]) / d;
        } else if (end < cur[i]) {
            step[i] = -1;
            tDelta[i] = -1.0 / d;
            tMax[i] = (a[i] - static_cast<double>(cur[i])) / -d;
        } else {
            step[i] = 0;
            tDelta[i] = INF;
            tMax[i] = INF;
        }
    }

    const uint64_t stepsToRun = totalSteps < maxSteps ? totalSteps : maxSteps;
    result.truncated = totalSteps > maxSteps;

    // The loop keeps every per-axis quantity in scalars (registers) and updates all three axes with
    // selects instead of branches or indexed stores: the axis sequence of a random ray is unpredictable
    // (a mispredicted branch per voxel), and a store to tMax[axis] would put a store-to-load forward on
    // the loop-carried dependency chain. Exhausted axes hold tMax = INF, so the axis choice is a plain
    // argmin; '<=' towards the lower index yields the X < Y < Z tie-break.
    for (size_t i = 0; i < 3; ++i) {
        if (remaining[i] == 0) {
            tMax[i] = INF;
        }
    }
    int32_t x = cur[0], y = cur[1], z = cur[2];
    double tx = tMax[0], ty = tMax[1], tz = tMax[2];
    uint32_t rx = remaining[0], ry = remaining[1], rz = remaining[2];
    // No lower clamp against tEnter: every tMax starts >= 0 and only grows, so the per-step minimum
    // never decreases. Keeping tEnter off the computation also keeps it off the loop-carried chain.
    const auto clampExit = [](double best) noexcept { return best > 1.0 ? 1.0 : best; };

    double tEnter = 0.0;
    for (uint64_t s = 0; s < stepsToRun; ++s) {
        const bool sx = tx <= ty && tx <= tz;
        const bool sy = !sx && ty <= tz;
        const bool sz = !sx && !sy;
        const double best = sx ? tx : (sy ? ty : tz);
        const double tExit = clampExit(best);

        ++result.visitedCount;
        if (!visit(core::BlockPos{x, y, z}, static_cast<float>(tEnter), static_cast<float>(tExit))) {
            return result;
        }
        x += sx ? step[0] : 0;
        y += sy ? step[1] : 0;
        z += sz ? step[2] : 0;
        rx -= sx ? 1u : 0u;
        ry -= sy ? 1u : 0u;
        rz -= sz ? 1u : 0u;
        tx = sx ? (rx == 0 ? INF : tx + tDelta[0]) : tx;
        ty = sy ? (ry == 0 ? INF : ty + tDelta[1]) : ty;
        tz = sz ? (rz == 0 ? INF : tz + tDelta[2]) : tz;
        tEnter = tExit;
    }

    // Last voxel: the segment end, or for a truncated ray the true exit of that voxel.
    const double tExit = result.truncated ? clampExit(std::min(tx, std::min(ty, tz))) : 1.0;
    ++result.visitedCount;
    if (!visit(core::BlockPos{x, y, z}, static_cast<float>(tEnter), static_cast<float>(tExit))) {
        return result;
    }
    result.reachedEnd = !result.truncated;
    return result;
}

} // namespace betacraft::acoustics
