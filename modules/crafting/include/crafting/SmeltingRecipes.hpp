#pragma once

#include "crafting/CraftingGrid.hpp"
#include "crafting/ItemIds.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace betacraft::crafting {

// Beta 1.7.3 TileEntityFurnace: one item cooks in 200 ticks (10 s at 20 TPS).
inline constexpr int32_t kSmeltTicks = 200;

struct SmeltingRecipe {
    ItemId input{kAir};
    SlotStack output{};
};

// Beta FurnaceRecipes, restricted to the items this engine has: ores, sand, cobblestone, logs
// (charcoal is folded onto coal) and the raw porkchop.
class SmeltingBook {
public:
    [[nodiscard]] static const SmeltingBook& beta();

    // The stack one smelt of `input` yields (vanilla aliases are canonicalised first);
    // nullopt when the item does not smelt.
    [[nodiscard]] std::optional<SlotStack> result(ItemId input) const noexcept;
    [[nodiscard]] const std::vector<SmeltingRecipe>& recipes() const noexcept { return m_recipes; }

private:
    SmeltingBook();

    std::vector<SmeltingRecipe> m_recipes;
};

// Beta TileEntityFurnace.getItemBurnTime: how many ticks one item of `id` burns; 0 = not a fuel.
[[nodiscard]] int32_t fuelBurnTicks(ItemId id) noexcept;

} // namespace betacraft::crafting
