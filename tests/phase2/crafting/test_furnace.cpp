// S2.3: Beta 1.7.3 TileEntityFurnace.updateEntity rules on world::FurnaceData.
#include "crafting/Furnace.hpp"

#include <gtest/gtest.h>

using namespace betacraft::crafting;
using betacraft::world::BlockId;
using betacraft::world::FurnaceData;
using betacraft::world::StoredStack;

namespace {

[[nodiscard]] StoredStack stack(ItemId id, uint8_t count) {
    return StoredStack{id, count, 0};
}

const StoredStack kIronOre8 = stack(toItemId(BlockId::IronOre), 8);

} // namespace

TEST(Furnace, OneCoalSmeltsEightOres) {
    FurnaceData f{};
    f.input = kIronOre8;
    f.fuel = stack(items::Coal, 1);
    int smelted = 0;
    for (int t = 0; t < 1600; ++t) {
        smelted += tickFurnace(f, SmeltingBook::beta()).smelted ? 1 : 0;
    }
    EXPECT_EQ(smelted, 8);
    EXPECT_TRUE(f.input.empty());
    EXPECT_TRUE(f.fuel.empty());
    EXPECT_EQ(f.output, stack(items::IronIngot, 8));
    EXPECT_EQ(f.burnTicks, 1); // The last tick of the coal is still to burn
    const FurnaceTickResult last = tickFurnace(f, SmeltingBook::beta());
    EXPECT_TRUE(last.litChanged);
    EXPECT_EQ(f.burnTicks, 0);
    EXPECT_EQ(f.cookTicks, 0);
}

TEST(Furnace, CookTakesExactly200Ticks) {
    FurnaceData f{};
    f.input = stack(toItemId(BlockId::Sand), 2);
    f.fuel = stack(items::Coal, 1);
    for (int t = 1; t < kSmeltTicks; ++t) {
        ASSERT_FALSE(tickFurnace(f, SmeltingBook::beta()).smelted) << t;
        EXPECT_EQ(f.cookTicks, t);
    }
    EXPECT_NEAR(cookProgress(f), 199.0f / 200.0f, 1e-6f);
    const FurnaceTickResult r = tickFurnace(f, SmeltingBook::beta());
    EXPECT_TRUE(r.smelted);
    EXPECT_TRUE(r.changed);
    EXPECT_EQ(f.cookTicks, 0);
    EXPECT_EQ(f.output, stack(toItemId(BlockId::Glass), 1));
    EXPECT_EQ(f.input.count, 1);
}

TEST(Furnace, NoFuelConsumedWithoutSmeltableInput) {
    FurnaceData f{};
    f.fuel = stack(items::Coal, 3);
    for (int t = 0; t < 50; ++t) {
        const FurnaceTickResult r = tickFurnace(f, SmeltingBook::beta());
        EXPECT_FALSE(r.changed);
        EXPECT_FALSE(r.litChanged);
    }
    EXPECT_EQ(f.fuel.count, 3);
    EXPECT_EQ(f.burnTicks, 0);

    f.input = stack(toItemId(BlockId::Dirt), 5); // Does not smelt either
    (void)tickFurnace(f, SmeltingBook::beta());
    EXPECT_EQ(f.fuel.count, 3);
    EXPECT_EQ(f.burnTicks, 0);
    EXPECT_FLOAT_EQ(burnProgress(f), 0.0f);
}

TEST(Furnace, FullOrMismatchedOutputStallsAndResetsCook) {
    // Mismatched output: gold ingots block iron.
    FurnaceData f{};
    f.input = kIronOre8;
    f.fuel = stack(items::Coal, 1);
    f.output = stack(items::GoldIngot, 1);
    for (int t = 0; t < 300; ++t) {
        EXPECT_FALSE(tickFurnace(f, SmeltingBook::beta()).smelted);
    }
    EXPECT_EQ(f.cookTicks, 0);
    EXPECT_EQ(f.fuel.count, 1) << "a stalled furnace does not light";

    // Full output: 64 ingots.
    FurnaceData g{};
    g.input = kIronOre8;
    g.fuel = stack(items::Coal, 1);
    for (int t = 0; t < 150; ++t) {
        (void)tickFurnace(g, SmeltingBook::beta());
    }
    ASSERT_EQ(g.cookTicks, 150);
    g.output = stack(items::IronIngot, 64);
    (void)tickFurnace(g, SmeltingBook::beta());
    EXPECT_EQ(g.cookTicks, 0);
    EXPECT_GT(g.burnTicks, 0) << "the lit fuel keeps burning";
    g.output = stack(items::IronIngot, 63);
    (void)tickFurnace(g, SmeltingBook::beta());
    EXPECT_EQ(g.cookTicks, 1);
}

TEST(Furnace, LitFlagTogglesOnFirstAndLastBurnTick) {
    FurnaceData f{};
    f.input = stack(toItemId(BlockId::Cobblestone), 64);
    f.fuel = stack(items::Stick, 1); // 100 ticks
    const FurnaceTickResult first = tickFurnace(f, SmeltingBook::beta());
    EXPECT_TRUE(first.litChanged);
    EXPECT_EQ(f.burnTicks, 100);
    EXPECT_EQ(f.burnTicksTotal, 100);
    EXPECT_TRUE(f.fuel.empty());
    EXPECT_FLOAT_EQ(burnProgress(f), 1.0f);
    for (int t = 2; t <= 100; ++t) {
        EXPECT_FALSE(tickFurnace(f, SmeltingBook::beta()).litChanged) << t;
    }
    EXPECT_EQ(f.burnTicks, 1);
    const FurnaceTickResult last = tickFurnace(f, SmeltingBook::beta());
    EXPECT_TRUE(last.litChanged);
    EXPECT_EQ(f.burnTicks, 0);
    EXPECT_EQ(f.cookTicks, 0) << "100 ticks of stick never finish a 200-tick cook";
    EXPECT_FALSE(tickFurnace(f, SmeltingBook::beta()).litChanged);
}

TEST(Furnace, RemovingInputMidCookResetsProgressButKeepsBurning) {
    FurnaceData f{};
    f.input = kIronOre8;
    f.fuel = stack(items::Coal, 2);
    for (int t = 0; t < 120; ++t) {
        (void)tickFurnace(f, SmeltingBook::beta());
    }
    ASSERT_EQ(f.cookTicks, 120);
    const int16_t burnBefore = f.burnTicks;
    f.input = {};
    const FurnaceTickResult r = tickFurnace(f, SmeltingBook::beta());
    EXPECT_FALSE(r.litChanged);
    EXPECT_EQ(f.cookTicks, 0);
    EXPECT_EQ(f.burnTicks, burnBefore - 1);
    EXPECT_EQ(f.fuel.count, 1);
}
