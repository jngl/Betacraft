#include "physics/Player.hpp"
#include "physics/Raycaster.hpp"
#include "tests/test_helpers.hpp"
#include "ui/CraftingSystem.hpp"
#include "world/Block.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

using namespace betacraft;
using namespace testing;

TEST(MiningCraftingIntegrationTest, CompleteWoodToPickaxeCycle) {
    world::BlockRegistry::initialize();
    test::TempDir tempDir;
    world::World world(173ULL, tempDir.path());
    world.ensureChunkLoaded({0, 0});

    // Clear test area in sky to avoid pre-generated terrain
    for (int z = 0; z < 16; ++z) {
        for (int x = 0; x < 16; ++x) {
            for (int y = 100; y <= 110; ++y) {
                world.setBlock(x, y, z, world::BlockId::Air);
            }
        }
    }

    // Place an OakLog block in the world at Y=105
    const core::BlockPos treePos{5, 105, 5};
    world.setBlock(treePos, world::BlockId::OakLog);

    physics::Player player;
    // Clear inventory
    for (size_t i = 0; i < physics::Player::TOTAL_INVENTORY_SIZE; ++i) {
        player.setInventorySlot(i, {});
    }

    // 1. Raycast to the tree log
    const auto hit = physics::Raycaster::cast(world, {5.5f, 105.5f, 3.0f}, {0.0f, 0.0f, 1.0f}, 5.0f);
    ASSERT_TRUE(hit.hit);
    EXPECT_EQ(hit.blockPos, treePos);
    EXPECT_EQ(hit.blockId, world::BlockId::OakLog);

    // 2. Mine the block: Beta 1.7.3 log hardness = 2.0f, tool multiplier = 1.0f
    const auto& logDef = world::BlockRegistry::get(hit.blockId);
    EXPECT_FLOAT_EQ(logDef.hardness, 2.0f);

    // Simulate mining completion: remove block, add drop
    EXPECT_TRUE(world.setBlock(treePos, world::BlockId::Air));
    EXPECT_TRUE(player.addItem(logDef.dropId, 1));
    EXPECT_EQ(player.countItem(world::BlockId::OakLog), 1);

    // 3. Craft Planks (1 Log -> 4 Planks)
    const auto recipes = ui::CraftingSystem::getRecipes();
    // recipe 0: 4 Planks
    ASSERT_TRUE(ui::CraftingSystem::canCraft(recipes[0], player));
    EXPECT_TRUE(ui::CraftingSystem::craft(recipes[0], player));
    EXPECT_EQ(player.countItem(world::BlockId::OakLog), 0);
    EXPECT_EQ(player.countItem(world::BlockId::OakPlanks), 4);

    // 4. Craft Sticks (2 Planks -> 4 Sticks, recipe 3)
    const auto stickRecipeIt = std::find_if(recipes.begin(), recipes.end(), [](const ui::CraftingRecipe& r) {
        return r.resultId == world::BlockId::Stick;
    });
    ASSERT_NE(stickRecipeIt, recipes.end());
    ASSERT_TRUE(ui::CraftingSystem::canCraft(*stickRecipeIt, player));
    EXPECT_TRUE(ui::CraftingSystem::craft(*stickRecipeIt, player));
    EXPECT_EQ(player.countItem(world::BlockId::OakPlanks), 2);
    EXPECT_EQ(player.countItem(world::BlockId::Stick), 4);

    // Add 1 more plank so we have 3 planks + 2 sticks for a Wooden Pickaxe
    player.addItem(world::BlockId::OakPlanks, 1);
    EXPECT_EQ(player.countItem(world::BlockId::OakPlanks), 3);

    // 5. Craft Wooden Pickaxe (3 Planks + 2 Sticks)
    const auto woodPickRecipeIt = std::find_if(recipes.begin(), recipes.end(), [](const ui::CraftingRecipe& r) {
        return r.resultId == world::BlockId::WoodPickaxe;
    });
    ASSERT_NE(woodPickRecipeIt, recipes.end());
    ASSERT_TRUE(ui::CraftingSystem::canCraft(*woodPickRecipeIt, player));
    EXPECT_TRUE(ui::CraftingSystem::craft(*woodPickRecipeIt, player));
    EXPECT_EQ(player.countItem(world::BlockId::WoodPickaxe), 1);

    // 6. Verify mining speed with newly crafted pickaxe
    const float speedWithHands = world::BlockRegistry::getPickaxeSpeedMultiplier(world::BlockId::Air, world::BlockId::Stone);
    const float speedWithPickaxe = world::BlockRegistry::getPickaxeSpeedMultiplier(world::BlockId::WoodPickaxe, world::BlockId::Stone);
    EXPECT_FLOAT_EQ(speedWithHands, 1.0f);
    EXPECT_FLOAT_EQ(speedWithPickaxe, 2.5f);
    EXPECT_GT(speedWithPickaxe, speedWithHands);
}
