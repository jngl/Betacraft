// Food (Beta 1.7.3 ItemFood / ItemSoup) is eaten at once with a right click through
// EntitySimulation::useHeldItem, even at full health, and heals through gameplay::heal.
#include "game/EntitySimulation.hpp"
#include "gameplay/Food.hpp"
#include "physics/Player.hpp"

#include <gtest/gtest.h>

using namespace betacraft;
using gameplay::foodInfo;
using physics::InventorySlot;
using physics::Player;
namespace items = gameplay::items;

namespace {

class FoodTest : public ::testing::Test {
protected:
    void SetUp() override {
        voxel::BlockRegistry::initialize();
        for (size_t i = 0; i < Player::TOTAL_INVENTORY_SIZE; ++i) m_player.setInventorySlot(i, {});
        m_player.setSelectedSlot(0);
    }

    Player m_player;
    game::EntitySimulation m_session{7ULL};
};

} // namespace

TEST(Food, BetaHealValues) {
    struct Case {
        gameplay::ItemId id;
        int32_t heal;
    };
    for (const Case c : {Case{items::Apple, 4}, Case{items::Bread, 5}, Case{items::RawPorkchop, 3},
                         Case{items::CookedPorkchop, 8}, Case{items::GoldenApple, 20}, Case{items::MushroomStew, 10}}) {
        const auto info = foodInfo(c.id);
        ASSERT_TRUE(info.has_value()) << c.id;
        EXPECT_EQ(info->heal, c.heal) << c.id;
        EXPECT_EQ(info->leftover, c.id == items::MushroomStew ? items::Bowl : gameplay::kNoItem) << c.id;
    }
    for (const gameplay::ItemId notFood : {gameplay::kNoItem, items::Bowl, items::Wheat, items::Stick,
                                           gameplay::toItemId(voxel::BlockId::Dirt), items::Cake}) {
        EXPECT_FALSE(foodInfo(notFood).has_value()) << notFood;
    }
}

TEST_F(FoodTest, BetaHealValuesThroughUseHeldItem) {
    m_player.setHealth(10);
    m_player.setHeldItem({items::Apple, 1});
    const auto healed = m_session.useHeldItem(m_player);
    ASSERT_TRUE(healed.has_value());
    EXPECT_EQ(*healed, 4);
    EXPECT_EQ(m_player.getHealth(), 14); // Playtest: 10 HP + an apple = 14 HP
    EXPECT_TRUE(m_player.getHeldItem().isEmpty());

    m_player.setHeldItem({gameplay::toItemId(voxel::BlockId::Dirt), 5});
    EXPECT_FALSE(m_session.useHeldItem(m_player).has_value());
    EXPECT_EQ(m_player.getHeldItem().count, 5);
}

TEST_F(FoodTest, StewLeavesABowl) {
    m_player.setHealth(5);
    m_player.setHeldItem({items::MushroomStew, 1});
    EXPECT_EQ(m_session.useHeldItem(m_player), std::optional<int32_t>{10});
    EXPECT_EQ(m_player.getHealth(), 15);
    EXPECT_EQ(m_player.getHeldItem(), InventorySlot(items::Bowl, 1));
}

TEST_F(FoodTest, EatingAtFullHealthStillConsumes) {
    ASSERT_EQ(m_player.getHealth(), gameplay::kPlayerMaxHealth);
    m_player.setHeldItem({items::Bread, 1});
    m_player.setInventorySlot(5, {items::Bread, 1});
    EXPECT_EQ(m_session.useHeldItem(m_player), std::optional<int32_t>{0});
    EXPECT_TRUE(m_player.getHeldItem().isEmpty()); // Beta eats it anyway
    EXPECT_EQ(m_player.getHealth(), gameplay::kPlayerMaxHealth);
    EXPECT_EQ(m_player.countItem(items::Bread), 1);

    m_player.setHealth(18); // Healing is capped at the maximum
    m_player.setHeldItem({items::CookedPorkchop, 1});
    EXPECT_EQ(m_session.useHeldItem(m_player), std::optional<int32_t>{2});
    EXPECT_EQ(m_player.getHealth(), gameplay::kPlayerMaxHealth);
}

TEST_F(FoodTest, DeadCannotEat) {
    m_player.setHealth(0);
    ASSERT_TRUE(m_player.isDead());
    m_player.setHeldItem({items::GoldenApple, 1});
    EXPECT_FALSE(m_session.useHeldItem(m_player).has_value());
    EXPECT_EQ(m_player.getHeldItem(), InventorySlot(items::GoldenApple, 1));
    EXPECT_EQ(m_player.getHealth(), 0);
}

// Leaves roll an apple 1 time in 200 on top of (and independently of) the sapling.
TEST(Food, LeavesDropAnAppleOneTimeIn200) {
    voxel::BlockRegistry::initialize();
    int apples = 0;
    int saplings = 0;
    int both = 0;
    constexpr int kRolls = 40000;
    for (int seed = 0; seed < kRolls; ++seed) {
        gameplay::ItemStack hand{};
        const gameplay::BlockBreakOutcome out =
            gameplay::breakBlockWith(voxel::BlockId::Leaves, hand, static_cast<uint64_t>(seed) * 0x9E3779B97F4A7C15ULL);
        ASSERT_TRUE(out.broken);
        const bool apple = out.bonusDrop.has_value();
        if (apple) {
            EXPECT_EQ(out.bonusDrop->id, items::Apple);
            EXPECT_EQ(out.bonusDrop->count, 1);
        }
        apples += apple ? 1 : 0;
        saplings += out.drop ? 1 : 0;
        both += apple && out.drop ? 1 : 0;
    }
    EXPECT_NEAR(apples, kRolls / 200, kRolls / 200 / 2);
    EXPECT_NEAR(saplings, kRolls / 20, kRolls / 20 / 4);
    EXPECT_LT(both, 30); // Independent rolls: ~10 expected
    gameplay::ItemStack hand{};
    EXPECT_FALSE(gameplay::breakBlockWith(voxel::BlockId::Stone, hand, 1).bonusDrop.has_value());
}
