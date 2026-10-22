#include "gameplay/DeathDrops.hpp"

#include <gtest/gtest.h>

#include <array>
#include <cmath>

using namespace betacraft::gameplay;
using betacraft::voxel::BlockId;

namespace {
const glm::vec3 kEye{10.0f, 65.62f, -4.0f};

std::array<ItemStack, 6> sampleInventory() {
    ItemStack pick = makeStack(toolItemId(ToolType::Pickaxe, ToolMaterial::Iron), 1);
    pick.damage = 42;
    return {makeStack(toItemId(BlockId::Dirt), 64), ItemStack{}, pick,
            makeStack(toItemId(BlockId::Torch), 12), ItemStack{}, makeStack(toItemId(BlockId::Stick), 3)};
}
} // namespace

TEST(DeathDrops, EmptyInventoryDropsNothing) {
    std::array<ItemStack, 4> inv{};
    EXPECT_TRUE(scatterInventoryOnDeath(inv, kEye, 1u).empty());
}

TEST(DeathDrops, DropsEveryNonEmptySlotAndClearsInventory) {
    auto inv = sampleInventory();
    const auto drops = scatterInventoryOnDeath(inv, kEye, 7u);
    ASSERT_EQ(drops.size(), 4u);
    EXPECT_EQ(drops[0].stack, makeStack(toItemId(BlockId::Dirt), 64));
    EXPECT_EQ(drops[1].stack.id, toolItemId(ToolType::Pickaxe, ToolMaterial::Iron));
    EXPECT_EQ(drops[1].stack.damage, 42) << "tool wear survives death";
    EXPECT_EQ(drops[2].stack.count, 12);
    EXPECT_EQ(drops[3].stack.count, 3);
    for (const ItemStack& slot : inv) {
        EXPECT_TRUE(slot.empty());
        EXPECT_EQ(slot, ItemStack{});
    }
}

TEST(DeathDrops, SpawnPositionAndPickupDelay) {
    auto inv = sampleInventory();
    for (const DroppedItem& d : scatterInventoryOnDeath(inv, kEye, 3u)) {
        EXPECT_NEAR(d.position.x, kEye.x, 1e-5f);
        EXPECT_NEAR(d.position.y, kEye.y - 0.3f, 1e-5f);
        EXPECT_NEAR(d.position.z, kEye.z, 1e-5f);
        EXPECT_EQ(d.pickupDelayTicks, kDeathDropPickupDelayTicks);
    }
    EXPECT_EQ(kDeathDropPickupDelayTicks, 40);
}

TEST(DeathDrops, VelocityEnvelope) {
    // Beta: horizontal speed rand[0, 0.5) blocks/tick (<10 m/s), vertical 0.2 b/t (4 m/s).
    for (uint64_t seed = 0; seed < 200; ++seed) {
        auto inv = sampleInventory();
        for (const DroppedItem& d : scatterInventoryOnDeath(inv, kEye, seed)) {
            const float h = std::sqrt(d.velocity.x * d.velocity.x + d.velocity.z * d.velocity.z);
            ASSERT_GE(h, 0.0f);
            ASSERT_LT(h, 10.0f + 1e-4f);
            ASSERT_NEAR(d.velocity.y, 4.0f, 1e-5f);
        }
    }
}

TEST(DeathDrops, DeterministicForSeed) {
    auto a = sampleInventory();
    auto b = sampleInventory();
    const auto da = scatterInventoryOnDeath(a, kEye, 0xBEEFu);
    const auto db = scatterInventoryOnDeath(b, kEye, 0xBEEFu);
    ASSERT_EQ(da.size(), db.size());
    for (size_t i = 0; i < da.size(); ++i) {
        EXPECT_EQ(da[i].velocity, db[i].velocity);
    }
}

TEST(DeathDrops, DifferentSeedsScatterDifferently) {
    auto a = sampleInventory();
    auto b = sampleInventory();
    const auto da = scatterInventoryOnDeath(a, kEye, 1u);
    const auto db = scatterInventoryOnDeath(b, kEye, 2u);
    bool anyDifferent = false;
    for (size_t i = 0; i < da.size(); ++i) {
        anyDifferent = anyDifferent || !(da[i].velocity == db[i].velocity);
    }
    EXPECT_TRUE(anyDifferent);
}

TEST(DeathDrops, DirectionsCoverAllQuadrants) {
    // Statistical sanity check of the angle distribution with a fixed seed.
    std::array<int, 4> quadrants{};
    std::array<ItemStack, 64> inv{};
    for (auto& s : inv) {
        s = makeStack(toItemId(BlockId::Dirt), 1);
    }
    for (const DroppedItem& d : scatterInventoryOnDeath(inv, kEye, 99u)) {
        const int q = (d.velocity.x >= 0.0f ? 0 : 1) + (d.velocity.z >= 0.0f ? 0 : 2);
        ++quadrants[static_cast<size_t>(q)];
    }
    for (int count : quadrants) {
        EXPECT_GE(count, 6);
    }
}
