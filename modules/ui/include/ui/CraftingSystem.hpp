#pragma once

#include "gameplay/ItemIds.hpp"
#include "physics/Player.hpp"
#include "voxel/Block.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace betacraft::ui {

struct RecipeIngredient {
    voxel::BlockId id{voxel::BlockId::Air};
    int32_t count{1};
};

struct CraftingRecipe {
    std::string_view label;
    voxel::BlockId resultId{voxel::BlockId::Air};
    int32_t resultCount{1};
    std::vector<RecipeIngredient> ingredients;
};

// Legacy (Phase 1) recipe list. The game uses crafting::RecipeRegistry with 2x2 / 3x3 grids
// (game::InventoryCrafting); this stays for the Phase 1 tests and as the reference the
// recipe book preserves (crafting::BetaRecipeOptions defaults).
class CraftingSystem {
public:
    [[nodiscard]] static std::span<const CraftingRecipe> getRecipes() noexcept;
    // Every creative palette entry, in category order (ui/CreativePalette.hpp allPaletteEntries).
    [[nodiscard]] static std::span<const gameplay::ItemId> getCreativePalette() noexcept;

    [[nodiscard]] static bool canCraft(const CraftingRecipe& recipe, const physics::Player& player) noexcept;
    static bool craft(const CraftingRecipe& recipe, physics::Player& player) noexcept;
};

} // namespace betacraft::ui
