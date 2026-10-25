// Octile heuristic: admissible (never above the real cost) and consistent (never drops
// by more than the cost of a move), so A* returns optimal paths without reopening nodes.
#include "test_support/AiTestSupport.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <random>

using namespace betacraft;
using namespace betacraft::test::ai;

TEST(AiOctileHeuristic, AdmissibleOnDiagonal) {
    EXPECT_NEAR(ai::octileDistance2D({0, 1, 0}, {5, 1, 5}), 5.0f * std::sqrt(2.0f), 1e-4f);
    EXPECT_NEAR(ai::octileDistance2D({0, 1, 0}, {7, 9, 3}), 4.0f + 3.0f * std::sqrt(2.0f), 1e-4f);
    EXPECT_FLOAT_EQ(ai::octileDistance2D({3, 1, 3}, {3, 40, 3}), 0.0f); // Y ignored

    // On an open floor the optimal diagonal path costs exactly the heuristic.
    const GridNavQuery grid = flatGrid(20, 4, 20);
    const glm::ivec3 start{2, kWalkY, 2};
    const glm::ivec3 goal{12, kWalkY, 12};
    const ai::PathResult r = ai::findPath(grid, start, goal, kZombie);
    ASSERT_EQ(r.status, ai::SearchStatus::Found);
    EXPECT_EQ(r.path.size(), 11U);
    const float cost = pathCost(grid, r.path, kZombie);
    EXPECT_GE(cost + 1e-4f, ai::octileDistance2D(start, goal));
    EXPECT_NEAR(cost, ai::octileDistance2D(start, goal), 1e-3f);
}

TEST(AiOctileHeuristic, ConsistentRandomPairs) {
    std::mt19937 rng(173);
    std::uniform_int_distribution<int32_t> coord(-1000, 1000);
    for (int i = 0; i < 2000; ++i) {
        const glm::ivec3 a{coord(rng), coord(rng) % 64, coord(rng)};
        const glm::ivec3 b{coord(rng), coord(rng) % 64, coord(rng)};
        const glm::ivec3 c{coord(rng), coord(rng) % 64, coord(rng)};
        EXPECT_LE(ai::octileDistance2D(a, c), ai::octileDistance2D(a, b) + ai::octileDistance2D(b, c) + 1e-2f);
        EXPECT_FLOAT_EQ(ai::octileDistance2D(a, b), ai::octileDistance2D(b, a));
    }

    // Every legal move on a cluttered grid costs at least the heuristic drop it causes.
    GridNavQuery grid = flatGrid(24, 8, 24);
    std::uniform_int_distribution<int32_t> cell(0, 23);
    std::uniform_int_distribution<int32_t> height(1, 3);
    std::uniform_int_distribution<int32_t> kind(0, 9);
    for (int i = 0; i < 120; ++i) {
        const int32_t x = cell(rng);
        const int32_t z = cell(rng);
        const int32_t k = kind(rng);
        const NavCell what = k < 6 ? NavCell::Solid : k < 8 ? NavCell::Liquid : k < 9 ? NavCell::Ladder : NavCell::Hazard;
        grid.fill({x, 1, z}, {x, height(rng), z}, what);
    }
    const glm::ivec3 goal{20, kWalkY, 20};
    int checked = 0;
    for (int32_t y = 1; y < 6; ++y) {
        for (int32_t z = 0; z < 24; ++z) {
            for (int32_t x = 0; x < 24; ++x) {
                const glm::ivec3 from{x, y, z};
                for (int32_t dy = -3; dy <= 1; ++dy) {
                    for (int32_t dz = -1; dz <= 1; ++dz) {
                        for (int32_t dx = -1; dx <= 1; ++dx) {
                            const glm::ivec3 to = from + glm::ivec3(dx, dy, dz);
                            if (const auto cost = ai::moveCost(grid, from, to, kZombie)) {
                                ++checked;
                                EXPECT_LE(ai::octileDistance2D(from, goal),
                                          *cost + ai::octileDistance2D(to, goal) + 1e-4f);
                            }
                        }
                    }
                }
            }
        }
    }
    EXPECT_GT(checked, 1000);
}
