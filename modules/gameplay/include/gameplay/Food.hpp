#pragma once

#include "gameplay/ItemIds.hpp"

#include <cstdint>
#include <optional>

namespace betacraft::gameplay {

// Beta 1.7.3 ItemFood: eating is instant and heals `heal` half-hearts, even at full health.
// Mushroom stew (ItemSoup) hands back its bowl.
struct FoodInfo {
    int32_t heal{0};
    ItemId leftover{kNoItem};
};

// nullopt for anything that is not food. Accepts vanilla-alias ids.
[[nodiscard]] std::optional<FoodInfo> foodInfo(ItemId id) noexcept;

} // namespace betacraft::gameplay
