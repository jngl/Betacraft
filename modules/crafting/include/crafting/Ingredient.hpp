#pragma once

#include "crafting/CraftingGrid.hpp"
#include "crafting/ItemIds.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

namespace betacraft::crafting {

// Predicate over one grid slot. A small fixed set of alternatives covers Beta's
// "any log"/"any plank" style ingredients without heap allocation, so recipes
// stay trivially copyable and matching never allocates.
struct Ingredient {
    static constexpr size_t kMaxAlternatives = 4;

    std::array<ItemId, kMaxAlternatives> accepted{};
    uint8_t acceptedCount{0};
    // Item left in the slot once the ingredient is used (milk bucket -> bucket).
    ItemId remainder{kAir};
    // > 0: the item is a tool that loses this much durability instead of being
    // consumed; it breaks once damage exceeds maxDurability() (Beta semantics).
    uint16_t damagePerCraft{0};

    [[nodiscard]] static constexpr Ingredient of(ItemId id) noexcept {
        return anyOf({id});
    }

    // Invalid (acceptedCount == 0) when the list is empty, contains Air or exceeds
    // capacity: silently dropping an alternative would hide recipe typos.
    [[nodiscard]] static constexpr Ingredient anyOf(std::initializer_list<ItemId> ids) noexcept {
        Ingredient ing{};
        if (ids.size() == 0 || ids.size() > kMaxAlternatives) {
            return ing;
        }
        for (const ItemId id : ids) {
            if (id == kAir) {
                return Ingredient{};
            }
            ing.accepted[ing.acceptedCount++] = canonicalize(id);
        }
        return ing;
    }

    [[nodiscard]] constexpr Ingredient withRemainder(ItemId item) const noexcept {
        Ingredient copy = *this;
        copy.remainder = item;
        return copy;
    }

    [[nodiscard]] constexpr Ingredient damaging(uint16_t amount) const noexcept {
        Ingredient copy = *this;
        copy.damagePerCraft = amount;
        return copy;
    }

    [[nodiscard]] constexpr bool valid() const noexcept {
        return acceptedCount > 0 && acceptedCount <= kMaxAlternatives;
    }

    [[nodiscard]] constexpr bool matches(ItemId id) const noexcept {
        if (id == kAir) {
            return false;
        }
        if (!valid()) {
            return false;
        }
        // Both sides are canonicalised: the fields are public, so an
        // ingredient may have been filled with vanilla numbers without anyOf().
        const ItemId c = canonicalize(id);
        for (size_t i = 0; i < acceptedCount; ++i) {
            if (canonicalize(accepted[i]) == c) {
                return true;
            }
        }
        return false;
    }

    [[nodiscard]] constexpr bool matches(const SlotStack& stack) const noexcept {
        return !stack.empty() && matches(stack.id);
    }
};

} // namespace betacraft::crafting
