// Amanatides-Woo voxel traversal: exact visited-voxel sequences.
#include "acoustics/VoxelDda.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdlib>
#include <limits>
#include <random>
#include <vector>

namespace {

using betacraft::acoustics::MAX_DDA_STEPS;
using betacraft::acoustics::TraversalResult;
using betacraft::acoustics::traverseVoxels;
using betacraft::core::BlockPos;

struct Visit {
    BlockPos pos;
    float tEnter;
    float tExit;
};

std::vector<Visit> collect(const glm::vec3& from, const glm::vec3& to, TraversalResult* outResult = nullptr,
                           uint32_t maxSteps = MAX_DDA_STEPS) {
    std::vector<Visit> visits;
    const TraversalResult r = traverseVoxels(
        from, to,
        [&](const BlockPos& p, float tEnter, float tExit) {
            visits.push_back({p, tEnter, tExit});
            return true;
        },
        maxSteps);
    if (outResult != nullptr) {
        *outResult = r;
    }
    return visits;
}

std::vector<BlockPos> positions(const std::vector<Visit>& visits) {
    std::vector<BlockPos> out;
    out.reserve(visits.size());
    for (const Visit& v : visits) {
        out.push_back(v.pos);
    }
    return out;
}

} // namespace

TEST(VoxelDda, StartEqualsEndVisitsSingleVoxel) {
    TraversalResult r;
    const auto v = collect({3.25f, 4.5f, -2.75f}, {3.25f, 4.5f, -2.75f}, &r);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].pos, (BlockPos{3, 4, -3}));
    EXPECT_FLOAT_EQ(v[0].tEnter, 0.0f);
    EXPECT_FLOAT_EQ(v[0].tExit, 1.0f);
    EXPECT_TRUE(r.reachedEnd);
    EXPECT_FALSE(r.truncated);
    EXPECT_EQ(r.visitedCount, 1u);
}

TEST(VoxelDda, SameVoxelDifferentPointsVisitsSingleVoxel) {
    const auto v = collect({0.1f, 0.1f, 0.1f}, {0.9f, 0.8f, 0.7f});
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].pos, (BlockPos{0, 0, 0}));
}

TEST(VoxelDda, AxisAlignedPositiveX) {
    const auto v = collect({0.5f, 0.5f, 0.5f}, {4.5f, 0.5f, 0.5f});
    const std::vector<BlockPos> expected{{0, 0, 0}, {1, 0, 0}, {2, 0, 0}, {3, 0, 0}, {4, 0, 0}};
    EXPECT_EQ(positions(v), expected);
}

TEST(VoxelDda, AxisAlignedNegativeY) {
    const auto v = collect({0.5f, 2.5f, 0.5f}, {0.5f, -1.5f, 0.5f});
    const std::vector<BlockPos> expected{{0, 2, 0}, {0, 1, 0}, {0, 0, 0}, {0, -1, 0}, {0, -2, 0}};
    EXPECT_EQ(positions(v), expected);
}

TEST(VoxelDda, AxisAlignedNegativeZAcrossOrigin) {
    const auto v = collect({0.5f, 0.5f, 1.5f}, {0.5f, 0.5f, -1.5f});
    const std::vector<BlockPos> expected{{0, 0, 1}, {0, 0, 0}, {0, 0, -1}, {0, 0, -2}};
    EXPECT_EQ(positions(v), expected);
}

TEST(VoxelDda, ParametricIntervalsAreContiguousAndCoverUnitRange) {
    const auto v = collect({0.5f, 0.5f, 0.5f}, {4.5f, 0.5f, 0.5f});
    ASSERT_EQ(v.size(), 5u);
    EXPECT_FLOAT_EQ(v.front().tEnter, 0.0f);
    EXPECT_FLOAT_EQ(v.back().tExit, 1.0f);
    for (size_t i = 1; i < v.size(); ++i) {
        EXPECT_FLOAT_EQ(v[i].tEnter, v[i - 1].tExit);
    }
    // Interior voxels are each crossed over exactly 1/4 of the ray.
    EXPECT_NEAR(v[1].tExit - v[1].tEnter, 0.25f, 1e-6f);
    EXPECT_NEAR(v[0].tExit - v[0].tEnter, 0.125f, 1e-6f);
}

TEST(VoxelDda, ShallowDiagonalXY) {
    // Slope 1/2 in XY: crosses x=1 at t=0.25 (y=0.75), y=1 at t=0.375, x=2 at 0.5 ...
    const auto v = collect({0.5f, 0.5f, 0.5f}, {4.5f, 2.5f, 0.5f});
    const std::vector<BlockPos> expected{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {2, 1, 0},
                                         {3, 1, 0}, {3, 2, 0}, {4, 2, 0}};
    EXPECT_EQ(positions(v), expected);
}

TEST(VoxelDda, DiagonalNegativeDirectionsAllAxes) {
    const auto v = collect({0.2f, 0.3f, 0.4f}, {-1.6f, -1.7f, -1.8f});
    TraversalResult r;
    (void)collect({0.2f, 0.3f, 0.4f}, {-1.6f, -1.7f, -1.8f}, &r);
    ASSERT_FALSE(v.empty());
    EXPECT_EQ(v.front().pos, (BlockPos{0, 0, 0}));
    EXPECT_EQ(v.back().pos, (BlockPos{-2, -2, -2}));
    EXPECT_EQ(v.size(), 7u); // 1 + |dx| + |dy| + |dz| = 1 + 2 + 2 + 2
    EXPECT_TRUE(r.reachedEnd);
    // Each step moves exactly one axis by exactly one voxel (6-connected path).
    for (size_t i = 1; i < v.size(); ++i) {
        const int dist = std::abs(v[i].pos.x - v[i - 1].pos.x) + std::abs(v[i].pos.y - v[i - 1].pos.y) +
                         std::abs(v[i].pos.z - v[i - 1].pos.z);
        EXPECT_EQ(dist, 1) << "step " << i;
    }
    // Crossings in time order: z boundary at t=0.4/2.2, y at 0.3/2.0, x at 0.2/1.8.
    // t(z=0)=0.1818, t(y=0)=0.15, t(x=0)=0.111 -> x first, then y, then z.
    EXPECT_EQ(v[1].pos, (BlockPos{-1, 0, 0}));
    EXPECT_EQ(v[2].pos, (BlockPos{-1, -1, 0}));
    EXPECT_EQ(v[3].pos, (BlockPos{-1, -1, -1}));
}

TEST(VoxelDda, ExactCornerCrossingBreaksTiesXThenYThenZ) {
    // Passes exactly through the edge (1,1) and corner points: deterministic tie-break X<Y<Z.
    const auto v = collect({0.5f, 0.5f, 0.5f}, {2.5f, 2.5f, 0.5f});
    const std::vector<BlockPos> expected{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {2, 1, 0}, {2, 2, 0}};
    EXPECT_EQ(positions(v), expected);

    const auto v3 = collect({0.5f, 0.5f, 0.5f}, {1.5f, 1.5f, 1.5f});
    const std::vector<BlockPos> expected3{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {1, 1, 1}};
    EXPECT_EQ(positions(v3), expected3);
}

TEST(VoxelDda, ExactCornerCrossingNegativeDirection) {
    const auto v = collect({0.5f, 0.5f, 0.5f}, {-1.5f, -1.5f, 0.5f});
    const std::vector<BlockPos> expected{{0, 0, 0}, {-1, 0, 0}, {-1, -1, 0}, {-2, -1, 0}, {-2, -2, 0}};
    EXPECT_EQ(positions(v), expected);
}

TEST(VoxelDda, StartExactlyOnIntegerBoundaryUsesFloor) {
    // x = 1.0 belongs to voxel 1; going +x reaches voxel 3 (x = 3.0 -> voxel 3).
    const auto v = collect({1.0f, 0.5f, 0.5f}, {3.0f, 0.5f, 0.5f});
    const std::vector<BlockPos> expected{{1, 0, 0}, {2, 0, 0}, {3, 0, 0}};
    EXPECT_EQ(positions(v), expected);
    EXPECT_NEAR(v[0].tExit, 0.5f, 1e-6f);
    EXPECT_NEAR(v[2].tEnter, 1.0f, 1e-6f); // End voxel touched only at the very end.
}

TEST(VoxelDda, NegativeDirectionFromIntegerBoundaryLeavesImmediately) {
    // From x = 2.0 (voxel 2) towards -x: voxel 2 is only touched at t = 0.
    const auto v = collect({2.0f, 0.5f, 0.5f}, {0.5f, 0.5f, 0.5f});
    const std::vector<BlockPos> expected{{2, 0, 0}, {1, 0, 0}, {0, 0, 0}};
    EXPECT_EQ(positions(v), expected);
    EXPECT_NEAR(v[0].tExit - v[0].tEnter, 0.0f, 1e-6f);
}

TEST(VoxelDda, NegativeCoordinatesFloorCorrectly) {
    const auto v = collect({-0.5f, -0.5f, -0.5f}, {-0.1f, -0.9f, -0.2f});
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].pos, (BlockPos{-1, -1, -1}));
}

TEST(VoxelDda, VisitorCanAbortEarly) {
    int calls = 0;
    const TraversalResult r = traverseVoxels(glm::vec3{0.5f}, glm::vec3{10.5f, 0.5f, 0.5f},
                                             [&](const BlockPos&, float, float) { return ++calls < 3; });
    EXPECT_EQ(calls, 3);
    EXPECT_EQ(r.visitedCount, 3u);
    EXPECT_FALSE(r.reachedEnd);
    EXPECT_FALSE(r.truncated);
}

TEST(VoxelDda, StepBudgetTruncatesLongRays) {
    TraversalResult r;
    const auto v = collect({0.5f, 0.5f, 0.5f}, {200.5f, 0.5f, 0.5f}, &r);
    EXPECT_EQ(v.size(), static_cast<size_t>(MAX_DDA_STEPS) + 1u);
    EXPECT_TRUE(r.truncated);
    EXPECT_FALSE(r.reachedEnd);
    EXPECT_EQ(v.back().pos, (BlockPos{static_cast<int32_t>(MAX_DDA_STEPS), 0, 0}));
}

TEST(VoxelDda, CustomStepBudgetZeroVisitsOnlyStart) {
    TraversalResult r;
    const auto v = collect({0.5f, 0.5f, 0.5f}, {5.5f, 0.5f, 0.5f}, &r, 0u);
    ASSERT_EQ(v.size(), 1u);
    EXPECT_EQ(v[0].pos, (BlockPos{0, 0, 0}));
    EXPECT_TRUE(r.truncated);
}

TEST(VoxelDda, SpecBoundOf144StepsCoversMaxAudibleRange) {
    EXPECT_EQ(MAX_DDA_STEPS, 144u);
    EXPECT_EQ(betacraft::acoustics::MAX_AUDIBLE_RANGE, 48);
    TraversalResult r;
    // Worst case inside the audible cube: 48 voxels on every axis.
    (void)collect({0.5f, 0.5f, 0.5f}, {48.4f, 48.3f, 48.2f}, &r);
    EXPECT_TRUE(r.reachedEnd);
    EXPECT_EQ(r.visitedCount, 145u);
}

TEST(VoxelDda, NonFiniteInputVisitsNothing) {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    TraversalResult r;
    EXPECT_TRUE(collect({nan, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, &r).empty());
    EXPECT_FALSE(r.reachedEnd);
    EXPECT_TRUE(collect({0.0f, 0.0f, 0.0f}, {1.0f, inf, 1.0f}, &r).empty());
    EXPECT_FALSE(r.reachedEnd);
}

TEST(VoxelDda, HugeCoordinatesAreRejectedInsteadOfOverflowing) {
    TraversalResult r;
    EXPECT_TRUE(collect({3.0e9f, 0.0f, 0.0f}, {3.0e9f, 1.0f, 0.0f}, &r).empty());
    EXPECT_FALSE(r.reachedEnd);
}

TEST(VoxelDda, PropertyRandomRaysAreSixConnectedAndEndAtTargetVoxel) {
    std::mt19937 rng(0xACu);
    std::uniform_real_distribution<float> coord(-40.0f, 40.0f);
    for (int iter = 0; iter < 2000; ++iter) {
        const glm::vec3 a{coord(rng), coord(rng), coord(rng)};
        const glm::vec3 b{coord(rng), coord(rng), coord(rng)};
        TraversalResult r;
        const auto v = collect(a, b, &r, 1000u);
        ASSERT_TRUE(r.reachedEnd);
        ASSERT_FALSE(v.empty());
        const BlockPos start{static_cast<int32_t>(std::floor(a.x)), static_cast<int32_t>(std::floor(a.y)),
                             static_cast<int32_t>(std::floor(a.z))};
        const BlockPos end{static_cast<int32_t>(std::floor(b.x)), static_cast<int32_t>(std::floor(b.y)),
                           static_cast<int32_t>(std::floor(b.z))};
        ASSERT_EQ(v.front().pos, start);
        ASSERT_EQ(v.back().pos, end);
        const size_t manhattan =
            static_cast<size_t>(std::abs(end.x - start.x) + std::abs(end.y - start.y) + std::abs(end.z - start.z));
        ASSERT_EQ(v.size(), manhattan + 1u);
        float prevExit = 0.0f;
        for (size_t i = 0; i < v.size(); ++i) {
            ASSERT_LE(v[i].tEnter, v[i].tExit + 1e-6f);
            ASSERT_NEAR(v[i].tEnter, prevExit, 1e-5f);
            prevExit = v[i].tExit;
            if (i > 0) {
                const int d = std::abs(v[i].pos.x - v[i - 1].pos.x) + std::abs(v[i].pos.y - v[i - 1].pos.y) +
                              std::abs(v[i].pos.z - v[i - 1].pos.z);
                ASSERT_EQ(d, 1);
            }
            // The midpoint of each visited interval must lie inside (or on the border of) that voxel.
            const float tm = 0.5f * (v[i].tEnter + v[i].tExit);
            const glm::vec3 p = a + (b - a) * tm;
            ASSERT_GE(p.x, static_cast<float>(v[i].pos.x) - 1e-3f);
            ASSERT_LE(p.x, static_cast<float>(v[i].pos.x + 1) + 1e-3f);
            ASSERT_GE(p.y, static_cast<float>(v[i].pos.y) - 1e-3f);
            ASSERT_LE(p.y, static_cast<float>(v[i].pos.y + 1) + 1e-3f);
            ASSERT_GE(p.z, static_cast<float>(v[i].pos.z) - 1e-3f);
            ASSERT_LE(p.z, static_cast<float>(v[i].pos.z + 1) + 1e-3f);
        }
        ASSERT_FLOAT_EQ(prevExit, 1.0f);
    }
}

TEST(VoxelDda, PropertyReverseRayVisitsSameVoxelSetForGenericRays) {
    std::mt19937 rng(1234u);
    std::uniform_real_distribution<float> coord(-20.0f, 20.0f);
    for (int iter = 0; iter < 500; ++iter) {
        const glm::vec3 a{coord(rng), coord(rng), coord(rng)};
        const glm::vec3 b{coord(rng), coord(rng), coord(rng)};
        auto fwd = positions(collect(a, b, nullptr, 1000u));
        auto bwd = positions(collect(b, a, nullptr, 1000u));
        ASSERT_EQ(fwd.size(), bwd.size());
        // Generic (non-tie) rays: backward traversal is the exact reverse sequence.
        for (size_t i = 0; i < fwd.size(); ++i) {
            ASSERT_EQ(fwd[i], bwd[bwd.size() - 1 - i]) << "iter " << iter << " i " << i;
        }
    }
}

// --- Endpoints exactly on integers in negative directions, rays lying on grid planes -------------

TEST(VoxelDdaBoundaries, NegativeRayEndingExactlyOnAnIntegerStopsInThatVoxel) {
    TraversalResult r;
    // Ends on the lower face x = 1.0 of voxel 1 = [1, 2): voxel 1 is the last one, voxel 0 never entered.
    const auto v = collect({2.5f, 0.5f, 0.5f}, {1.0f, 0.5f, 0.5f}, &r);
    EXPECT_EQ(positions(v), (std::vector<BlockPos>{{2, 0, 0}, {1, 0, 0}}));
    ASSERT_EQ(v.size(), 2u);
    EXPECT_NEAR(v[0].tExit, 1.0f / 3.0f, 1e-6f); // x = 2.0
    EXPECT_NEAR(v[1].tEnter, 1.0f / 3.0f, 1e-6f);
    EXPECT_FLOAT_EQ(v[1].tExit, 1.0f);
    EXPECT_TRUE(r.reachedEnd);
    EXPECT_FALSE(r.truncated);
}

TEST(VoxelDdaBoundaries, NegativeRayEndingOnTheLowerFaceOfItsStartVoxel) {
    TraversalResult r;
    const auto v = collect({2.5f, 0.5f, 0.5f}, {2.0f, 0.5f, 0.5f}, &r); // Still voxel 2
    EXPECT_EQ(positions(v), (std::vector<BlockPos>{{2, 0, 0}}));
    EXPECT_TRUE(r.reachedEnd);
}

TEST(VoxelDdaBoundaries, NegativeRayEndingOnANegativeInteger) {
    const auto v = collect({-0.5f, 0.5f, 0.5f}, {-2.0f, 0.5f, 0.5f});
    EXPECT_EQ(positions(v), (std::vector<BlockPos>{{-1, 0, 0}, {-2, 0, 0}}));
}

TEST(VoxelDdaBoundaries, NegativeYAndZEndingOnIntegers) {
    EXPECT_EQ(positions(collect({0.5f, 3.5f, 0.5f}, {0.5f, 1.0f, 0.5f})),
              (std::vector<BlockPos>{{0, 3, 0}, {0, 2, 0}, {0, 1, 0}}));
    EXPECT_EQ(positions(collect({0.5f, 0.5f, 0.5f}, {0.5f, 0.5f, -1.0f})),
              (std::vector<BlockPos>{{0, 0, 0}, {0, 0, -1}}));
}

TEST(VoxelDdaBoundaries, RayLyingOnAGridPlaneStaysInTheUpperVoxelRow) {
    // y == 1.0 exactly for the whole ray: floor() puts it in row y = 1, in both directions.
    const std::vector<BlockPos> row{{0, 1, 0}, {1, 1, 0}, {2, 1, 0}, {3, 1, 0}, {4, 1, 0}};
    EXPECT_EQ(positions(collect({0.5f, 1.0f, 0.5f}, {4.5f, 1.0f, 0.5f})), row);
    std::vector<BlockPos> reversed(row.rbegin(), row.rend());
    EXPECT_EQ(positions(collect({4.5f, 1.0f, 0.5f}, {0.5f, 1.0f, 0.5f})), reversed);
}

TEST(VoxelDdaBoundaries, RayAlongAVoxelEdgeStaysInOneRow) {
    EXPECT_EQ(positions(collect({0.5f, 1.0f, 2.0f}, {3.5f, 1.0f, 2.0f})),
              (std::vector<BlockPos>{{0, 1, 2}, {1, 1, 2}, {2, 1, 2}, {3, 1, 2}}));
    EXPECT_EQ(positions(collect({-0.5f, -1.0f, -2.0f}, {-3.5f, -1.0f, -2.0f})),
              (std::vector<BlockPos>{{-1, -1, -2}, {-2, -1, -2}, {-3, -1, -2}, {-4, -1, -2}}));
}

TEST(VoxelDdaBoundaries, DiagonalOnAGridPlaneCrossesOnlyInPlane) {
    // z == 0.0 exactly while x and y move: every voxel is in layer z = 0.
    const auto v = collect({0.5f, 0.5f, 0.0f}, {2.7f, 1.3f, 0.0f});
    for (const Visit& visit : v) {
        EXPECT_EQ(visit.pos.z, 0);
    }
    EXPECT_EQ(v.back().pos, (BlockPos{2, 1, 0}));
    EXPECT_EQ(v.size(), 4u);
}

TEST(VoxelDdaBoundaries, DiagonalEndingExactlyOnACorner) {
    TraversalResult r;
    const auto v = collect({0.5f, 0.5f, 0.5f}, {2.0f, 2.0f, 0.5f}, &r);
    EXPECT_EQ(positions(v), (std::vector<BlockPos>{{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {2, 1, 0}, {2, 2, 0}}));
    ASSERT_EQ(v.size(), 5u);
    EXPECT_FLOAT_EQ(v[1].tEnter, v[1].tExit); // Tie at the (1,1) corner: zero-length X neighbour
    EXPECT_FLOAT_EQ(v[3].tEnter, 1.0f);       // Tie at the end corner
    EXPECT_FLOAT_EQ(v[4].tExit, 1.0f);
    EXPECT_TRUE(r.reachedEnd);
}

TEST(VoxelDdaBoundaries, NegativeDiagonalStartingAndEndingOnCorners) {
    const auto v = collect({2.0f, 2.0f, 0.5f}, {0.0f, 0.0f, 0.5f});
    EXPECT_EQ(positions(v),
              (std::vector<BlockPos>{{2, 2, 0}, {1, 2, 0}, {1, 1, 0}, {0, 1, 0}, {0, 0, 0}}));
}
