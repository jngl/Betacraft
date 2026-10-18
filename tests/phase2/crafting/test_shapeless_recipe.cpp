// P2.6 — Shapeless recipes: order-independent multiset matching.
#include "crafting/ShapelessRecipe.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <random>
#include <set>
#include <vector>

using namespace betacraft::crafting;
using betacraft::world::BlockId;

namespace {
constexpr ItemId kPlanks = toItemId(BlockId::OakPlanks);
constexpr ItemId kCobble = toItemId(BlockId::Cobblestone);
constexpr ItemId kSand = toItemId(BlockId::Sand);
constexpr ItemId kOakLog = toItemId(BlockId::OakLog);

ShapelessRecipe mustCreate(std::initializer_list<Ingredient> ings, RecipeResult result) {
    ShapelessParse p = ShapelessRecipe::create(ings, result);
    EXPECT_EQ(p.error, RecipeError::None);
    EXPECT_TRUE(p.recipe.has_value());
    return p.recipe.value_or(ShapelessRecipe{});
}

void put(CraftingGrid& g, int x, int y, ItemId id) {
    ASSERT_TRUE(g.set(x, y, SlotStack{id, 1, 0}));
}

void putIndex(CraftingGrid& g, size_t idx, ItemId id) {
    put(g, static_cast<int>(idx % static_cast<size_t>(g.width())),
        static_cast<int>(idx / static_cast<size_t>(g.width())), id);
}
} // namespace

TEST(ShapelessCreate, RejectsEmptyIngredientList) {
    EXPECT_EQ(ShapelessRecipe::create({}, {kPlanks, 1}).error, RecipeError::EmptyPattern);
}

TEST(ShapelessCreate, RejectsMoreThanNineIngredients) {
    std::vector<Ingredient> ings(10, Ingredient::of(kPlanks));
    EXPECT_EQ(ShapelessRecipe::create(std::span{ings}, {kCobble, 1}).error, RecipeError::TooManyIngredients);
    ings.pop_back();
    EXPECT_EQ(ShapelessRecipe::create(std::span{ings}, {kCobble, 1}).error, RecipeError::None);
}

TEST(ShapelessCreate, RejectsInvalidIngredientAndResult) {
    EXPECT_EQ(ShapelessRecipe::create({Ingredient{}}, {kPlanks, 1}).error, RecipeError::InvalidIngredient);
    EXPECT_EQ(ShapelessRecipe::create({Ingredient::of(kOakLog)}, {kAir, 4}).error, RecipeError::InvalidResult);
    EXPECT_EQ(ShapelessRecipe::create({Ingredient::of(kOakLog)}, {kPlanks, 0}).error, RecipeError::InvalidResult);
}

TEST(ShapelessCreate, SizeGatingByIngredientCount) {
    const ShapelessRecipe four = mustCreate({Ingredient::of(kSand), Ingredient::of(kSand),
                                             Ingredient::of(kSand), Ingredient::of(kSand)},
                                            {kCobble, 1});
    EXPECT_TRUE(four.fitsIn(GridSize::Inventory2x2));
    const ShapelessRecipe five = mustCreate({Ingredient::of(kSand), Ingredient::of(kSand),
                                             Ingredient::of(kSand), Ingredient::of(kSand),
                                             Ingredient::of(kSand)},
                                            {kCobble, 1});
    EXPECT_FALSE(five.fitsIn(GridSize::Inventory2x2));
    EXPECT_TRUE(five.fitsIn(GridSize::Workbench3x3));
    EXPECT_EQ(five.ingredientCount(), 5u);
    EXPECT_EQ(five.result().id, kCobble);
}

TEST(ShapelessMatch, EmptyGridNeverMatches) {
    const ShapelessRecipe r = mustCreate({Ingredient::of(kOakLog)}, {kPlanks, 4});
    EXPECT_FALSE(r.match(CraftingGrid{GridSize::Workbench3x3}).has_value());
}

TEST(ShapelessMatch, SingleIngredientMatchesInEverySlot) {
    const ShapelessRecipe r = mustCreate({Ingredient::of(kOakLog)}, {kPlanks, 4});
    for (size_t i = 0; i < 9; ++i) {
        CraftingGrid g{GridSize::Workbench3x3};
        putIndex(g, i, kOakLog);
        const auto m = r.match(g);
        ASSERT_TRUE(m.has_value()) << i;
        EXPECT_EQ(m->slotToIngredient[i], 0);
    }
    for (size_t i = 0; i < 4; ++i) {
        CraftingGrid g{GridSize::Inventory2x2};
        putIndex(g, i, kOakLog);
        EXPECT_TRUE(r.match(g).has_value()) << i;
    }
}

TEST(ShapelessMatch, OrderIndependentOverRandomPlacements) {
    const ShapelessRecipe r = mustCreate({Ingredient::of(kPlanks), Ingredient::of(kCobble),
                                          Ingredient::of(kSand)},
                                         {kOakLog, 1});
    std::mt19937 rng{0xC0FFEEu};
    std::array<size_t, 9> slots{};
    std::iota(slots.begin(), slots.end(), size_t{0});
    for (int iter = 0; iter < 300; ++iter) {
        std::shuffle(slots.begin(), slots.end(), rng);
        std::array<ItemId, 3> items{kPlanks, kCobble, kSand};
        std::shuffle(items.begin(), items.end(), rng);
        CraftingGrid g{GridSize::Workbench3x3};
        for (size_t k = 0; k < 3; ++k) {
            putIndex(g, slots[k], items[k]);
        }
        ASSERT_TRUE(r.match(g).has_value()) << "iteration " << iter;
    }
}

TEST(ShapelessMatch, ExtraOrMissingItemsBreakTheMatch) {
    const ShapelessRecipe r = mustCreate({Ingredient::of(kPlanks), Ingredient::of(kCobble)}, {kSand, 1});
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 0, 0, kPlanks);
    EXPECT_FALSE(r.match(g).has_value());
    put(g, 1, 1, kCobble);
    EXPECT_TRUE(r.match(g).has_value());
    put(g, 0, 1, kCobble);
    EXPECT_FALSE(r.match(g).has_value());
}

TEST(ShapelessMatch, MultisetMultiplicityMatters) {
    const ShapelessRecipe r = mustCreate({Ingredient::of(kPlanks), Ingredient::of(kPlanks),
                                          Ingredient::of(kCobble)},
                                         {kSand, 1});
    CraftingGrid ok{GridSize::Workbench3x3};
    put(ok, 2, 2, kPlanks);
    put(ok, 0, 0, kCobble);
    put(ok, 1, 0, kPlanks);
    EXPECT_TRUE(r.match(ok).has_value());

    CraftingGrid bad{GridSize::Workbench3x3};
    put(bad, 2, 2, kPlanks);
    put(bad, 0, 0, kCobble);
    put(bad, 1, 0, kCobble);
    EXPECT_FALSE(r.match(bad).has_value());
}

TEST(ShapelessMatch, BacktracksOverOverlappingAlternatives) {
    // Greedy slot-order assignment would bind the planks to the wildcard first
    // and then fail on the cobble; a correct matcher must backtrack.
    const ShapelessRecipe r = mustCreate({Ingredient::anyOf({kPlanks, kCobble}), Ingredient::of(kPlanks)},
                                         {kSand, 1});
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 0, 0, kPlanks);
    put(g, 1, 0, kCobble);
    const auto m = r.match(g);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->slotToIngredient[0], 1);
    EXPECT_EQ(m->slotToIngredient[1], 0);
}

TEST(ShapelessMatch, AssignmentIsABijectionOntoIngredients) {
    const ShapelessRecipe r = mustCreate({Ingredient::of(kPlanks), Ingredient::of(kCobble),
                                          Ingredient::of(kSand), Ingredient::of(kOakLog)},
                                         {kSand, 1});
    CraftingGrid g{GridSize::Workbench3x3};
    put(g, 2, 0, kOakLog);
    put(g, 0, 1, kSand);
    put(g, 1, 1, kPlanks);
    put(g, 2, 2, kCobble);
    const auto m = r.match(g);
    ASSERT_TRUE(m.has_value());
    std::set<int> used;
    for (size_t i = 0; i < 9; ++i) {
        const int ing = m->slotToIngredient[i];
        if (g.at(i).empty()) {
            EXPECT_EQ(ing, -1) << i;
        } else {
            ASSERT_GE(ing, 0);
            EXPECT_TRUE(r.ingredient(static_cast<size_t>(ing)).matches(g.at(i)));
            EXPECT_TRUE(used.insert(ing).second);
        }
    }
    EXPECT_EQ(used.size(), 4u);
}

TEST(ShapelessMatch, NineIngredientRecipeFillsWorkbench) {
    std::vector<Ingredient> ings(9, Ingredient::of(kCobble));
    const ShapelessParse p = ShapelessRecipe::create(std::span{ings}, {kSand, 1});
    ASSERT_TRUE(p.recipe.has_value());
    CraftingGrid g{GridSize::Workbench3x3};
    for (size_t i = 0; i < 9; ++i) {
        putIndex(g, i, kCobble);
    }
    EXPECT_TRUE(p.recipe->match(g).has_value());
}
