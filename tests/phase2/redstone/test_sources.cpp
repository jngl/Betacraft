// S8.6 sources: the lever switches, the button stays down for 20 ticks, a plate stays down
// while something stands on it.
#include "RedstoneTestUtils.hpp"

using namespace betacraft;
using namespace betacraft::test::redstone;

namespace {
using betacraft::redstone::RedstoneEventKind;

size_t countEvents(Bench& b, RedstoneEventKind kind) {
    size_t n = 0;
    for (const auto& ev : b.engine->takeEvents()) n += ev.kind == kind ? 1 : 0;
    return n;
}
} // namespace

TEST(RedstoneSources, ButtonReleasesAfter20Ticks) {
    // A button on the side of a block, a wire on top of the far side of that block: the
    // strongly powered block feeds the wire.
    Bench b;
    const core::BlockPos block{5, kY, 5};
    const core::BlockPos button{4, kY, 5};
    b.put(block, BlockId::Stone);
    b.put(button, BlockId::StoneButton, world::kAttachWallPosX);
    b.wirePath({{6, 5}, {8, 5}});
    b.settle();
    ASSERT_EQ(b.power(6, 5), 0);

    ASSERT_TRUE(b.engine->pressButton(button));
    EXPECT_FALSE(b.engine->pressButton(button)) << "already down";
    const uint64_t pressed = b.now;
    EXPECT_EQ(countEvents(b, RedstoneEventKind::Click), 1u);
    (void)b.tick();
    EXPECT_EQ(b.power(6, 5), 15);
    EXPECT_EQ(b.power(8, 5), 13);
    while (b.now < pressed + RedstoneEngine::kButtonTicks - 1) {
        (void)b.tick();
        ASSERT_NE(b.meta(button) & world::kButtonPressedBit, 0) << "tick " << b.now - pressed;
    }
    (void)b.tick();
    EXPECT_EQ(b.meta(button) & world::kButtonPressedBit, 0) << "released after 20 ticks";
    EXPECT_EQ(b.meta(button) & world::kAttachMask, world::kAttachWallPosX) << "attachment kept";
    EXPECT_EQ(countEvents(b, RedstoneEventKind::Click), 1u);
    EXPECT_EQ(b.power(6, 5), 0);
}

TEST(RedstoneSources, PlateStaysWhileEntityOn) {
    Bench b;
    const core::BlockPos plate{5, kY, 5};
    b.put(plate, BlockId::WoodPressurePlate);
    b.wirePath({{6, 5}, {9, 5}});
    b.settle();
    ASSERT_EQ(b.power(6, 5), 0);

    // Something stands on it for 100 ticks: down the whole time.
    for (int32_t i = 0; i < 100; ++i) {
        (void)b.engine->reportPlateContact(plate);
        (void)b.tick();
        ASSERT_NE(b.meta(plate) & world::kPlatePressedBit, 0) << "tick " << i;
        if (i > 0) {
            ASSERT_EQ(b.power(6, 5), 15) << "tick " << i;
        }
    }
    EXPECT_EQ(countEvents(b, RedstoneEventKind::Click), 1u) << "one click going down";
    // It steps off: the plate comes up at its next check, at most 20 ticks later.
    int32_t up = -1;
    for (int32_t i = 0; i < 25 && up < 0; ++i) {
        (void)b.tick();
        if ((b.meta(plate) & world::kPlatePressedBit) == 0) up = i + 1;
    }
    ASSERT_GT(up, 0);
    EXPECT_LE(up, static_cast<int32_t>(RedstoneEngine::kPlateTicks));
    (void)b.tick();
    EXPECT_EQ(b.power(6, 5), 0);
    EXPECT_EQ(countEvents(b, RedstoneEventKind::Click), 1u) << "one click coming up";
    EXPECT_FALSE(b.engine->reportPlateContact({6, kY, 5})) << "not a plate";
}

TEST(RedstoneSources, LeverToggles) {
    Bench b;
    const core::BlockPos lever{4, kY, 4};
    b.put(lever, BlockId::Lever, floorLever(false));
    b.wirePath({{5, 4}, {7, 4}});
    b.settle();
    EXPECT_EQ(b.power(5, 4), 0);
    for (int32_t round = 0; round < 3; ++round) {
        ASSERT_TRUE(b.engine->toggleLever(lever));
        b.settle();
        EXPECT_NE(b.meta(lever) & world::kLeverOnBit, 0);
        EXPECT_EQ(b.meta(lever) & world::kAttachMask, world::kAttachFloor);
        EXPECT_EQ(b.power(5, 4), 15);
        EXPECT_EQ(b.power(7, 4), 13);
        ASSERT_TRUE(b.engine->toggleLever(lever));
        b.settle();
        EXPECT_EQ(b.meta(lever) & world::kLeverOnBit, 0);
        EXPECT_EQ(b.power(5, 4), 0);
        EXPECT_EQ(b.power(7, 4), 0);
    }
    EXPECT_EQ(countEvents(b, RedstoneEventKind::Click), 6u);
    EXPECT_FALSE(b.engine->toggleLever({5, kY, 4})) << "a wire is no lever";
    // A lever powers the block it hangs on strongly: a wire on the other side of it lights.
    const core::BlockPos wallBlock{10, kY, 10};
    const core::BlockPos wallLever{9, kY, 10};
    b.put(wallBlock, BlockId::Stone);
    b.put(wallLever, BlockId::Lever, static_cast<uint8_t>(world::kAttachWallPosX | world::kLeverOnBit));
    b.wirePath({{11, 10}, {12, 10}});
    b.settle();
    EXPECT_EQ(b.power(11, 10), 15);
    EXPECT_EQ(b.power(12, 10), 14);
    EXPECT_EQ(b.engine->powerAt({10, kY + 1, 10}), 15) << "the cell above the powered block";
}
