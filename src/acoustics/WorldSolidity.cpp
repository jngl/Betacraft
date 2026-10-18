#include "acoustics/WorldSolidity.hpp"

#include "world/World.hpp"

namespace betacraft::acoustics {

bool blocksSound(world::BlockId id) noexcept {
    return world::BlockRegistry::isSolid(id) && !world::BlockRegistry::isLiquid(id);
}

float computeVoxelOcclusion(const world::World& world, const glm::vec3& listener, const glm::vec3& soundPos) noexcept {
    const WorldChunkSolidityQuery query(world);
    return computeVoxelOcclusion(query, listener, soundPos);
}

} // namespace betacraft::acoustics
