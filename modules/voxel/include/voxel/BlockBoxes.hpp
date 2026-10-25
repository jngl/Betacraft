#pragma once

// A block's boxes in world space: collision, selection and the per-cell walk used by physics.
#include "math/AABB.hpp"
#include "voxel/Block.hpp"
#include "voxel/BlockShapes.hpp"

#include <glm/glm.hpp>

namespace betacraft::voxel {

[[nodiscard]] inline math::AABB shapeBoxAABB(int32_t x, int32_t y, int32_t z, const ShapeBox& b) noexcept {
    constexpr float s = 1.0f / 16.0f;
    const glm::vec3 o{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
    return {o + glm::vec3(b.x0, b.y0, b.z0) * s, o + glm::vec3(b.x1, b.y1, b.z1) * s};
}

// The block's box (metadata-dependent for ladders, doors, stairs...): ladder 2/16,
// door 3/16 by orientation and state, bed 9/16, fence 1.5 high, farmland 15/16, crops
// their 0.25 pick slab (they do not collide), stairs the whole cell (forEachBlockAABB has
// their two boxes). meta 0 is the original full-block form.
[[nodiscard]] inline math::AABB blockAABB(int32_t x, int32_t y, int32_t z, BlockId id, uint8_t meta = 0) noexcept {
    const auto& def = BlockRegistry::get(id);
    const float fx = static_cast<float>(x);
    const float fy = static_cast<float>(y);
    const float fz = static_cast<float>(z);

    if (isDetailedShape(def.shape)) {
        const ShapeBoxes boxes = collisionShapeBoxes(def.shape, meta);
        if (boxes.count == 1) {
            return shapeBoxAABB(x, y, z, boxes.boxes[0]);
        }
        return shapeBoxAABB(x, y, z, blockSelectionBox(id, def.shape, meta));
    }
    if (def.shape == RenderShape::Slab) {
        return {{fx, fy, fz}, {fx + 1.0f, fy + 0.5f, fz + 1.0f}};
    }
    if (def.shape == RenderShape::Cactus) {
        constexpr float inset = 1.0f / 16.0f;
        return {{fx + inset, fy, fz + inset}, {fx + 1.0f - inset, fy + 1.0f, fz + 1.0f - inset}};
    }
    if (def.shape == RenderShape::Torch) {
        return {{fx + 0.375f, fy, fz + 0.375f}, {fx + 0.625f, fy + 0.625f, fz + 0.625f}};
    }
    if (def.shape == RenderShape::Cross) {
        return {{fx + 0.15f, fy, fz + 0.15f}, {fx + 0.85f, fy + 0.8f, fz + 0.85f}};
    }
    return {{fx, fy, fz}, {fx + 1.0f, fy + 1.0f, fz + 1.0f}};
}

// Ray-pick and outline box: blockAABB, except that a fence picks as a plain cube.
[[nodiscard]] inline math::AABB selectionAABB(int32_t x, int32_t y, int32_t z, BlockId id, uint8_t meta) noexcept {
    const auto& def = BlockRegistry::get(id);
    if (isDetailedShape(def.shape)) {
        return shapeBoxAABB(x, y, z, blockSelectionBox(id, def.shape, meta));
    }
    return blockAABB(x, y, z, id, meta);
}

// Every collision box of the cell (Beta getCollidingBoundingBoxes), fn(const math::AABB&)
// returning bool; a true return stops the walk, which then returns true. Non-solid blocks
// have none, stairs two, anything else its blockAABB box. The fence box reaches 0.5 into
// the cell above, so callers scan one cell below their box.
template <class Fn>
bool forEachBlockAABB(int32_t x, int32_t y, int32_t z, BlockId id, uint8_t meta, Fn&& fn) {
    const auto& def = BlockRegistry::get(id);
    if (!def.solid) {
        return false;
    }
    if (isDetailedShape(def.shape)) {
        const ShapeBoxes boxes = collisionShapeBoxes(def.shape, meta);
        for (uint8_t i = 0; i < boxes.count; ++i) {
            if (fn(shapeBoxAABB(x, y, z, boxes.boxes[i]))) {
                return true;
            }
        }
        return false;
    }
    return fn(blockAABB(x, y, z, id, meta));
}

} // namespace betacraft::voxel
