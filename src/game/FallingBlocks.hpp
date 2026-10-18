#pragma once

#include "core/Types.hpp"
#include "ecs/Components.hpp"
#include "ecs/Registry.hpp"
#include "world/Block.hpp"

#include <glm/glm.hpp>

#include <cstdint>

namespace betacraft::world { class World; }

namespace betacraft::game {

// S5.3 falling sand and gravel (Beta EntityFallingSand): a 0.98 cube with the mobs' gravity and
// the items' 0.98 air drag per tick, moved by the ECS movement and voxel collision systems.
inline constexpr float kFallingBlockSize = 0.98f;
inline constexpr float kFallingBlockGravityScale = 1.0f;
inline constexpr float kFallingBlockDrag = 0.40405414f; // -20 ln 0.98: 0.98 per tick
// Beta: a falling block still in flight after 600 ticks gives up and drops as an item.
inline constexpr int32_t kFallingBlockMaxTicks = 600;

// The entity for a block that just left `cell`, feet on the cell floor, centred on it.
ecs::EntityId spawnFallingBlock(ecs::Registry& reg, const core::BlockPos& cell, world::BlockId id);

// Cell a falling block occupies: the one holding its centre (Beta floor(posY) with the
// entity's half-height yOffset).
[[nodiscard]] core::BlockPos fallingBlockCell(const glm::vec3& feet) noexcept;

enum class FallingBlockOutcome : uint8_t {
    Falling, // Still in flight
    Place,   // Landed in a replaceable cell: becomes the block again at `cell`
    Drop     // Landed in a cell it cannot take (a torch, a slab...) or flew too long: an item
};

struct FallingBlockStep {
    FallingBlockOutcome outcome{FallingBlockOutcome::Falling};
    core::BlockPos cell{};
};

// Decides what a falling block does after this tick's movement (`ticks` already counted).
[[nodiscard]] FallingBlockStep resolveFallingBlock(const world::World& world, const ecs::TransformComp& transform,
                                                   const ecs::VelocityComp& velocity,
                                                   const ecs::FallingBlockComp& falling);

} // namespace betacraft::game
