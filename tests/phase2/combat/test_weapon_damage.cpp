#include "gameplay/Combat.hpp"

#include <gtest/gtest.h>

using namespace betacraft::gameplay;
using betacraft::world::BlockId;

namespace {
ItemStack tool(ToolType type, ToolMaterial mat) {
    return makeStack(toolItemId(type, mat), 1);
}
} // namespace

TEST(WeaponDamage, FistAndNonWeaponsDealOne) {
    EXPECT_EQ(weaponDamage(ItemStack{}), 1);
    EXPECT_EQ(weaponDamage(makeStack(toItemId(BlockId::Cobblestone), 64)), 1);
    EXPECT_EQ(weaponDamage(makeStack(toItemId(BlockId::Stick), 1)), 1);
}

TEST(WeaponDamage, SwordsBeta173) {
    // Beta 1.7.3 ItemSword: weaponDamage = 4 + material bonus * 2 (half-hearts).
    EXPECT_EQ(weaponDamage(tool(ToolType::Sword, ToolMaterial::Wood)), 4);
    EXPECT_EQ(weaponDamage(tool(ToolType::Sword, ToolMaterial::Gold)), 4);
    EXPECT_EQ(weaponDamage(tool(ToolType::Sword, ToolMaterial::Stone)), 6);
    EXPECT_EQ(weaponDamage(tool(ToolType::Sword, ToolMaterial::Iron)), 8);
    EXPECT_EQ(weaponDamage(tool(ToolType::Sword, ToolMaterial::Diamond)), 10);
}

TEST(WeaponDamage, DiggingToolsBeta173) {
    // ItemTool: base (shovel 1, pickaxe 2, axe 3) + material bonus.
    EXPECT_EQ(weaponDamage(tool(ToolType::Shovel, ToolMaterial::Wood)), 1);
    EXPECT_EQ(weaponDamage(tool(ToolType::Shovel, ToolMaterial::Diamond)), 4);
    EXPECT_EQ(weaponDamage(tool(ToolType::Pickaxe, ToolMaterial::Wood)), 2);
    EXPECT_EQ(weaponDamage(tool(ToolType::Pickaxe, ToolMaterial::Iron)), 4);
    EXPECT_EQ(weaponDamage(tool(ToolType::Pickaxe, ToolMaterial::Diamond)), 5);
    EXPECT_EQ(weaponDamage(tool(ToolType::Axe, ToolMaterial::Stone)), 4);
    EXPECT_EQ(weaponDamage(tool(ToolType::Axe, ToolMaterial::Gold)), 3);
    EXPECT_EQ(weaponDamage(tool(ToolType::Axe, ToolMaterial::Diamond)), 6);
}

TEST(WeaponDamage, HoesAreNotWeapons) {
    for (ToolMaterial m : {ToolMaterial::Wood, ToolMaterial::Stone, ToolMaterial::Iron,
                           ToolMaterial::Gold, ToolMaterial::Diamond}) {
        EXPECT_EQ(weaponDamage(tool(ToolType::Hoe, m)), 1);
    }
}

TEST(WeaponDamage, SwordAlwaysBeatsSameTierTools) {
    for (ToolMaterial m : {ToolMaterial::Wood, ToolMaterial::Stone, ToolMaterial::Iron,
                           ToolMaterial::Gold, ToolMaterial::Diamond}) {
        const int32_t sword = weaponDamage(tool(ToolType::Sword, m));
        for (ToolType t : {ToolType::Pickaxe, ToolType::Axe, ToolType::Shovel, ToolType::Hoe}) {
            EXPECT_GT(sword, weaponDamage(tool(t, m)));
        }
    }
}

TEST(WeaponDamage, FallingAttackerAddsOne) {
    // Beta 1.7.3 attackTargetEntityWithCurrentItem: if (motionY < 0) damage++ (pre-1.8 "crit").
    const ItemStack sword = tool(ToolType::Sword, ToolMaterial::Iron);
    EXPECT_EQ(attackDamage(sword, {.attackerFalling = false}), 8);
    EXPECT_EQ(attackDamage(sword, {.attackerFalling = true}), 9);
    EXPECT_EQ(attackDamage(ItemStack{}, {.attackerFalling = true}), 2);
}
