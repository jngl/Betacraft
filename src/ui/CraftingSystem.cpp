#include "ui/CraftingSystem.hpp"
#include "ui/CreativePalette.hpp"

#include <array>

namespace betacraft::ui {

std::span<const CraftingRecipe> CraftingSystem::getRecipes() noexcept {
    static const std::vector<CraftingRecipe> s_recipes = {
        {"4 Planks (Oak Log)", world::BlockId::OakPlanks, 4, {{world::BlockId::OakLog, 1}}},
        {"4 Planks (Birch)",   world::BlockId::OakPlanks, 4, {{world::BlockId::BirchLog, 1}}},
        {"4 Planks (Pine)",    world::BlockId::OakPlanks, 4, {{world::BlockId::PineLog, 1}}},
        {"4 Sticks",           world::BlockId::Stick, 4, {{world::BlockId::OakPlanks, 2}}},
        {"Workbench",          world::BlockId::CraftingTable, 1, {{world::BlockId::OakPlanks, 4}}},
        {"4 Torches",          world::BlockId::Torch, 4, {{world::BlockId::CoalItem, 1}, {world::BlockId::Stick, 1}}},
        {"Furnace",            world::BlockId::Furnace, 1, {{world::BlockId::Cobblestone, 8}}},
        {"6 Stone Slabs",      world::BlockId::CobbleSlab, 6, {{world::BlockId::Cobblestone, 3}}},
        {"Sandstone",          world::BlockId::Sandstone, 1, {{world::BlockId::Sand, 4}}},
        {"Bookshelf",          world::BlockId::Bookshelf, 1, {{world::BlockId::OakPlanks, 6}, {world::BlockId::SugarCane, 3}}},
        {"Beta 1.7 Piston",    world::BlockId::Piston, 1, {{world::BlockId::OakPlanks, 3}, {world::BlockId::Cobblestone, 4}, {world::BlockId::IronOre, 1}, {world::BlockId::RedstoneOre, 1}}},
        {"Wooden Pickaxe",     world::BlockId::WoodPickaxe, 1, {{world::BlockId::OakPlanks, 3}, {world::BlockId::Stick, 2}}},
        {"Stone Pickaxe",      world::BlockId::StonePickaxe, 1, {{world::BlockId::Cobblestone, 3}, {world::BlockId::Stick, 2}}},
        {"Iron Pickaxe",       world::BlockId::IronPickaxe, 1, {{world::BlockId::IronOre, 3}, {world::BlockId::Stick, 2}}},
        {"Diamond Pickaxe",    world::BlockId::DiamondPickaxe, 1, {{world::BlockId::DiamondOre, 3}, {world::BlockId::Stick, 2}}}
    };
    return s_recipes;
}

std::span<const world::BlockId> CraftingSystem::getCreativePalette() noexcept {
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
