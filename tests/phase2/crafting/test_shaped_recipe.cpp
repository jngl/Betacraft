// P2.6 — Shaped recipes: sub-matrix normalisation, translation invariance,
// horizontal mirroring and grid size gating.
#include "crafting/ShapedRecipe.hpp"

#include <gtest/gtest.h>

#include <string_view>
#include <vector>

using namespace betacraft::crafting;
using betacraft::world::BlockId;

namespace {
constexpr ItemId kPlanks = toItemId(BlockId::OakPlanks);
constexpr ItemId kCobble = toItemId(BlockId::Cobblestone);
constexpr ItemId kStick = items::Stick;

ShapedRecipe mustParse(std::initializer_list<std::string_view> rows,
                       std::initializer_list<PatternKey> keys, RecipeResult result,
                       bool mirrorable = true) {
    ShapedParse p = ShapedRecipe::parse(rows, keys, result, mirrorable);
    EXPECT_EQ(p.error, RecipeError::None);
    EXPECT_TRUE(p.recipe.has_value());
    return p.recipe.value_or(ShapedRecipe{});
}

ShapedRecipe workbench() {
    return mustParse({"##", "##"}, {{'#', Ingredient::of(kPlanks)}},
                     {toItemId(BlockId::CraftingTable), 1});
}

ShapedRecipe sticks() {
    return mustParse({"#", "#"}, {{'#', Ingredient::of(kPlanks)}}, {kStick, 4});
}

// Beta axe, "left-handed" definition: mirror is the right-handed layout.
ShapedRecipe stoneAxe() {
    return mustParse({"XX", "X#", " #"},
                     {{'X', Ingredient::of(kCobble)}, {'#', Ingredient::of(kStick)}},
                     {items::StoneAxe, 1});
}

ShapedRecipe woodPickaxe() {
    return mustParse({"XXX", " # ", " # "},
                     {{'X', Ingredient::of(kPlanks)}, {'#', Ingredient::of(kStick)}},
                     {items::WoodPickaxe, 1});
}

void put(CraftingGrid& g, int x, int y, ItemId id, uint8_t count = 1) {
    ASSERT_TRUE(g.set(x, y, SlotStack{id, count, 0}));
}
} // namespace

// ---------------------------------------------------------------- parsing ---

TEST(ShapedParse, ReportsDimensionsAndResult) {
    const ShapedRecipe r = stoneAxe();
    EXPECT_EQ(r.width(), 2);
    EXPECT_EQ(r.height(), 3);
    EXPECT_EQ(r.result().id, items::StoneAxe);
    EXPECT_EQ(r.result().count, 1);
    EXPECT_TRUE(r.mirrorable());
    EXPECT_EQ(r.ingredientCount(), 5u);
}

TEST(ShapedParse, CellAccessorExposesIngredientsAndHoles) {
    const ShapedRecipe r = stoneAxe();
    ASSERT_NE(r.cell(0, 0), nullptr);
    EXPECT_TRUE(r.cell(0, 0)->matches(kCobble));
    EXPECT_EQ(r.cell(0, 2), nullptr); // the ' ' hole
    ASSERT_NE(r.cell(1, 2), nullptr);
    EXPECT_TRUE(r.cell(1, 2)->matches(kStick));
    EXPECT_EQ(r.cell(5, 0), nullptr);
    EXPECT_EQ(r.cell(-1, 0), nullptr);
}

TEST(ShapedParse, TrimsBlankBorderRowsAndColumns) {
    const ShapedRecipe r = mustParse({"   ", " # ", " # "}, {{'#', Ingredient::of(kPlanks)}},
                                     {kStick, 4});
    EXPECT_EQ(r.width(), 1);
    EXPECT_EQ(r.height(), 2);
}

TEST(ShapedParse, OccupancyMaskIsRowMajorInTrimmedBox) {
    const ShapedRecipe r = stoneAxe();
    // rows: XX / X# / .#  -> bits (y*w+x): 0,1,2,3,5
    EXPECT_EQ(r.occupancyMask(), 0b101111u);
    EXPECT_EQ(r.mirroredOccupancyMask(), 0b011111u);
}

TEST(ShapedParse, EmptyPatternIsRejected) {
    EXPECT_EQ(ShapedRecipe::parse({}, {}, {kStick, 1}).error, RecipeError::EmptyPattern);
    EXPECT_EQ(ShapedRecipe::parse({"  ", "  "}, {}, {kStick, 1}).error, RecipeError::EmptyPattern);
}

TEST(ShapedParse, OversizedPatternIsRejected) {
    const Ingredient p = Ingredient::of(kPlanks);
    EXPECT_EQ(ShapedRecipe::parse({"####"}, {{'#', p}}, {kStick, 1}).error, RecipeError::TooLarge);
    EXPECT_EQ(ShapedRecipe::parse({"#", "#", "#", "#"}, {{'#', p}}, {kStick, 1}).error,
              RecipeError::TooLarge);
}

TEST(ShapedParse, RaggedRowsAreRejected) {
    EXPECT_EQ(ShapedRecipe::parse({"##", "#"}, {{'#', Ingredient::of(kPlanks)}}, {kStick, 1}).error,
              RecipeError::RaggedRows);
}

TEST(ShapedParse, UnknownKeyIsRejected) {
    EXPECT_EQ(ShapedRecipe::parse({"#X"}, {{'#', Ingredient::of(kPlanks)}}, {kStick, 1}).error,
              RecipeError::UnknownKey);
}

TEST(ShapedParse, InvalidIngredientIsRejected) {
    EXPECT_EQ(ShapedRecipe::parse({"#"}, {{'#', Ingredient{}}}, {kStick, 1}).error,
              RecipeError::InvalidIngredient);
}

TEST(ShapedParse, DuplicateOrReservedKeyIsRejected) {
    const Ingredient p = Ingredient::of(kPlanks);
    EXPECT_EQ(ShapedRecipe::parse({"#"}, {{'#', p}, {'#', p}}, {kStick, 1}).error,
              RecipeError::DuplicateKey);
    EXPECT_EQ(ShapedRecipe::parse({"#"}, {{'#', p}, {' ', p}}, {kStick, 1}).error,
              RecipeError::DuplicateKey);
}

TEST(ShapedParse, InvalidResultIsRejected) {
    const Ingredient p = Ingredient::of(kPlanks);
    EXPECT_EQ(ShapedRecipe::parse({"#"}, {{'#', p}}, {kAir, 1}).error, RecipeError::InvalidResult);
    EXPECT_EQ(ShapedRecipe::parse({"#"}, {{'#', p}}, {kStick, 0}).error, RecipeError::InvalidResult);
    EXPECT_EQ(ShapedRecipe::parse({"#"}, {{'#', p}}, {items::IronSword, 2}).error,
              RecipeError::InvalidResult);
    EXPECT_EQ(ShapedRecipe::parse({"#"}, {{'#', p}}, {kStick, 65}).error, RecipeError::InvalidResult);
}

TEST(ShapedParse, AcceptsRuntimeContainers) {
    const std::vector<std::string_view> rows{"#", "#"};
    const std::vector<PatternKey> keys{{'#', Ingredient::of(kPlanks)}};
    const ShapedParse p = ShapedRecipe::parse(std::span{rows}, std::span{keys}, {kStick, 4}, true);
    EXPECT_EQ(p.error, RecipeError::None);
    ASSERT_TRUE(p.recipe.has_value());
    EXPECT_EQ(p.recipe->height(), 2);
}

TEST(ShapedParse, SizeGatingByGrid) {
    EXPECT_TRUE(workbench().fitsIn(GridSize::Inventory2x2));
    EXPECT_TRUE(sticks().fitsIn(GridSize::Inventory2x2));
    EXPECT_FALSE(stoneAxe().fitsIn(GridSize::Inventory2x2));
    EXPECT_FALSE(woodPickaxe().fitsIn(GridSize::Inventory2x2));
    EXPECT_TRUE(woodPickaxe().fitsIn(GridSize::Workbench3x3));
}

// --------------------------------------------------------------- matching ---

TEST(ShapedMatch, EmptyGridNeverMatches) {
    EXPECT_FALSE(workbench().match(CraftingGrid{GridSize::Inventory2x2}).has_value());
    EXPECT_FALSE(workbench().match(CraftingGrid{GridSize::Workbench3x3}).has_value());
}

TEST(ShapedMatch, WorkbenchFillsInventoryGrid) {
    CraftingGrid g{GridSize::Inventory2x2};
    for (int y = 0; y < 2; ++y) {
        for (int x = 0; x < 2; ++x) {
            put(g, x, y, kPlanks);
        }
    }
    const auto m = workbench().match(g);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->originX, 0);
    EXPECT_EQ(m->originY, 0);
    EXPECT_FALSE(m->mirrored);
}

TEST(ShapedMatch, TranslationInvarianceForEveryOffsetIn3x3) {
    const ShapedRecipe r = workbench();
    for (int oy = 0; oy < 2; ++oy) {
        for (int ox = 0; ox < 2; ++ox) {
            CraftingGrid g{GridSize::Workbench3x3};
            for (int y = 0; y < 2; ++y) {
                for (int x = 0; x < 2; ++x) {
                    put(g, ox + x, oy + y, kPlanks);
                }
            }
            const auto m = r.match(g);
            ASSERT_TRUE(m.has_value()) << "offset " << ox << "," << oy;
            EXPECT_EQ(m->originX, ox);
            EXPECT_EQ(m->originY, oy);
        }
    }
}

TEST(ShapedMatch, VerticalStickPairMatchesAtAllSixPositions) {
    const ShapedRecipe r = sticks();
    int hits = 0;
    for (int oy = 0; oy < 2; ++oy) {
        for (int ox = 0; ox < 3; ++ox) {
            CraftingGrid g{GridSize::Workbench3x3};
            put(g, ox, oy, kPlanks);
            put(g, ox, oy + 1, kPlanks);
            hits += r.match(g).has_value() ? 1 : 0;
        }
    }
    EXPECT_EQ(hits, 6);
}

TEST(ShapedMatch, HorizontalPairDoesNotMatchVerticalRecipe) {
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 0, 0, kPlanks);
    put(g, 1, 0, kPlanks);
    EXPECT_FALSE(sticks().match(g).has_value());
}

TEST(ShapedMatch, StrayExtraItemBreaksTheMatch) {
    CraftingGrid g{GridSize::Workbench3x3};
    put(g, 0, 0, kPlanks);
    put(g, 0, 1, kPlanks);
    put(g, 2, 2, kCobble);
    EXPECT_FALSE(sticks().match(g).has_value());
}

TEST(ShapedMatch, MissingCellBreaksTheMatch) {
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 0, 0, kPlanks);
    put(g, 1, 0, kPlanks);
    put(g, 0, 1, kPlanks);
    EXPECT_FALSE(workbench().match(g).has_value());
}

TEST(ShapedMatch, WrongItemBreaksTheMatch) {
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 0, 0, kPlanks);
    put(g, 1, 0, kPlanks);
    put(g, 0, 1, kPlanks);
    put(g, 1, 1, kCobble);
    EXPECT_FALSE(workbench().match(g).has_value());
}

TEST(ShapedMatch, StackCountsAboveOneStillMatch) {
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 1, 0, kPlanks, 64);
    put(g, 1, 1, kPlanks, 2);
    EXPECT_TRUE(sticks().match(g).has_value());
}

TEST(ShapedMatch, HolesMustStayEmpty) {
    CraftingGrid g{GridSize::Workbench3x3};
    put(g, 0, 0, kPlanks);
    put(g, 1, 0, kPlanks);
    put(g, 2, 0, kPlanks);
    put(g, 1, 1, kStick);
    put(g, 1, 2, kStick);
    EXPECT_TRUE(woodPickaxe().match(g).has_value());
    put(g, 0, 2, kStick); // fill a hole
    EXPECT_FALSE(woodPickaxe().match(g).has_value());
}

TEST(ShapedMatch, AxeMatchesDirectAndMirrored) {
    const ShapedRecipe r = stoneAxe();
    CraftingGrid direct{GridSize::Workbench3x3};
    put(direct, 0, 0, kCobble);
    put(direct, 1, 0, kCobble);
    put(direct, 0, 1, kCobble);
    put(direct, 1, 1, kStick);
    put(direct, 1, 2, kStick);
    const auto md = r.match(direct);
    ASSERT_TRUE(md.has_value());
    EXPECT_FALSE(md->mirrored);

    CraftingGrid mirrored{GridSize::Workbench3x3};
    put(mirrored, 1, 0, kCobble);
    put(mirrored, 2, 0, kCobble);
    put(mirrored, 2, 1, kCobble);
    put(mirrored, 1, 1, kStick);
    put(mirrored, 1, 2, kStick);
    const auto mm = r.match(mirrored);
    ASSERT_TRUE(mm.has_value());
    EXPECT_TRUE(mm->mirrored);
    EXPECT_EQ(mm->originX, 1);
}

TEST(ShapedMatch, NonMirrorableRecipeRejectsMirroredLayout) {
    const ShapedRecipe r = mustParse({"XX", "X#", " #"},
                                     {{'X', Ingredient::of(kCobble)}, {'#', Ingredient::of(kStick)}},
                                     {items::StoneAxe, 1}, false);
    EXPECT_FALSE(r.mirrorable());
    CraftingGrid mirrored{GridSize::Workbench3x3};
    put(mirrored, 0, 0, kCobble);
    put(mirrored, 1, 0, kCobble);
    put(mirrored, 1, 1, kCobble);
    put(mirrored, 0, 1, kStick);
    put(mirrored, 0, 2, kStick);
    EXPECT_FALSE(r.match(mirrored).has_value());
}

TEST(ShapedMatch, SymmetricRecipeReportsNotMirrored) {
    CraftingGrid g{GridSize::Workbench3x3};
    put(g, 0, 0, kPlanks);
    put(g, 1, 0, kPlanks);
    put(g, 2, 0, kPlanks);
    put(g, 1, 1, kStick);
    put(g, 1, 2, kStick);
    const auto m = woodPickaxe().match(g);
    ASSERT_TRUE(m.has_value());
    EXPECT_FALSE(m->mirrored);
}

TEST(ShapedMatch, MirroringDistinguishesIngredientsNotOnlyOccupancy) {
    // Same occupancy when mirrored, but different items: "AB" vs "BA".
    const ShapedRecipe r = mustParse({"AB"}, {{'A', Ingredient::of(kPlanks)}, {'B', Ingredient::of(kCobble)}},
                                     {kStick, 1}, true);
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 0, 1, kCobble);
    put(g, 1, 1, kPlanks);
    const auto m = r.match(g);
    ASSERT_TRUE(m.has_value());
    EXPECT_TRUE(m->mirrored);

    const ShapedRecipe strict = mustParse({"AB"}, {{'A', Ingredient::of(kPlanks)}, {'B', Ingredient::of(kCobble)}},
                                          {kStick, 1}, false);
    EXPECT_FALSE(strict.match(g).has_value());
}

TEST(ShapedMatch, ThreeWideRecipeCannotMatchIn2x2) {
    // The 2x2 grid can at most hold the top-left 2x2 of the pickaxe: never a match.
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 0, 0, kPlanks);
    put(g, 1, 0, kPlanks);
    put(g, 1, 1, kStick);
    EXPECT_FALSE(woodPickaxe().match(g).has_value());
}

TEST(ShapedMatch, AlternativesAndVanillaAliasesAreAccepted) {
    const ShapedRecipe torch = mustParse(
        {"C", "#"}, {{'C', Ingredient::of(items::Coal)}, {'#', Ingredient::of(kStick)}},
        {toItemId(BlockId::Torch), 4});
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 1, 0, items::beta::Coal);
    put(g, 1, 1, items::beta::Stick);
    EXPECT_TRUE(torch.match(g).has_value());
}
