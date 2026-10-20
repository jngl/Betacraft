#pragma once

#include "voxel/Block.hpp"
#include "voxel/BlockShapes.hpp"
#include <glm/glm.hpp>

namespace betacraft::math {

struct AABB {
    glm::vec3 min{0.0f};
    glm::vec3 max{0.0f};

    [[nodiscard]] constexpr bool intersects(const AABB& other) const noexcept {
        return (min.x < other.max.x && max.x > other.min.x) &&
               (min.y < other.max.y && max.y > other.min.y) &&
               (min.z < other.max.z && max.z > other.min.z);
    }

    [[nodiscard]] constexpr AABB translated(const glm::vec3& delta) const noexcept {
        return {min + delta, max + delta};
    }

    [[nodiscard]] constexpr AABB expanded(const glm::vec3& amount) const noexcept {
        return {min - amount, max + amount};
    }

    // Pre-S5.4 form: the box at metadata 0.
    [[nodiscard]] static inline AABB fromBlock(int32_t x, int32_t y, int32_t z, voxel::BlockId id) noexcept {
        return fromBlock(x, y, z, id, 0);
    }

    [[nodiscard]] static inline AABB fromShapeBox(int32_t x, int32_t y, int32_t z, const voxel::ShapeBox& b) noexcept {
        constexpr float s = 1.0f / 16.0f;
        const glm::vec3 o{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
        return {o + glm::vec3(b.x0, b.y0, b.z0) * s, o + glm::vec3(b.x1, b.y1, b.z1) * s};
    }

    // The block's box (S5.4: metadata-dependent for ladders, doors, stairs...): ladder 2/16,
    // door 3/16 by orientation and state, bed 9/16, fence 1.5 high, farmland 15/16, crops
    // their 0.25 pick slab (they do not collide), stairs the whole cell (forEachBlockBox has
    // their two boxes).
    [[nodiscard]] static inline AABB fromBlock(int32_t x, int32_t y, int32_t z, voxel::BlockId id,
                                               uint8_t meta) noexcept {
        const auto& def = voxel::BlockRegistry::get(id);
        const float fx = static_cast<float>(x);
        const float fy = static_cast<float>(y);
        const float fz = static_cast<float>(z);

        if (voxel::isDetailedShape(def.shape)) {
            const voxel::ShapeBoxes boxes = voxel::collisionShapeBoxes(def.shape, meta);
            if (boxes.count == 1) {
                return fromShapeBox(x, y, z, boxes.boxes[0]);
            }
            return fromShapeBox(x, y, z, voxel::blockSelectionBox(id, def.shape, meta));
        }
        if (def.shape == voxel::RenderShape::Slab) {
            return {{fx, fy, fz}, {fx + 1.0f, fy + 0.5f, fz + 1.0f}};
        }
        if (def.shape == voxel::RenderShape::Cactus) {
            constexpr float inset = 1.0f / 16.0f;
            return {{fx + inset, fy, fz + inset}, {fx + 1.0f - inset, fy + 1.0f, fz + 1.0f - inset}};
        }
        if (def.shape == voxel::RenderShape::Torch) {
            return {{fx + 0.375f, fy, fz + 0.375f}, {fx + 0.625f, fy + 0.625f, fz + 0.625f}};
        }
        if (def.shape == voxel::RenderShape::Cross) {
            return {{fx + 0.15f, fy, fz + 0.15f}, {fx + 0.85f, fy + 0.8f, fz + 0.85f}};
        }
        return {{fx, fy, fz}, {fx + 1.0f, fy + 1.0f, fz + 1.0f}};
    }

    // Ray-pick and outline box: fromBlock, except that a fence picks as a plain cube.
    [[nodiscard]] static inline AABB selectionBox(int32_t x, int32_t y, int32_t z, voxel::BlockId id,
                                                  uint8_t meta) noexcept {
        const auto& def = voxel::BlockRegistry::get(id);
        if (voxel::isDetailedShape(def.shape)) {
            return fromShapeBox(x, y, z, voxel::blockSelectionBox(id, def.shape, meta));
        }
        return fromBlock(x, y, z, id, meta);
    }

    // S5.10: every collision box of the cell (Beta getCollidingBoundingBoxes), fn(const AABB&)
    // returning bool; a true return stops the walk, which then returns true. Non-solid blocks
    // have none, stairs two, anything else its fromBlock box. The fence box reaches 0.5 into
    // the cell above, so callers scan one cell below their box.
    template <class Fn>
    static bool forEachBlockBox(int32_t x, int32_t y, int32_t z, voxel::BlockId id, uint8_t meta, Fn&& fn) {
        const auto& def = voxel::BlockRegistry::get(id);
        if (!def.solid) {
            return false;
        }
        if (voxel::isDetailedShape(def.shape)) {
            const voxel::ShapeBoxes boxes = voxel::collisionShapeBoxes(def.shape, meta);
            for (uint8_t i = 0; i < boxes.count; ++i) {
                if (fn(fromShapeBox(x, y, z, boxes.boxes[i]))) {
                    return true;
                }
            }
            return false;
        }
        return fn(fromBlock(x, y, z, id, meta));
    }
};

} // namespace betacraft::math
