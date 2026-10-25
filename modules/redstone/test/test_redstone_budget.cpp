// Budget: at most 1024 evaluations per tick, the rest carried over to the next ticks
// with the same end result.
#include "RedstoneTestUtils.hpp"

#include <algorithm>

using namespace betacraft;
using namespace betacraft::test::redstone;

namespace {

// 40 parallel lines of 30 wires, each fed by a lever at both ends, all switched at once.
void buildLines(Bench& b) {
    for (int32_t z = 1; z <= 79; z += 2) {
        b.put({0, kY, z}, BlockId::Lever, floorLever(false));
        b.put({31, kY, z}, BlockId::Lever, floorLever(false));
        b.wirePath({{1, z}, {30, z}}, false);
    }
    b.settle();
    for (int32_t z = 1; z <= 79; z += 2) {
        ASSERT_TRUE(b.engine->toggleLever({0, kY, z}));
        ASSERT_TRUE(b.engine->toggleLever({31, kY, z}));
    }
}

uint8_t expectedPower(int32_t x) {
    return static_cast<uint8_t>(std::max({0, 16 - x, x - 15}));
}

} // namespace

TEST(RedstoneBudget, Max1024EvalsPerTickAndCarryOver) {
    Bench b(40, 4, 84);
    buildLines(b);
    bool exhausted = false;
    int32_t ticks = 0;
    for (; ticks < 200; ++ticks) {
        const RedstoneTickStats s = b.tick();
        ASSERT_LE(s.evaluations, RedstoneEngine::kMaxEvaluationsPerTick) << "tick " << ticks;
        if (s.budgetExhausted) {
            exhausted = true;
            EXPECT_EQ(s.evaluations, RedstoneEngine::kMaxEvaluationsPerTick);
            EXPECT_GT(s.pending, 0u);
        }
        if (s.pending == 0 && b.engine->scheduledTicks().empty()) break;
    }
    EXPECT_TRUE(exhausted) << "the work does not fit one tick";
    EXPECT_GT(ticks, 0);
    EXPECT_LT(ticks, 200);
    for (int32_t z = 1; z <= 79; z += 2) {
        for (int32_t x = 1; x <= 30; ++x) {
            ASSERT_EQ(b.power(x, z), expectedPower(x)) << x << "," << z;
        }
    }

    // A tiny budget spreads the same work over many more ticks and ends the same way.
    Bench small(40, 4, 84, 50);
    buildLines(small);
    int32_t smallTicks = 0;
    for (; smallTicks < 5000; ++smallTicks) {
        const RedstoneTickStats s = small.tick();
        ASSERT_LE(s.evaluations, 50u);
        if (s.pending == 0) break;
    }
    EXPECT_GT(smallTicks, ticks);
    for (int32_t z = 1; z <= 79; z += 2) {
        for (int32_t x = 1; x <= 30; ++x) {
            ASSERT_EQ(small.power(x, z), b.power(x, z)) << x << "," << z;
        }
    }
}
