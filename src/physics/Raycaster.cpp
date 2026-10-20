#include "physics/Raycaster.hpp"
#include "math/AABB.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace betacraft::physics {

namespace {

bool intersectRayAABB(
    const glm::vec3& origin,
    const glm::vec3& dir,
    const math::AABB& box,
    float maxDist,
    float& outT,
    core::FaceDir& outFace
) noexcept {
    float tMin = 0.0f;
    float tMax = maxDist;
    core::FaceDir hitFace = core::FaceDir::PosY;

    for (int axis = 0; axis < 3; ++axis) {
        const float o = origin[axis];
        const float d = dir[axis];
        const float bMin = box.min[axis];
        const float bMax = box.max[axis];

        if (std::abs(d) < 1e-6f) {
            if (o < bMin || o > bMax) {
                return false;
            }
        } else {
            const float invD = 1.0f / d;
            float t1 = (bMin - o) * invD;
            float t2 = (bMax - o) * invD;
            core::FaceDir faceNear{};
            if (axis == 0) faceNear = (d > 0.0f) ? core::FaceDir::NegX : core::FaceDir::PosX;
            if (axis == 1) faceNear = (d > 0.0f) ? core::FaceDir::NegY : core::FaceDir::PosY;
            if (axis == 2) faceNear = (d > 0.0f) ? core::FaceDir::NegZ : core::FaceDir::PosZ;

            if (t1 > t2) {
                std::swap(t1, t2);
            }
            if (t1 > tMin) {
                tMin = t1;
                hitFace = faceNear;
            }
            tMax = std::min(tMax, t2);
            if (tMin > tMax) {
                return false;
            }
        }
    }

    outT = tMin;
    outFace = hitFace;
    return tMin <= maxDist && tMax >= 0.0f;
}

} // namespace

RaycastHit Raycaster::cast(
    const world::World& world,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maxDistance
) noexcept {
    return cast(world, origin, direction, maxDistance, RaycastMode::Solid);
}

RaycastHit Raycaster::cast(
    const world::World& world,
    const glm::vec3& origin,
    const glm::vec3& direction,
    float maxDistance,
    RaycastMode mode
) noexcept {
    RaycastHit result{};
    const float dirLen = glm::length(direction);
    if (dirLen < 1e-6f) {
        return result;
    }
    const glm::vec3 dir = direction / dirLen;

    int32_t x = static_cast<int32_t>(std::floor(origin.x));
    int32_t y = static_cast<int32_t>(std::floor(origin.y));
    int32_t z = static_cast<int32_t>(std::floor(origin.z));

    const int32_t stepX = (dir.x > 0.0f) ? 1 : (dir.x < 0.0f ? -1 : 0);
    const int32_t stepY = (dir.y > 0.0f) ? 1 : (dir.y < 0.0f ? -1 : 0);
    const int32_t stepZ = (dir.z > 0.0f) ? 1 : (dir.z < 0.0f ? -1 : 0);

    const float tDeltaX = (stepX != 0) ? std::abs(1.0f / dir.x) : std::numeric_limits<float>::infinity();
    const float tDeltaY = (stepY != 0) ? std::abs(1.0f / dir.y) : std::numeric_limits<float>::infinity();
    const float tDeltaZ = (stepZ != 0) ? std::abs(1.0f / dir.z) : std::numeric_limits<float>::infinity();

    float tMaxX = (stepX > 0) ? (static_cast<float>(x + 1) - origin.x) * tDeltaX
                : (stepX < 0) ? (origin.x - static_cast<float>(x)) * tDeltaX
                              : std::numeric_limits<float>::infinity();
    float tMaxY = (stepY > 0) ? (static_cast<float>(y + 1) - origin.y) * tDeltaY
                : (stepY < 0) ? (origin.y - static_cast<float>(y)) * tDeltaY
                              : std::numeric_limits<float>::infinity();
    float tMaxZ = (stepZ > 0) ? (static_cast<float>(z + 1) - origin.z) * tDeltaZ
                : (stepZ < 0) ? (origin.z - static_cast<float>(z)) * tDeltaZ
                              : std::numeric_limits<float>::infinity();

    core::BlockPos prevPos{x, y, z};
    core::FaceDir steppedFace = core::FaceDir::PosY;
    float dist = 0.0f;

    while (dist <= maxDistance) {
        const voxel::BlockId id = world.getBlock(x, y, z);
        const voxel::BlockDef& def = voxel::BlockRegistry::get(id);
        // A liquid source is hit like a whole cube in LiquidSources mode.
        const bool liquidSource = mode == RaycastMode::LiquidSources && voxel::BlockRegistry::isLiquid(id) &&
                                  world.getMetadata(x, y, z) == 0;
        if (def.selectable || liquidSource) {
            const uint8_t meta = world.getMetadata(x, y, z);
            const bool wholeCell = liquidSource || def.shape == voxel::RenderShape::Cube;
            float subT = dist;
            core::FaceDir subFace = steppedFace;
            if (wholeCell ||
                intersectRayAABB(origin, dir, math::AABB::selectionBox(x, y, z, id, meta), maxDistance, subT, subFace)) {
                result.hit = true;
                result.blockPos = {x, y, z};
                result.face = wholeCell ? steppedFace : subFace;
                const glm::ivec3 n = core::faceNormal(result.face);
                result.adjacentPos = wholeCell ? prevPos : core::BlockPos{x + n.x, y + n.y, z + n.z};
                result.blockId = id;
                result.blockMeta = meta;
                result.distance = wholeCell ? dist : subT;
                return result;
            }
        }

        prevPos = {x, y, z};
        if (tMaxX < tMaxY) {
            if (tMaxX < tMaxZ) {
                dist = tMaxX;
                tMaxX += tDeltaX;
                x += stepX;
                steppedFace = (stepX > 0) ? core::FaceDir::NegX : core::FaceDir::PosX;
            } else {
                dist = tMaxZ;
                tMaxZ += tDeltaZ;
                z += stepZ;
                steppedFace = (stepZ > 0) ? core::FaceDir::NegZ : core::FaceDir::PosZ;
            }
        } else {
            if (tMaxY < tMaxZ) {
                dist = tMaxY;
                tMaxY += tDeltaY;
                y += stepY;
                steppedFace = (stepY > 0) ? core::FaceDir::NegY : core::FaceDir::PosY;
            } else {
                dist = tMaxZ;
                tMaxZ += tDeltaZ;
                z += stepZ;
                steppedFace = (stepZ > 0) ? core::FaceDir::NegZ : core::FaceDir::PosZ;
            }
        }
    }

    return result;
}

} // namespace betacraft::physics
