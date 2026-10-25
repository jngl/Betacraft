// Beta 1.7.3 armor (ItemArmor values, InventoryPlayer.getTotalArmorValue / damageArmor,
// EntityPlayer.damageEntity), plus the armor slots of the inventory screen.
#include "game/InventoryCrafting.hpp"
#include "gameplay/Armor.hpp"
#include "gameplay/Health.hpp"
#include "gameplay/ItemIds.hpp"
#include "gameplay/ItemStack.hpp"

#include <gtest/gtest.h>

#include <array>

using namespace betacraft;
using gameplay::ArmorMaterial;
using gameplay::ArmorSlot;
using gameplay::ItemStack;

namespace {

using Set = std::array<ItemStack, gameplay::kArmorSlotCount>;

[[nodiscard]] Set fullSet(ArmorMaterial material, int32_t damage = 0) {
    Set set{};
    for (size_t i = 0; i < set.size(); ++i) {
        set[i] = gameplay::makeArmorStack(gameplay::armorItemId(material, static_cast<ArmorSlot>(i)), damage);
    }
    return set;
}

[[nodiscard]] int32_t maxDamage(ArmorMaterial material, ArmorSlot slot) {
    return gameplay::armorInfo(gameplay::armorItemId(material, slot))->maxDamage;
}

} // namespace

TEST(Armor, ItemIdsFollowBetaNumbering) {
    EXPECT_EQ(gameplay::armorItemId(ArmorMaterial::Leather, ArmorSlot::Helmet), 298);
    EXPECT_EQ(gameplay::armorItemId(ArmorMaterial::Leather, ArmorSlot::Boots), 301);
    EXPECT_EQ(gameplay::armorItemId(ArmorMaterial::Chain, ArmorSlot::Helmet), 302);
    EXPECT_EQ(gameplay::armorItemId(ArmorMaterial::Iron, ArmorSlot::Chestplate), 307);
    EXPECT_EQ(gameplay::armorItemId(ArmorMaterial::Diamond, ArmorSlot::Leggings), 312);
    EXPECT_EQ(gameplay::armorItemId(ArmorMaterial::Gold, ArmorSlot::Boots), 317);
    EXPECT_EQ(gameplay::armorInfo(309)->slot, ArmorSlot::Boots);
    EXPECT_FALSE(gameplay::armorInfo(297).has_value());
    EXPECT_FALSE(gameplay::armorInfo(318).has_value());
    EXPECT_FALSE(gameplay::armorInfo(gameplay::items::IronIngot).has_value());
    EXPECT_FALSE(gameplay::armorInfo(gameplay::kNoItem).has_value());
    for (gameplay::ItemId id = gameplay::kFirstArmorItem; id <= gameplay::kLastArmorItem; ++id) {
        EXPECT_EQ(gameplay::maxStackSize(id), 1) << id;
        EXPECT_FALSE(gameplay::armorName(id).empty()) << id;
    }
    EXPECT_EQ(gameplay::armorName(298), "Leather Cap");
    EXPECT_EQ(gameplay::armorName(311), "Diamond Chestplate");
}

TEST(Armor, ReductionIsTheSameForEveryMaterial) {
    for (const ArmorMaterial m : {ArmorMaterial::Leather, ArmorMaterial::Chain, ArmorMaterial::Iron,
                                  ArmorMaterial::Diamond, ArmorMaterial::Gold}) {
        EXPECT_EQ(gameplay::armorInfo(gameplay::armorItemId(m, ArmorSlot::Helmet))->reduce, 3);
        EXPECT_EQ(gameplay::armorInfo(gameplay::armorItemId(m, ArmorSlot::Chestplate))->reduce, 8);
        EXPECT_EQ(gameplay::armorInfo(gameplay::armorItemId(m, ArmorSlot::Leggings))->reduce, 6);
        EXPECT_EQ(gameplay::armorInfo(gameplay::armorItemId(m, ArmorSlot::Boots))->reduce, 3);
    }
}

// Beta ItemArmor: maxDamageArray[type] * 3 << material, material leather 0, chain 1, iron 2,
// diamond 3, gold 1. (363/528/495/429 for diamond are the 1.0 values; Beta 1.7.3
// gives 264/384/360/312.)
TEST(Armor, DurabilitiesFollowBetaFormula) {
    const std::array<int32_t, 4> leather{33, 48, 45, 39};
    const std::array<int32_t, 4> chainAndGold{66, 96, 90, 78};
    const std::array<int32_t, 4> iron{132, 192, 180, 156};
    const std::array<int32_t, 4> diamond{264, 384, 360, 312};
    for (size_t i = 0; i < 4; ++i) {
        const auto slot = static_cast<ArmorSlot>(i);
        EXPECT_EQ(maxDamage(ArmorMaterial::Leather, slot), leather[i]);
        EXPECT_EQ(maxDamage(ArmorMaterial::Chain, slot), chainAndGold[i]);
        EXPECT_EQ(maxDamage(ArmorMaterial::Gold, slot), chainAndGold[i]);
        EXPECT_EQ(maxDamage(ArmorMaterial::Iron, slot), iron[i]);
        EXPECT_EQ(maxDamage(ArmorMaterial::Diamond, slot), diamond[i]);
    }
    // Stacks built anywhere carry the durability (HUD bar, wear).
    EXPECT_EQ(gameplay::makeStack(310, 1).maxDamage, 264);
}

TEST(Armor, FullDiamondSetIsWorthTwentyAndTurnsTenDamageIntoTwo) {
    const Set set = fullSet(ArmorMaterial::Diamond);
    const int32_t armor = gameplay::totalArmorValue(set);
    EXPECT_EQ(armor, 20);
    int32_t remainder = 0;
    EXPECT_EQ(gameplay::absorbDamage(10, armor, remainder), 2);
    EXPECT_EQ(remainder, 0);
    // A full leather set protects as much while it lasts (Beta: only durability differs).
    EXPECT_EQ(gameplay::totalArmorValue(fullSet(ArmorMaterial::Leather)), 20);
}

TEST(Armor, NoArmorAbsorbsNothing) {
    const Set none{};
    EXPECT_EQ(gameplay::totalArmorValue(none), 0);
    int32_t remainder = 0;
    for (int32_t dmg = 1; dmg <= 20; ++dmg) {
        EXPECT_EQ(gameplay::absorbDamage(dmg, 0, remainder), dmg);
        EXPECT_EQ(remainder, 0);
    }
    EXPECT_EQ(gameplay::absorbDamage(0, 20, remainder), 0);
}

TEST(Armor, PartialSetsAddUp) {
    Set set{};
    set[1] = gameplay::makeArmorStack(gameplay::armorItemId(ArmorMaterial::Iron, ArmorSlot::Chestplate));
    EXPECT_EQ(gameplay::totalArmorValue(set), 8); // (8 - 1) x 1 + 1
    set[0] = gameplay::makeArmorStack(gameplay::armorItemId(ArmorMaterial::Iron, ArmorSlot::Helmet));
    EXPECT_EQ(gameplay::totalArmorValue(set), 11);
}

TEST(Armor, RemainderCarriesFromOneHitToTheNext) {
    int32_t remainder = 0;
    // 3 x 5 = 15 / 25 -> 0, 15 left; then 15 + 15 = 30 -> 1, 5 left; then 5 + 15 = 20 -> 0.
    EXPECT_EQ(gameplay::absorbDamage(3, 20, remainder), 0);
    EXPECT_EQ(remainder, 15);
    EXPECT_EQ(gameplay::absorbDamage(3, 20, remainder), 1);
    EXPECT_EQ(remainder, 5);
    EXPECT_EQ(gameplay::absorbDamage(3, 20, remainder), 0);
    EXPECT_EQ(remainder, 20);
    EXPECT_EQ(gameplay::absorbDamage(3, 20, remainder), 1);
    EXPECT_EQ(remainder, 10);
}

TEST(Armor, HalfWornSetIsWorthAboutHalf) {
    Set set = fullSet(ArmorMaterial::Iron);
    for (ItemStack& piece : set) piece.damage = piece.maxDamage / 2;
    const int32_t armor = gameplay::totalArmorValue(set);
    EXPECT_GE(armor, 9);
    EXPECT_LE(armor, 11);
    // Worn down completely (damage == maxDamage, one use left) a piece is worth the minimum.
    for (ItemStack& piece : set) piece.damage = piece.maxDamage;
    EXPECT_EQ(gameplay::totalArmorValue(set), 1);
}

TEST(Armor, EveryPieceTakesTheFullDamageAsWear) {
    Set set = fullSet(ArmorMaterial::Diamond);
    gameplay::damageArmor(set, 7);
    for (const ItemStack& piece : set) {
        EXPECT_EQ(piece.damage, 7);
        EXPECT_EQ(piece.count, 1);
    }
    gameplay::damageArmor(set, 0);
    for (const ItemStack& piece : set) EXPECT_EQ(piece.damage, 7);
}

TEST(Armor, AFullyWornPieceBreaksAndItsSlotEmpties) {
    Set set = fullSet(ArmorMaterial::Leather);
    set[3].damage = set[3].maxDamage; // Boots: one use left
    set[0] = {};                      // No helmet
    gameplay::damageArmor(set, 1);
    EXPECT_TRUE(set[0].empty());
    EXPECT_TRUE(set[3].empty()) << "worn past maxDamage: the boots are gone";
    EXPECT_EQ(set[1].damage, 1);
    EXPECT_EQ(set[2].damage, 1);
    EXPECT_EQ(gameplay::totalArmorValue(set), 13); // (8 + 6 - 1) x 91 / 93 + 1
}

TEST(Armor, DamageEntityRulesThroughTheHurtWindow) {
    // The window works on the raw damage, armor on the part that lands.
    gameplay::Health health = gameplay::makeHealth(20);
    int32_t remainder = 0;
    const auto reduce = [&](int32_t landing) { return gameplay::absorbDamage(landing, 20, remainder); };
    gameplay::HitResult hit = gameplay::applyDamage(health, 10, reduce);
    EXPECT_EQ(hit.outcome, gameplay::HitOutcome::Full);
    EXPECT_EQ(hit.applied, 2);
    EXPECT_EQ(health.lastDamage, 10);
    hit = gameplay::applyDamage(health, 10, reduce);
    EXPECT_EQ(hit.outcome, gameplay::HitOutcome::Absorbed);
    hit = gameplay::applyDamage(health, 15, reduce); // Excess 5 -> 5 x 5 / 25 = 1
    EXPECT_EQ(hit.outcome, gameplay::HitOutcome::Partial);
    EXPECT_EQ(hit.applied, 1);
    EXPECT_EQ(health.current, 17);
}

// --- Inventory screen armor slots ---------------------------------------------------------

TEST(ArmorSlots, OnlyTheMatchingPieceGoesIn) {
    game::InventoryCrafting screen;
    physics::Player player;
    const gameplay::ItemId helmet = gameplay::armorItemId(ArmorMaterial::Iron, ArmorSlot::Helmet);
    const gameplay::ItemId boots = gameplay::armorItemId(ArmorMaterial::Iron, ArmorSlot::Boots);
    player.setInventorySlot(0, {helmet, 1});
    player.setInventorySlot(1, {gameplay::items::IronIngot, 5});

    ASSERT_TRUE(screen.clickInventorySlot(player, 0));
    EXPECT_FALSE(screen.clickArmorSlot(player, static_cast<size_t>(ArmorSlot::Boots))) << "a helmet is no boots";
    ASSERT_TRUE(screen.clickArmorSlot(player, static_cast<size_t>(ArmorSlot::Helmet)));
    EXPECT_EQ(player.getArmorSlot(0).id, helmet);
    EXPECT_TRUE(screen.carried().isEmpty());

    ASSERT_TRUE(screen.clickInventorySlot(player, 1));
    EXPECT_FALSE(screen.clickArmorSlot(player, 3)) << "ingots cannot be worn";
    ASSERT_TRUE(screen.clickInventorySlot(player, 1)); // Put them back

    // A second helmet swaps with the worn one.
    player.setInventorySlot(2, {helmet, 1, 17});
    ASSERT_TRUE(screen.clickInventorySlot(player, 2));
    ASSERT_TRUE(screen.clickArmorSlot(player, 0));
    EXPECT_EQ(player.getArmorSlot(0).damage, 17);
    EXPECT_EQ(screen.carried().id, helmet);
    EXPECT_EQ(screen.carried().damage, 0);
    // Picking a worn piece up with an empty cursor.
    ASSERT_TRUE(screen.clickInventorySlot(player, 2));
    ASSERT_TRUE(screen.clickArmorSlot(player, 0));
    EXPECT_TRUE(player.getArmorSlot(0).isEmpty());
    EXPECT_EQ(screen.carried().damage, 17);
    (void)boots;
}

TEST(ArmorSlots, ShiftClickEquipsAndUnequips) {
    game::InventoryCrafting screen;
    physics::Player player;
    const gameplay::ItemId plate = gameplay::armorItemId(ArmorMaterial::Diamond, ArmorSlot::Chestplate);
    player.setInventorySlot(12, {plate, 1, 3});
    ASSERT_TRUE(screen.shiftClickInventorySlot(player, nullptr, 12));
    EXPECT_EQ(player.getArmorSlot(1), (physics::InventorySlot{plate, 1, 3}));
    EXPECT_TRUE(player.getInventorySlot(12).isEmpty());
    EXPECT_EQ(player.armorValue(), 7); // (8 - 1) x 381 / 384 + 1: slightly worn

    // The slot is taken: a second chestplate moves like any other stack (main -> hotbar).
    player.setInventorySlot(12, {plate, 1});
    ASSERT_TRUE(screen.shiftClickInventorySlot(player, nullptr, 12));
    EXPECT_EQ(player.getHotbarSlot(0).id, plate);

    ASSERT_TRUE(screen.shiftClickArmorSlot(player, 1));
    EXPECT_TRUE(player.getArmorSlot(1).isEmpty());
    EXPECT_EQ(player.countItem(plate), 2);
    EXPECT_FALSE(screen.shiftClickArmorSlot(player, 1));
}

TEST(ArmorSlots, WorkbenchShiftClickDoesNotEquip) {
    game::InventoryCrafting screen;
    physics::Player player;
    (void)screen.open(crafting::GridSize::Workbench3x3, player);
    const gameplay::ItemId boots = gameplay::armorItemId(ArmorMaterial::Leather, ArmorSlot::Boots);
    player.setInventorySlot(20, {boots, 1});
    ASSERT_TRUE(screen.shiftClickInventorySlot(player, nullptr, 20));
    EXPECT_TRUE(player.getArmorSlot(3).isEmpty());
    EXPECT_EQ(player.getHotbarSlot(0).id, boots);
}
