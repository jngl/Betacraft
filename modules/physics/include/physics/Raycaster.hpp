#pragma once

#include "core/Types.hpp"
#include "voxel/Block.hpp"
#include "world/World.hpp"

#include <glm/glm.hpp>

namespace betacraft::physics {

struct RaycastHit {
    bool hit{false};
    core::BlockPos blockPos{};
    core::BlockPos adjacentPos{};
    core::FaceDir face{core::FaceDir::PosY};
    voxel::BlockId blockId{voxel::BlockId::Air};
    float distance{0.0f};
    uint8_t blockMeta{0}; // Metadata of the hit block (shapes: outline box)
};

// What a ray stops on. Solid: the selectable blocks (the crosshair, line of sight).
// LiquidSources (empty bucket, Beta rayTraceBlocks_do(..., true)): those plus the
// liquid cells of metadata 0, as whole cubes.
enum class RaycastMode : uint8_t { Solid, LiquidSources };

class Raycaster {
public:
    // RaycastMode::Solid.
    [[nodiscard]] static RaycastHit cast(
        const world::World& world,
        const glm::vec3& origin,
        const glm::vec3& direction,
        float maxDistance
    ) noexcept;

    [[nodiscard]] static RaycastHit cast(
        const world::World& world,
        const glm::vec3& origin,
        const glm::vec3& direction,
        float maxDistance,
        RaycastMode mode
    ) noexcept;
};

} // namespace betacraft::physics
