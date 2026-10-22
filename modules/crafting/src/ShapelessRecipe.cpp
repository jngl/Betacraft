#include "crafting/ShapelessRecipe.hpp"

namespace betacraft::crafting {

namespace {

constexpr size_t kSlots = ShapelessRecipe::kMaxIngredients;

struct Assignment {
    std::array<uint8_t, kSlots> slotIndex{};  // occupied grid slots, compacted
    std::array<SlotStack, kSlots> stacks{};
    size_t occupied{0};
    std::array<int8_t, kSlots> slotToIngredient{};
    uint32_t usedIngredients{0};
};

// Depth-first augmenting search. With at most 9 slots and 9 ingredients the
// worst case is tiny, and it is required for overlapping alternatives where a
// greedy first-fit would reject a valid layout.
[[nodiscard]] bool assign(const ShapelessRecipe& recipe, Assignment& a, size_t k) noexcept {
    if (k == a.occupied) {
        return true;
    }
    for (size_t i = 0; i < recipe.ingredientCount(); ++i) {
        const uint32_t bit = 1u << i;
        if ((a.usedIngredients & bit) != 0 || !recipe.ingredient(i).matches(a.stacks[k])) {
            continue;
        }
        a.usedIngredients |= bit;
        a.slotToIngredient[a.slotIndex[k]] = static_cast<int8_t>(i);
        if (assign(recipe, a, k + 1)) {
            return true;
        }
        a.usedIngredients &= ~bit;
        a.slotToIngredient[a.slotIndex[k]] = -1;
    }
    return false;
}

} // namespace

ShapelessParse ShapelessRecipe::create(std::span<const Ingredient> ingredients, RecipeResult result) {
    if (ingredients.empty()) {
        return {std::nullopt, RecipeError::EmptyPattern};
    }
    if (ingredients.size() > kMaxIngredients) {
        return {std::nullopt, RecipeError::TooManyIngredients};
    }
    ShapelessRecipe r;
    for (const Ingredient& ing : ingredients) {
        if (!ing.valid()) {
            return {std::nullopt, RecipeError::InvalidIngredient};
        }
        r.m_ingredients[r.m_count++] = ing;
    }
    if (result.id == kAir || result.count == 0 || result.count > maxStackSize(result.id)) {
        return {std::nullopt, RecipeError::InvalidResult};
    }
    r.m_result = result;
    return {r, RecipeError::None};
}

ShapelessParse ShapelessRecipe::create(std::initializer_list<Ingredient> ingredients, RecipeResult result) {
    return create(std::span<const Ingredient>{ingredients.begin(), ingredients.size()}, result);
}

std::optional<ShapelessMatch> ShapelessRecipe::match(const CraftingGrid& grid) const noexcept {
    Assignment a;
    a.slotToIngredient.fill(-1);
    for (size_t i = 0; i < grid.slotCount(); ++i) {
        const SlotStack s = grid.at(i);
        if (s.empty()) {
            continue;
        }
        if (a.occupied == m_count) {
            return std::nullopt; // more occupied slots than ingredients
        }
        a.slotIndex[a.occupied] = static_cast<uint8_t>(i);
        a.stacks[a.occupied] = s;
        ++a.occupied;
    }
    if (a.occupied != m_count || !assign(*this, a, 0)) {
        return std::nullopt;
    }
    return ShapelessMatch{a.slotToIngredient};
}

} // namespace betacraft::crafting
