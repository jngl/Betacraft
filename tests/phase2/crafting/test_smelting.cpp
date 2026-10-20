// S2.2: Beta 1.7.3 FurnaceRecipes and TileEntityFurnace.getItemBurnTime.
#include "crafting/SmeltingRecipes.hpp"

#include <gtest/gtest.h>

using namespace betacraft::crafting;
using betacraft::voxel::BlockId;

TEST(Smelting, EveryBetaRecipeSmelts) {
    const SmeltingBook& book = SmeltingBook::beta();
    const struct {
        ItemId input;
        ItemId output;
    } expected[] = {
        {toItemId(BlockId::IronOre), items::IronIngot},
        {toItemId(BlockId::GoldOre), items::GoldIngot},
        {toItemId(BlockId::DiamondOre), items::Diamond},
        {toItemId(BlockId::Sand), toItemId(BlockId::Glass)},
        {toItemId(BlockId::Cobblestone), toItemId(BlockId::Stone)},
        {toItemId(BlockId::OakLog), items::Coal},
        {toItemId(BlockId::BirchLog), items::Coal},
        {toItemId(BlockId::PineLog), items::Coal},
        {items::RawPorkchop, items::CookedPorkchop},
    };
    for (const auto& e : expected) {
        const auto r = book.result(e.input);
        ASSERT_TRUE(r.has_value()) << e.input;
        EXPECT_EQ(r->id, e.output) << e.input;
        EXPECT_EQ(r->count, 1) << e.input;
        EXPECT_EQ(r->damage, 0) << e.input;
    }
    EXPECT_EQ(book.recipes().size(), std::size(expected));
}

TEST(Smelting, UnknownInputHasNoResult) {
    const SmeltingBook& book = SmeltingBook::beta();
    EXPECT_FALSE(book.result(kAir).has_value());
    EXPECT_FALSE(book.result(toItemId(BlockId::Stone)).has_value());
    EXPECT_FALSE(book.result(toItemId(BlockId::Dirt)).has_value());
    EXPECT_FALSE(book.result(items::IronIngot).has_value());
    EXPECT_FALSE(book.result(items::Coal).has_value());
    EXPECT_FALSE(book.result(9999).has_value());
}

TEST(Smelting, FuelTableMatchesBeta) {
    EXPECT_EQ(fuelBurnTicks(items::Coal), 1600);
    for (const BlockId wood : {BlockId::OakPlanks, BlockId::OakLog, BlockId::BirchLog, BlockId::PineLog,
                               BlockId::CraftingTable, BlockId::Bookshelf, BlockId::Chest}) {
        EXPECT_EQ(fuelBurnTicks(toItemId(wood)), 300) << static_cast<int>(wood);
    }
    EXPECT_EQ(fuelBurnTicks(items::Stick), 100);
    EXPECT_EQ(fuelBurnTicks(items::beta::Stick), 100);
    EXPECT_EQ(fuelBurnTicks(toItemId(BlockId::Sapling)), 100);
    EXPECT_EQ(fuelBurnTicks(items::LavaBucket), 20000);
    EXPECT_EQ(fuelBurnTicks(kAir), 0);
    EXPECT_EQ(fuelBurnTicks(toItemId(BlockId::Cobblestone)), 0);
    EXPECT_EQ(fuelBurnTicks(items::Bucket), 0);
    EXPECT_EQ(fuelBurnTicks(items::IronIngot), 0);
}

TEST(Smelting, VanillaCoalAliasBurns1600) {
    EXPECT_EQ(items::beta::Coal, 263);
    EXPECT_EQ(canonicalize(263), items::Coal);
    EXPECT_EQ(fuelBurnTicks(263), 1600);
}
