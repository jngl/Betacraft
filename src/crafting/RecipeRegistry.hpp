#pragma once

#include "crafting/CraftingGrid.hpp"
#include "crafting/RecipeTypes.hpp"
#include "crafting/ShapedRecipe.hpp"
#include "crafting/ShapelessRecipe.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace betacraft::crafting {

enum class RecipeKind : uint8_t { Shaped, Shapeless };

struct CraftMatch {
    size_t recipeIndex{0};
    RecipeKind kind{RecipeKind::Shaped};
    RecipeResult result{};
    bool mirrored{false};
};

struct CraftOutcome {
    static constexpr size_t kMaxByproducts = CraftingGrid::kMaxDim * CraftingGrid::kMaxDim;

    size_t recipeIndex{0};
    RecipeResult result{};
    std::array<SlotStack, kMaxByproducts> byproducts{};
    size_t byproductCount{0};
};

// Ordered recipe book. Priority is registration order, across shaped and
// shapeless recipes alike, so conflicts resolve identically on every run.
class RecipeRegistry {
public:
    RecipeError addShaped(std::span<const std::string_view> rows, std::span<const PatternKey> keys,
                          RecipeResult result, bool mirrorable = true, std::string_view label = {});
    RecipeError addShaped(std::initializer_list<std::string_view> rows, std::initializer_list<PatternKey> keys,
                          RecipeResult result, bool mirrorable = true, std::string_view label = {});
    RecipeError addShapeless(std::span<const Ingredient> ingredients, RecipeResult result,
                             std::string_view label = {});
    RecipeError addShapeless(std::initializer_list<Ingredient> ingredients, RecipeResult result,
                             std::string_view label = {});

    [[nodiscard]] size_t size() const noexcept { return m_entries.size(); }
    [[nodiscard]] std::string_view label(size_t index) const noexcept;
    [[nodiscard]] RecipeKind kind(size_t index) const noexcept;
    [[nodiscard]] RecipeResult result(size_t index) const noexcept;
    [[nodiscard]] const ShapedRecipe* shaped(size_t index) const noexcept;
    [[nodiscard]] const ShapelessRecipe* shapeless(size_t index) const noexcept;

    // Highest-priority (lowest registration index) recipe matching the grid.
    [[nodiscard]] std::optional<CraftMatch> match(const CraftingGrid& grid) const noexcept;

    // Number of recipes the hashed index forwards to full matching for this
    // grid; exposed so tests/profiling can verify the fast path stays narrow.
    [[nodiscard]] size_t candidateCount(const CraftingGrid& grid) const noexcept;

    // Matches, then uses one unit of every occupied slot (applying remainders
    // and durability use). Returns nullopt and leaves the grid untouched if no
    // recipe matches. Byproducts are remainders that could not stay in the grid.
    std::optional<CraftOutcome> craft(CraftingGrid& grid) const noexcept;

private:
    struct Entry {
        RecipeKind kind{RecipeKind::Shaped};
        uint32_t slot{0}; // index into m_shaped or m_shapeless
        std::string label;
    };

    struct Found {
        size_t index{0};
        ShapedMatch shaped{};
        ShapelessMatch shapeless{};
    };

    // Shape key = trimmed width/height + occupancy bitmask. Two layouts can only
    // match the same shaped recipe if their keys are equal, so a single hash
    // lookup yields the complete candidate list.
    [[nodiscard]] static constexpr uint32_t shapeKey(int width, int height, uint32_t mask) noexcept {
        return mask | (static_cast<uint32_t>(width) << 9) | (static_cast<uint32_t>(height) << 11);
    }

    struct Candidates {
        const std::vector<uint32_t>* shaped{nullptr};
        const std::vector<uint32_t>* shapeless{nullptr};
    };

    [[nodiscard]] Candidates candidatesFor(const CraftingGrid& grid) const noexcept;
    [[nodiscard]] std::optional<Found> find(const CraftingGrid& grid) const noexcept;

    std::vector<Entry> m_entries;
    std::vector<ShapedRecipe> m_shaped;
    std::vector<ShapelessRecipe> m_shapeless;
    std::unordered_map<uint32_t, std::vector<uint32_t>> m_shapedIndex;
    std::array<std::vector<uint32_t>, ShapelessRecipe::kMaxIngredients + 1> m_shapelessIndex{};
};

} // namespace betacraft::crafting
