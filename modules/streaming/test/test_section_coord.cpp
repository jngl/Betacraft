#include "voxel/SectionCoord.hpp"

#include <gtest/gtest.h>

#include <unordered_set>

using betacraft::voxel::SectionCoord;

TEST(SectionCoord, WorldOriginIsSixteenTimesCoord) {
    const SectionCoord c{2, 3, -4};
    EXPECT_EQ(c.worldOrigin(), glm::ivec3(32, 48, -64));
}

TEST(SectionCoord, FromWorldBlockUsesFloorDivisionForNegatives) {
    EXPECT_EQ(SectionCoord::fromWorldBlock(0, 0, 0), (SectionCoord{0, 0, 0}));
    EXPECT_EQ(SectionCoord::fromWorldBlock(15, 15, 15), (SectionCoord{0, 0, 0}));
    EXPECT_EQ(SectionCoord::fromWorldBlock(16, 16, 16), (SectionCoord{1, 1, 1}));
    EXPECT_EQ(SectionCoord::fromWorldBlock(-1, 127, -16), (SectionCoord{-1, 7, -1}));
    EXPECT_EQ(SectionCoord::fromWorldBlock(-17, 0, -33), (SectionCoord{-2, 0, -3}));
}

TEST(SectionCoord, ChunkProjectionDropsY) {
    const SectionCoord c{-5, 6, 9};
    EXPECT_EQ(c.chunk(), (betacraft::core::ChunkCoord{-5, 9}));
}

TEST(SectionCoord, ValidYRangeMatchesChunkHeight) {
    EXPECT_TRUE((SectionCoord{0, 0, 0}).hasValidY());
    EXPECT_TRUE((SectionCoord{0, 7, 0}).hasValidY());
    EXPECT_FALSE((SectionCoord{0, -1, 0}).hasValidY());
    EXPECT_FALSE((SectionCoord{0, 8, 0}).hasValidY());
}

TEST(SectionCoord, CenterDistanceSquared) {
    const SectionCoord c{0, 0, 0};
    // Section centre is (8, 8, 8).
    EXPECT_FLOAT_EQ(c.distanceSqTo(glm::vec3(8.0f, 8.0f, 8.0f)), 0.0f);
    EXPECT_FLOAT_EQ(c.distanceSqTo(glm::vec3(11.0f, 8.0f, 12.0f)), 25.0f);
}

TEST(SectionCoord, HashDistinguishesManyCoords) {
    std::unordered_set<SectionCoord> set;
    for (int x = -8; x < 8; ++x) {
        for (int y = 0; y < 8; ++y) {
            for (int z = -8; z < 8; ++z) {
                set.insert(SectionCoord{x, y, z});
            }
        }
    }
    EXPECT_EQ(set.size(), 16u * 8u * 16u);
}

TEST(SectionCoord, CentreIsTheMiddleOfTheSectionIncludingNegativeCoords) {
    EXPECT_EQ((SectionCoord{0, 0, 0}.centre()), glm::vec3(8.0f));
    EXPECT_EQ((SectionCoord{-1, 7, 2}.centre()), glm::vec3(-8.0f, 120.0f, 40.0f));
    const SectionCoord c{-3, 2, 5};
    EXPECT_FLOAT_EQ(c.distanceSqTo(c.centre()), 0.0f);
}

// A voxel is read by its own section's mesh and, through the 1-voxel guard of the 18^3
// snapshot, by the meshes of the sections across every seam it touches.
TEST(SectionCoord, InteriorVoxelIsSeenOnlyByItsOwnSection) {
    const auto box = betacraft::voxel::sectionsSeeingVoxel(5, 40, 9);
    EXPECT_EQ(box.min, (SectionCoord{0, 2, 0}));
    EXPECT_EQ(box.max, (SectionCoord{0, 2, 0}));
}

TEST(SectionCoord, SeamVoxelIsSeenAcrossEverySeamItTouches) {
    const auto corner = betacraft::voxel::sectionsSeeingVoxel(15, 31, 16);
    EXPECT_EQ(corner.min, (SectionCoord{0, 1, 0}));
    EXPECT_EQ(corner.max, (SectionCoord{1, 2, 1}));

    const auto negative = betacraft::voxel::sectionsSeeingVoxel(-16, 48, -1);
    EXPECT_EQ(negative.min, (SectionCoord{-2, 2, -1}));
    EXPECT_EQ(negative.max, (SectionCoord{-1, 3, 0}));
}

TEST(SectionCoord, SeenSectionsAreClippedToTheColumn) {
    const auto bottom = betacraft::voxel::sectionsSeeingVoxel(3, 0, 3);
    EXPECT_EQ(bottom.min.y, 0);
    EXPECT_EQ(bottom.max.y, 0);
    const auto top = betacraft::voxel::sectionsSeeingVoxel(3, 127, 3);
    EXPECT_EQ(top.min.y, 7);
    EXPECT_EQ(top.max.y, 7);
    // Above the column a voxel still borders the top section (World reads it as sky).
    const auto above = betacraft::voxel::sectionsSeeingVoxel(3, 128, 3);
    EXPECT_EQ(above.min.y, 7);
    EXPECT_EQ(above.max.y, 7);
}
