#pragma once

#include "crafting/RecipeRegistry.hpp"

#include <cstddef>

namespace betacraft::crafting {

// Defaults preserve every legacy ui::CraftingSystem recipe so existing worlds
// and inventories keep working; vanilla() is the strict Beta 1.7.3 book.
struct BetaRecipeOptions {
    // Also register raw-ore variants (iron/gold/diamond/redstone ore where Beta
    // wants ingot/gem/dust), as the legacy CraftingSystem did before smelting
    // existed. Each variant is a separate recipe, so one craft never mixes
    // ingots and ores.
    bool acceptLegacyOres = true;
    // 6 slabs per 3 cobblestone (legacy) instead of vanilla Beta's 3.
    bool legacySlabYield = true;
    // Non-vanilla bookshelf from 6 planks + 3 sugar cane (legacy shortcut).
    bool legacySugarCaneBookshelf = true;

    [[nodiscard]] static constexpr BetaRecipeOptions vanilla() noexcept {
        return BetaRecipeOptions{false, false, false};
    }
};

struct BookStats {
    size_t added{0};
    size_t failed{0};
};

// Appends the standard book in a fixed order (that order is the match
// priority): basic blocks, 25 tools/weapons (each followed by its raw-ore
// variant when enabled), containers.
BookStats registerBetaRecipes(RecipeRegistry& registry, const BetaRecipeOptions& options = {});
[[nodiscard]] RecipeRegistry makeBetaRecipeBook(const BetaRecipeOptions& options = {});

} // namespace betacraft::crafting
