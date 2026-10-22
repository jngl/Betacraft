// S8.2 single moves: no corner cutting, and the mob's whole box (height, width) must fit.
#include "test_support/AiTestSupport.hpp"

#include <gtest/gtest.h>

using namespace betacraft;
using namespace betacraft::test::ai;

TEST(AiCanTraverse, DiagonalBlockedByEitherOrthogonal) {
    const glm::vec3 zombie = kZombie.extents;
    const glm::ivec3 from{5, kWalkY, 5};
    const glm::ivec3 to{6, kWalkY, 6};
    {
        const GridNavQuery grid = flatGrid(12, 5, 12);
        EXPECT_TRUE(ai::canTraverse(grid, from, to, zombie));
    }
    {
        GridNavQuery grid = flatGrid(12, 5, 12);
        grid.set({6, 1, 5}, NavCell::Solid); // +X neighbour
        EXPECT_FALSE(ai::canTraverse(grid, from, to, zombie));
        EXPECT_TRUE(ai::canTraverse(grid, from, {5, kWalkY, 6}, zombie)); // orthogonal still fine
    }
    {
        GridNavQuery grid = flatGrid(12, 5, 12);
        grid.set({5, 1, 6}, NavCell::Solid); // +Z neighbour
        EXPECT_FALSE(ai::canTraverse(grid, from, to, zombie));
    }
    {
        GridNavQuery grid = flatGrid(12, 5, 12);
        grid.set({6, 2, 5}, NavCell::Solid); // only at head height: still a corner to cut
        EXPECT_FALSE(ai::canTraverse(grid, from, to, zombie));
    }
    {
        GridNavQuery grid = flatGrid(12, 5, 12);
        grid.set({6, 1, 5}, NavCell::Fence);
        EXPECT_FALSE(ai::canTraverse(grid, from, to, zombie));
    }
}

TEST(AiCanTraverse, TallMobNeedsTwoCellsClearance) {
    GridNavQuery grid = flatGrid(12, 5, 12);
    grid.set({6, 2, 5}, NavCell::Solid); // a 1-block gap under an overhang
    const glm::ivec3 from{5, kWalkY, 5};
    const glm::ivec3 to{6, kWalkY, 5};
    EXPECT_FALSE(ai::canTraverse(grid, from, to, kZombie.extents));
    EXPECT_TRUE(ai::canTraverse(grid, from, to, {0.9f, 0.9f, 0.9f})); // a pig fits
    EXPECT_TRUE(ai::canTraverse(grid, from, to, kSpider.extents)); // 0.9 tall as well
    // A step up also needs a free cell above the head.
    GridNavQuery steps = flatGrid(12, 6, 12);
    steps.set({6, 1, 5}, NavCell::Solid);
    EXPECT_TRUE(ai::canTraverse(steps, from, {6, 2, 5}, kZombie.extents));
    steps.set({5, 3, 5}, NavCell::Solid);
    EXPECT_FALSE(ai::canTraverse(steps, from, {6, 2, 5}, kZombie.extents));
    // Two blocks up is never one move.
    GridNavQuery high = flatGrid(12, 6, 12);
    high.fill({6, 1, 5}, {6, 2, 5}, NavCell::Solid);
    EXPECT_FALSE(ai::canTraverse(high, from, {6, 3, 5}, kZombie.extents));
}

TEST(AiCanTraverse, SpiderNeedsWideClearance) {
    // A 1-wide corridor along X at z = 5 (walls at z = 4 and z = 6).
    GridNavQuery grid = flatGrid(16, 4, 12);
    grid.fill({0, 1, 4}, {15, 2, 4}, NavCell::Solid);
    grid.fill({0, 1, 6}, {15, 2, 6}, NavCell::Solid);
    const glm::ivec3 from{5, kWalkY, 5};
    const glm::ivec3 to{6, kWalkY, 5};
    EXPECT_TRUE(ai::canTraverse(grid, from, to, kZombie.extents));
    EXPECT_FALSE(ai::canTraverse(grid, from, to, kSpider.extents)); // 2x2 footprint
    EXPECT_FALSE(ai::isStandable(grid, from, kSpider));

    // Two cells wide: the spider (footprint corner at z = 5, covering z 5..6) passes.
    grid.fill({0, 1, 6}, {15, 2, 6}, NavCell::Passable);
    grid.fill({0, 1, 7}, {15, 2, 7}, NavCell::Solid);
    EXPECT_TRUE(ai::canTraverse(grid, from, to, kSpider.extents));
    // Low ceiling over the corridor: fine for the 0.9-tall spider, not for a zombie.
    grid.fill({0, 2, 5}, {15, 2, 6}, NavCell::Solid);
    EXPECT_TRUE(ai::canTraverse(grid, from, to, kSpider.extents));
    EXPECT_FALSE(ai::canTraverse(grid, from, to, kZombie.extents));
}
