#pragma once

// Shared helpers for the fluid simulator tests.

#include "fluids/FluidSimulator.hpp"
#include "fluids/GridFluidVoxelAccess.hpp"

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <sstream>
#include <string>

namespace fluidtest {

using betacraft::core::BlockPos;
using betacraft::fluids::FluidConfig;
using betacraft::fluids::FluidSimulator;
using betacraft::fluids::FluidState;
using betacraft::fluids::FluidType;
using betacraft::fluids::GridFluidVoxelAccess;
using betacraft::voxel::BlockId;

// A grid with a stone floor at y = 0 and air above.
inline std::unique_ptr<GridFluidVoxelAccess> makeFlatWorld(int32_t sx, int32_t sy, int32_t sz) {
    auto g = std::make_unique<GridFluidVoxelAccess>(sx, sy, sz);
    g->fill({0, 0, 0}, {sx - 1, 0, sz - 1}, BlockId::Stone);
    return g;
}

inline int manhattan2D(const BlockPos& a, const BlockPos& b) {
    return std::abs(a.x - b.x) + std::abs(a.z - b.z);
}

// ASCII dump of one horizontal layer: '#' block, '.' air, digit = level, 'S' source,
// 'F' falling; lava uses lowercase letters a..h for levels 0..7 to tell them apart.
inline std::string dumpLayer(const GridFluidVoxelAccess& g, int32_t y) {
    std::ostringstream os;
    for (int32_t z = 0; z < g.sizeZ(); ++z) {
        for (int32_t x = 0; x < g.sizeX(); ++x) {
            const FluidState f = g.getFluid({x, y, z});
            if (f.isEmpty()) {
                os << (g.getBlock({x, y, z}) == BlockId::Air ? '.' : '#');
            } else if (f.isFalling()) {
                os << (f.type == FluidType::Water ? 'F' : 'f');
            } else if (f.type == FluidType::Water) {
                os << (f.isSource() ? 'S' : static_cast<char>('0' + f.level));
            } else {
                os << static_cast<char>('a' + f.level);
            }
        }
        os << '\n';
    }
    return os.str();
}

} // namespace fluidtest
