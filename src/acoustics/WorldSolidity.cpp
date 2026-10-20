#include "acoustics/WorldSolidity.hpp"

#include "world/World.hpp"

namespace betacraft::acoustics {

bool blocksSound(voxel::BlockId id) noexcept {
    return voxel::BlockRegistry::isSolid(id) && !voxel::BlockRegistry::isLiquid(id);
}

float computeVoxelOcclusion(const world::World& world, const glm::vec3& listener, const glm::vec3& soundPos) noexcept {
    const WorldChunkSolidityQuery query(world);
    return computeVoxelOcclusion(query, listener, soundPos);
}

} // namespace betacraft::acoustics
