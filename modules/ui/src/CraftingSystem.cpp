#include "ui/CraftingSystem.hpp"
#include "ui/CreativePalette.hpp"

#include <array>

namespace betacraft::ui {

std::span<const CraftingRecipe> CraftingSystem::getRecipes() noexcept {
    static const std::vector<CraftingRecipe> s_recipes = {
        {"4 Planks (Oak Log)", voxel::BlockId::OakPlanks, 4, {{voxel::BlockId::OakLog, 1}}},
        {"4 Planks (Birch)",   voxel::BlockId::OakPlanks, 4, {{voxel::BlockId::BirchLog, 1}}},
        {"4 Planks (Pine)",    voxel::BlockId::OakPlanks, 4, {{voxel::BlockId::PineLog, 1}}},
        {"4 Sticks",           voxel::BlockId::Stick, 4, {{voxel::BlockId::OakPlanks, 2}}},
        {"Workbench",          voxel::BlockId::CraftingTable, 1, {{voxel::BlockId::OakPlanks, 4}}},
        {"4 Torches",          voxel::BlockId::Torch, 4, {{voxel::BlockId::CoalItem, 1}, {voxel::BlockId::Stick, 1}}},
        {"Furnace",            voxel::BlockId::Furnace, 1, {{voxel::BlockId::Cobblestone, 8}}},
        {"6 Stone Slabs",      voxel::BlockId::CobbleSlab, 6, {{voxel::BlockId::Cobblestone, 3}}},
        {"Sandstone",          voxel::BlockId::Sandstone, 1, {{voxel::BlockId::Sand, 4}}},
        {"Bookshelf",          voxel::BlockId::Bookshelf, 1, {{voxel::BlockId::OakPlanks, 6}, {voxel::BlockId::SugarCane, 3}}},
        {"Beta 1.7 Piston",    voxel::BlockId::Piston, 1, {{voxel::BlockId::OakPlanks, 3}, {voxel::BlockId::Cobblestone, 4}, {voxel::BlockId::IronOre, 1}, {voxel::BlockId::RedstoneOre, 1}}},
        {"Wooden Pickaxe",     voxel::BlockId::WoodPickaxe, 1, {{voxel::BlockId::OakPlanks, 3}, {voxel::BlockId::Stick, 2}}},
        {"Stone Pickaxe",      voxel::BlockId::StonePickaxe, 1, {{voxel::BlockId::Cobblestone, 3}, {voxel::BlockId::Stick, 2}}},
        {"Iron Pickaxe",       voxel::BlockId::IronPickaxe, 1, {{voxel::BlockId::IronOre, 3}, {voxel::BlockId::Stick, 2}}},
        {"Diamond Pickaxe",    voxel::BlockId::DiamondPickaxe, 1, {{voxel::BlockId::DiamondOre, 3}, {voxel::BlockId::Stick, 2}}}
    };
    return s_recipes;
}

std::span<const voxel::BlockId> CraftingSystem::getCreativePalette() noexcept {
    return creativePalette();
}

bool CraftingSystem::canCraft(const CraftingRecipe& recipe, const physics::Player& player) noexcept {
    for (const auto& ing : recipe.ingredients) {
        if (player.countItem(ing.id) < ing.count) {
            return false;
        }
    }
    return true;
}

bool CraftingSystem::craft(const CraftingRecipe& recipe, physics::Player& player) noexcept {
    if (!canCraft(recipe, player)) {
        return false;
    }
    for (const auto& ing : recipe.ingredients) {
        (void)player.consumeItem(ing.id, ing.count);
    }
    (void)player.addItem(recipe.resultId, recipe.resultCount);
    return true;
}

} // namespace betacraft::ui
