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
#include <string_view>

namespace betacraft::crafting {

struct PatternKey {
    char symbol{' '};
    Ingredient ingredient{};
};

struct ShapedMatch {
    int originX{0};
    int originY{0};
    bool mirrored{false};
};

struct ShapedParse;

// Shaped recipe stored as its trimmed sub-matrix (Beta ShapedRecipes semantics):
// a grid matches when its own trimmed bounding box has the same dimensions and
// every cell agrees, optionally after a horizontal flip.
class ShapedRecipe {
public:
    static constexpr int kMaxDim = CraftingGrid::kMaxDim;

    ShapedRecipe() = default;

    [[nodiscard]] static ShapedParse parse(std::span<const std::string_view> rows,
                                           std::span<const PatternKey> keys, RecipeResult result,
                                           bool mirrorable = true);
    [[nodiscard]] static ShapedParse parse(std::initializer_list<std::string_view> rows,
                                           std::initializer_list<PatternKey> keys, RecipeResult result,
                                           bool mirrorable = true);

    [[nodiscard]] int width() const noexcept { return m_width; }
    [[nodiscard]] int height() const noexcept { return m_height; }
    [[nodiscard]] RecipeResult result() const noexcept { return m_result; }
    [[nodiscard]] bool mirrorable() const noexcept { return m_mirrorable; }
    [[nodiscard]] size_t ingredientCount() const noexcept;

    // Ingredient at (x, y) of the trimmed pattern, nullptr for holes / out of range.
    [[nodiscard]] const Ingredient* cell(int x, int y) const noexcept;

    // Bit (y * width + x) set for every non-hole cell; together with the
    // dimensions this is the shape key used by the registry's hashed index.
    [[nodiscard]] uint32_t occupancyMask() const noexcept;
    [[nodiscard]] uint32_t mirroredOccupancyMask() const noexcept;

    // Size gating: a pattern wider/taller than the grid can never be placed in it.
    [[nodiscard]] bool fitsIn(GridSize size) const noexcept {
        const int dim = static_cast<int>(size);
        return m_width <= dim && m_height <= dim;
    }

    [[nodiscard]] std::optional<ShapedMatch> match(const CraftingGrid& grid) const noexcept;

private:
    [[nodiscard]] bool matchesAt(const CraftingGrid& grid, int ox, int oy, bool mirrored) const noexcept;

    std::array<Ingredient, kMaxDim * kMaxDim> m_cells{};
    int m_width{0};
    int m_height{0};
    RecipeResult m_result{};
    bool m_mirrorable{true};
};

struct ShapedParse {
    std::optional<ShapedRecipe> recipe;
    RecipeError error{RecipeError::None};
};

} // namespace betacraft::crafting
