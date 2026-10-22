#pragma once

#include "crafting/CraftingGrid.hpp"
#include "crafting/Ingredient.hpp"
#include "crafting/RecipeTypes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>

namespace betacraft::crafting {

struct ShapelessMatch {
    // Grid slot (row-major) -> ingredient index, -1 for empty slots.
    std::array<int8_t, CraftingGrid::kMaxDim * CraftingGrid::kMaxDim> slotToIngredient{};
};

struct ShapelessParse;

// Order-independent recipe: the occupied grid slots must form a perfect
// one-to-one assignment onto the ingredient multiset.
class ShapelessRecipe {
public:
    static constexpr size_t kMaxIngredients = CraftingGrid::kMaxDim * CraftingGrid::kMaxDim;

    ShapelessRecipe() = default;

    [[nodiscard]] static ShapelessParse create(std::span<const Ingredient> ingredients, RecipeResult result);
    [[nodiscard]] static ShapelessParse create(std::initializer_list<Ingredient> ingredients, RecipeResult result);

    [[nodiscard]] size_t ingredientCount() const noexcept { return m_count; }
    [[nodiscard]] const Ingredient& ingredient(size_t i) const noexcept { return m_ingredients[i]; }
    [[nodiscard]] RecipeResult result() const noexcept { return m_result; }
    [[nodiscard]] bool fitsIn(GridSize size) const noexcept {
        const size_t dim = static_cast<size_t>(size);
        return m_count <= dim * dim;
    }

    [[nodiscard]] std::optional<ShapelessMatch> match(const CraftingGrid& grid) const noexcept;

private:
    std::array<Ingredient, kMaxIngredients> m_ingredients{};
    uint8_t m_count{0};
    RecipeResult m_result{};
};

struct ShapelessParse {
    std::optional<ShapelessRecipe> recipe;
    RecipeError error{RecipeError::None};
};

} // namespace betacraft::crafting
