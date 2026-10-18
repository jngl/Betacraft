#include "game/FallingBlocks.hpp"

#include "world/Chunk.hpp"
#include "world/World.hpp"

#include <cmath>

namespace betacraft::game {

ecs::EntityId spawnFallingBlock(ecs::Registry& reg, const core::BlockPos& cell, world::BlockId id) {
    const ecs::EntityId e = reg.create();
    if (e == ecs::NULL_ENTITY) {
        return ecs::NULL_ENTITY;
    }
    const glm::vec3 feet(static_cast<float>(cell.x) + 0.5f, static_cast<float>(cell.y),
                         static_cast<float>(cell.z) + 0.5f);
    reg.emplace<ecs::TransformComp>(e, ecs::TransformComp{feet, feet, 0.0f, 0.0f});
    ecs::VelocityComp v{};
    v.gravityScale = kFallingBlockGravityScale;
    v.horizontalDrag = kFallingBlockDrag;
    v.verticalDrag = kFallingBlockDrag;
    reg.emplace<ecs::VelocityComp>(e, v);
    reg.emplace<ecs::AABBColliderComp>(e, ecs::AABBColliderComp{kFallingBlockSize, kFallingBlockSize});
    reg.emplace<ecs::FallingBlockComp>(e, ecs::FallingBlockComp{id, 0});
    return e;
}

core::BlockPos fallingBlockCell(const glm::vec3& feet) noexcept {
    return {static_cast<int32_t>(std::floor(feet.x)), static_cast<int32_t>(std::floor(feet.y + kFallingBlockSize * 0.5f)),
            static_cast<int32_t>(std::floor(feet.z))};
}

FallingBlockStep resolveFallingBlock(const world::World& world, const ecs::TransformComp& transform,
                                     const ecs::VelocityComp& velocity, const ecs::FallingBlockComp& falling) {
    const core::BlockPos cell = fallingBlockCell(transform.pos);
    if (velocity.onGround) {
        const bool placeable = cell.y >= 0 && cell.y < world::Chunk::HEIGHT &&
                               world::BlockRegistry::get(world.getBlock(cell)).replaceable;
        return {placeable ? FallingBlockOutcome::Place : FallingBlockOutcome::Drop, cell};
    }
    if (falling.ticks > kFallingBlockMaxTicks) {
        return {FallingBlockOutcome::Drop, cell};
    }
    return {FallingBlockOutcome::Falling, cell};
}

} // namespace betacraft::game
