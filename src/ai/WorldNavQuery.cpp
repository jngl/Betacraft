#include "ai/WorldNavQuery.hpp"

#include "world/BlockShapes.hpp"
#include "world/Chunk.hpp"
#include "world/World.hpp"

namespace betacraft::ai {

NavCell classifyBlock(world::BlockId id, uint8_t meta) noexcept {
    using world::BlockId;
    if (world::isWaterBlock(id)) {
        return NavCell::Liquid;
    }
    if (world::isLavaBlock(id)) {
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
            return (meta & world::kDoorOpenBit) != 0 ? NavCell::DoorOpen : NavCell::DoorClosed;
        default: return world::BlockRegistry::isSolid(id) ? NavCell::Solid : NavCell::Passable;
    }
}

bool WorldNavQuery::isLoaded(core::BlockPos pos) const {
    if (pos.y < 0 || pos.y >= world::Chunk::HEIGHT) {
        return false;
    }
    return m_world.getChunk({core::floorDiv(pos.x, world::Chunk::WIDTH), core::floorDiv(pos.z, world::Chunk::DEPTH)}) !=
           nullptr;
}

NavCell WorldNavQuery::classify(core::BlockPos pos) const {
    return classifyBlock(m_world.getBlock(pos), m_world.getMetadata(pos));
}

} // namespace betacraft::ai
