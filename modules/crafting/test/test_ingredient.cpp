// Ingredient predicate (alternatives, by-products, durability use).
#include "crafting/Ingredient.hpp"

#include <gtest/gtest.h>

using namespace betacraft::crafting;
using betacraft::voxel::BlockId;

namespace {
constexpr ItemId kPlanks = toItemId(BlockId::OakPlanks);
constexpr ItemId kCobble = toItemId(BlockId::Cobblestone);
constexpr ItemId kOakLog = toItemId(BlockId::OakLog);
constexpr ItemId kBirchLog = toItemId(BlockId::BirchLog);
} // namespace

TEST(Ingredient, DefaultIsInvalidAndMatchesNothing) {
    constexpr Ingredient ing{};
    EXPECT_FALSE(ing.valid());
    EXPECT_FALSE(ing.matches(kPlanks));
    EXPECT_FALSE(ing.matches(kAir));
}

TEST(Ingredient, SingleItemMatchesOnlyThatItem) {
    constexpr Ingredient ing = Ingredient::of(kPlanks);
    EXPECT_TRUE(ing.valid());
    EXPECT_TRUE(ing.matches(kPlanks));
    EXPECT_FALSE(ing.matches(kCobble));
    EXPECT_FALSE(ing.matches(kAir));
}

TEST(Ingredient, OfAirIsInvalid) {
    EXPECT_FALSE(Ingredient::of(kAir).valid());
}

TEST(Ingredient, AnyOfMatchesEachAlternative) {
    const Ingredient ing = Ingredient::anyOf({kOakLog, kBirchLog});
    EXPECT_TRUE(ing.valid());
    EXPECT_TRUE(ing.matches(kOakLog));
    EXPECT_TRUE(ing.matches(kBirchLog));
    EXPECT_FALSE(ing.matches(kPlanks));
}

TEST(Ingredient, AnyOfBeyondCapacityIsRejected) {
    const Ingredient ing = Ingredient::anyOf({1, 2, 3, 4, 5});
    EXPECT_FALSE(ing.valid());
}

TEST(Ingredient, AnyOfWithAirOrEmptyListIsRejected) {
    EXPECT_FALSE(Ingredient::anyOf({}).valid());
    EXPECT_FALSE(Ingredient::anyOf({kPlanks, kAir}).valid());
}

TEST(Ingredient, AnyOfAtCapacityIsAccepted) {
    const Ingredient ing = Ingredient::anyOf({1, 2, 3, 4});
    EXPECT_TRUE(ing.valid());
    EXPECT_TRUE(ing.matches(4));
}

TEST(Ingredient, MatchesVanillaAliasesThroughCanonicalisation) {
    const Ingredient stick = Ingredient::of(items::Stick);
    EXPECT_TRUE(stick.matches(items::beta::Stick));
    const Ingredient vanillaStick = Ingredient::of(items::beta::Stick);
    EXPECT_TRUE(vanillaStick.matches(items::Stick));
}

TEST(Ingredient, MatchesSlotStackOnlyWhenNonEmpty) {
    const Ingredient ing = Ingredient::of(kPlanks);
    EXPECT_TRUE(ing.matches(SlotStack{kPlanks, 3, 0}));
    EXPECT_FALSE(ing.matches(SlotStack{kPlanks, 0, 0}));
}

TEST(Ingredient, RemainderAndDamageDecorators) {
    const Ingredient milk = Ingredient::of(items::MilkBucket).withRemainder(items::Bucket);
    EXPECT_EQ(milk.remainder, items::Bucket);
    EXPECT_EQ(milk.damagePerCraft, 0);
    EXPECT_TRUE(milk.matches(items::MilkBucket));

    const Ingredient tool = Ingredient::of(items::IronAxe).damaging(3);
    EXPECT_EQ(tool.damagePerCraft, 3);
    EXPECT_EQ(tool.remainder, kAir);
}

// Ingredient is a public aggregate: one built field by field (bypassing anyOf)
// with vanilla numbers must still match both numberings.
TEST(Ingredient, HandBuiltIngredientWithVanillaIdMatchesBothNumberings) {
    Ingredient ing{};
    ing.accepted[0] = items::beta::Stick;
    ing.acceptedCount = 1;
    ASSERT_TRUE(ing.valid());
    EXPECT_TRUE(ing.matches(items::beta::Stick));
    EXPECT_TRUE(ing.matches(items::Stick));
    EXPECT_FALSE(ing.matches(items::Coal));
}

TEST(Ingredient, HandBuiltIngredientIgnoresSlotsBeyondAcceptedCount) {
    Ingredient ing{};
    ing.accepted = {kPlanks, kCobble, kOakLog, kBirchLog};
    ing.acceptedCount = 2;
    EXPECT_TRUE(ing.matches(kCobble));
    EXPECT_FALSE(ing.matches(kOakLog));
}

TEST(Ingredient, HandBuiltAcceptedCountAboveCapacityIsInvalidAndSafe) {
    Ingredient ing{};
    ing.accepted = {kPlanks, kCobble, kOakLog, kBirchLog};
    ing.acceptedCount = 200;
    EXPECT_FALSE(ing.valid());
    EXPECT_FALSE(ing.matches(kPlanks));
}
