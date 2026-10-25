// Redstone torches: they invert the power of the block they hang on 2 ticks later, burn
// out when switched more than 8 times in 60 ticks, come back after 60 ticks, and two of them
// make a stable RS NOR latch.
#include "RedstoneTestUtils.hpp"

using namespace betacraft;
using namespace betacraft::test::redstone;

namespace {

// The tick (from b.now) at which the block at `p` first becomes `id`, or -1 within maxTicks.
int64_t tickWhen(Bench& b, const core::BlockPos& p, BlockId id, int32_t maxTicks = 40) {
    for (int32_t i = 0; i < maxTicks; ++i) {
        (void)b.tick();
        if (b.at(p) == id) return static_cast<int64_t>(b.now);
    }
    return -1;
}

// A torch clock: the torch hangs on the +X side of block A and its wire loops back into A
// from +Z (6 wires), so it keeps switching itself.
struct Clock {
    static constexpr core::BlockPos kBlock{1, kY, 1};
    static constexpr core::BlockPos kTorch{2, kY, 1};
    static constexpr core::BlockPos kLoopWire{3, kY, 3};

    explicit Clock(Bench& b) {
        b.put(kBlock, BlockId::Stone);
        b.put(kTorch, BlockId::RedstoneTorchOn, voxel::kAttachWallNegX);
        b.wirePath({{3, 1}, {3, 3}, {1, 3}, {1, 2}});
    }
};

} // namespace

TEST(TorchInverter, TwoTickDelay) {
    // Lever on the side of block A, torch T1 on A, block B over T1 (T1 powers it), torch T2 on
    // B: each torch switches 2 ticks after the tick that saw its input change.
    Bench b;
    const core::BlockPos a{4, kY, 4};
    const core::BlockPos lever{3, kY, 4};
    const core::BlockPos t1{4, kY + 1, 4};
    const core::BlockPos blockB{4, kY + 2, 4};
    const core::BlockPos t2{4, kY + 3, 4};
    b.put(a, BlockId::Stone);
    b.put(lever, BlockId::Lever, voxel::kAttachWallPosX);
    b.put(t1, BlockId::RedstoneTorchOn, voxel::kAttachFloor);
    b.put(blockB, BlockId::Stone);
    b.put(t2, BlockId::RedstoneTorchOn, voxel::kAttachFloor);
    b.settle();
    ASSERT_EQ(b.at(t1), BlockId::RedstoneTorchOn);
    ASSERT_EQ(b.at(t2), BlockId::RedstoneTorchOff) << "B is powered by T1";

    ASSERT_TRUE(b.engine->toggleLever(lever));
    const auto toggledAt = static_cast<int64_t>(b.now);
    const int64_t t1Off = tickWhen(b, t1, BlockId::RedstoneTorchOff);
    ASSERT_GT(t1Off, 0);
    // The change is seen on the next tick, the torch switches 2 ticks after that.
    EXPECT_EQ(t1Off - toggledAt, 1 + RedstoneEngine::kTorchDelayTicks);
    const int64_t t2On = tickWhen(b, t2, BlockId::RedstoneTorchOn);
    ASSERT_GT(t2On, 0);
    EXPECT_EQ(t2On - t1Off, RedstoneEngine::kTorchDelayTicks);
}

TEST(TorchInverter, BurnoutAfter8TogglesIn60Ticks) {
    Bench b;
    const Clock clock(b);
    uint32_t toggles = 0;
    uint32_t burnouts = 0;
    for (int32_t i = 0; i < 60; ++i) {
        const RedstoneTickStats s = b.tick();
        toggles += s.torchToggles;
        burnouts += s.burnouts;
    }
    EXPECT_EQ(burnouts, 1u);
    // 8 switches are allowed within the window; the one that burns it out puts it out.
    EXPECT_GE(toggles, 8u);
    EXPECT_LE(toggles, 9u);
    EXPECT_EQ(b.at(Clock::kTorch), BlockId::RedstoneTorchOff);
    EXPECT_TRUE(b.engine->isBurntOut(Clock::kTorch));
    bool fizzed = false;
    for (const auto& ev : b.engine->takeEvents()) {
        fizzed |= ev.kind == betacraft::redstone::RedstoneEventKind::TorchBurnout && ev.pos == Clock::kTorch;
    }
    EXPECT_TRUE(fizzed);
}

TEST(TorchInverter, RecoversAfter60Ticks) {
    Bench b;
    const Clock clock(b);
    int32_t burntTick = -1;
    for (int32_t i = 0; i < 80 && burntTick < 0; ++i) {
        if (b.tick().burnouts > 0) burntTick = static_cast<int32_t>(b.now);
    }
    ASSERT_GT(burntTick, 0);
    // Break the loop: once the burnout is over the torch relights and stays lit.
    b.remove(Clock::kLoopWire);
    for (int32_t i = 0; i < 55; ++i) {
        (void)b.tick();
        ASSERT_EQ(b.at(Clock::kTorch), BlockId::RedstoneTorchOff) << "still burnt out " << b.now - burntTick;
    }
    const int64_t relit = tickWhen(b, Clock::kTorch, BlockId::RedstoneTorchOn, 20);
    ASSERT_GT(relit, 0);
    EXPECT_GE(relit - burntTick, 60);
    EXPECT_LE(relit - burntTick, 63);
    EXPECT_EQ(b.run(100), 0u) << "no loop any more: it stays lit";
    EXPECT_EQ(b.at(Clock::kTorch), BlockId::RedstoneTorchOn);
}

TEST(TorchInverter, RsNorLatchStable) {
    // Two torches, each hanging on the block the other one's wire powers. A lever on the far
    // side of each block sets or resets the latch; released, the latch keeps its state.
    Bench b;
    const core::BlockPos a{3, kY, 1};
    const core::BlockPos ta{3, kY, 2};
    const core::BlockPos blockB{7, kY, 1};
    const core::BlockPos tb{7, kY, 2};
    const core::BlockPos leverA{3, kY, 0};
    const core::BlockPos leverB{7, kY, 0};
    b.put(a, BlockId::Stone);
    b.put(blockB, BlockId::Stone);
    b.put(ta, BlockId::RedstoneTorchOn, voxel::kAttachWallNegZ);
    b.put(tb, BlockId::RedstoneTorchOff, voxel::kAttachWallNegZ);
    b.put(leverA, BlockId::Lever, voxel::kAttachWallPosZ);
    b.put(leverB, BlockId::Lever, voxel::kAttachWallPosZ);
    b.wirePath({{3, 3}, {5, 3}, {5, 1}, {6, 1}});         // TA -> into B (6 wires)
    b.wirePath({{7, 3}, {7, 5}, {1, 5}, {1, 1}, {2, 1}}); // TB -> into A (14 wires)
    b.settle();
    ASSERT_EQ(b.at(ta), BlockId::RedstoneTorchOn);
    ASSERT_EQ(b.at(tb), BlockId::RedstoneTorchOff);
    EXPECT_EQ(b.run(200), 0u) << "a latch holds its state";

    // Set: lever A on puts TA out, TB lights and holds A powered once the lever is off again.
    ASSERT_TRUE(b.engine->toggleLever(leverA));
    b.settle();
    ASSERT_TRUE(b.engine->toggleLever(leverA));
    b.settle();
    EXPECT_EQ(b.at(ta), BlockId::RedstoneTorchOff);
    EXPECT_EQ(b.at(tb), BlockId::RedstoneTorchOn);
    EXPECT_EQ(b.run(200), 0u);

    // Reset through lever B.
    ASSERT_TRUE(b.engine->toggleLever(leverB));
    b.settle();
    ASSERT_TRUE(b.engine->toggleLever(leverB));
    b.settle();
    EXPECT_EQ(b.at(ta), BlockId::RedstoneTorchOn);
    EXPECT_EQ(b.at(tb), BlockId::RedstoneTorchOff);
    EXPECT_EQ(b.run(200), 0u);
    EXPECT_FALSE(b.engine->isBurntOut(ta));
    EXPECT_FALSE(b.engine->isBurntOut(tb));
}
