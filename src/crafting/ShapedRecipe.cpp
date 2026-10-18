#include "crafting/ShapedRecipe.hpp"

#include <algorithm>
#include <bit>

namespace betacraft::crafting {

namespace {

[[nodiscard]] bool validResult(RecipeResult r) noexcept {
    return r.id != kAir && r.count > 0 && r.count <= maxStackSize(r.id);
}

} // namespace

ShapedParse ShapedRecipe::parse(std::span<const std::string_view> rows, std::span<const PatternKey> keys,
                                RecipeResult result, bool mirrorable) {
    if (rows.empty()) {
        return {std::nullopt, RecipeError::EmptyPattern};
    }
    if (rows.size() > static_cast<size_t>(kMaxDim)) {
        return {std::nullopt, RecipeError::TooLarge};
    }
    const size_t rawWidth = rows.front().size();
    for (const std::string_view row : rows) {
        if (row.size() > static_cast<size_t>(kMaxDim)) {
            return {std::nullopt, RecipeError::TooLarge};
        }
        if (row.size() != rawWidth) {
            return {std::nullopt, RecipeError::RaggedRows};
        }
    }
    for (size_t i = 0; i < keys.size(); ++i) {
        if (keys[i].symbol == ' ') {
            return {std::nullopt, RecipeError::DuplicateKey}; // ' ' is reserved for holes
        }
        for (size_t j = 0; j < i; ++j) {
            if (keys[j].symbol == keys[i].symbol) {
                return {std::nullopt, RecipeError::DuplicateKey};
            }
        }
        if (!keys[i].ingredient.valid()) {
            return {std::nullopt, RecipeError::InvalidIngredient};
        }
    }

    const auto lookup = [&](char c) -> const Ingredient* {
        for (const PatternKey& k : keys) {
            if (k.symbol == c) {
                return &k.ingredient;
            }
        }
        return nullptr;
    };

    // Bounding box of the non-hole symbols: patterns are stored trimmed so that
    // "XX " and " XX" describe the same recipe.
    int minX = kMaxDim, minY = kMaxDim, maxX = -1, maxY = -1;
    for (size_t y = 0; y < rows.size(); ++y) {
        for (size_t x = 0; x < rawWidth; ++x) {
            const char c = rows[y][x];
            if (c == ' ') {
                continue;
            }
            if (lookup(c) == nullptr) {
                return {std::nullopt, RecipeError::UnknownKey};
            }
            minX = std::min(minX, static_cast<int>(x));
            minY = std::min(minY, static_cast<int>(y));
            maxX = std::max(maxX, static_cast<int>(x));
            maxY = std::max(maxY, static_cast<int>(y));
        }
    }
    if (maxX < 0) {
        return {std::nullopt, RecipeError::EmptyPattern};
    }
    if (!validResult(result)) {
        return {std::nullopt, RecipeError::InvalidResult};
    }

    ShapedRecipe r;
    r.m_width = maxX - minX + 1;
    r.m_height = maxY - minY + 1;
    r.m_result = result;
    r.m_mirrorable = mirrorable;
    for (int y = 0; y < r.m_height; ++y) {
        for (int x = 0; x < r.m_width; ++x) {
            const char c = rows[static_cast<size_t>(minY + y)][static_cast<size_t>(minX + x)];
            if (c != ' ') {
                r.m_cells[static_cast<size_t>(y * r.m_width + x)] = *lookup(c);
            }
        }
    }
    return {r, RecipeError::None};
}

ShapedParse ShapedRecipe::parse(std::initializer_list<std::string_view> rows,
                                std::initializer_list<PatternKey> keys, RecipeResult result,
                                bool mirrorable) {
    return parse(std::span<const std::string_view>{rows.begin(), rows.size()},
                 std::span<const PatternKey>{keys.begin(), keys.size()}, result, mirrorable);
}

size_t ShapedRecipe::ingredientCount() const noexcept {
    return static_cast<size_t>(std::popcount(occupancyMask()));
}

const Ingredient* ShapedRecipe::cell(int x, int y) const noexcept {
    if (x < 0 || y < 0 || x >= m_width || y >= m_height) {
        return nullptr;
    }
    const Ingredient& ing = m_cells[static_cast<size_t>(y * m_width + x)];
    return ing.valid() ? &ing : nullptr;
}

uint32_t ShapedRecipe::occupancyMask() const noexcept {
    uint32_t mask = 0;
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            if (cell(x, y) != nullptr) {
                mask |= 1u << (y * m_width + x);
            }
        }
    }
    return mask;
}

uint32_t ShapedRecipe::mirroredOccupancyMask() const noexcept {
    uint32_t mask = 0;
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            if (cell(m_width - 1 - x, y) != nullptr) {
                mask |= 1u << (y * m_width + x);
            }
        }
    }
    return mask;
}

bool ShapedRecipe::matchesAt(const CraftingGrid& grid, int ox, int oy, bool mirrored) const noexcept {
    for (int y = 0; y < m_height; ++y) {
        for (int x = 0; x < m_width; ++x) {
            const Ingredient* ing = cell(mirrored ? m_width - 1 - x : x, y);
            const SlotStack slot = grid.get(ox + x, oy + y);
            if (ing == nullptr ? !slot.empty() : !ing->matches(slot)) {
                return false;
            }
        }
    }
    return true;
}

std::optional<ShapedMatch> ShapedRecipe::match(const CraftingGrid& grid) const noexcept {
    // Anything outside the grid's bounding box is empty by construction, so
    // comparing the two trimmed boxes cell by cell is a complete check.
    const auto box = grid.bounds();
    if (!box || box->width() != m_width || box->height() != m_height) {
        return std::nullopt;
    }
    // Direct orientation first: symmetric recipes then report mirrored == false.
    if (matchesAt(grid, box->minX, box->minY, false)) {
        return ShapedMatch{box->minX, box->minY, false};
    }
    if (m_mirrorable && matchesAt(grid, box->minX, box->minY, true)) {
        return ShapedMatch{box->minX, box->minY, true};
    }
    return std::nullopt;
}

} // namespace betacraft::crafting
