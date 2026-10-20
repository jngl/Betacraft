#include "gameplay/Food.hpp"

namespace betacraft::gameplay {

std::optional<FoodInfo> foodInfo(ItemId id) noexcept {
    switch (canonicalize(id)) {
        case items::Apple:          return FoodInfo{4};
        case items::Bread:          return FoodInfo{5};
        case items::RawPorkchop:    return FoodInfo{3};
        case items::CookedPorkchop: return FoodInfo{8};
        case items::GoldenApple:    return FoodInfo{20};
        case items::MushroomStew:   return FoodInfo{10, items::Bowl};
        default:                    return std::nullopt;
    }
}

} // namespace betacraft::gameplay
