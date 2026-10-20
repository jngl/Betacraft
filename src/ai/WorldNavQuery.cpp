#include "ai/WorldNavQuery.hpp"

#include "voxel/BlockShapes.hpp"
#include "voxel/Chunk.hpp"
#include "world/World.hpp"

namespace betacraft::ai {

NavCell classifyBlock(voxel::BlockId id, uint8_t meta) noexcept {
    using voxel::BlockId;
    if (voxel::isWaterBlock(id)) {
        return NavCell::Liquid;
    }
    if (voxel::isLavaBlock(id)) {
        return NavCell::Hazard;
    }
    switch (id) {
        case BlockId::Air: return NavCell::Passable;
        case BlockId::Cactus: return NavCell::Hazard;
        case BlockId::Ladder: return NavCell::Ladder;
        case BlockId::Fence: return NavCell::Fence;
        case BlockId::WoodDoor:
        case BlockId::IronDoor: // S8.8: opened by redstone only, same metadata
            // Both halves carry the open bit (toggleDoor and the redstone engine write it to each).
            return (meta & voxel::kDoorOpenBit) != 0 ? NavCell::DoorOpen : NavCell::DoorClosed;
        default: return voxel::BlockRegistry::isSolid(id) ? NavCell::Solid : NavCell::Passable;
    }
}

bool WorldNavQuery::isLoaded(core::BlockPos pos) const {
    if (pos.y < 0 || pos.y >= voxel::Chunk::HEIGHT) {
        return false;
    }
    return m_world.getChunk({core::floorDiv(pos.x, voxel::Chunk::WIDTH), core::floorDiv(pos.z, voxel::Chunk::DEPTH)}) !=
           nullptr;
}

NavCell WorldNavQuery::classify(core::BlockPos pos) const {
    return classifyBlock(m_world.getBlock(pos), m_world.getMetadata(pos));
}

} // namespace betacraft::ai
