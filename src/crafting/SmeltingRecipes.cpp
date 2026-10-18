#include "crafting/SmeltingRecipes.hpp"

namespace betacraft::crafting {

namespace {

using world::BlockId;

[[nodiscard]] constexpr SlotStack one(ItemId id) noexcept {
    return SlotStack{id, 1, 0};
}

} // namespace

SmeltingBook::SmeltingBook()
    : m_recipes{
          {toItemId(BlockId::IronOre), one(items::IronIngot)},
          {toItemId(BlockId::GoldOre), one(items::GoldIngot)},
          {toItemId(BlockId::DiamondOre), one(items::Diamond)},
          {toItemId(BlockId::Sand), one(toItemId(BlockId::Glass))},
          {toItemId(BlockId::Cobblestone), one(toItemId(BlockId::Stone))},
          // Beta gives charcoal (coal damage 1); the engine has a single coal item.
          {toItemId(BlockId::OakLog), one(items::Coal)},
          {toItemId(BlockId::BirchLog), one(items::Coal)},
          {toItemId(BlockId::PineLog), one(items::Coal)},
          {items::RawPorkchop, one(items::CookedPorkchop)},
      } {}

const SmeltingBook& SmeltingBook::beta() {
    static const SmeltingBook book;
    return book;
}

std::optional<SlotStack> SmeltingBook::result(ItemId input) const noexcept {
    const ItemId id = canonicalize(input);
    if (id == kAir) {
        return std::nullopt;
    }
    for (const SmeltingRecipe& r : m_recipes) {
        if (r.input == id) {
            return r.output;
        }
    }
    return std::nullopt;
}

int32_t fuelBurnTicks(ItemId rawId) noexcept {
    const ItemId id = canonicalize(rawId);
    if (id == items::Coal) {
        return 1600;
    }
    if (id == items::Stick || id == toItemId(BlockId::Sapling)) {
        return 100;
    }
    if (id == items::LavaBucket) {
        return 20000;
    }
    // Wooden blocks (Beta: any block of Material.wood).
    if (id == toItemId(BlockId::OakPlanks) || id == toItemId(BlockId::OakLog) || id == toItemId(BlockId::BirchLog) ||
        id == toItemId(BlockId::PineLog) || id == toItemId(BlockId::CraftingTable) ||
        id == toItemId(BlockId::Bookshelf) || id == items::Chest) {
        return 300;
    }
    return 0;
}

} // namespace betacraft::crafting
