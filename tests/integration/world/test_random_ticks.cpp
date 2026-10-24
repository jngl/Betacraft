// Sprint 5 (S5.1) random block ticks: World::tickRandomBlocks gives 80 random cells per nearby
// decorated chunk a Beta 1.7.3 updateTick each game tick. Saplings grow into oaks in the light,
// grass spreads over lit dirt, leaves cut off from their log decay (unless the player placed
// them), cactus and sugar cane grow to 3 blocks.
#include "tests/integration/support/GameplaySupport.hpp"

#include "world/BlockTicks.hpp"

#include <array>
#include <vector>

using namespace betacraft;
using namespace betacraft::test::p2gameplay;

namespace {

class RandomTicks : public GameplayWorld {
protected:
    void SetUp() override {
        GameplayWorld::SetUp();
        ASSERT_TRUE(m_world->isChunkDecorated({0, 0})) << "random ticks only reach decorated chunks";
    }

    // Runs `n` random-tick passes around chunk (0, 0); stops early once `done()` holds.
    template <typename Done>
    int32_t tickRandomUntil(int32_t n, Done&& done, int32_t skylightSubtracted = 0) {
        for (int32_t t = 1; t <= n; ++t) {
            m_world->tickRandomBlocks(kCentre, m_rng, skylightSubtracted);
            if (done()) return t;
        }
        return -1;
    }

    void tickRandom(int32_t n, int32_t skylightSubtracted = 0) {
        (void)tickRandomUntil(n, [] { return false; }, skylightSubtracted);
    }

    void plantSapling(int32_t x, int32_t z) {
        ASSERT_TRUE(m_world->setBlock(x, kFloorY, z, BlockId::Dirt));
        ASSERT_TRUE(m_world->setBlockWithMetadata(x, kY, z, BlockId::Sapling, 0));
    }

    [[nodiscard]] bool isTree(int32_t x, int32_t z) const {
        for (int32_t y = kY; y < kY + 4; ++y) {
            if (m_world->getBlock(x, y, z) != BlockId::OakLog) return false;
        }
        return m_world->getBlock(x + 1, kY + 4, z) == BlockId::Leaves;
    }

    // Seals the 1x2x1 cell column at (x, kY..kY+1, z) in stone: no sky or block light inside.
    void encase(int32_t x, int32_t z) {
        for (int32_t y = kY; y <= kY + 1; ++y) {
            ASSERT_TRUE(m_world->setBlock(x + 1, y, z, BlockId::Stone));
            ASSERT_TRUE(m_world->setBlock(x - 1, y, z, BlockId::Stone));
            ASSERT_TRUE(m_world->setBlock(x, y, z + 1, BlockId::Stone));
            ASSERT_TRUE(m_world->setBlock(x, y, z - 1, BlockId::Stone));
        }
        ASSERT_TRUE(m_world->setBlock(x, kY + 2, z, BlockId::Stone));
    }

    static constexpr glm::vec3 kCentre{8.0f, 120.0f, 8.0f};
    static constexpr std::array<int32_t, 3> kGrid{4, 8, 12};
    gameplay::SplitMix64 m_rng{0x5EEDBEEFULL};
};

} // namespace

TEST_F(RandomTicks, LitSaplingGrowsIntoATreeWithinTwentyThousandTicks) {
    for (const int32_t z : kGrid) {
        for (const int32_t x : kGrid) plantSapling(x, z);
    }
    const auto anyTree = [&] {
        for (const int32_t z : kGrid) {
            for (const int32_t x : kGrid) {
                if (isTree(x, z)) return true;
            }
        }
        return false;
    };
    // Beta: 2 hits of 1 in 30 at 80 / 32768 cells per tick, about 24500 ticks per sapling on
    // average; the first of nine is well under 20000.
    const int32_t first = tickRandomUntil(20000, anyTree);
    ASSERT_GT(first, 0) << "no sapling grew in 20000 ticks";

    tickRandom(150000);
    for (const int32_t z : kGrid) {
        for (const int32_t x : kGrid) {
            EXPECT_TRUE(isTree(x, z)) << x << "," << z;
            EXPECT_EQ(m_world->getBlock(x, kFloorY, z), BlockId::Dirt) << "the trunk stands on soil";
            EXPECT_EQ(m_world->getMetadata(x, kY, z), 0) << "the sapling's ready bit does not leak into the log";
        }
    }
    // Grown leaves are like generated ones: metadata 0, no decay check pending.
    EXPECT_EQ(m_world->getMetadata(kGrid[0] + 1, kY + 4, kGrid[0]), 0);
}

TEST_F(RandomTicks, SaplingInTheDarkNeverGrows) {
    plantSapling(4, 4);
    encase(4, 4);
    ASSERT_LT(m_world->getEffectiveLight({4, kY + 1, 4}), 9);
    tickRandom(60000);
    EXPECT_EQ(m_world->getBlock(4, kY, 4), BlockId::Sapling);
    EXPECT_EQ(m_world->getMetadata(4, kY, 4), 0) << "not even the first growth step";
}

TEST_F(RandomTicks, SaplingDoesNotGrowAtNight) {
    plantSapling(4, 4);
    // Beta skylightSubtracted at midnight is 11: sky 15 reads 4, below the sapling's 9.
    tickRandom(60000, 11);
    EXPECT_EQ(m_world->getBlock(4, kY, 4), BlockId::Sapling);
    EXPECT_EQ(m_world->getMetadata(4, kY, 4), 0);
}

TEST_F(RandomTicks, GrassSpreadsOverLitDirtButNeverCoveredDirt) {
    // A 5x5 dirt patch in the floor around one grass block; one dirt cell is roofed with stone.
    for (int32_t z = 6; z <= 10; ++z) {
        for (int32_t x = 6; x <= 10; ++x) {
            ASSERT_TRUE(m_world->setBlock(x, kFloorY, z, BlockId::Dirt));
        }
    }
    ASSERT_TRUE(m_world->setBlock(8, kFloorY, 8, BlockId::Grass));
    ASSERT_TRUE(m_world->setBlock(9, kY, 8, BlockId::Stone));

    const auto grassCount = [&] {
        int32_t n = 0;
        for (int32_t z = 6; z <= 10; ++z) {
            for (int32_t x = 6; x <= 10; ++x) n += m_world->getBlock(x, kFloorY, z) == BlockId::Grass ? 1 : 0;
        }
        return n;
    };
    tickRandom(150000);
    EXPECT_GE(grassCount(), 20) << "grass creeps over the lit patch";
    EXPECT_EQ(m_world->getBlock(9, kFloorY, 8), BlockId::Dirt) << "a dirt cell under stone never turns";
}

TEST_F(RandomTicks, GrassInTheDarkUnderABlockRevertsToDirt) {
    ASSERT_TRUE(m_world->setBlock(4, kFloorY, 4, BlockId::Grass));
    ASSERT_TRUE(m_world->setBlock(4, kY, 4, BlockId::Stone));
    // Seal the sides of the grass so no light reaches the cell above it from anywhere.
    for (const auto& [dx, dz] : std::array<std::pair<int32_t, int32_t>, 4>{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}}) {
        ASSERT_TRUE(m_world->setBlock(4 + dx, kY, 4 + dz, BlockId::Stone));
    }
    ASSERT_TRUE(m_world->setBlock(4, kY + 1, 4, BlockId::Stone));
    // The cell above the grass is the stone itself: effective light 0, opacity 15.
    ASSERT_LT(m_world->getEffectiveLight({4, kY, 4}), 4);
    const int32_t t = tickRandomUntil(100000, [&] { return m_world->getBlock(4, kFloorY, 4) == BlockId::Dirt; });
    EXPECT_GT(t, 0);
}

TEST_F(RandomTicks, LeavesWithoutTrunkAllDecayExceptPlayerPlacedOnes) {
    // A hand-built oak: 4 logs and a 5x5 crown, generated-style leaves at metadata 0.
    constexpr int32_t x0 = 8;
    constexpr int32_t z0 = 8;
    std::vector<core::BlockPos> leaves;
    for (int32_t y = kY; y < kY + 4; ++y) ASSERT_TRUE(m_world->setBlock(x0, y, z0, BlockId::OakLog));
    for (int32_t y = kY + 2; y <= kY + 4; ++y) {
        for (int32_t dz = -2; dz <= 2; ++dz) {
            for (int32_t dx = -2; dx <= 2; ++dx) {
                if (m_world->getBlock(x0 + dx, y, z0 + dz) != BlockId::Air) continue;
                ASSERT_TRUE(m_world->setBlockWithMetadata(x0 + dx, y, z0 + dz, BlockId::Leaves, 0));
                leaves.push_back({x0 + dx, y, z0 + dz});
            }
        }
    }
    // One leaf the player placed against the crown (placeHeldBlock sets the bit).
    ASSERT_TRUE(m_world->setBlockWithMetadata(x0 + 3, kY + 3, z0, BlockId::Leaves, world::kLeavesPlayerPlacedBit));

    // With the trunk standing, ticking changes nothing.
    tickRandom(5000);
    for (const core::BlockPos& p : leaves) ASSERT_EQ(m_world->getBlock(p), BlockId::Leaves);

    for (int32_t y = kY; y < kY + 4; ++y) ASSERT_TRUE(m_world->setBlock(x0, y, z0, BlockId::Air));
    EXPECT_NE(m_world->getMetadata(leaves.front()) & world::kLeavesCheckDecayBit, 0) << "the cut flags the crown";
    (void)m_world->takeUnsupportedBreaks();

    const auto allGone = [&] {
        for (const core::BlockPos& p : leaves) {
            if (m_world->getBlock(p) == BlockId::Leaves) return false;
        }
        return true;
    };
    const int32_t t = tickRandomUntil(40000, allGone);
    EXPECT_GT(t, 0) << "every leaf of the cut tree decays";
    EXPECT_EQ(m_world->getBlock(x0 + 3, kY + 3, z0), BlockId::Leaves) << "player-placed leaves never decay";

    // Decayed leaves are reported like broken flora: the game tick drops them (a sapling 1 in 20).
    size_t decayed = 0;
    for (const world::World::UnsupportedBreak& b : m_world->takeUnsupportedBreaks()) {
        decayed += b.block == BlockId::Leaves ? 1U : 0U;
    }
    EXPECT_EQ(decayed, leaves.size());
}

TEST_F(RandomTicks, LeavesStillReachingALogThroughLeavesSurvive) {
    // Two logs under a shared row of leaves: cutting one leaves the other holding the leaves
    // within 4 steps of it, and only those (Beta's reach through leaves).
    for (int32_t y = kY; y < kY + 3; ++y) {
        ASSERT_TRUE(m_world->setBlock(5, y, 8, BlockId::OakLog));
        ASSERT_TRUE(m_world->setBlock(9, y, 8, BlockId::OakLog));
    }
    for (int32_t x = 4; x <= 10; ++x) {
        ASSERT_TRUE(m_world->setBlockWithMetadata(x, kY + 3, 8, BlockId::Leaves, 0));
    }
    for (int32_t y = kY; y < kY + 3; ++y) ASSERT_TRUE(m_world->setBlock(5, y, 8, BlockId::Air));
    tickRandom(40000);
    EXPECT_EQ(m_world->getBlock(4, kY + 3, 8), BlockId::Air) << "5 steps from the remaining log";
    for (int32_t x = 6; x <= 10; ++x) {
        EXPECT_EQ(m_world->getBlock(x, kY + 3, 8), BlockId::Leaves) << x;
        EXPECT_EQ(m_world->getMetadata(x, kY + 3, 8) & world::kLeavesCheckDecayBit, 0) << "the check clears the flag";
    }
}

TEST_F(RandomTicks, SugarCaneAndCactusStopAtThreeBlocks) {
    ASSERT_TRUE(m_world->setBlock(4, kFloorY, 4, BlockId::Sand));
    ASSERT_TRUE(m_world->setBlockWithMetadata(4, kY, 4, BlockId::SugarCane, 0));
    ASSERT_TRUE(m_world->setBlock(12, kFloorY, 12, BlockId::Sand));
    ASSERT_TRUE(m_world->setBlockWithMetadata(12, kY, 12, BlockId::Cactus, 0));
    tickRandom(120000);
    for (int32_t y = kY; y < kY + 3; ++y) {
        EXPECT_EQ(m_world->getBlock(4, y, 4), BlockId::SugarCane) << y;
        EXPECT_EQ(m_world->getBlock(12, y, 12), BlockId::Cactus) << y;
    }
    EXPECT_EQ(m_world->getBlock(4, kY + 3, 4), BlockId::Air) << "sugar cane caps at 3 blocks";
    EXPECT_EQ(m_world->getBlock(12, kY + 3, 12), BlockId::Air) << "cactus caps at 3 blocks";
}

TEST_F(RandomTicks, GeneratedLeavesDoNotDecay) {
    // Floating leaves at metadata 0, as worldgen writes them, never check for a log.
    for (int32_t x = 4; x <= 6; ++x) {
        ASSERT_TRUE(m_world->setBlockWithMetadata(x, kY + 2, 4, BlockId::Leaves, 0));
    }
    // And the real generated trees of the chunk keep every leaf.
    const voxel::Chunk* chunk = m_world->getChunk({0, 0});
    ASSERT_NE(chunk, nullptr);
    const auto generatedLeaves = [&] {
        int32_t n = 0;
        for (int32_t y = 0; y < kFloorY; ++y) {
            for (int32_t z = 0; z < voxel::Chunk::DEPTH; ++z) {
                for (int32_t x = 0; x < voxel::Chunk::WIDTH; ++x) n += chunk->getBlock(x, y, z) == BlockId::Leaves ? 1 : 0;
            }
        }
        return n;
    };
    const int32_t before = generatedLeaves();
    tickRandom(30000);
    for (int32_t x = 4; x <= 6; ++x) EXPECT_EQ(m_world->getBlock(x, kY + 2, 4), BlockId::Leaves);
    EXPECT_EQ(generatedLeaves(), before);
}

TEST_F(RandomTicks, OnlyNearbyDecoratedChunksAreTicked) {
    // Chunk (1, 0) is loaded but undecorated (its east neighbours are missing): nothing grows there.
    ASSERT_FALSE(m_world->isChunkDecorated({1, 0}));
    ASSERT_TRUE(m_world->setBlock(20, kFloorY, 4, BlockId::Sand));
    ASSERT_TRUE(m_world->setBlockWithMetadata(20, kY, 4, BlockId::SugarCane, 0));
    tickRandom(30000);
    EXPECT_EQ(m_world->getMetadata(20, kY, 4), 0);
    EXPECT_EQ(m_world->getBlock(20, kY + 1, 4), BlockId::Air);
}
