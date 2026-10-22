// S8.6 wire networks: decay of one level per wire, 15 at most, two-phase updates that clear
// a network once its source goes, and no recursion on a 10,000-wire network.
#include "RedstoneTestUtils.hpp"

#include <algorithm>

using namespace betacraft;
using namespace betacraft::test::redstone;

TEST(WirePropagation, DecaysOnePerBlock) {
    Bench b;
    b.put({0, kY, 0}, BlockId::Lever, floorLever(true));
    b.wirePath({{1, 0}, {20, 0}});
    b.settle();
    for (int32_t x = 1; x <= 20; ++x) {
        EXPECT_EQ(b.power(x, 0), std::max(0, 16 - x)) << "wire " << x;
        EXPECT_EQ(b.meta({x, kY, 0}), std::max(0, 16 - x)) << "written power of wire " << x;
    }
}

TEST(WirePropagation, Max15) {
    // Several sources around one wire, and a ring the network loops through: nothing ever goes
    // above 15 nor wraps around.
    Bench b;
    b.put({5, kY, 4}, BlockId::Lever, floorLever(true));
    b.put({5, kY, 6}, BlockId::RedstoneTorchOn, voxel::kAttachFloor);
    b.put({4, kY, 5}, BlockId::StonePressurePlate, voxel::kPlatePressedBit);
    b.wirePath({{5, 5}, {10, 5}, {10, 10}, {5, 10}, {5, 7}});
    b.settle();
    EXPECT_EQ(b.power(5, 5), 15);
    for (int32_t z = 0; z < 32; ++z) {
        for (int32_t x = 0; x < 32; ++x) {
            EXPECT_LE(b.power(x, z), 15);
            EXPECT_LE(b.meta({x, kY, z}), 15);
        }
    }
    EXPECT_EQ(b.power(6, 5), 14);
    // The torch feeds the far end of the ring too: 8 wires from it, 10 from the lever.
    EXPECT_EQ(b.power(10, 10), 7);
}

TEST(WirePropagation, RemovingSourceClearsWholeNetwork) {
    // A ring with branches: once the lever is off nothing keeps feeding itself.
    Bench b;
    b.put({2, kY, 3}, BlockId::Lever, floorLever(true));
    b.wirePath({{3, 3}, {9, 3}, {9, 9}, {3, 9}, {3, 4}});
    b.wirePath({{6, 4}, {6, 8}});
    b.wirePath({{10, 6}, {14, 6}});
    b.settle();
    ASSERT_EQ(b.power(3, 3), 15);
    ASSERT_GT(b.power(6, 6), 0);
    ASSERT_GT(b.power(11, 6), 0);

    ASSERT_TRUE(b.engine->toggleLever({2, kY, 3}));
    b.settle();
    for (int32_t z = 0; z < 32; ++z) {
        for (int32_t x = 0; x < 32; ++x) {
            EXPECT_EQ(b.power(x, z), 0) << x << "," << z;
            if (b.at({x, kY, z}) == BlockId::RedstoneWire) {
                EXPECT_EQ(b.meta({x, kY, z}), 0) << x << "," << z;
            }
        }
    }

    // Removing a wire (instead of switching the source off) cuts the far side only.
    ASSERT_TRUE(b.engine->toggleLever({2, kY, 3}));
    b.settle();
    b.remove({9, kY, 6});
    b.remove({6, kY, 4});
    b.settle();
    EXPECT_EQ(b.power(10, 6), 0) << "only fed through the removed wire";
    EXPECT_EQ(b.power(9, 3), 9);
    EXPECT_EQ(b.power(3, 8), 10);
    EXPECT_EQ(b.power(9, 7), 1); // the long way round: 3,3 -> 3,9 -> 9,9 -> 9,7
}

TEST(WirePropagation, TwoSourcesMaxWins) {
    Bench b;
    b.put({0, kY, 0}, BlockId::Lever, floorLever(true));
    b.put({21, kY, 0}, BlockId::Lever, floorLever(true));
    b.wirePath({{1, 0}, {20, 0}});
    b.settle();
    for (int32_t x = 1; x <= 20; ++x) {
        EXPECT_EQ(b.power(x, 0), std::max({0, 16 - x, x - 5})) << "wire " << x;
    }
    // One source off: the other's decay alone remains.
    ASSERT_TRUE(b.engine->toggleLever({0, kY, 0}));
    b.settle();
    for (int32_t x = 1; x <= 20; ++x) {
        EXPECT_EQ(b.power(x, 0), std::max(0, x - 5)) << "wire " << x;
    }
}

TEST(WirePropagation, NoRecursionOn10kWire) {
    // A 100 x 100 sheet of wire (10,000 cells, all connected) lit from a corner and switched
    // off again: the queues run it without recursion, spread over budget-bound ticks.
    Bench b(104, 4, 104);
    for (int32_t z = 1; z <= 100; ++z) {
        for (int32_t x = 1; x <= 100; ++x) {
            b.grid.setBlock({x, kY, z}, BlockId::RedstoneWire, 0);
        }
    }
    b.put({0, kY, 1}, BlockId::Lever, floorLever(true));
    EXPECT_LT(b.settle(2000), 2000);
    for (int32_t z = 1; z <= 100; z += 7) {
        for (int32_t x = 1; x <= 100; x += 3) {
            EXPECT_EQ(b.power(x, z), std::max(0, 15 - (x - 1) - (z - 1))) << x << "," << z;
        }
    }
    ASSERT_TRUE(b.engine->toggleLever({0, kY, 1}));
    EXPECT_LT(b.settle(2000), 2000);
    for (int32_t z = 1; z <= 20; ++z) {
        for (int32_t x = 1; x <= 20; ++x) {
            EXPECT_EQ(b.power(x, z), 0) << x << "," << z;
        }
    }
}
