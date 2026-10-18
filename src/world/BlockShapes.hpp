#pragma once

#include "core/Types.hpp"
#include "world/Block.hpp"

#include <array>
#include <cstdint>
#include <glm/glm.hpp>
#include <optional>

// S5.4 metadata-dependent shapes (Beta 1.7.3 block bounds), in sixteenths of a block so the
// meshers can emit them exactly (PackedChunkVertex fine offsets) and physics::AABB can turn
// them into boxes. One table feeds collision, selection and both meshers.

namespace betacraft::world {

// Box inside a cell, in 1/16 block units. y1 may exceed 16 (the fence's 1.5-block collision).
struct ShapeBox {
    uint8_t x0{0}, y0{0}, z0{0};
    uint8_t x1{16}, y1{16}, z1{16};
};

struct ShapeBoxes {
    std::array<ShapeBox, 2> boxes{};
    uint8_t count{0};
};

inline constexpr ShapeBox kFullShapeBox{0, 0, 0, 16, 16, 16};

inline constexpr uint8_t kLadderThickness = 2;     // BlockLadder: 0.125
inline constexpr uint8_t kDoorThickness = 3;       // BlockDoor: 0.1875
inline constexpr uint8_t kBedHeight = 9;           // BlockBed: 0.5625
inline constexpr uint8_t kFarmlandHeight = 15;     // BlockFarmland: 0.9375
inline constexpr uint8_t kFenceCollisionHeight = 24; // BlockFence.getCollisionBoundingBox: 1.5
inline constexpr uint8_t kCropsSelectionHeight = 4;  // BlockCrops: 0.25

inline constexpr uint8_t kDoorOrientationMask = 0x3;
inline constexpr uint8_t kDoorOpenBit = 0x4;
inline constexpr uint8_t kDoorUpperBit = 0x8;
inline constexpr uint8_t kBedOrientationMask = 0x3;
inline constexpr uint8_t kBedHeadBit = 0x8;
inline constexpr uint8_t kCropsMaxStage = 7;

// --- Ladder (65) ---------------------------------------------------------------------------
// Beta metadata 2..5 names the side of the ladder cell its wall is on: 2 = +Z, 3 = -Z,
// 4 = +X, 5 = -X (anything else draws like 2, as Beta leaves the bounds unset).
[[nodiscard]] constexpr glm::ivec3 ladderSupportOffset(uint8_t meta) noexcept {
    switch (meta & 0x7) {
        case 3: return {0, 0, -1};
        case 4: return {1, 0, 0};
        case 5: return {-1, 0, 0};
        default: return {0, 0, 1};
    }
}

[[nodiscard]] constexpr ShapeBox ladderBox(uint8_t meta) noexcept {
    constexpr uint8_t t = kLadderThickness;
    switch (meta & 0x7) {
        case 3: return {0, 0, 0, 16, 16, t};
        case 4: return {16 - t, 0, 0, 16, 16, 16};
        case 5: return {0, 0, 0, t, 16, 16};
        default: return {0, 0, 16 - t, 16, 16, 16};
    }
}

// Metadata of a ladder put on the `clicked` face of a wall block (Beta onBlockPlaced side
// 2..5); nullopt for a top or bottom face.
[[nodiscard]] constexpr std::optional<uint8_t> ladderMetaForClickedFace(core::FaceDir clicked) noexcept {
    switch (clicked) {
        case core::FaceDir::NegZ: return uint8_t{2};
        case core::FaceDir::PosZ: return uint8_t{3};
        case core::FaceDir::NegX: return uint8_t{4};
        case core::FaceDir::PosX: return uint8_t{5};
        default: return std::nullopt;
    }
}

// --- Door (64) -----------------------------------------------------------------------------
// Beta BlockDoor.getState: the closed orientation, turned a quarter when open.
[[nodiscard]] constexpr uint8_t doorState(uint8_t meta) noexcept {
    return (meta & kDoorOpenBit) == 0 ? static_cast<uint8_t>(((meta & 3) + 3) & 3)
                                      : static_cast<uint8_t>(meta & 3);
}

[[nodiscard]] constexpr ShapeBox doorBox(uint8_t meta) noexcept {
    constexpr uint8_t t = kDoorThickness;
    switch (doorState(meta)) {
        case 0: return {0, 0, 0, 16, 16, t};
        case 1: return {16 - t, 0, 0, 16, 16, 16};
        case 2: return {0, 0, 16 - t, 16, 16, 16};
        default: return {0, 0, 0, t, 16, 16};
    }
}

// The cell of the other half of a door (S5.6): above the lower half, below the upper one.
[[nodiscard]] constexpr glm::ivec3 doorPartnerOffset(uint8_t meta) noexcept {
    return (meta & kDoorUpperBit) != 0 ? glm::ivec3{0, -1, 0} : glm::ivec3{0, 1, 0};
}

// --- Bed (26) ------------------------------------------------------------------------------
// Beta BlockBed.headBlockToFootBlockMap: the step from the foot to the head for orientation
// 0..3 (+Z, -X, -Z, +X).
[[nodiscard]] constexpr glm::ivec3 bedHeadDirection(uint8_t meta) noexcept {
    switch (meta & kBedOrientationMask) {
        case 1: return {-1, 0, 0};
        case 2: return {0, 0, -1};
        case 3: return {1, 0, 0};
        default: return {0, 0, 1};
    }
}

// The cell of the other half of a bed (S5.8).
[[nodiscard]] constexpr glm::ivec3 bedPartnerOffset(uint8_t meta) noexcept {
    return (meta & kBedHeadBit) != 0 ? -bedHeadDirection(meta) : bedHeadDirection(meta);
}

// --- Stairs (53, 67) ------------------------------------------------------------------------
// Beta BlockStairs.getCollidingBoundingBoxes: a bottom half plus a full-height half on the
// side the metadata names (0 = +X, 1 = -X, 2 = +Z, 3 = -Z).
[[nodiscard]] constexpr ShapeBoxes stairsBoxes(uint8_t meta) noexcept {
    switch (meta & 3) {
        case 0: return {{{{0, 0, 0, 8, 8, 16}, {8, 0, 0, 16, 16, 16}}}, 2};
        case 1: return {{{{0, 0, 0, 8, 16, 16}, {8, 0, 0, 16, 8, 16}}}, 2};
        case 2: return {{{{0, 0, 0, 16, 8, 8}, {0, 0, 8, 16, 16, 16}}}, 2};
        default: return {{{{0, 0, 0, 16, 16, 8}, {0, 0, 8, 16, 8, 16}}}, 2};
    }
}

// Beta BlockStairs.onBlockPlacedBy: the high step ends up on the far side of the look
// direction. `lookX`/`lookZ` are the horizontal look vector.
[[nodiscard]] constexpr uint8_t stairsMetaForLook(float lookX, float lookZ) noexcept {
    const float ax = lookX < 0.0f ? -lookX : lookX;
    const float az = lookZ < 0.0f ? -lookZ : lookZ;
    if (ax >= az) {
        return lookX >= 0.0f ? uint8_t{0} : uint8_t{1};
    }
    return lookZ >= 0.0f ? uint8_t{2} : uint8_t{3};
}

// --- Redstone parts (S8.5) -------------------------------------------------------------------
// Lever, button and redstone torch metadata bits 0-2 (Beta): 1..4 hang on the wall at -X, +X,
// -Z, +Z of the cell, anything else stands on the floor (Beta 5/6; 0 is what the torches
// placed before S8.5 and the engine's own torches carry).
inline constexpr uint8_t kAttachMask = 0x7;
inline constexpr uint8_t kAttachWallNegX = 1;
inline constexpr uint8_t kAttachWallPosX = 2;
inline constexpr uint8_t kAttachWallNegZ = 3;
inline constexpr uint8_t kAttachWallPosZ = 4;
inline constexpr uint8_t kAttachFloor = 5;
inline constexpr uint8_t kLeverOnBit = 0x8;
inline constexpr uint8_t kButtonPressedBit = 0x8;
inline constexpr uint8_t kPlatePressedBit = 0x1;
inline constexpr uint8_t kPlateHeight = 1;        // BlockPressurePlate: 0.0625 (pressed 0.03125, drawn the same)
inline constexpr uint8_t kButtonDepth = 2;        // BlockButton: 0.125, pressed 0.0625
inline constexpr uint8_t kPistonFacingMask = 0x7; // Beta BlockPistonBase: 0 down, 1 up, 2 -Z, 3 +Z, 4 -X, 5 +X
inline constexpr uint8_t kPistonPoweredBit = 0x8; // Set while powered (the push fires on the rising edge)

// The cell holding up the lever, button or redstone torch with metadata `meta`.
[[nodiscard]] constexpr glm::ivec3 attachmentOffset(uint8_t meta) noexcept {
    switch (meta & kAttachMask) {
        case kAttachWallNegX: return {-1, 0, 0};
        case kAttachWallPosX: return {1, 0, 0};
        case kAttachWallNegZ: return {0, 0, -1};
        case kAttachWallPosZ: return {0, 0, 1};
        default: return {0, -1, 0};
    }
}

// Attachment metadata for a part put on the `clicked` face of a block (the block is on the
// opposite side of the new cell); nullopt for a ceiling.
[[nodiscard]] constexpr std::optional<uint8_t> attachmentForClickedFace(core::FaceDir clicked) noexcept {
    switch (clicked) {
        case core::FaceDir::PosX: return kAttachWallNegX;
        case core::FaceDir::NegX: return kAttachWallPosX;
        case core::FaceDir::PosZ: return kAttachWallNegZ;
        case core::FaceDir::NegZ: return kAttachWallPosZ;
        case core::FaceDir::PosY: return kAttachFloor;
        default: return std::nullopt;
    }
}

// Beta piston facing (metadata bits 0-2) as a push direction.
[[nodiscard]] constexpr core::FaceDir pistonFacing(uint8_t meta) noexcept {
    switch (meta & kPistonFacingMask) {
        case 1: return core::FaceDir::PosY;
        case 2: return core::FaceDir::NegZ;
        case 3: return core::FaceDir::PosZ;
        case 4: return core::FaceDir::NegX;
        case 5: return core::FaceDir::PosX;
        default: return core::FaceDir::NegY;
    }
}

// Beta BlockPistonBase.determineOrientation: the piston faces the player; looking steeply
// down it faces up, steeply up it faces down.
[[nodiscard]] constexpr uint8_t pistonMetaForLook(float lookX, float lookY, float lookZ) noexcept {
    if (lookY < -0.8f) return 1;
    if (lookY > 0.8f) return 0;
    const float ax = lookX < 0.0f ? -lookX : lookX;
    const float az = lookZ < 0.0f ? -lookZ : lookZ;
    if (ax >= az) {
        return lookX >= 0.0f ? uint8_t{4} : uint8_t{5};
    }
    return lookZ >= 0.0f ? uint8_t{2} : uint8_t{3};
}

// A box given on the floor (x, z across the surface, y away from it) turned onto the wall
// the attachment metadata names: y becomes the distance from the wall, z the height.
[[nodiscard]] constexpr ShapeBox orientAttachedBox(const ShapeBox& b, uint8_t meta) noexcept {
    switch (meta & kAttachMask) {
        case kAttachWallNegX: return {b.y0, b.z0, b.x0, b.y1, b.z1, b.x1};
        case kAttachWallPosX:
            return {static_cast<uint8_t>(16 - b.y1), b.z0, b.x0, static_cast<uint8_t>(16 - b.y0), b.z1, b.x1};
        case kAttachWallNegZ: return {b.x0, b.z0, b.y0, b.x1, b.z1, b.y1};
        case kAttachWallPosZ:
            return {b.x0, b.z0, static_cast<uint8_t>(16 - b.y1), b.x1, b.z1, static_cast<uint8_t>(16 - b.y0)};
        default: return b;
    }
}

// Floor-local boxes of the lever (base and handle, leaning one way off and the other way on)
// and of the button (thinner when pressed), for orientAttachedBox.
[[nodiscard]] constexpr ShapeBox leverBaseBox() noexcept { return {5, 0, 4, 11, 3, 12}; }
[[nodiscard]] constexpr ShapeBox leverHandleBox(uint8_t meta) noexcept {
    return (meta & kLeverOnBit) != 0 ? ShapeBox{7, 3, 9, 9, 11, 11} : ShapeBox{7, 3, 5, 9, 11, 7};
}
[[nodiscard]] constexpr ShapeBox buttonBox(uint8_t meta) noexcept {
    return {5, 0, 6, 11, static_cast<uint8_t>((meta & kButtonPressedBit) != 0 ? 1 : kButtonDepth), 10};
}

// The flat plate of the wire (whole cell) and of the pressure plates (inset by 1/16).
[[nodiscard]] constexpr ShapeBox flatBox(BlockId id) noexcept {
    return id == BlockId::RedstoneWire ? ShapeBox{0, 0, 0, 16, kPlateHeight, 16}
                                       : ShapeBox{1, 0, 1, 15, kPlateHeight, 15};
}

// --- Per-shape tables ----------------------------------------------------------------------

// Collision boxes of a detailed shape (isDetailedShape); crops have none.
[[nodiscard]] constexpr ShapeBoxes collisionShapeBoxes(RenderShape shape, uint8_t meta) noexcept {
    switch (shape) {
        case RenderShape::Ladder: return {{{ladderBox(meta), {}}}, 1};
        case RenderShape::Door: return {{{doorBox(meta), {}}}, 1};
        case RenderShape::Bed: return {{{{0, 0, 0, 16, kBedHeight, 16}, {}}}, 1};
        case RenderShape::Fence: return {{{{0, 0, 0, 16, kFenceCollisionHeight, 16}, {}}}, 1};
        case RenderShape::Farmland: return {{{{0, 0, 0, 16, kFarmlandHeight, 16}, {}}}, 1};
        case RenderShape::Stairs: return stairsBoxes(meta);
        default: return {};
    }
}

// The outline / ray-pick box of a detailed shape: the fence and the stairs pick as a full
// cube (Beta selection bounds), crops as their 0.25-high slab.
[[nodiscard]] constexpr ShapeBox selectionShapeBox(RenderShape shape, uint8_t meta) noexcept {
    switch (shape) {
        case RenderShape::Ladder: return ladderBox(meta);
        case RenderShape::Door: return doorBox(meta);
        case RenderShape::Bed: return {0, 0, 0, 16, kBedHeight, 16};
        case RenderShape::Farmland: return {0, 0, 0, 16, kFarmlandHeight, 16};
        case RenderShape::Crops: return {0, 0, 0, 16, kCropsSelectionHeight, 16};
        default: return kFullShapeBox;
    }
}

// The outline / ray-pick box of the block `id` with metadata `meta` when it has a detailed
// shape: selectionShapeBox, with the S8.5 parts told apart by their block.
[[nodiscard]] constexpr ShapeBox blockSelectionBox(BlockId id, RenderShape shape, uint8_t meta) noexcept {
    switch (shape) {
        case RenderShape::Flat: return flatBox(id);
        case RenderShape::Lever:
            return orientAttachedBox(id == BlockId::Lever ? ShapeBox{4, 0, 3, 12, 10, 13} : buttonBox(meta), meta);
        default: return selectionShapeBox(shape, meta);
    }
}

// Beta World.isBlockNormalCube: what ladders hang on and fences do not connect through.
[[nodiscard]] inline bool isNormalCube(BlockId id) noexcept {
    const BlockDef& def = BlockRegistry::get(id);
    return def.solid && def.opaque && def.shape == RenderShape::Cube;
}

} // namespace betacraft::world
