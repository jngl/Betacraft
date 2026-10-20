// S8.5 redstone blocks: Beta ids, registry flags, the Flat and Lever shapes (appended to
// RenderShape), their boxes, and the geometry the shared shape mesher builds for them.
#include "gameplay/Harvesting.hpp"
#include "gameplay/ItemIds.hpp"
#include "math/AABB.hpp"
#include "streaming/BlockShapeMesher.hpp"
#include "voxel/Block.hpp"
#include "voxel/BlockBoxes.hpp"
#include "voxel/BlockShapes.hpp"

#include <gtest/gtest.h>

#include <vector>

using namespace betacraft;
using voxel::BlockId;
using voxel::BlockRegistry;
using voxel::RenderShape;

TEST(RedstoneBlocks, BetaIdsAndAppendedShapes) {
    EXPECT_EQ(static_cast<int>(BlockId::RedstoneWire), 55);
    EXPECT_EQ(static_cast<int>(BlockId::Lever), 69);
    EXPECT_EQ(static_cast<int>(BlockId::StonePressurePlate), 70);
    EXPECT_EQ(static_cast<int>(BlockId::IronDoor), 71);
    EXPECT_EQ(static_cast<int>(BlockId::WoodPressurePlate), 72);
    EXPECT_EQ(static_cast<int>(BlockId::RedstoneOre), 73);
    EXPECT_EQ(static_cast<int>(BlockId::RedstoneTorchOff), 75);
    EXPECT_EQ(static_cast<int>(BlockId::RedstoneTorchOn), 76);
    EXPECT_EQ(static_cast<int>(BlockId::StoneButton), 77);
    EXPECT_EQ(static_cast<int>(BlockId::SnowGrass), 78) << "78 is never reassigned";
    // Appended after the S5.4 shapes.
    EXPECT_EQ(static_cast<int>(RenderShape::Flat), static_cast<int>(RenderShape::Stairs) + 1);
    EXPECT_EQ(static_cast<int>(RenderShape::Lever), static_cast<int>(RenderShape::Stairs) + 2);
    EXPECT_TRUE(voxel::isDetailedShape(RenderShape::Flat));
    EXPECT_TRUE(voxel::isDetailedShape(RenderShape::Lever));
    EXPECT_TRUE(voxel::shapeDependsOnMetadata(RenderShape::Flat));
    EXPECT_TRUE(voxel::shapeDependsOnMetadata(RenderShape::Lever));
    EXPECT_EQ(gameplay::items::IronDoor, 330);
    EXPECT_EQ(gameplay::maxStackSize(gameplay::items::IronDoor), 1);
}

TEST(RedstoneBlocks, RegistryFlags) {
    BlockRegistry::initialize();
    for (const BlockId id : {BlockId::RedstoneWire, BlockId::Lever, BlockId::StonePressurePlate,
                             BlockId::WoodPressurePlate, BlockId::RedstoneTorchOff, BlockId::RedstoneTorchOn,
                             BlockId::StoneButton}) {
        const voxel::BlockDef& def = BlockRegistry::get(id);
        EXPECT_EQ(def.id, id);
        EXPECT_FALSE(def.solid) << def.name << ": nothing of it collides";
        EXPECT_FALSE(def.opaque) << def.name;
        EXPECT_TRUE(def.selectable) << def.name;
        EXPECT_FALSE(def.replaceable) << def.name;
        EXPECT_FALSE(def.isItemOnly) << def.name;
    }
    EXPECT_EQ(BlockRegistry::get(BlockId::RedstoneTorchOn).lightEmission, 7);
    EXPECT_EQ(BlockRegistry::get(BlockId::RedstoneTorchOff).lightEmission, 0);
    EXPECT_EQ(BlockRegistry::get(BlockId::RedstoneTorchOn).shape, RenderShape::Torch);
    const voxel::BlockDef& iron = BlockRegistry::get(BlockId::IronDoor);
    EXPECT_TRUE(iron.solid);
    EXPECT_EQ(iron.shape, RenderShape::Door);
    EXPECT_FLOAT_EQ(iron.hardness, 5.0f);
    EXPECT_TRUE(voxel::isDoorBlock(BlockId::IronDoor));
    EXPECT_TRUE(voxel::isDoorBlock(BlockId::WoodDoor));
    EXPECT_FALSE(voxel::isDoorBlock(BlockId::Lever));
    EXPECT_EQ(BlockRegistry::get(BlockId::RedstoneWire).shape, RenderShape::Flat);
    EXPECT_EQ(BlockRegistry::get(BlockId::StonePressurePlate).shape, RenderShape::Flat);
    EXPECT_EQ(BlockRegistry::get(BlockId::StoneButton).shape, RenderShape::Lever);
}

TEST(RedstoneBlocks, Drops) {
    BlockRegistry::initialize();
    // Redstone ore yields 4 or 5 dust (Beta quantityDropped 4 + nextInt(2)).
    bool four = false;
    bool five = false;
    for (uint64_t seed = 0; seed < 64; ++seed) {
        gameplay::ItemStack pick = gameplay::makeStack(gameplay::items::IronPickaxe, 1);
        const gameplay::BlockBreakOutcome r = gameplay::breakBlockWith(BlockId::RedstoneOre, pick, seed);
        ASSERT_TRUE(r.drop.has_value());
        EXPECT_EQ(r.drop->id, gameplay::items::Redstone);
        four |= r.drop->count == 4;
        five |= r.drop->count == 5;
        EXPECT_TRUE(r.drop->count == 4 || r.drop->count == 5);
    }
    EXPECT_TRUE(four && five);
    gameplay::ItemStack hand{};
    const auto wire = gameplay::breakBlockWith(BlockId::RedstoneWire, hand, 1);
    ASSERT_TRUE(wire.drop.has_value());
    EXPECT_EQ(wire.drop->id, gameplay::items::Redstone);
    const auto unlit = gameplay::breakBlockWith(BlockId::RedstoneTorchOff, hand, 1);
    ASSERT_TRUE(unlit.drop.has_value());
    EXPECT_EQ(unlit.drop->id, gameplay::toItemId(BlockId::RedstoneTorchOn)) << "an unlit torch drops a lit one";
    gameplay::ItemStack pick = gameplay::makeStack(gameplay::items::StonePickaxe, 1);
    const auto door = gameplay::breakBlockWith(BlockId::IronDoor, pick, 1);
    ASSERT_TRUE(door.drop.has_value());
    EXPECT_EQ(door.drop->id, gameplay::items::IronDoor);
    gameplay::ItemStack bare{};
    EXPECT_FALSE(gameplay::breakBlockWith(BlockId::IronDoor, bare, 1).drop.has_value()) << "needs a pickaxe";
}

TEST(RedstoneBlocks, AttachmentAndBoxes) {
    using voxel::attachmentOffset;
    EXPECT_EQ(attachmentOffset(voxel::kAttachWallNegX), glm::ivec3(-1, 0, 0));
    EXPECT_EQ(attachmentOffset(voxel::kAttachWallPosX), glm::ivec3(1, 0, 0));
    EXPECT_EQ(attachmentOffset(voxel::kAttachWallNegZ), glm::ivec3(0, 0, -1));
    EXPECT_EQ(attachmentOffset(voxel::kAttachWallPosZ), glm::ivec3(0, 0, 1));
    EXPECT_EQ(attachmentOffset(voxel::kAttachFloor), glm::ivec3(0, -1, 0));
    EXPECT_EQ(attachmentOffset(0), glm::ivec3(0, -1, 0)) << "torches placed before S8.5 stand";
    EXPECT_EQ(attachmentOffset(voxel::kAttachWallPosX | voxel::kLeverOnBit), glm::ivec3(1, 0, 0));
    EXPECT_EQ(voxel::attachmentForClickedFace(core::FaceDir::PosX), voxel::kAttachWallNegX);
    EXPECT_EQ(voxel::attachmentForClickedFace(core::FaceDir::PosY), voxel::kAttachFloor);
    EXPECT_FALSE(voxel::attachmentForClickedFace(core::FaceDir::NegY).has_value());

    // A button against the wall at +X sits flush with that side of the cell.
    const voxel::ShapeBox button = voxel::orientAttachedBox(voxel::buttonBox(0), voxel::kAttachWallPosX);
    EXPECT_EQ(button.x1, 16);
    EXPECT_EQ(button.x0, 16 - voxel::kButtonDepth);
    const voxel::ShapeBox pressed =
        voxel::orientAttachedBox(voxel::buttonBox(voxel::kButtonPressedBit), voxel::kAttachWallPosX);
    EXPECT_EQ(pressed.x1 - pressed.x0, 1) << "a pressed button is thinner";
    const voxel::ShapeBox negZ = voxel::orientAttachedBox(voxel::buttonBox(0), voxel::kAttachWallNegZ);
    EXPECT_EQ(negZ.z0, 0);
    EXPECT_EQ(negZ.z1, voxel::kButtonDepth);
    EXPECT_LT(negZ.y1 - negZ.y0, 16);

    const math::AABB wire = voxel::selectionAABB(0, 0, 0, BlockId::RedstoneWire, 5);
    EXPECT_FLOAT_EQ(wire.max.y, 1.0f / 16.0f);
    EXPECT_FLOAT_EQ(wire.min.x, 0.0f);
    const math::AABB plate = voxel::selectionAABB(0, 0, 0, BlockId::StonePressurePlate, 0);
    EXPECT_FLOAT_EQ(plate.min.x, 1.0f / 16.0f);
    EXPECT_FLOAT_EQ(plate.max.z, 15.0f / 16.0f);
    // Lever off and on lean their handles opposite ways.
    EXPECT_NE(voxel::leverHandleBox(0).z0, voxel::leverHandleBox(voxel::kLeverOnBit).z0);
    // Pistons: Beta facing 0..5 and the look rule (it faces the player).
    EXPECT_EQ(voxel::pistonFacing(5), core::FaceDir::PosX);
    EXPECT_EQ(voxel::pistonFacing(voxel::kPistonPoweredBit | 2), core::FaceDir::NegZ);
    EXPECT_EQ(voxel::pistonMetaForLook(1.0f, 0.0f, 0.0f), 4);  // looking +X: faces -X
    EXPECT_EQ(voxel::pistonMetaForLook(0.0f, -1.0f, 0.0f), 1); // looking down: faces up
}

TEST(RedstoneBlocks, ShapeMesherBuildsEveryPart) {
    BlockRegistry::initialize();
    const auto air = [](int32_t, int32_t, int32_t) { return streaming::ShapeVoxel{}; };
    const auto quads = [&](BlockId id, uint8_t meta) {
        std::vector<streaming::PackedChunkVertex> out;
        streaming::emitDetailedShape(out, BlockRegistry::get(id), meta, 1, 1, 1, 0, air);
        return out.size() / 4;
    };
    EXPECT_EQ(quads(BlockId::RedstoneWire, 0), 6u);
    EXPECT_EQ(quads(BlockId::StonePressurePlate, 0), 6u);
    EXPECT_EQ(quads(BlockId::WoodPressurePlate, voxel::kPlatePressedBit), 6u);
    EXPECT_EQ(quads(BlockId::Lever, voxel::kAttachFloor), 12u) << "base + handle";
    EXPECT_EQ(quads(BlockId::Lever, voxel::kAttachWallNegZ | voxel::kLeverOnBit), 12u);
    EXPECT_EQ(quads(BlockId::StoneButton, voxel::kAttachWallPosX), 6u);
    EXPECT_EQ(quads(BlockId::IronDoor, 0), 6u);
    // The bottom of a plate on a stone block is hidden.
    const auto onStone = [](int32_t, int32_t dy, int32_t) {
        return streaming::ShapeVoxel{dy < 0 ? BlockId::Stone : BlockId::Air, 0, 15, 0};
    };
    std::vector<streaming::PackedChunkVertex> out;
    streaming::emitDetailedShape(out, BlockRegistry::get(BlockId::RedstoneWire), 3, 1, 1, 1, 0, onStone);
    EXPECT_EQ(out.size() / 4, 5u);
}
