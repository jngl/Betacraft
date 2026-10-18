#pragma once

#include "crafting/ItemIds.hpp"

#include <cstdint>

namespace betacraft::crafting {

struct RecipeResult {
    ItemId id{kAir};
    uint8_t count{0};

    constexpr bool operator==(const RecipeResult&) const noexcept = default;
};

// Why a recipe definition was rejected (registration never throws).
enum class RecipeError : uint8_t {
    None = 0,
    EmptyPattern,
    TooLarge,
    RaggedRows,
    UnknownKey,
    DuplicateKey,
    InvalidIngredient,
    InvalidResult,
    TooManyIngredients
};

} // namespace betacraft::crafting
