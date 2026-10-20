// MiningProgress when the dig rate changes mid-dig (held item swapped, jumping, entering
// water). Beta PlayerControllerSP adds Block.blockStrength (a fraction) to curBlockDamage on
// every held tick, so progress already made is kept and only the remaining fraction is
// mined at the new rate.
#include "gameplay/Harvesting.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

using namespace betacraft::gameplay;
using betacraft::voxel::BlockId;
using betacraft::voxel::BlockRegistry;

namespace {
ItemStack tool(ToolType type, ToolMaterial mat) {
    return makeStack(toolItemId(type, mat), 1);
}
const ItemStack kHand{};
const betacraft::core::BlockPos kPos{3, 40, -7};
constexpr DigConditions kAirborne{.inWater = false, .onGround = false};

// Advances until the block breaks; returns the number of advances it took (capped).
int32_t ticksUntilBreak(MiningProgress& p, const DigResult& dig, int32_t cap = 100'000) {
    for (int32_t t = 1; t <= cap; ++t) {
        if (p.advance(kPos, dig)) {
            return t;
        }
    }
    return -1;
}

void advanceN(MiningProgress& p, const DigResult& dig, int32_t n) {
    for (int32_t i = 0; i < n; ++i) {
        ASSERT_FALSE(p.advance(kPos, dig)) << "unexpected break at tick " << i + 1;
    }
}
} // namespace

TEST(MiningRateChange, HandThenWoodPickaxeKeepsProgressFraction) {
    // Stone by hand: 1/150 per tick. 30 ticks = 0.2 done. Wooden pickaxe: 1/22.5 per tick, so
    // the remaining 0.8 needs ceil(0.8 * 22.5) = 18 more ticks (not an instant break).
    MiningProgress p;
    const DigResult hand = computeDig(BlockId::Stone, kHand);
    const DigResult wood = computeDig(BlockId::Stone, tool(ToolType::Pickaxe, ToolMaterial::Wood));
    ASSERT_EQ(hand.ticks, 150);
    ASSERT_EQ(wood.ticks, 23);
    advanceN(p, hand, 30);
    EXPECT_NEAR(p.fraction(), 0.2f, 1e-5f);
    EXPECT_EQ(ticksUntilBreak(p, wood), 18);
}

TEST(MiningRateChange, JumpingNearTheEndOnlyCostsTheRemainingFraction) {
    // Iron pickaxe on stone: 7.5 ticks on the ground, 37.5 airborne. After 7 ground ticks
    // 14/15 is done; the last 1/15 airborne takes 37.5 / 15 = 2.5 -> 3 ticks.
    MiningProgress p;
    const ItemStack iron = tool(ToolType::Pickaxe, ToolMaterial::Iron);
    const DigResult ground = computeDig(BlockId::Stone, iron);
    const DigResult air = computeDig(BlockId::Stone, iron, kAirborne);
    ASSERT_EQ(ground.ticks, 8);
    ASSERT_EQ(air.ticks, 38);
    advanceN(p, ground, 7);
    const int32_t stageBefore = p.crackStage();
    EXPECT_EQ(stageBefore, 9);
    EXPECT_FALSE(p.advance(kPos, air));
    EXPECT_GE(p.crackStage(), stageBefore) << "the crack overlay never moves backwards";
    EXPECT_FALSE(p.advance(kPos, air));
    EXPECT_TRUE(p.advance(kPos, air));
}

TEST(MiningRateChange, LandingAfterAirborneDigDoesNotBreakInstantly) {
    // Wooden pickaxe on stone: 112.5 ticks airborne, 22.5 on the ground. 20 airborne ticks
    // = 0.1778 done; the remaining 0.8222 * 22.5 = 18.5 -> 19 ground ticks.
    MiningProgress p;
    const ItemStack wood = tool(ToolType::Pickaxe, ToolMaterial::Wood);
    advanceN(p, computeDig(BlockId::Stone, wood, kAirborne), 20);
    EXPECT_EQ(ticksUntilBreak(p, computeDig(BlockId::Stone, wood)), 19);
}

TEST(MiningRateChange, SwappingToAFasterToolDoesNotSkipTheRemainder) {
    // Cobblestone by hand is unharvestable: 2 * 100 = 200 ticks. 10 ticks = 0.05 done. A gold
    // pickaxe (2 * 30 / 12 = 5 ticks) then needs 0.95 * 5 = 4.75 -> 5 ticks.
    MiningProgress p;
    advanceN(p, computeDig(BlockId::Cobblestone, kHand), 10);
    const DigResult gold = computeDig(BlockId::Cobblestone, tool(ToolType::Pickaxe, ToolMaterial::Gold));
    ASSERT_EQ(gold.ticks, 5);
    EXPECT_EQ(ticksUntilBreak(p, gold), 5);
}

TEST(MiningRateChange, SwappingToASlowerToolKeepsProgress) {
    // Diamond pickaxe on obsidian (Beta hardness 10): 10 * 30 / 1 = 300 ticks (no efficiency
    // bonus). 150 ticks = 0.5; a wooden pickaxe (unharvestable: 1000 ticks) then needs 500.
    MiningProgress p;
    const DigResult diamond = computeDig(BlockId::Obsidian, tool(ToolType::Pickaxe, ToolMaterial::Diamond));
    const DigResult wood = computeDig(BlockId::Obsidian, tool(ToolType::Pickaxe, ToolMaterial::Wood));
    ASSERT_EQ(diamond.ticks, 300);
    ASSERT_EQ(wood.ticks, 1000);
    advanceN(p, diamond, 150);
    EXPECT_EQ(p.crackStage(), 5);
    EXPECT_EQ(ticksUntilBreak(p, wood), 500);
}

TEST(MiningRateChange, ElapsedTicksStillCountsHeldTicks) {
    MiningProgress p;
    advanceN(p, computeDig(BlockId::Stone, kHand), 10);
    advanceN(p, computeDig(BlockId::Stone, kHand, kAirborne), 5);
    EXPECT_EQ(p.elapsedTicks(), 15);
}

TEST(MiningRateChange, PropertyConstantConditionsBreakAfterExactlyDigTicks) {
    // Accumulating fractions must not drift by a tick from DigResult::ticks for any block,
    // held item and condition combination of the engine.
    std::vector<ItemStack> held{kHand, makeStack(toItemId(BlockId::Dirt), 1)};
    for (ToolType type : {ToolType::Pickaxe, ToolType::Axe, ToolType::Shovel, ToolType::Sword, ToolType::Hoe}) {
        for (ToolMaterial mat : {ToolMaterial::Wood, ToolMaterial::Stone, ToolMaterial::Iron,
                                 ToolMaterial::Gold, ToolMaterial::Diamond}) {
            held.push_back(tool(type, mat));
        }
    }
    const std::array<DigConditions, 4> conditions{DigConditions{false, true}, DigConditions{true, true},
                                                  DigConditions{false, false}, DigConditions{true, false}};
    for (int i = 1; i < static_cast<int>(BlockId::Count); ++i) {
        const auto id = static_cast<BlockId>(i);
        for (const ItemStack& item : held) {
            for (const DigConditions c : conditions) {
                const DigResult dig = computeDig(id, item, c);
                if (!dig.breakable) {
                    continue;
                }
                MiningProgress p;
                const int32_t expected = dig.instant ? 1 : dig.ticks;
                ASSERT_EQ(ticksUntilBreak(p, dig, expected + 1), expected)
                    << BlockRegistry::get(id).name << " item " << item.id;
            }
        }
    }
}

TEST(MiningRateChange, PropertyRandomConditionChangesAreMonotonicAndBounded) {
    // Fixed seed: flip ground / water / held item every tick. Fraction stays in [0, 1], the
    // crack stage never decreases, and the break happens no later than the slowest rate and
    // no earlier than the fastest rate would allow.
    std::mt19937 rng(0xC0FFEEu);
    const std::array<ItemStack, 4> items{kHand, tool(ToolType::Pickaxe, ToolMaterial::Wood),
                                         tool(ToolType::Pickaxe, ToolMaterial::Iron),
                                         tool(ToolType::Shovel, ToolMaterial::Gold)};
    for (int trial = 0; trial < 200; ++trial) {
        MiningProgress p;
        double minTicks = 1e18;
        double maxTicks = 0.0;
        int32_t lastStage = -1;
        float lastFraction = 0.0f;
        int32_t ticks = 0;
        bool broken = false;
        while (!broken && ticks < 100'000) {
            const ItemStack& item = items[rng() % items.size()];
            const DigConditions c{.inWater = (rng() & 1u) != 0u, .onGround = (rng() & 2u) != 0u};
            const DigResult dig = computeDig(BlockId::Stone, item, c);
            minTicks = std::min(minTicks, 1.0 / dig.progressPerTick);
            maxTicks = std::max(maxTicks, 1.0 / dig.progressPerTick);
            ++ticks;
            broken = p.advance(kPos, dig);
            if (!broken) {
                ASSERT_GE(p.crackStage(), lastStage);
                ASSERT_GE(p.fraction(), lastFraction);
                ASSERT_LE(p.fraction(), 1.0f);
                lastStage = p.crackStage();
                lastFraction = p.fraction();
            }
        }
        ASSERT_TRUE(broken);
        EXPECT_GE(ticks, static_cast<int32_t>(std::ceil(minTicks - 1e-4))) << trial;
        EXPECT_LE(ticks, static_cast<int32_t>(std::ceil(maxTicks - 1e-4))) << trial;
    }
}
