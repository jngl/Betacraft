# crafting

Beta 1.7.3 recipes (P2.6): 2×2 and 3×3 grids, shaped and shapeless matching, the recipe book, and the furnace. Pure data over item ids; it needs only `gameplay` and `voxel`.

## Public entry points

- `crafting/CraftingGrid.hpp`: `CraftingGrid` and its slot view.
- `crafting/ShapedRecipe.hpp`, `crafting/ShapelessRecipe.hpp`, `crafting/Ingredient.hpp`, `crafting/RecipeTypes.hpp`: recipe definitions and matching.
- `crafting/RecipeRegistry.hpp`: the ordered book; the first registered match wins.
- `crafting/BetaRecipes.hpp`: `makeBetaRecipeBook`, the legacy and strict vanilla books.
- `crafting/Furnace.hpp`, `crafting/SmeltingRecipes.hpp`: furnace ticks and smelting results.
- `crafting/ItemIds.hpp`: the `gameplay/ItemIds.hpp` scheme under the crafting names, plus the `BlockId` bridge.

## What it does not do

No inventory screens or slot clicks: those are `game/InventoryCrafting` and `ui`.
