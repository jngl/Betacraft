// RecipeRegistry: deterministic priority, hashed shape index,
// consumption (1 per slot), by-products (empty bucket) and durability use.
#include "crafting/RecipeRegistry.hpp"

#include <gtest/gtest.h>

#include <random>
#include <string>
#include <vector>

using namespace betacraft::crafting;
using betacraft::voxel::BlockId;

namespace {
constexpr ItemId kPlanks = toItemId(BlockId::OakPlanks);
constexpr ItemId kCobble = toItemId(BlockId::Cobblestone);
constexpr ItemId kSand = toItemId(BlockId::Sand);
constexpr ItemId kOakLog = toItemId(BlockId::OakLog);
constexpr ItemId kTable = toItemId(BlockId::CraftingTable);
constexpr ItemId kGlass = toItemId(BlockId::Glass);

void put(CraftingGrid& g, int x, int y, ItemId id, uint8_t count = 1, uint16_t damage = 0) {
    ASSERT_TRUE(g.set(x, y, SlotStack{id, count, damage}));
}

CraftingGrid gridOf(GridSize size, std::initializer_list<std::string_view> rows,
                    std::initializer_list<std::pair<char, ItemId>> legend) {
    CraftingGrid g{size};
    int y = 0;
    for (const std::string_view row : rows) {
        for (int x = 0; x < static_cast<int>(row.size()); ++x) {
            for (const auto& [c, id] : legend) {
                if (row[static_cast<size_t>(x)] == c) {
                    EXPECT_TRUE(g.set(x, y, SlotStack{id, 1, 0}));
                }
            }
        }
        ++y;
    }
    return g;
}
} // namespace

TEST(RecipeRegistry, EmptyRegistryMatchesNothing) {
    const RecipeRegistry reg;
    EXPECT_EQ(reg.size(), 0u);
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 0, 0, kOakLog);
    EXPECT_FALSE(reg.match(g).has_value());
}

TEST(RecipeRegistry, AddPropagatesValidationErrorsWithoutRegistering) {
    RecipeRegistry reg;
    EXPECT_EQ(reg.addShaped({"####"}, {{'#', Ingredient::of(kPlanks)}}, {kTable, 1}), RecipeError::TooLarge);
    EXPECT_EQ(reg.addShapeless({}, {kTable, 1}), RecipeError::EmptyPattern);
    EXPECT_EQ(reg.size(), 0u);
    EXPECT_EQ(reg.addShaped({"##", "##"}, {{'#', Ingredient::of(kPlanks)}}, {kTable, 1}, true, "Workbench"),
              RecipeError::None);
    EXPECT_EQ(reg.size(), 1u);
    EXPECT_EQ(reg.label(0), "Workbench");
    EXPECT_EQ(reg.kind(0), RecipeKind::Shaped);
    EXPECT_EQ(reg.result(0).id, kTable);
    EXPECT_NE(reg.shaped(0), nullptr);
    EXPECT_EQ(reg.shapeless(0), nullptr);
    EXPECT_EQ(reg.label(99), "");
}

TEST(RecipeRegistry, MatchReturnsResultCountAndIndex) {
    RecipeRegistry reg;
    ASSERT_EQ(reg.addShapeless({Ingredient::of(kOakLog)}, {kPlanks, 4}, "Planks"), RecipeError::None);
    ASSERT_EQ(reg.addShaped({"##", "##"}, {{'#', Ingredient::of(kPlanks)}}, {kTable, 1}), RecipeError::None);
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 1, 1, kOakLog, 12);
    const auto m = reg.match(g);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->recipeIndex, 0u);
    EXPECT_EQ(m->kind, RecipeKind::Shapeless);
    EXPECT_EQ(m->result.id, kPlanks);
    EXPECT_EQ(m->result.count, 4);
    EXPECT_EQ(reg.kind(0), RecipeKind::Shapeless);
    EXPECT_NE(reg.shapeless(0), nullptr);
}

TEST(RecipeRegistry, FirstRegisteredWinsAmongShapedConflicts) {
    RecipeRegistry reg;
    ASSERT_EQ(reg.addShaped({"#"}, {{'#', Ingredient::of(kSand)}}, {kGlass, 1}), RecipeError::None);
    ASSERT_EQ(reg.addShaped({"#"}, {{'#', Ingredient::of(kSand)}}, {kCobble, 1}), RecipeError::None);
    CraftingGrid g{GridSize::Workbench3x3};
    put(g, 2, 2, kSand);
    const auto m = reg.match(g);
    ASSERT_TRUE(m.has_value());
    EXPECT_EQ(m->recipeIndex, 0u);
    EXPECT_EQ(m->result.id, kGlass);
}

TEST(RecipeRegistry, PriorityIsRegistrationOrderAcrossKinds) {
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 0, 0, kSand);
    {
        RecipeRegistry reg;
        ASSERT_EQ(reg.addShapeless({Ingredient::of(kSand)}, {kGlass, 1}), RecipeError::None);
        ASSERT_EQ(reg.addShaped({"#"}, {{'#', Ingredient::of(kSand)}}, {kCobble, 1}), RecipeError::None);
        EXPECT_EQ(reg.match(g)->result.id, kGlass);
    }
    {
        RecipeRegistry reg;
        ASSERT_EQ(reg.addShaped({"#"}, {{'#', Ingredient::of(kSand)}}, {kCobble, 1}), RecipeError::None);
        ASSERT_EQ(reg.addShapeless({Ingredient::of(kSand)}, {kGlass, 1}), RecipeError::None);
        EXPECT_EQ(reg.match(g)->result.id, kCobble);
    }
}

TEST(RecipeRegistry, MirroredMatchIsReported) {
    RecipeRegistry reg;
    ASSERT_EQ(reg.addShaped({"XX", "X#", " #"},
                            {{'X', Ingredient::of(kCobble)}, {'#', Ingredient::of(items::Stick)}},
                            {items::StoneAxe, 1}),
              RecipeError::None);
    const CraftingGrid g = gridOf(GridSize::Workbench3x3, {" XX", " #X", " # "},
                                  {{'X', kCobble}, {'#', items::Stick}});
    const auto m = reg.match(g);
    ASSERT_TRUE(m.has_value());
    EXPECT_TRUE(m->mirrored);
    EXPECT_EQ(m->result.id, items::StoneAxe);
}

TEST(RecipeRegistry, ThreeByThreeOnlyRecipeNeverMatchesInInventoryGrid) {
    RecipeRegistry reg;
    ASSERT_EQ(reg.addShaped({"###", "# #", "###"}, {{'#', Ingredient::of(kCobble)}},
                            {toItemId(BlockId::Furnace), 1}),
              RecipeError::None);
    const CraftingGrid full = gridOf(GridSize::Inventory2x2, {"##", "##"}, {{'#', kCobble}});
    EXPECT_FALSE(reg.match(full).has_value());
    const CraftingGrid bench = gridOf(GridSize::Workbench3x3, {"###", "# #", "###"}, {{'#', kCobble}});
    EXPECT_TRUE(reg.match(bench).has_value());
}

TEST(RecipeRegistry, CraftOnNonMatchingGridLeavesGridUntouched) {
    RecipeRegistry reg;
    ASSERT_EQ(reg.addShapeless({Ingredient::of(kOakLog)}, {kPlanks, 4}), RecipeError::None);
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 0, 0, kCobble, 3);
    const CraftingGrid before = g;
    EXPECT_FALSE(reg.craft(g).has_value());
    for (size_t i = 0; i < g.slotCount(); ++i) {
        EXPECT_EQ(g.at(i), before.at(i));
    }
}

TEST(RecipeRegistry, CraftConsumesExactlyOnePerOccupiedSlot) {
    RecipeRegistry reg;
    ASSERT_EQ(reg.addShaped({"##", "##"}, {{'#', Ingredient::of(kPlanks)}}, {kTable, 1}), RecipeError::None);
    CraftingGrid g{GridSize::Workbench3x3};
    put(g, 1, 1, kPlanks, 1);
    put(g, 2, 1, kPlanks, 5);
    put(g, 1, 2, kPlanks, 64);
    put(g, 2, 2, kPlanks, 2);
    const auto out = reg.craft(g);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(out->result.id, kTable);
    EXPECT_EQ(out->result.count, 1);
    EXPECT_EQ(out->byproductCount, 0u);
    EXPECT_TRUE(g.get(1, 1).empty());
    EXPECT_EQ(g.get(2, 1).count, 4);
    EXPECT_EQ(g.get(1, 2).count, 63);
    EXPECT_EQ(g.get(2, 2).count, 1);
    EXPECT_EQ(g.occupiedCount(), 3u);
    // Grid no longer holds a full 2x2 square -> no further match.
    EXPECT_FALSE(reg.match(g).has_value());
}

TEST(RecipeRegistry, RepeatedCraftingUntilExhaustion) {
    RecipeRegistry reg;
    ASSERT_EQ(reg.addShapeless({Ingredient::of(kOakLog)}, {kPlanks, 4}), RecipeError::None);
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 0, 1, kOakLog, 7);
    int crafted = 0;
    while (reg.craft(g).has_value()) {
        ++crafted;
        ASSERT_LE(crafted, 7);
    }
    EXPECT_EQ(crafted, 7);
    EXPECT_TRUE(g.isEmpty());
}

TEST(RecipeRegistry, ContainerRemainderStaysInSlot) {
    RecipeRegistry reg;
    ASSERT_EQ(reg.addShapeless({Ingredient::of(items::MilkBucket).withRemainder(items::Bucket),
                                Ingredient::of(items::Sugar)},
                               {items::Cake, 1}),
              RecipeError::None);
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 0, 0, items::MilkBucket);
    put(g, 1, 0, items::Sugar, 3);
    const auto out = reg.craft(g);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(g.get(0, 0).id, items::Bucket);
    EXPECT_EQ(g.get(0, 0).count, 1);
    EXPECT_EQ(g.get(1, 0).count, 2);
    EXPECT_EQ(out->byproductCount, 0u);
}

TEST(RecipeRegistry, RemainderBecomesByproductWhenSlotStillOccupied) {
    RecipeRegistry reg;
    ASSERT_EQ(reg.addShapeless({Ingredient::of(kSand).withRemainder(kGlass)}, {kCobble, 1}), RecipeError::None);
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 1, 1, kSand, 2);
    const auto out = reg.craft(g);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(g.get(1, 1).id, kSand);
    EXPECT_EQ(g.get(1, 1).count, 1);
    ASSERT_EQ(out->byproductCount, 1u);
    EXPECT_EQ(out->byproducts[0].id, kGlass);
    EXPECT_EQ(out->byproducts[0].count, 1);
}

TEST(RecipeRegistry, DamagingIngredientLosesDurabilityInsteadOfBeingConsumed) {
    RecipeRegistry reg;
    ASSERT_EQ(reg.addShapeless({Ingredient::of(items::IronAxe).damaging(1), Ingredient::of(kOakLog)},
                               {kPlanks, 6}),
              RecipeError::None);
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 0, 0, items::IronAxe, 1, 10);
    put(g, 1, 0, kOakLog, 2);
    ASSERT_TRUE(reg.craft(g).has_value());
    EXPECT_EQ(g.get(0, 0).id, items::IronAxe);
    EXPECT_EQ(g.get(0, 0).count, 1);
    EXPECT_EQ(g.get(0, 0).damage, 11);
    EXPECT_EQ(g.get(1, 0).count, 1);
}

// Beta 1.7.3 ItemStack.damageItem (and gameplay::ItemStack::applyWear) break
// a tool only once damage EXCEEDS maxDamage: a tool has maxDamage + 1 uses.
TEST(RecipeRegistry, DamagingIngredientSurvivesAtExactlyMaxDurability) {
    RecipeRegistry reg;
    ASSERT_EQ(reg.addShapeless({Ingredient::of(items::GoldAxe).damaging(1), Ingredient::of(kOakLog)},
                               {kPlanks, 6}),
              RecipeError::None);
    CraftingGrid g{GridSize::Inventory2x2};
    const auto max = maxDurability(items::GoldAxe);
    put(g, 0, 0, items::GoldAxe, 1, static_cast<uint16_t>(max - 1));
    put(g, 1, 0, kOakLog, 1);
    ASSERT_TRUE(reg.craft(g).has_value());
    EXPECT_EQ(g.get(0, 0).id, items::GoldAxe);
    EXPECT_EQ(g.get(0, 0).damage, max);
}

TEST(RecipeRegistry, DamagingIngredientBreaksOnceDamageExceedsMax) {
    RecipeRegistry reg;
    ASSERT_EQ(reg.addShapeless({Ingredient::of(items::GoldAxe).damaging(1), Ingredient::of(kOakLog)},
                               {kPlanks, 6}),
              RecipeError::None);
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 0, 0, items::GoldAxe, 1, maxDurability(items::GoldAxe));
    put(g, 1, 0, kOakLog, 1);
    ASSERT_TRUE(reg.craft(g).has_value());
    EXPECT_TRUE(g.get(0, 0).empty());
    EXPECT_TRUE(g.isEmpty());
}

TEST(RecipeRegistry, FreshToolLastsMaxDurabilityPlusOneCrafts) {
    RecipeRegistry reg;
    ASSERT_EQ(reg.addShapeless({Ingredient::of(items::GoldAxe).damaging(1), Ingredient::of(kOakLog)},
                               {kPlanks, 6}),
              RecipeError::None);
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 0, 0, items::GoldAxe);
    int crafts = 0;
    while (!g.get(0, 0).empty()) {
        put(g, 1, 0, kOakLog, 1);
        ASSERT_TRUE(reg.craft(g).has_value());
        ++crafts;
        ASSERT_LE(crafts, 1000);
    }
    EXPECT_EQ(crafts, maxDurability(items::GoldAxe) + 1);
}

TEST(RecipeRegistry, DamagingNonDamageableItemIsConsumed) {
    RecipeRegistry reg;
    ASSERT_EQ(reg.addShapeless({Ingredient::of(kSand).damaging(1)}, {kGlass, 1}), RecipeError::None);
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 0, 0, kSand, 2);
    ASSERT_TRUE(reg.craft(g).has_value());
    EXPECT_EQ(g.get(0, 0).count, 1);
    EXPECT_EQ(g.get(0, 0).damage, 0);
}

TEST(RecipeRegistry, MirroredCraftAppliesPerCellIngredientEffects) {
    // Asymmetric recipe whose left cell leaves a by-product: the mirrored layout
    // must put the remainder in the (mirrored) right-hand slot.
    RecipeRegistry reg;
    ASSERT_EQ(reg.addShaped({"MS"},
                            {{'M', Ingredient::of(items::MilkBucket).withRemainder(items::Bucket)},
                             {'S', Ingredient::of(items::Sugar)}},
                            {items::Cake, 1}),
              RecipeError::None);
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 0, 1, items::Sugar, 4);
    put(g, 1, 1, items::MilkBucket);
    const auto out = reg.craft(g);
    ASSERT_TRUE(out.has_value());
    EXPECT_EQ(g.get(1, 1).id, items::Bucket);
    EXPECT_EQ(g.get(0, 1).count, 3);
}

TEST(RecipeRegistry, HashedIndexNarrowsCandidates) {
    RecipeRegistry reg;
    // 30 single-cell shaped recipes + one 2x2: a 2x2 layout must probe one candidate only.
    for (ItemId id = 200; id < 230; ++id) {
        ASSERT_EQ(reg.addShaped({"#"}, {{'#', Ingredient::of(id)}}, {kGlass, 1}), RecipeError::None);
    }
    ASSERT_EQ(reg.addShaped({"##", "##"}, {{'#', Ingredient::of(kPlanks)}}, {kTable, 1}), RecipeError::None);
    const CraftingGrid square = gridOf(GridSize::Inventory2x2, {"##", "##"}, {{'#', kPlanks}});
    EXPECT_EQ(reg.candidateCount(square), 1u);
    CraftingGrid single{GridSize::Inventory2x2};
    put(single, 0, 0, 215);
    EXPECT_EQ(reg.candidateCount(single), 30u);
    EXPECT_EQ(reg.match(single)->recipeIndex, 15u);
    EXPECT_EQ(reg.candidateCount(CraftingGrid{GridSize::Workbench3x3}), 0u);
}

TEST(RecipeRegistry, HashedIndexAgreesWithLinearScanOnRandomRecipes) {
    // Property test: the indexed lookup must return exactly the first recipe a
    // naive linear scan over every registered recipe would return.
    std::mt19937 rng{1234u};
    const std::array<ItemId, 3> palette{kPlanks, kCobble, kSand};
    std::uniform_int_distribution<int> dim(1, 3);
    std::uniform_int_distribution<int> cellPick(0, 3); // 0 = hole
    std::uniform_int_distribution<int> coin(0, 1);

    RecipeRegistry reg;
    std::vector<std::string> storage;
    for (int n = 0; n < 120; ++n) {
        if (coin(rng) == 0) {
            const int w = dim(rng), h = dim(rng);
            std::vector<std::string> rows(static_cast<size_t>(h), std::string(static_cast<size_t>(w), ' '));
            for (auto& row : rows) {
                for (char& c : row) {
                    const int p = cellPick(rng);
                    c = p == 0 ? ' ' : static_cast<char>('a' + p - 1);
                }
            }
            std::vector<std::string_view> views(rows.begin(), rows.end());
            const std::vector<PatternKey> keys{{'a', Ingredient::of(palette[0])},
                                               {'b', Ingredient::of(palette[1])},
                                               {'c', Ingredient::of(palette[2])}};
            (void)reg.addShaped(std::span{views}, std::span{keys}, {kGlass, 1}, coin(rng) == 0);
        } else {
            std::vector<Ingredient> ings(static_cast<size_t>(dim(rng)));
            for (auto& ing : ings) {
                ing = Ingredient::of(palette[static_cast<size_t>(cellPick(rng) % 3)]);
            }
            (void)reg.addShapeless(std::span{ings}, {kGlass, 1});
        }
    }
    ASSERT_GT(reg.size(), 60u);

    int matched = 0;
    for (int iter = 0; iter < 3000; ++iter) {
        CraftingGrid g{coin(rng) == 0 ? GridSize::Inventory2x2 : GridSize::Workbench3x3};
        for (int y = 0; y < g.height(); ++y) {
            for (int x = 0; x < g.width(); ++x) {
                const int p = cellPick(rng);
                if (p != 0 && coin(rng) == 0) {
                    put(g, x, y, palette[static_cast<size_t>(p - 1)]);
                }
            }
        }
        std::optional<size_t> expected;
        for (size_t i = 0; i < reg.size() && !expected; ++i) {
            const bool hit = reg.kind(i) == RecipeKind::Shaped ? reg.shaped(i)->match(g).has_value()
                                                               : reg.shapeless(i)->match(g).has_value();
            if (hit) {
                expected = i;
            }
        }
        const auto got = reg.match(g);
        ASSERT_EQ(got.has_value(), expected.has_value()) << "iteration " << iter;
        if (got) {
            ++matched;
            ASSERT_EQ(got->recipeIndex, *expected) << "iteration " << iter;
        }
    }
    EXPECT_GT(matched, 100); // the generator must actually exercise hits
}

TEST(RecipeRegistry, LargeDamagePerCraftDoesNotOverflow) {
    RecipeRegistry reg;
    ASSERT_EQ(reg.addShapeless({Ingredient::of(items::DiamondAxe).damaging(65535), Ingredient::of(kOakLog)},
                               {kPlanks, 6}),
              RecipeError::None);
    CraftingGrid g{GridSize::Inventory2x2};
    put(g, 0, 0, items::DiamondAxe, 1, 65000);
    put(g, 1, 0, kOakLog, 1);
    ASSERT_TRUE(reg.craft(g).has_value());
    EXPECT_TRUE(g.get(0, 0).empty()); // broken, not wrapped around to a fresh tool
}

TEST(RecipeRegistry, ShapedDamagingToolKeepsPositionUnderTranslation) {
    RecipeRegistry reg;
    ASSERT_EQ(reg.addShaped({"A", "L"},
                            {{'A', Ingredient::of(items::StoneAxe).damaging(2)}, {'L', Ingredient::of(kOakLog)}},
                            {kPlanks, 6}),
              RecipeError::None);
    CraftingGrid g{GridSize::Workbench3x3};
    put(g, 2, 1, items::StoneAxe);
    put(g, 2, 2, kOakLog, 3);
    ASSERT_TRUE(reg.craft(g).has_value());
    EXPECT_EQ(g.get(2, 1).id, items::StoneAxe);
    EXPECT_EQ(g.get(2, 1).damage, 2);
    EXPECT_EQ(g.get(2, 2).count, 2);
    EXPECT_EQ(g.occupiedCount(), 2u);
}

TEST(RecipeRegistry, CandidateCountOfShapelessBucketIsByOccupiedSlots) {
    RecipeRegistry reg;
    ASSERT_EQ(reg.addShapeless({Ingredient::of(kSand), Ingredient::of(kSand)}, {kGlass, 1}), RecipeError::None);
    ASSERT_EQ(reg.addShapeless({Ingredient::of(kSand)}, {kGlass, 1}), RecipeError::None);
    CraftingGrid g{GridSize::Workbench3x3};
    put(g, 0, 0, kSand);
    put(g, 2, 2, kSand);
    EXPECT_EQ(reg.candidateCount(g), 1u);
    EXPECT_EQ(reg.match(g)->recipeIndex, 0u);
}
