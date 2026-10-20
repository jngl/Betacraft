#include "voxel/Block.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace betacraft::world;
using namespace testing;

TEST(BlockRegistryTest, InitializationAndSolidity) {
    BlockRegistry::initialize();

    EXPECT_FALSE(BlockRegistry::isSolid(BlockId::Air));
    EXPECT_FALSE(BlockRegistry::isSolid(BlockId::Water));
    EXPECT_FALSE(BlockRegistry::isSolid(BlockId::TallGrass));
    EXPECT_FALSE(BlockRegistry::isSolid(BlockId::Torch));
    EXPECT_FALSE(BlockRegistry::isSolid(BlockId::Sapling));
    EXPECT_FALSE(BlockRegistry::isSolid(BlockId::Dandelion));
    EXPECT_FALSE(BlockRegistry::isSolid(BlockId::Rose));

    EXPECT_TRUE(BlockRegistry::isSolid(BlockId::Stone));
    EXPECT_TRUE(BlockRegistry::isSolid(BlockId::Grass));
    EXPECT_TRUE(BlockRegistry::isSolid(BlockId::Dirt));
    EXPECT_TRUE(BlockRegistry::isSolid(BlockId::Cobblestone));
    EXPECT_TRUE(BlockRegistry::isSolid(BlockId::Bedrock));
    EXPECT_TRUE(BlockRegistry::isSolid(BlockId::Glass));
    EXPECT_TRUE(BlockRegistry::isSolid(BlockId::Piston));
}

TEST(BlockRegistryTest, OpacityAndLiquids) {
    EXPECT_TRUE(BlockRegistry::isOpaque(BlockId::Stone));
    EXPECT_TRUE(BlockRegistry::isOpaque(BlockId::Dirt));
    EXPECT_TRUE(BlockRegistry::isOpaque(BlockId::Bedrock));

    EXPECT_FALSE(BlockRegistry::isOpaque(BlockId::Air));
    EXPECT_FALSE(BlockRegistry::isOpaque(BlockId::Glass));
    EXPECT_FALSE(BlockRegistry::isOpaque(BlockId::Leaves));
    EXPECT_FALSE(BlockRegistry::isOpaque(BlockId::Water));
    EXPECT_FALSE(BlockRegistry::isOpaque(BlockId::Ice));

    EXPECT_TRUE(BlockRegistry::isLiquid(BlockId::Water));
    EXPECT_FALSE(BlockRegistry::isLiquid(BlockId::Stone));
    EXPECT_FALSE(BlockRegistry::isLiquid(BlockId::Air));
}

TEST(BlockRegistryTest, LightEmissionAndOpacity) {
    EXPECT_EQ(BlockRegistry::get(BlockId::Torch).lightEmission, 14);
    EXPECT_EQ(BlockRegistry::get(BlockId::Glowstone).lightEmission, 15);
    EXPECT_EQ(BlockRegistry::get(BlockId::RedstoneOre).lightEmission, 4);
    EXPECT_EQ(BlockRegistry::get(BlockId::Air).lightEmission, 0);
    EXPECT_EQ(BlockRegistry::get(BlockId::Stone).lightEmission, 0);

    EXPECT_EQ(BlockRegistry::get(BlockId::Stone).lightOpacity, 15);
    EXPECT_EQ(BlockRegistry::get(BlockId::Water).lightOpacity, 2);
    EXPECT_EQ(BlockRegistry::get(BlockId::Ice).lightOpacity, 2);
    EXPECT_EQ(BlockRegistry::get(BlockId::Leaves).lightOpacity, 1);
    EXPECT_EQ(BlockRegistry::get(BlockId::Air).lightOpacity, 0);
}

TEST(BlockRegistryTest, PickaxeMultipliers) {
    // Hand on stone
    EXPECT_FLOAT_EQ(BlockRegistry::getPickaxeSpeedMultiplier(BlockId::Air, BlockId::Stone), 1.0f);

    // Pickaxes on stone
    EXPECT_GT(BlockRegistry::getPickaxeSpeedMultiplier(BlockId::WoodPickaxe, BlockId::Stone), 2.0f);
    EXPECT_GT(BlockRegistry::getPickaxeSpeedMultiplier(BlockId::StonePickaxe, BlockId::Stone), 3.5f);
    EXPECT_GT(BlockRegistry::getPickaxeSpeedMultiplier(BlockId::IronPickaxe, BlockId::Stone), 5.5f);
    EXPECT_GT(BlockRegistry::getPickaxeSpeedMultiplier(BlockId::DiamondPickaxe, BlockId::Stone), 8.0f);

    // Pickaxe on dirt or wood does not give stone tool multiplier
    EXPECT_FLOAT_EQ(BlockRegistry::getPickaxeSpeedMultiplier(BlockId::DiamondPickaxe, BlockId::Dirt), 1.0f);
    EXPECT_FLOAT_EQ(BlockRegistry::getPickaxeSpeedMultiplier(BlockId::DiamondPickaxe, BlockId::OakLog), 1.0f);
}

TEST(BlockRegistryTest, BlockDrops) {
    EXPECT_EQ(BlockRegistry::get(BlockId::Stone).dropId, BlockId::Cobblestone);
    EXPECT_EQ(BlockRegistry::get(BlockId::Grass).dropId, BlockId::Dirt);
    EXPECT_EQ(BlockRegistry::get(BlockId::CoalOre).dropId, BlockId::CoalItem);
    EXPECT_EQ(BlockRegistry::get(BlockId::DeadBush).dropId, BlockId::Stick);
    EXPECT_EQ(BlockRegistry::get(BlockId::Leaves).dropId, BlockId::Sapling);
    EXPECT_EQ(BlockRegistry::get(BlockId::Bedrock).dropId, BlockId::Air);
}
