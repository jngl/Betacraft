// S8.2 VoxelPathfinder: optimal straight lines, detours around walls, one-block steps, drops
// of at most 3 blocks, no lava, closed doors for zombies, best partial paths, time slicing and
// determinism.
#include "AiTestSupport.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <limits>
#include <vector>

using namespace betacraft;
using namespace betacraft::test::ai;

namespace {

// Each consecutive pair of cells is a legal move.
[[nodiscard]] ::testing::AssertionResult isWalkable(const GridNavQuery& grid, const std::vector<glm::ivec3>& path,
                                                    const NavAgent& agent) {
    for (size_t i = 1; i < path.size(); ++i) {
        if (!ai::moveCost(grid, path[i - 1], path[i], agent)) {
            return ::testing::AssertionFailure() << "illegal move " << i;
        }
    }
    return ::testing::AssertionSuccess();
}

} // namespace

TEST(AiPathfinder, StraightLineOptimal) {
    const GridNavQuery grid = flatGrid(20, 4, 12);
    const ai::PathResult r = ai::findPath(grid, {2, kWalkY, 5}, {12, kWalkY, 5}, kZombie);
    ASSERT_EQ(r.status, ai::SearchStatus::Found);
    ASSERT_EQ(r.path.size(), 11U);
    for (size_t i = 0; i < r.path.size(); ++i) {
        EXPECT_EQ(r.path[i], glm::ivec3(2 + static_cast<int32_t>(i), kWalkY, 5));
    }
    EXPECT_NEAR(pathCost(grid, r.path, kZombie), 10.0f, 1e-4f);
}

TEST(AiPathfinder, GoesAroundWall) {
    GridNavQuery grid = flatGrid(20, 6, 16);
    grid.fill({8, 1, 2}, {8, 3, 7}, NavCell::Solid); // 6 blocks wide, 3 high
    const glm::ivec3 start{5, kWalkY, 5};
    const glm::ivec3 goal{11, kWalkY, 5};
    const ai::PathResult r = ai::findPath(grid, start, goal, kZombie);
    ASSERT_EQ(r.status, ai::SearchStatus::Found);
    EXPECT_EQ(r.path.front(), start);
    EXPECT_EQ(r.path.back(), goal);
    EXPECT_TRUE(isWalkable(grid, r.path, kZombie));
    for (const glm::ivec3& c : r.path) {
        EXPECT_EQ(c.y, kWalkY);
        EXPECT_FALSE(c.x == 8 && c.z >= 2 && c.z <= 7) << "through the wall at z " << c.z;
    }
    EXPECT_GT(pathCost(grid, r.path, kZombie), 6.0f);
}

TEST(AiPathfinder, StepUpOneBlock) {
    GridNavQuery grid = flatGrid(20, 6, 12);
    grid.fill({9, 1, 0}, {19, 1, 11}, NavCell::Solid); // a one-block terrace
    const ai::PathResult r = ai::findPath(grid, {3, kWalkY, 5}, {13, kWalkY + 1, 5}, kZombie);
    ASSERT_EQ(r.status, ai::SearchStatus::Found);
    EXPECT_TRUE(isWalkable(grid, r.path, kZombie));
    int ups = 0;
    for (size_t i = 1; i < r.path.size(); ++i) {
        ups += r.path[i].y - r.path[i - 1].y == 1 ? 1 : 0;
    }
    EXPECT_EQ(ups, 1);

    // Two blocks high: unreachable for a walker.
    grid.fill({9, 2, 0}, {19, 2, 11}, NavCell::Solid);
    const ai::PathResult high = ai::findPath(grid, {3, kWalkY, 5}, {13, kWalkY + 2, 5}, kZombie);
    EXPECT_EQ(high.status, ai::SearchStatus::Partial);
}

TEST(AiPathfinder, NeverDropsMoreThan3) {
    // A tower 4 blocks above the floor: no way down.
    GridNavQuery grid = flatGrid(20, 10, 12);
    grid.fill({2, 1, 4}, {4, 4, 6}, NavCell::Solid);
    const ai::PathResult r = ai::findPath(grid, {3, 5, 5}, {12, kWalkY, 5}, kZombie);
    EXPECT_EQ(r.status, ai::SearchStatus::Partial);
    for (size_t i = 1; i < r.path.size(); ++i) {
        EXPECT_GE(r.path[i].y - r.path[i - 1].y, -3);
    }
    EXPECT_EQ(r.path.back().y, 5); // stays on top

    // Three blocks high: one drop of 3.
    GridNavQuery low = flatGrid(20, 10, 12);
    low.fill({2, 1, 4}, {4, 3, 6}, NavCell::Solid);
    const ai::PathResult down = ai::findPath(low, {3, 4, 5}, {12, kWalkY, 5}, kZombie);
    ASSERT_EQ(down.status, ai::SearchStatus::Found);
    EXPECT_TRUE(isWalkable(low, down.path, kZombie));
    int drops = 0;
    for (size_t i = 1; i < down.path.size(); ++i) {
        const int32_t dy = down.path[i].y - down.path[i - 1].y;
        EXPECT_GE(dy, -3);
        drops += dy == -3 ? 1 : 0;
    }
    EXPECT_EQ(drops, 1);
}

TEST(AiPathfinder, AvoidsLava) {
    GridNavQuery grid = flatGrid(20, 5, 14);
    grid.fill({7, 0, 3}, {10, 0, 8}, NavCell::Hazard); // a lava pool across the straight line
    const ai::PathResult r = ai::findPath(grid, {4, kWalkY, 5}, {13, kWalkY, 5}, kZombie);
    ASSERT_EQ(r.status, ai::SearchStatus::Found);
    EXPECT_TRUE(isWalkable(grid, r.path, kZombie));
    for (const glm::ivec3& c : r.path) {
        EXPECT_EQ(c.y, kWalkY);
        EXPECT_FALSE(c.x >= 7 && c.x <= 10 && c.z >= 3 && c.z <= 8) << "over the lava at " << c.x << "," << c.z;
    }
    // A trench of lava one block down, across the whole grid, is never jumped into.
    GridNavQuery pit = flatGrid(20, 6, 14);
    pit.fill({0, 1, 0}, {19, 1, 13}, NavCell::Solid); // walking at y = 2
    pit.fill({7, 1, 0}, {10, 1, 13}, NavCell::Hazard);
    const ai::PathResult across = ai::findPath(pit, {4, kWalkY + 1, 5}, {13, kWalkY + 1, 5}, kZombie);
    EXPECT_EQ(across.status, ai::SearchStatus::Partial);
    for (const glm::ivec3& c : across.path) {
        EXPECT_LT(c.x, 7);
        EXPECT_EQ(c.y, kWalkY + 1);
    }
}

TEST(AiPathfinder, ClosedDoorBlocksZombie) {
    GridNavQuery grid = flatGrid(16, 5, 12);
    grid.fill({8, 1, 0}, {8, 3, 11}, NavCell::Solid); // a wall across the whole grid
    grid.fill({8, 1, 5}, {8, 2, 5}, NavCell::DoorClosed);
    const glm::ivec3 start{4, kWalkY, 5};
    const glm::ivec3 goal{12, kWalkY, 5};
    EXPECT_EQ(ai::findPath(grid, start, goal, kZombie).status, ai::SearchStatus::Partial);
    NavAgent opener = kZombie;
    opener.canOpenDoors = true;
    EXPECT_EQ(ai::findPath(grid, start, goal, opener).status, ai::SearchStatus::Found);
    grid.fill({8, 1, 5}, {8, 2, 5}, NavCell::DoorOpen);
    const ai::PathResult open = ai::findPath(grid, start, goal, kZombie);
    ASSERT_EQ(open.status, ai::SearchStatus::Found);
    EXPECT_NE(std::find(open.path.begin(), open.path.end(), glm::ivec3(8, kWalkY, 5)), open.path.end());
}

TEST(AiPathfinder, UnreachableReturnsBestPartial) {
    GridNavQuery grid = flatGrid(24, 6, 16);
    // A closed 3-high box around the goal (ring at distance 1).
    grid.fill({15, 1, 6}, {17, 3, 8}, NavCell::Solid);
    grid.fill({16, 1, 7}, {16, 3, 7}, NavCell::Passable);
    const glm::ivec3 start{4, kWalkY, 7};
    const glm::ivec3 goal{16, kWalkY, 7};
    const ai::PathResult r = ai::findPath(grid, start, goal, kZombie);
    ASSERT_EQ(r.status, ai::SearchStatus::Partial);
    ASSERT_FALSE(r.path.empty());
    EXPECT_EQ(r.path.front(), start);
    EXPECT_TRUE(isWalkable(grid, r.path, kZombie));
    // The closest standable cells are 2 blocks from the goal, straight across the wall.
    EXPECT_NEAR(ai::octileDistance2D(r.path.back(), goal), 2.0f, 1e-4f);
    EXPECT_EQ(r.path.back().y, kWalkY);

    // The node cap bounds the search: a tiny cap still answers, with a partial path.
    const ai::PathResult capped = ai::findPath(grid, start, goal, kZombie, 16);
    EXPECT_EQ(capped.status, ai::SearchStatus::Partial);
    EXPECT_LE(capped.expandedNodes, 16);
    EXPECT_FALSE(capped.path.empty());

    // Unloaded start: nothing to plan from.
    EXPECT_EQ(ai::findPath(grid, {-5, kWalkY, 7}, goal, kZombie).status, ai::SearchStatus::Failed);
}

TEST(AiPathfinder, BudgetRespectedAcrossSteps) {
    GridNavQuery grid = flatGrid(40, 6, 40);
    grid.fill({20, 1, 2}, {20, 3, 37}, NavCell::Solid); // a long wall: a big detour
    const glm::ivec3 start{10, kWalkY, 20};
    const glm::ivec3 goal{30, kWalkY, 20};
    ai::PathSearch search(start, goal, kZombie);
    int steps = 0;
    int32_t before = 0;
    while (search.step(grid, 7) == ai::SearchStatus::InProgress) {
        EXPECT_LE(search.expandedNodes() - before, 7);
        EXPECT_GT(search.expandedNodes(), before);
        EXPECT_TRUE(search.path().empty());
        before = search.expandedNodes();
        ++steps;
        ASSERT_LT(steps, 10000);
    }
    EXPECT_LE(search.expandedNodes() - before, 7);
    EXPECT_GT(steps, 5);
    ASSERT_EQ(search.status(), ai::SearchStatus::Found);
    EXPECT_LE(search.expandedNodes(), ai::PathSearch::DEFAULT_MAX_NODES);
    // Same answer as an unsliced search; further steps change nothing.
    const ai::PathResult whole = ai::findPath(grid, start, goal, kZombie);
    EXPECT_EQ(std::vector<glm::ivec3>(search.path().begin(), search.path().end()), whole.path);
    EXPECT_EQ(search.step(grid, 100), ai::SearchStatus::Found);
}

TEST(AiPathfinder, Deterministic) {
    GridNavQuery grid = flatGrid(32, 6, 32);
    // A small maze of pillars, steps and water.
    for (int32_t i = 2; i < 30; i += 4) {
        grid.fill({i, 1, 2}, {i, 2, 26}, NavCell::Solid);
        grid.fill({i + 2, 1, 6}, {i + 2, 1, 29}, NavCell::Solid);
        grid.fill({i + 1, 1, 28}, {i + 1, 1, 30}, NavCell::Liquid);
    }
    const glm::ivec3 start{1, kWalkY, 1};
    const glm::ivec3 goal{29, kWalkY, 29};
    const ai::PathResult a = ai::findPath(grid, start, goal, kZombie);
    const ai::PathResult b = ai::findPath(grid, start, goal, kZombie);
    EXPECT_EQ(a.status, b.status);
    EXPECT_EQ(a.path, b.path);
    EXPECT_EQ(a.expandedNodes, b.expandedNodes);
    ai::PathSearch sliced(start, goal, kZombie);
    while (sliced.step(grid, 13) == ai::SearchStatus::InProgress) {
    }
    EXPECT_EQ(sliced.status(), a.status);
    EXPECT_EQ(std::vector<glm::ivec3>(sliced.path().begin(), sliced.path().end()), a.path);
}
