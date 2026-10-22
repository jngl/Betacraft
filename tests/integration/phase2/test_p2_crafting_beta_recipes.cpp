// P2.6 — Standard Beta 1.7.3 recipe book + preservation of every legacy
// ui::CraftingSystem recipe.
#include "crafting/BetaRecipes.hpp"
#include "game/InventoryCrafting.hpp"
#include "ui/CraftingSystem.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <random>
#include <string>
#include <tuple>
#include <vector>

using namespace betacraft::crafting;
using betacraft::voxel::BlockId;

namespace {
constexpr ItemId kPlanks = toItemId(BlockId::OakPlanks);
constexpr ItemId kCobble = toItemId(BlockId::Cobblestone);
constexpr ItemId kStick = items::Stick;

CraftingGrid layout(GridSize size, std::initializer_list<std::string_view> rows,
                    std::initializer_list<std::pair<char, ItemId>> legend, int ox = 0, int oy = 0,
                    uint8_t count = 1) {
    CraftingGrid g{size};
    int y = 0;
    for (const std::string_view row : rows) {
        for (int x = 0; x < static_cast<int>(row.size()); ++x) {
            for (const auto& [c, id] : legend) {
                if (row[static_cast<size_t>(x)] == c) {
                    EXPECT_TRUE(g.set(ox + x, oy + y, SlotStack{id, count, 0}));
                }
            }
        }
        ++y;
    }
    return g;
}

const RecipeRegistry& book() {
    static const RecipeRegistry s_book = makeBetaRecipeBook();
    return s_book;
}

RecipeResult resultOf(const RecipeRegistry& reg, const CraftingGrid& g) {
    const auto m = reg.match(g);
    return m ? m->result : RecipeResult{};
}

// Fills `grid` with the first accepted alternative of every recipe cell.
// Returns false if the recipe cannot be placed at (ox, oy).
bool instantiate(const RecipeRegistry& reg, size_t index, CraftingGrid& grid, int ox, int oy, bool mirrored) {
    if (const ShapedRecipe* r = reg.shaped(index)) {
        if (ox + r->width() > grid.width() || oy + r->height() > grid.height()) {
            return false;
        }
        for (int y = 0; y < r->height(); ++y) {
            for (int x = 0; x < r->width(); ++x) {
                const Ingredient* ing = r->cell(mirrored ? r->width() - 1 - x : x, y);
                if (ing != nullptr) {
                    EXPECT_TRUE(grid.set(ox + x, oy + y, SlotStack{ing->accepted[0], 1, 0}));
                }
            }
        }
        return true;
    }
    const ShapelessRecipe* r = reg.shapeless(index);
    if (r == nullptr || r->ingredientCount() > grid.slotCount()) {
        return false;
    }
    for (size_t i = 0; i < r->ingredientCount(); ++i) {
        // Fill from the last slot backwards: exercises order independence.
        const size_t slot = grid.slotCount() - 1 - i;
        EXPECT_TRUE(grid.set(static_cast<int>(slot) % grid.width(), static_cast<int>(slot) / grid.width(),
                             SlotStack{r->ingredient(i).accepted[0], 1, 0}));
    }
    return true;
}
} // namespace

// ----------------------------------------------------------- book sanity ---

TEST(BetaRecipeBook, RegistersEveryRecipeWithoutError) {
    RecipeRegistry reg;
    const BookStats stats = registerBetaRecipes(reg);
    EXPECT_EQ(stats.failed, 0u);
    EXPECT_EQ(stats.added, reg.size());
    EXPECT_GE(reg.size(), 40u);
    for (size_t i = 0; i < reg.size(); ++i) {
        EXPECT_FALSE(reg.label(i).empty()) << i;
    }
}

namespace {
// Independent statement of the book's priority order (registration order is
// the match priority, so it is part of the contract).
std::vector<std::string> expectedLabels(bool legacy) {
    std::vector<std::string> v{"4 Planks", "4 Sticks", "Workbench", "4 Torches", "Sandstone",
                               "Glowstone", "Snow Block", "Furnace", "Chest", "Stone Slabs",
                               "3 Paper", "Book", "Bookshelf"};
    if (legacy) {
        v.emplace_back("Bookshelf (Sugar Cane)");
    }
    v.emplace_back("Piston");
    if (legacy) {
        v.emplace_back("Piston (Ore)");
    }
    for (const char* m : {"Wooden", "Stone", "Iron", "Gold", "Diamond"}) {
        for (const char* k : {"Pickaxe", "Axe", "Shovel", "Sword", "Hoe"}) {
            v.push_back(std::string{m} + " " + k);
            const bool hasOre = std::string_view{m} != "Wooden" && std::string_view{m} != "Stone";
            if (legacy && hasOre) {
                v.push_back(std::string{m} + " " + k + " (Ore)");
            }
        }
    }
    v.emplace_back("Bucket");
    if (legacy) {
        v.emplace_back("Bucket (Ore)");
    }
    v.emplace_back("Cake");
    for (const char* label : {"2 Ladders", "2 Fences", "4 Wooden Stairs", "4 Cobblestone Stairs", "Wooden Door"}) {
        v.emplace_back(label);
    }
    // S8.5 redstone
    v.emplace_back("Iron Door");
    if (legacy) {
        v.emplace_back("Iron Door (Ore)");
    }
    for (const char* label : {"Lever", "Redstone Torch", "Stone Pressure Plate", "Wooden Pressure Plate", "Button",
                              "Bread", "Bed",
                              // S6.6
                              "Flint and Steel", "TNT",
                              // S6.7 armor (RecipesArmor order; chain armor is not craftable)
                              "Leather Cap", "Leather Tunic", "Leather Pants", "Leather Boots",
                              "Iron Helmet", "Iron Chestplate", "Iron Leggings", "Iron Boots",
                              "Diamond Helmet", "Diamond Chestplate", "Diamond Leggings", "Diamond Boots",
                              "Gold Helmet", "Gold Chestplate", "Gold Leggings", "Gold Boots"}) {
        v.emplace_back(label);
    }
    return v;
}

void expectLabels(const RecipeRegistry& reg, const std::vector<std::string>& expected) {
    ASSERT_EQ(reg.size(), expected.size());
    for (size_t i = 0; i < expected.size(); ++i) {
        EXPECT_EQ(reg.label(i), expected[i]) << "priority slot " << i;
    }
}
} // namespace

TEST(BetaRecipeBook, DefaultPriorityOrderIsPinned) {
    expectLabels(book(), expectedLabels(true));
}

TEST(BetaRecipeBook, VanillaPriorityOrderIsPinned) {
    expectLabels(makeBetaRecipeBook(BetaRecipeOptions::vanilla()), expectedLabels(false));
}

// Self-consistency: no recipe is shadowed by an earlier one. Pattern
// correctness itself is checked by the hard-coded layouts below.
TEST(BetaRecipeBook, EveryRecipeIsReachableAtEveryPlacement) {
    // No recipe is shadowed by an earlier one, in either grid size, at any
    // translation, and mirrored when allowed.
    const RecipeRegistry& reg = book();
    for (size_t i = 0; i < reg.size(); ++i) {
        for (const GridSize size : {GridSize::Inventory2x2, GridSize::Workbench3x3}) {
            const int dim = static_cast<int>(size);
            const ShapedRecipe* shaped = reg.shaped(i);
            const bool canMirror = shaped != nullptr && shaped->mirrorable();
            for (int oy = 0; oy < dim; ++oy) {
                for (int ox = 0; ox < dim; ++ox) {
                    for (const bool mirrored : {false, true}) {
                        if (mirrored && !canMirror) {
                            continue;
                        }
                        if (shaped == nullptr && (ox != 0 || oy != 0 || mirrored)) {
                            continue;
                        }
                        CraftingGrid g{size};
                        if (!instantiate(reg, i, g, ox, oy, mirrored)) {
                            continue;
                        }
                        const auto m = reg.match(g);
                        ASSERT_TRUE(m.has_value()) << reg.label(i) << " @" << ox << "," << oy;
                        EXPECT_EQ(m->recipeIndex, i) << reg.label(i) << " shadowed by " << reg.label(m->recipeIndex);
                    }
                }
            }
        }
    }
}

// ------------------------------------------------- legacy preservation ---

TEST(BetaRecipeBook, PreservesEveryLegacyCraftingSystemRecipe) {
    // For each legacy recipe (unordered ingredient counts) there must be a
    // recipe in the book that uses exactly that multiset and yields the same
    // result id and count once laid out in a workbench grid. CraftingSystem still yields
    // the legacy ids 100..105, which fold onto the Beta numbers since S2.10.
    const RecipeRegistry& reg = book();
    for (const betacraft::ui::CraftingRecipe& legacy : betacraft::ui::CraftingSystem::getRecipes()) {
        bool found = false;
        for (size_t i = 0; i < reg.size() && !found; ++i) {
            if (reg.result(i).id != canonicalize(toItemId(legacy.resultId)) ||
                reg.result(i).count != static_cast<uint8_t>(legacy.resultCount)) {
                continue;
            }
            std::vector<std::pair<ItemId, int>> budget;
            for (const auto& ing : legacy.ingredients) {
                budget.emplace_back(toItemId(ing.id), ing.count);
            }
            CraftingGrid g{GridSize::Workbench3x3};
            std::vector<std::tuple<int, int, const Ingredient*>> slots;
            if (const ShapedRecipe* r = reg.shaped(i)) {
                for (int y = 0; y < r->height(); ++y) {
                    for (int x = 0; x < r->width(); ++x) {
                        if (r->cell(x, y) != nullptr) {
                            slots.emplace_back(x, y, r->cell(x, y));
                        }
                    }
                }
            } else {
                const ShapelessRecipe* sr = reg.shapeless(i);
                for (size_t k = 0; k < sr->ingredientCount(); ++k) {
                    slots.emplace_back(static_cast<int>(k % 3), static_cast<int>(k / 3), &sr->ingredient(k));
                }
            }
            bool ok = true;
            for (const auto& [x, y, ing] : slots) {
                auto it = std::find_if(budget.begin(), budget.end(),
                                       [&](const auto& b) { return b.second > 0 && ing->matches(b.first); });
                if (it == budget.end()) {
                    ok = false;
                    break;
                }
                --it->second;
                EXPECT_TRUE(g.set(x, y, SlotStack{it->first, 1, 0}));
            }
            ok = ok && std::all_of(budget.begin(), budget.end(), [](const auto& b) { return b.second == 0; });
            if (ok) {
                const auto m = reg.match(g);
                found = m.has_value() && m->result.id == canonicalize(toItemId(legacy.resultId)) &&
                        m->result.count == legacy.resultCount;
            }
        }
        EXPECT_TRUE(found) << "legacy recipe not preserved: " << legacy.label;
    }
}

// --------------------------------------------------------- basic items ---

TEST(BetaRecipes, PlanksFromAnyLogInInventoryGrid) {
    for (const BlockId log : {BlockId::OakLog, BlockId::BirchLog, BlockId::PineLog}) {
        const CraftingGrid g = layout(GridSize::Inventory2x2, {" L"}, {{'L', toItemId(log)}});
        const RecipeResult r = resultOf(book(), g);
        EXPECT_EQ(r.id, kPlanks);
        EXPECT_EQ(r.count, 4);
    }
}

TEST(BetaRecipes, SticksWorkbenchTorchInInventoryGrid) {
    EXPECT_EQ(resultOf(book(), layout(GridSize::Inventory2x2, {"#", "#"}, {{'#', kPlanks}})),
              (RecipeResult{kStick, 4}));
    EXPECT_EQ(resultOf(book(), layout(GridSize::Inventory2x2, {"##", "##"}, {{'#', kPlanks}})),
              (RecipeResult{toItemId(BlockId::CraftingTable), 1}));
    EXPECT_EQ(resultOf(book(), layout(GridSize::Inventory2x2, {" C", " #"}, {{'C', items::Coal}, {'#', kStick}})),
              (RecipeResult{toItemId(BlockId::Torch), 4}));
    // Vanilla-numbered coal/stick from another producer are accepted.
    EXPECT_EQ(resultOf(book(), layout(GridSize::Inventory2x2, {"C", "#"},
                                      {{'C', items::beta::Coal}, {'#', items::beta::Stick}})),
              (RecipeResult{toItemId(BlockId::Torch), 4}));
}

TEST(BetaRecipes, FurnaceAndChestNeedTheWorkbench) {
    EXPECT_EQ(resultOf(book(), layout(GridSize::Workbench3x3, {"###", "# #", "###"}, {{'#', kCobble}})),
              (RecipeResult{toItemId(BlockId::Furnace), 1}));
    EXPECT_EQ(resultOf(book(), layout(GridSize::Workbench3x3, {"###", "# #", "###"}, {{'#', kPlanks}})),
              (RecipeResult{items::Chest, 1}));
    // Full 3x3 cobble (centre filled) is not a furnace.
    EXPECT_FALSE(book().match(layout(GridSize::Workbench3x3, {"###", "###", "###"}, {{'#', kCobble}})).has_value());
    // The best a 2x2 grid can hold is a 2x2 corner, which is something else / nothing.
    EXPECT_NE(resultOf(book(), layout(GridSize::Inventory2x2, {"##", "# "}, {{'#', kCobble}})).id,
              toItemId(BlockId::Furnace));
}

// S5.5, S5.9, S5.10 at Beta 1.7.3 yields.
TEST(BetaRecipes, LadderFenceAndStairs) {
    const ItemId stick = items::Stick;
    EXPECT_EQ(resultOf(book(), layout(GridSize::Workbench3x3, {"# #", "###", "# #"}, {{'#', stick}})),
              (RecipeResult{toItemId(BlockId::Ladder), 2}));
    EXPECT_EQ(resultOf(book(), layout(GridSize::Workbench3x3, {"###", "###"}, {{'#', stick}})),
              (RecipeResult{toItemId(BlockId::Fence), 2}));
    EXPECT_EQ(resultOf(book(), layout(GridSize::Workbench3x3, {"   ", "###", "###"}, {{'#', stick}})),
              (RecipeResult{toItemId(BlockId::Fence), 2}));
    EXPECT_EQ(resultOf(book(), layout(GridSize::Workbench3x3, {"#  ", "## ", "###"}, {{'#', kPlanks}})),
              (RecipeResult{toItemId(BlockId::WoodStairs), 4}));
    EXPECT_EQ(resultOf(book(), layout(GridSize::Workbench3x3, {"  #", " ##", "###"}, {{'#', kPlanks}})),
              (RecipeResult{toItemId(BlockId::WoodStairs), 4}))
        << "mirrored";
    EXPECT_EQ(resultOf(book(), layout(GridSize::Workbench3x3, {"#  ", "## ", "###"}, {{'#', kCobble}})),
              (RecipeResult{toItemId(BlockId::CobbleStairs), 4}));
    EXPECT_FALSE(book().match(layout(GridSize::Workbench3x3, {"## ", "## ", "###"}, {{'#', kPlanks}})).has_value());
}

TEST(BetaRecipes, LegacySlabYieldAndVanillaOption) {
    const CraftingGrid g = layout(GridSize::Workbench3x3, {"   ", "###"}, {{'#', kCobble}});
    EXPECT_EQ(resultOf(book(), g), (RecipeResult{toItemId(BlockId::CobbleSlab), 6}));
    BetaRecipeOptions opts;
    opts.legacySlabYield = false;
    EXPECT_EQ(resultOf(makeBetaRecipeBook(opts), g), (RecipeResult{toItemId(BlockId::CobbleSlab), 3}));
}

TEST(BetaRecipes, BookshelfVanillaAndLegacyVariants) {
    const ItemId shelf = toItemId(BlockId::Bookshelf);
    EXPECT_EQ(resultOf(book(), layout(GridSize::Workbench3x3, {"###", "BBB", "###"},
                                      {{'#', kPlanks}, {'B', items::Book}})).id,
              shelf);
    EXPECT_EQ(resultOf(book(), layout(GridSize::Workbench3x3, {"###", "SSS", "###"},
                                      {{'#', kPlanks}, {'S', toItemId(BlockId::SugarCane)}})).id,
              shelf);
    EXPECT_EQ(resultOf(book(), layout(GridSize::Workbench3x3, {"SSS"}, {{'S', toItemId(BlockId::SugarCane)}})),
              (RecipeResult{items::Paper, 3}));
    EXPECT_EQ(resultOf(book(), layout(GridSize::Workbench3x3, {"P", "P", "P"}, {{'P', items::Paper}})),
              (RecipeResult{items::Book, 1}));
}

TEST(BetaRecipes, PistonWithIngotOrLegacyOre) {
    const auto piston = [](ItemId iron, ItemId red) {
        return layout(GridSize::Workbench3x3, {"TTT", "#X#", "#R#"},
                      {{'T', kPlanks}, {'#', kCobble}, {'X', iron}, {'R', red}});
    };
    const ItemId p = toItemId(BlockId::Piston);
    EXPECT_EQ(resultOf(book(), piston(items::IronIngot, items::Redstone)).id, p);
    EXPECT_EQ(resultOf(book(), piston(toItemId(BlockId::IronOre), toItemId(BlockId::RedstoneOre))).id, p);
}

TEST(BetaRecipes, SandstoneFromFourSand) {
    EXPECT_EQ(resultOf(book(), layout(GridSize::Inventory2x2, {"##", "##"}, {{'#', toItemId(BlockId::Sand)}})),
              (RecipeResult{toItemId(BlockId::Sandstone), 1}));
}

TEST(BetaRecipes, GlowstoneAndSnowBlockFromFourOfTheirItems) {
    EXPECT_EQ(resultOf(book(), layout(GridSize::Inventory2x2, {"##", "##"}, {{'#', items::GlowstoneDust}})),
              (RecipeResult{toItemId(BlockId::Glowstone), 1}));
    EXPECT_EQ(resultOf(book(), layout(GridSize::Workbench3x3, {"##", "##"}, {{'#', items::Snowball}})),
              (RecipeResult{toItemId(BlockId::SnowBlock), 1}));
    // Three dust are not enough.
    EXPECT_FALSE(book().match(layout(GridSize::Inventory2x2, {"##", "# "}, {{'#', items::GlowstoneDust}})).has_value());
}

TEST(BetaRecipes, BucketAndCakeWithContainerReturn) {
    EXPECT_EQ(resultOf(book(), layout(GridSize::Workbench3x3, {"# #", " # "}, {{'#', items::IronIngot}})),
              (RecipeResult{items::Bucket, 1}));
    CraftingGrid cake = layout(GridSize::Workbench3x3, {"MMM", "SES", "WWW"},
                               {{'M', items::MilkBucket}, {'S', items::Sugar}, {'E', items::Egg},
                                {'W', items::Wheat}});
    const auto out = book().craft(cake);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->result.id, items::Cake);
    for (int x = 0; x < 3; ++x) {
        EXPECT_EQ(cake.get(x, 0).id, items::Bucket) << x;
        EXPECT_TRUE(cake.get(x, 1).empty());
        EXPECT_TRUE(cake.get(x, 2).empty());
    }
}

// ---------------------------------------------------------------- tools ---

namespace {
struct ToolCase {
    ToolKind kind;
    ToolMaterial material;
};

std::string toolName(const testing::TestParamInfo<ToolCase>& info) {
    static constexpr std::array<const char*, 5> kinds{"Pickaxe", "Axe", "Shovel", "Sword", "Hoe"};
    static constexpr std::array<const char*, 5> mats{"Wood", "Stone", "Iron", "Gold", "Diamond"};
    return std::string{mats[static_cast<size_t>(info.param.material)]} + kinds[static_cast<size_t>(info.param.kind)];
}

std::vector<ToolCase> allTools() {
    std::vector<ToolCase> v;
    for (int k = 0; k < 5; ++k) {
        for (int m = 0; m < 5; ++m) {
            v.push_back({static_cast<ToolKind>(k), static_cast<ToolMaterial>(m)});
        }
    }
    return v;
}

ItemId vanillaMaterial(ToolMaterial m) {
    switch (m) {
        case ToolMaterial::Wood: return kPlanks;
        case ToolMaterial::Stone: return kCobble;
        case ToolMaterial::Iron: return items::IronIngot;
        case ToolMaterial::Gold: return items::GoldIngot;
        case ToolMaterial::Diamond: return items::Diamond;
    }
    return kAir;
}

std::vector<std::string_view> toolRows(ToolKind k) {
    switch (k) {
        case ToolKind::Pickaxe: return {"XXX", " # ", " # "};
        case ToolKind::Axe: return {"XX", "X#", " #"};
        case ToolKind::Shovel: return {"X", "#", "#"};
        case ToolKind::Sword: return {"X", "X", "#"};
        case ToolKind::Hoe: return {"XX", " #", " #"};
    }
    return {};
}

CraftingGrid toolGrid(ToolKind k, ItemId material, int ox, bool mirrored) {
    CraftingGrid g{GridSize::Workbench3x3};
    const auto rows = toolRows(k);
    const int w = static_cast<int>(rows[0].size());
    for (int y = 0; y < static_cast<int>(rows.size()); ++y) {
        for (int x = 0; x < w; ++x) {
            const char c = rows[static_cast<size_t>(y)][static_cast<size_t>(mirrored ? w - 1 - x : x)];
            if (c != ' ') {
                EXPECT_TRUE(g.set(ox + x, y, SlotStack{c == 'X' ? material : kStick, 1, 0}));
            }
        }
    }
    return g;
}

class BetaToolRecipe : public testing::TestWithParam<ToolCase> {};
} // namespace

TEST_P(BetaToolRecipe, CraftsAtEveryColumnOffsetAndMirrored) {
    const ToolCase tc = GetParam();
    const ItemId expected = toolItemId(tc.kind, tc.material);
    const int w = static_cast<int>(toolRows(tc.kind)[0].size());
    for (int ox = 0; ox + w <= 3; ++ox) {
        for (const bool mirrored : {false, true}) {
            const CraftingGrid g = toolGrid(tc.kind, vanillaMaterial(tc.material), ox, mirrored);
            EXPECT_EQ(resultOf(book(), g), (RecipeResult{expected, 1}))
                << "offset " << ox << (mirrored ? " mirrored" : "");
        }
    }
}

TEST_P(BetaToolRecipe, AllToolsAreThreeHighSoNeverIn2x2) {
    const ToolCase tc = GetParam();
    const ItemId expected = toolItemId(tc.kind, tc.material);
    for (size_t i = 0; i < book().size(); ++i) {
        if (book().result(i).id == expected) {
            ASSERT_NE(book().shaped(i), nullptr);
            EXPECT_FALSE(book().shaped(i)->fitsIn(GridSize::Inventory2x2));
        }
    }
}

TEST_P(BetaToolRecipe, ConsumesMaterialsAndSticks) {
    const ToolCase tc = GetParam();
    CraftingGrid g = toolGrid(tc.kind, vanillaMaterial(tc.material), 0, false);
    const auto out = book().craft(g);
    ASSERT_TRUE(out.has_value());
    EXPECT_TRUE(g.isEmpty());
}

INSTANTIATE_TEST_SUITE_P(AllKindsAndMaterials, BetaToolRecipe, testing::ValuesIn(allTools()), toolName);

TEST(BetaRecipes, LegacyOreSubstitutesCanBeDisabled) {
    const CraftingGrid oreAxe = toolGrid(ToolKind::Axe, toItemId(BlockId::IronOre), 0, false);
    EXPECT_EQ(resultOf(book(), oreAxe).id, items::IronAxe);
    BetaRecipeOptions opts;
    opts.acceptLegacyOres = false;
    const RecipeRegistry strict = makeBetaRecipeBook(opts);
    EXPECT_FALSE(strict.match(oreAxe).has_value());
    EXPECT_EQ(resultOf(strict, toolGrid(ToolKind::Axe, items::IronIngot, 0, false)).id, items::IronAxe);
}

// S2.8: the game screen uses the strict vanilla book now that the furnace makes ingots. The
// library default keeps the legacy variants (tests above) for compatibility.
TEST(BetaRecipes, GameBookRejectsRawOreTools) {
    const betacraft::game::InventoryCrafting screen;
    const RecipeRegistry& game = screen.recipes();
    for (size_t k = 0; k < 5; ++k) {
        const auto kind = static_cast<ToolKind>(k);
        for (const BlockId ore : {BlockId::IronOre, BlockId::GoldOre, BlockId::DiamondOre}) {
            EXPECT_FALSE(game.match(toolGrid(kind, toItemId(ore), 0, false)).has_value())
                << k << " " << static_cast<int>(ore);
        }
        EXPECT_EQ(resultOf(game, toolGrid(kind, items::IronIngot, 0, false)).id, toolItemId(kind, ToolMaterial::Iron));
        EXPECT_EQ(resultOf(game, toolGrid(kind, items::Diamond, 0, false)).id, toolItemId(kind, ToolMaterial::Diamond));
    }
    // Vanilla yields: 3 slabs, and no sugar-cane bookshelf.
    EXPECT_EQ(resultOf(game, layout(GridSize::Workbench3x3, {"###"}, {{'#', kCobble}})),
              (RecipeResult{toItemId(BlockId::CobbleSlab), 3}));
    EXPECT_FALSE(game.match(layout(GridSize::Workbench3x3, {"###", "SSS", "###"},
                                   {{'#', kPlanks}, {'S', toItemId(BlockId::SugarCane)}}))
                     .has_value());
    EXPECT_EQ(game.size(), makeBetaRecipeBook(BetaRecipeOptions::vanilla()).size());
}

TEST(BetaRecipes, MixedMaterialsDoNotCraft) {
    CraftingGrid g = toolGrid(ToolKind::Pickaxe, kCobble, 0, false);
    ASSERT_TRUE(g.set(1, 0, SlotStack{kPlanks, 1, 0}));
    EXPECT_FALSE(book().match(g).has_value());
}

// ------------------------------------------------ randomized invariants ---

TEST(BetaRecipesProperty, EveryCraftChangesExactlyTheOccupiedSlots) {
    // Fill a random book recipe with random stack sizes, craft until it stops
    // matching; each craft must touch every occupied slot (decrement, remainder
    // or durability) and never create items in empty slots.
    const RecipeRegistry& reg = book();
    std::mt19937 rng{42u};
    std::uniform_int_distribution<size_t> pick(0, reg.size() - 1);
    std::uniform_int_distribution<int> stack(1, 64);
    for (int iter = 0; iter < 200; ++iter) {
        const size_t idx = pick(rng);
        CraftingGrid g{GridSize::Workbench3x3};
        ASSERT_TRUE(instantiate(reg, idx, g, 0, 0, false));
        for (size_t i = 0; i < g.slotCount(); ++i) {
            SlotStack s = g.at(i);
            if (!s.empty()) {
                s.count = static_cast<uint8_t>(std::min<int>(stack(rng), maxStackSize(s.id)));
                ASSERT_TRUE(g.set(static_cast<int>(i) % 3, static_cast<int>(i) / 3, s));
            }
        }
        int crafts = 0;
        while (true) {
            const CraftingGrid before = g;
            const auto out = reg.craft(g);
            if (!out) {
                break;
            }
            ++crafts;
            ASSERT_LE(crafts, 64) << reg.label(idx);
            for (size_t i = 0; i < g.slotCount(); ++i) {
                if (before.at(i).empty()) {
                    EXPECT_TRUE(g.at(i).empty());
                } else {
                    EXPECT_NE(g.at(i), before.at(i)) << reg.label(idx) << " slot " << i;
                }
            }
        }
        EXPECT_GE(crafts, 1) << reg.label(idx);
    }
}

namespace {
// Reference matcher: plain linear scan over every recipe in priority order,
// bypassing the hashed shape index and the shapeless count index.
std::optional<size_t> linearScan(const RecipeRegistry& reg, const CraftingGrid& g) {
    for (size_t i = 0; i < reg.size(); ++i) {
        if (const ShapedRecipe* r = reg.shaped(i)) {
            if (r->fitsIn(g.size()) && r->match(g)) {
                return i;
            }
        } else if (const ShapelessRecipe* r = reg.shapeless(i)) {
            if (r->fitsIn(g.size()) && r->match(g)) {
                return i;
            }
        }
    }
    return std::nullopt;
}

void expectIndexAgreesWithLinearScan(const RecipeRegistry& reg, uint32_t seed, size_t& hits) {
    const std::array<ItemId, 10> palette{kPlanks, kCobble, kStick, items::Coal, items::IronIngot,
                                         toItemId(BlockId::IronOre), toItemId(BlockId::Sand),
                                         toItemId(BlockId::OakLog), items::Diamond, items::Book};
    std::mt19937 rng{seed};
    std::uniform_int_distribution<size_t> pickItem(0, palette.size() - 1);
    std::uniform_int_distribution<size_t> pickRecipe(0, reg.size() - 1);
    std::uniform_int_distribution<int> pickCell(0, 8);
    for (int iter = 0; iter < 6000; ++iter) {
        const GridSize size = (iter & 1) != 0 ? GridSize::Inventory2x2 : GridSize::Workbench3x3;
        CraftingGrid g{size};
        if (iter % 3 == 0) {
            // Pure noise, sparse so that small recipes come up.
            for (int y = 0; y < g.height(); ++y) {
                for (int x = 0; x < g.width(); ++x) {
                    if ((rng() & 3u) == 0) {
                        (void)g.set(x, y, SlotStack{palette[pickItem(rng)], 1, 0});
                    }
                }
            }
        } else {
            // A real recipe at a random placement/mirroring, then a random
            // near-miss mutation (one cell replaced, cleared or added) half the time.
            const int ox = static_cast<int>(rng() % 3u);
            const int oy = static_cast<int>(rng() % 3u);
            (void)instantiate(reg, pickRecipe(rng), g, ox, oy, (rng() & 1u) != 0);
            if ((rng() & 1u) != 0) {
                const int c = pickCell(rng);
                const int x = c % 3;
                const int y = c / 3;
                (void)g.set(x, y, (rng() & 1u) != 0 ? SlotStack{palette[pickItem(rng)], 1, 0} : SlotStack{});
            }
        }
        const auto expected = linearScan(reg, g);
        const auto actual = reg.match(g);
        ASSERT_EQ(actual.has_value(), expected.has_value()) << "iter " << iter;
        if (actual) {
            ++hits;
            ASSERT_EQ(actual->recipeIndex, *expected) << reg.label(actual->recipeIndex) << " vs "
                                                      << reg.label(*expected) << " iter " << iter;
            EXPECT_EQ(actual->result, reg.result(*expected));
        }
    }
}
} // namespace

TEST(BetaRecipesProperty, IndexedMatchAgreesWithLinearScanOnDefaultBook) {
    size_t hits = 0;
    expectIndexAgreesWithLinearScan(book(), 7u, hits);
    EXPECT_GT(hits, 400u);
}

TEST(BetaRecipesProperty, IndexedMatchAgreesWithLinearScanOnVanillaBook) {
    size_t hits = 0;
    expectIndexAgreesWithLinearScan(makeBetaRecipeBook(BetaRecipeOptions::vanilla()), 99u, hits);
    EXPECT_GT(hits, 400u);
}

// S8.5: the Beta 1.7.3 redstone recipes, one of each.
TEST(BetaRecipes, RedstoneParts) {
    const ItemId stone = toItemId(BlockId::Stone);
    const ItemId redstone = items::Redstone;
    EXPECT_EQ(resultOf(book(), layout(GridSize::Inventory2x2, {"X", "#"}, {{'X', kStick}, {'#', kCobble}})),
              (RecipeResult{toItemId(BlockId::Lever), 1}));
    EXPECT_EQ(resultOf(book(), layout(GridSize::Inventory2x2, {"X", "#"}, {{'X', redstone}, {'#', kStick}})),
              (RecipeResult{toItemId(BlockId::RedstoneTorchOn), 1}));
    EXPECT_EQ(resultOf(book(), layout(GridSize::Inventory2x2, {"##"}, {{'#', stone}})),
              (RecipeResult{toItemId(BlockId::StonePressurePlate), 1}));
    EXPECT_EQ(resultOf(book(), layout(GridSize::Inventory2x2, {"##"}, {{'#', kPlanks}})),
              (RecipeResult{toItemId(BlockId::WoodPressurePlate), 1}));
    EXPECT_EQ(resultOf(book(), layout(GridSize::Inventory2x2, {"#", "#"}, {{'#', stone}})),
              (RecipeResult{toItemId(BlockId::StoneButton), 1}));
    EXPECT_EQ(resultOf(book(), layout(GridSize::Workbench3x3, {"##", "##", "##"}, {{'#', items::IronIngot}})),
              (RecipeResult{items::IronDoor, 1}));
    // Cobblestone makes no plate nor button (Beta: smooth stone only).
    EXPECT_FALSE(book().match(layout(GridSize::Inventory2x2, {"##"}, {{'#', kCobble}})).has_value());
    EXPECT_NE(resultOf(book(), layout(GridSize::Inventory2x2, {"#", "#"}, {{'#', kCobble}})),
              (RecipeResult{toItemId(BlockId::StoneButton), 1}));
}
