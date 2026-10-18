// P4.2 — voxel occlusion measurement and the spec's cutoff / gain mapping.
#include "AcousticsTestUtils.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <random>

namespace {

using namespace betacraft::acoustics;
using betacraft::acoustics::test::FakeVoxelGrid;
using betacraft::core::BlockPos;

} // namespace

TEST(Occlusion, OpenAirHasNoOcclusion) {
    FakeVoxelGrid grid;
    const OcclusionSample s = measureOcclusion(grid, {0.5f, 0.5f, 0.5f}, {10.5f, 3.5f, -4.5f});
    EXPECT_TRUE(s.valid);
    EXPECT_FALSE(s.truncated);
    EXPECT_EQ(s.solidVoxels, 0u);
    EXPECT_FLOAT_EQ(s.solidThickness, 0.0f);
    EXPECT_NEAR(s.pathLength, std::sqrt(100.0f + 9.0f + 25.0f), 1e-4f);
}

TEST(Occlusion, SingleAxisAlignedWallCountsOneFullBlock) {
    FakeVoxelGrid grid;
    grid.fillBox({2, -3, -3}, {2, 3, 3});
    const OcclusionSample s = measureOcclusion(grid, {0.5f, 0.5f, 0.5f}, {5.5f, 0.5f, 0.5f});
    EXPECT_EQ(s.solidVoxels, 1u);
    EXPECT_NEAR(s.solidThickness, 1.0f, 1e-5f);
    EXPECT_NEAR(s.pathLength, 5.0f, 1e-5f);
}

TEST(Occlusion, ThickWallCountsEveryVoxel) {
    FakeVoxelGrid grid;
    grid.fillBox({2, -3, -3}, {4, 3, 3});
    const OcclusionSample s = measureOcclusion(grid, {0.5f, 0.5f, 0.5f}, {7.5f, 0.5f, 0.5f});
    EXPECT_EQ(s.solidVoxels, 3u);
    EXPECT_NEAR(s.solidThickness, 3.0f, 1e-5f);
}

TEST(Occlusion, DiagonalCrossingOfSlabIsThickerThanAxisAligned) {
    FakeVoxelGrid grid;
    grid.fillBox({3, -10, -10}, {3, 10, 10});
    const OcclusionSample s = measureOcclusion(grid, {0.5f, 0.5f, 0.5f}, {6.5f, 6.5f, 0.5f});
    EXPECT_GE(s.solidVoxels, 1u);
    EXPECT_NEAR(s.solidThickness, std::sqrt(2.0f), 1e-4f); // 45 degrees through a 1-block slab
}

TEST(Occlusion, ListenerAndSourceVoxelsAreIgnored) {
    // Head inside a block (or a block-break sound emitted from the broken block) must not muffle.
    FakeVoxelGrid grid;
    grid.set({0, 0, 0});
    grid.set({5, 0, 0});
    const OcclusionSample s = measureOcclusion(grid, {0.5f, 0.5f, 0.5f}, {5.5f, 0.5f, 0.5f});
    EXPECT_EQ(s.solidVoxels, 0u);
    EXPECT_FLOAT_EQ(s.solidThickness, 0.0f);
}

TEST(Occlusion, SameVoxelIsNeverOccluded) {
    FakeVoxelGrid grid;
    grid.set({0, 0, 0});
    const OcclusionSample s = measureOcclusion(grid, {0.2f, 0.2f, 0.2f}, {0.8f, 0.8f, 0.8f});
    EXPECT_TRUE(s.valid);
    EXPECT_EQ(s.solidVoxels, 0u);
}

TEST(Occlusion, EndpointsAreNotQueried) {
    FakeVoxelGrid grid;
    (void)measureOcclusion(grid, {0.5f, 0.5f, 0.5f}, {10.5f, 0.5f, 0.5f});
    EXPECT_EQ(grid.queries(), 9u); // voxels 1..9 only
}

TEST(Occlusion, QueryCountBoundedBySpecForFarSources) {
    FakeVoxelGrid grid;
    const OcclusionSample s = measureOcclusion(grid, {0.5f, 0.5f, 0.5f}, {300.5f, 200.5f, -100.5f});
    EXPECT_TRUE(s.truncated);
    EXPECT_LE(grid.queries(), static_cast<uint64_t>(MAX_DDA_STEPS));
}

TEST(Occlusion, NonFiniteInputIsInvalidAndUnoccluded) {
    FakeVoxelGrid grid;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const OcclusionSample s = measureOcclusion(grid, {nan, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f});
    EXPECT_FALSE(s.valid);
    EXPECT_EQ(s.solidVoxels, 0u);
    const OcclusionTarget t = occlusionTargetFor(s);
    EXPECT_FLOAT_EQ(t.gain, 1.0f);
    EXPECT_FLOAT_EQ(t.cutoffHz, 20000.0f);
}

TEST(Occlusion, PropertySymmetricSolidCountForGenericRays) {
    std::mt19937 rng(77u);
    std::uniform_int_distribution<int> cell(-8, 8);
    FakeVoxelGrid grid;
    for (int i = 0; i < 600; ++i) {
        grid.set({cell(rng), cell(rng), cell(rng)});
    }
    std::uniform_real_distribution<float> coord(-9.0f, 9.0f);
    for (int iter = 0; iter < 300; ++iter) {
        const glm::vec3 a{coord(rng), coord(rng), coord(rng)};
        const glm::vec3 b{coord(rng), coord(rng), coord(rng)};
        const OcclusionSample f = measureOcclusion(grid, a, b);
        const OcclusionSample r = measureOcclusion(grid, b, a);
        ASSERT_EQ(f.solidVoxels, r.solidVoxels);
        ASSERT_NEAR(f.solidThickness, r.solidThickness, 1e-3f);
        ASSERT_LE(f.solidThickness, f.pathLength + 1e-4f);
        ASSERT_GE(f.solidThickness, 0.0f);
    }
}

// --- Mapping (spec: f_cutoff = clamp(20000 * 0.65^N, 400, 20000)) ---------------------------------

TEST(OcclusionMapping, CutoffMatchesSpecFormula) {
    EXPECT_FLOAT_EQ(occlusionCutoffHz(0.0f), 20000.0f);
    EXPECT_NEAR(occlusionCutoffHz(1.0f), 13000.0f, 0.01f);
    EXPECT_NEAR(occlusionCutoffHz(2.0f), 8450.0f, 0.01f);
    EXPECT_NEAR(occlusionCutoffHz(5.0f), 20000.0f * std::pow(0.65f, 5.0f), 0.01f);
    EXPECT_NEAR(occlusionCutoffHz(9.0f), 20000.0f * std::pow(0.65f, 9.0f), 0.01f); // ~414 Hz
}

TEST(OcclusionMapping, CutoffClampsToSpecFloor) {
    EXPECT_FLOAT_EQ(occlusionCutoffHz(10.0f), 400.0f);
    EXPECT_FLOAT_EQ(occlusionCutoffHz(1000.0f), 400.0f);
}

TEST(OcclusionMapping, InvalidCountsMapToUnoccluded) {
    EXPECT_FLOAT_EQ(occlusionCutoffHz(-3.0f), 20000.0f);
    EXPECT_FLOAT_EQ(occlusionCutoffHz(std::numeric_limits<float>::quiet_NaN()), 20000.0f);
    EXPECT_FLOAT_EQ(occlusionGain(-1.0f), 1.0f);
    EXPECT_FLOAT_EQ(occlusionGain(std::numeric_limits<float>::quiet_NaN()), 1.0f);
    EXPECT_FLOAT_EQ(occlusionCutoffHz(std::numeric_limits<float>::infinity()), 400.0f);
}

TEST(OcclusionMapping, GainIsMonotoneAndBounded) {
    const OcclusionParams p{};
    float prev = occlusionGain(0.0f);
    EXPECT_FLOAT_EQ(prev, 1.0f);
    for (float n = 0.25f; n <= 40.0f; n += 0.25f) {
        const float g = occlusionGain(n);
        EXPECT_LE(g, prev);
        EXPECT_GE(g, p.minGain);
        prev = g;
    }
    EXPECT_NEAR(occlusionGain(1.0f), p.gainFactorPerBlock, 1e-6f);
    EXPECT_FLOAT_EQ(occlusionGain(1000.0f), p.minGain);
}

TEST(OcclusionMapping, CutoffMonotoneNonIncreasing) {
    float prev = occlusionCutoffHz(0.0f);
    for (float n = 0.5f; n <= 20.0f; n += 0.5f) {
        const float c = occlusionCutoffHz(n);
        EXPECT_LE(c, prev);
        prev = c;
    }
}

TEST(OcclusionMapping, TargetUsesCountForCutoffAndThicknessForGain) {
    OcclusionSample s;
    s.valid = true;
    s.solidVoxels = 2;
    s.solidThickness = 1.5f;
    const OcclusionTarget t = occlusionTargetFor(s);
    EXPECT_FLOAT_EQ(t.cutoffHz, occlusionCutoffHz(2.0f));
    EXPECT_FLOAT_EQ(t.gain, occlusionGain(1.5f));
}

TEST(OcclusionMapping, CustomParamsAreHonoured) {
    OcclusionParams p;
    p.maxCutoffHz = 10000.0f;
    p.minCutoffHz = 1000.0f;
    p.cutoffFactorPerBlock = 0.5f;
    EXPECT_FLOAT_EQ(occlusionCutoffHz(0.0f, p), 10000.0f);
    EXPECT_FLOAT_EQ(occlusionCutoffHz(1.0f, p), 5000.0f);
    EXPECT_FLOAT_EQ(occlusionCutoffHz(4.0f, p), 1000.0f);
}

// --- Exact edge / corner ties: zero-length visits (P4.2 "chaque bloc solide traversé") ----------

TEST(OcclusionTies, GrazingTheEdgeOfOneBlockDoesNotOcclude) {
    // The ray passes exactly through the corner point (1,1): it only touches the edge of (1,0,0)
    // (zero path length inside), so it must not count as a traversed solid block.
    for (const BlockPos solid : {BlockPos{1, 0, 0}, BlockPos{0, 1, 0}, BlockPos{2, 1, 0}, BlockPos{1, 2, 0}}) {
        FakeVoxelGrid grid;
        grid.set(solid);
        const OcclusionSample f = measureOcclusion(grid, {0.5f, 0.5f, 0.5f}, {2.5f, 2.5f, 0.5f});
        const OcclusionSample r = measureOcclusion(grid, {2.5f, 2.5f, 0.5f}, {0.5f, 0.5f, 0.5f});
        EXPECT_EQ(f.solidVoxels, 0u) << solid.x << "," << solid.y;
        EXPECT_EQ(r.solidVoxels, 0u) << solid.x << "," << solid.y;
        EXPECT_FLOAT_EQ(occlusionTargetFor(f).cutoffHz, 20000.0f);
    }
}

TEST(OcclusionTies, DiagonalSeamOfTwoBlocksIsSealedInBothDirections) {
    // Two blocks meeting at an edge form a wall in-game: a ray through the seam counts one block.
    FakeVoxelGrid grid;
    grid.set({1, 0, 0});
    grid.set({0, 1, 0});
    const OcclusionSample f = measureOcclusion(grid, {0.5f, 0.5f, 0.5f}, {2.5f, 2.5f, 0.5f});
    const OcclusionSample r = measureOcclusion(grid, {2.5f, 2.5f, 0.5f}, {0.5f, 0.5f, 0.5f});
    EXPECT_EQ(f.solidVoxels, 1u);
    EXPECT_EQ(r.solidVoxels, 1u);
    EXPECT_FLOAT_EQ(f.solidThickness, 0.0f); // No material actually lies on the path
}

TEST(OcclusionTies, CornerTieIsSealedOnlyWhenEveryPathThroughTheCornerIsBlocked) {
    const glm::vec3 a{0.5f, 0.5f, 0.5f};
    const glm::vec3 b{2.5f, 2.5f, 2.5f};
    {
        FakeVoxelGrid grid; // All three face neighbours of the start voxel: sealed corner
        grid.set({1, 0, 0});
        grid.set({0, 1, 0});
        grid.set({0, 0, 1});
        EXPECT_EQ(measureOcclusion(grid, a, b).solidVoxels, 1u);
        EXPECT_EQ(measureOcclusion(grid, b, a).solidVoxels, 1u);
    }
    {
        FakeVoxelGrid grid; // Two of three: sound slips through the z-first path
        grid.set({1, 0, 0});
        grid.set({0, 1, 0});
        EXPECT_EQ(measureOcclusion(grid, a, b).solidVoxels, 0u);
        EXPECT_EQ(measureOcclusion(grid, b, a).solidVoxels, 0u);
    }
    {
        FakeVoxelGrid grid; // All three edge neighbours of the target voxel: sealed as well
        grid.set({1, 1, 0});
        grid.set({1, 0, 1});
        grid.set({0, 1, 1});
        EXPECT_EQ(measureOcclusion(grid, a, b).solidVoxels, 1u);
        EXPECT_EQ(measureOcclusion(grid, b, a).solidVoxels, 1u);
    }
    {
        FakeVoxelGrid grid; // Two edge neighbours: the x-then-y path is open
        grid.set({1, 0, 1});
        grid.set({0, 1, 1});
        EXPECT_EQ(measureOcclusion(grid, a, b).solidVoxels, 0u);
        EXPECT_EQ(measureOcclusion(grid, b, a).solidVoxels, 0u);
    }
}

TEST(OcclusionTies, SolidBlocksOnTheRealPathStillCountAroundATie) {
    FakeVoxelGrid grid;
    grid.set({1, 1, 0}); // Entered through the corner, then crossed with positive length
    grid.set({1, 0, 0}); // Grazed only
    const OcclusionSample s = measureOcclusion(grid, {0.5f, 0.5f, 0.5f}, {2.5f, 2.5f, 0.5f});
    EXPECT_EQ(s.solidVoxels, 1u);
    EXPECT_NEAR(s.solidThickness, std::sqrt(2.0f), 1e-4f);
}

TEST(OcclusionTies, PropertySymmetricSolidCountForLatticeCentreRays) {
    // Sounds and the listener often sit at block centres (x.5): exact 45-degree ties are common.
    std::mt19937 rng(4242u);
    std::uniform_int_distribution<int> cell(-5, 5);
    for (int world = 0; world < 20; ++world) {
        FakeVoxelGrid grid;
        for (int i = 0; i < 300; ++i) {
            grid.set({cell(rng), cell(rng), cell(rng)});
        }
        for (int iter = 0; iter < 200; ++iter) {
            const glm::vec3 a{static_cast<float>(cell(rng)) + 0.5f, static_cast<float>(cell(rng)) + 0.5f,
                              static_cast<float>(cell(rng)) + 0.5f};
            glm::vec3 b{static_cast<float>(cell(rng)) + 0.5f, static_cast<float>(cell(rng)) + 0.5f,
                        static_cast<float>(cell(rng)) + 0.5f};
            if (iter % 2 == 0) { // Force exact diagonals half of the time
                const float d = static_cast<float>(cell(rng));
                b = a + glm::vec3{d, (iter % 4 == 0) ? d : -d, (iter % 8 == 0) ? d : 0.0f};
            }
            const OcclusionSample f = measureOcclusion(grid, a, b);
            const OcclusionSample r = measureOcclusion(grid, b, a);
            ASSERT_EQ(f.solidVoxels, r.solidVoxels)
                << "(" << a.x << "," << a.y << "," << a.z << ") -> (" << b.x << "," << b.y << "," << b.z << ")";
            ASSERT_NEAR(f.solidThickness, r.solidThickness, 1e-3f);
        }
    }
}

TEST(OcclusionTies, QueryCountStaysLinearForAllTieDiagonals) {
    FakeVoxelGrid grid;
    const OcclusionSample s = measureOcclusion(grid, {0.5f, 0.5f, 0.5f}, {48.5f, 48.5f, 48.5f});
    EXPECT_FALSE(s.truncated);
    EXPECT_LE(grid.queries(), static_cast<uint64_t>(7u * MAX_DDA_STEPS / 3u));
}

// --- Audible range R_max (Fiche 5 §4) -------------------------------------------------------------

TEST(OcclusionRange, SourcesBeyondRmaxAreFlaggedEvenWhenTheRayFitsTheStepBudget) {
    FakeVoxelGrid grid;
    const OcclusionSample far = measureOcclusion(grid, {0.5f, 0.5f, 0.5f}, {100.5f, 0.5f, 0.5f});
    EXPECT_FALSE(far.truncated); // 100 steps < 144
    EXPECT_TRUE(far.beyondAudibleRange);
}

TEST(OcclusionRange, BoundaryIsInclusiveAtExactlyRmax) {
    FakeVoxelGrid grid;
    const auto r = static_cast<float>(MAX_AUDIBLE_RANGE);
    EXPECT_FALSE(measureOcclusion(grid, {0.5f, 0.5f, 0.5f}, {0.5f + r, 0.5f, 0.5f}).beyondAudibleRange);
    EXPECT_TRUE(measureOcclusion(grid, {0.5f, 0.5f, 0.5f}, {0.6f + r, 0.5f, 0.5f}).beyondAudibleRange);
    // Diagonal: Euclidean distance, not the Manhattan step count, decides.
    const float d = r / std::sqrt(3.0f) - 0.01f;
    const OcclusionSample diag = measureOcclusion(grid, {0.5f, 0.5f, 0.5f}, {0.5f + d, 0.5f + d, 0.5f + d});
    EXPECT_FALSE(diag.beyondAudibleRange);
    EXPECT_FALSE(diag.truncated);
}

TEST(OcclusionRange, TruncatedRaysAreAlwaysBeyondRange) {
    std::mt19937 rng(9u);
    std::uniform_real_distribution<float> coord(-200.0f, 200.0f);
    FakeVoxelGrid grid;
    for (int i = 0; i < 500; ++i) {
        const OcclusionSample s = measureOcclusion(grid, {0.0f, 0.0f, 0.0f}, {coord(rng), coord(rng), coord(rng)});
        if (s.truncated) {
            ASSERT_TRUE(s.beyondAudibleRange);
        }
        ASSERT_EQ(s.beyondAudibleRange, s.pathLength > static_cast<float>(MAX_AUDIBLE_RANGE));
    }
}

TEST(OcclusionRange, InvalidSampleIsNotFlagged) {
    FakeVoxelGrid grid;
    const float nan = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(measureOcclusion(grid, {nan, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}).beyondAudibleRange);
}

// --- Spec facade: SpatialAudioEngine::computeVoxelOcclusion(world, soundPos) -> float -------------

TEST(ComputeVoxelOcclusion, ReturnsTheSpecLowPassCutoffInHz) {
    // The float feeds AudioCommand::lpfCutoffHz: 20 kHz unoccluded, x0.65 per solid block, >= 400 Hz.
    FakeVoxelGrid grid;
    const glm::vec3 listener{0.5f, 0.5f, 0.5f};
    const glm::vec3 source{8.5f, 0.5f, 0.5f};
    EXPECT_FLOAT_EQ(computeVoxelOcclusion(grid, listener, source), 20000.0f);
    grid.set({2, 0, 0});
    EXPECT_NEAR(computeVoxelOcclusion(grid, listener, source), 13000.0f, 0.01f);
    grid.set({4, 0, 0});
    EXPECT_NEAR(computeVoxelOcclusion(grid, listener, source), 8450.0f, 0.01f);
    grid.fillBox({1, 0, 0}, {7, 0, 0});
    EXPECT_NEAR(computeVoxelOcclusion(grid, listener, source), 20000.0f * std::pow(0.65f, 7.0f), 0.05f);
    grid.fillBox({1, 0, 0}, {20, 0, 0});
    EXPECT_FLOAT_EQ(computeVoxelOcclusion(grid, listener, {21.5f, 0.5f, 0.5f}), 400.0f);
}

TEST(ComputeVoxelOcclusion, MatchesTheTargetOfTheFullMeasurement) {
    std::mt19937 rng(5u);
    std::uniform_int_distribution<int> cell(-6, 6);
    FakeVoxelGrid grid;
    for (int i = 0; i < 400; ++i) {
        grid.set({cell(rng), cell(rng), cell(rng)});
    }
    std::uniform_real_distribution<float> coord(-7.0f, 7.0f);
    for (int i = 0; i < 100; ++i) {
        const glm::vec3 a{coord(rng), coord(rng), coord(rng)};
        const glm::vec3 b{coord(rng), coord(rng), coord(rng)};
        ASSERT_FLOAT_EQ(computeVoxelOcclusion(grid, a, b), occlusionTargetFor(measureOcclusion(grid, a, b)).cutoffHz);
    }
}

TEST(ComputeVoxelOcclusion, InvalidInputIsUnoccluded) {
    FakeVoxelGrid grid;
    const float inf = std::numeric_limits<float>::infinity();
    EXPECT_FLOAT_EQ(computeVoxelOcclusion(grid, {0.0f, 0.0f, 0.0f}, {inf, 0.0f, 0.0f}), 20000.0f);
}

TEST(Occlusion, CustomStepBudgetsLongerThanOneBatchCountEveryVoxel) {
    // measureOcclusion buffers visits in a fixed stack batch; rays longer than it must lose nothing.
    FakeVoxelGrid grid;
    grid.fillBox({1, 0, 0}, {499, 0, 0});
    const OcclusionSample s = measureOcclusion(grid, {0.5f, 0.5f, 0.5f}, {500.5f, 0.5f, 0.5f}, 1000u);
    EXPECT_FALSE(s.truncated);
    EXPECT_EQ(s.solidVoxels, 499u);
    EXPECT_NEAR(s.solidThickness, 499.0f, 1e-2f);
    // Truncated variant: the last visited voxel is an occluder candidate as well.
    const OcclusionSample t = measureOcclusion(grid, {0.5f, 0.5f, 0.5f}, {500.5f, 0.5f, 0.5f}, 333u);
    EXPECT_TRUE(t.truncated);
    EXPECT_EQ(t.solidVoxels, 333u);
}

TEST(OcclusionTies, JunctionsAcrossBatchBoundariesAreResolved) {
    // An all-tie diagonal longer than one batch with a sealed seam at every corner.
    FakeVoxelGrid grid;
    for (int i = 0; i < 400; ++i) {
        grid.set({i + 1, i, 0});
        grid.set({i, i + 1, 0});
    }
    const OcclusionSample s = measureOcclusion(grid, {0.5f, 0.5f, 0.5f}, {300.5f, 300.5f, 0.5f}, 1000u);
    EXPECT_FALSE(s.truncated);
    EXPECT_EQ(s.solidVoxels, 300u); // One sealed seam per corner crossed
}
