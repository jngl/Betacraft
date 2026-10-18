#include "render/PackedChunkVertex.hpp"
#include <gtest/gtest.h>
#include <vector>

using namespace betacraft::render;

TEST(SharedEboTest, RepeatingPatternFirstQuad) {
    const auto ebo = generateSharedEboPattern(1);
    ASSERT_EQ(ebo.size(), 6u);
    EXPECT_EQ(ebo[0], 0u);
    EXPECT_EQ(ebo[1], 1u);
    EXPECT_EQ(ebo[2], 2u);
    EXPECT_EQ(ebo[3], 0u);
    EXPECT_EQ(ebo[4], 2u);
    EXPECT_EQ(ebo[5], 3u);
}

TEST(SharedEboTest, RepeatingPatternSecondQuad) {
    const auto ebo = generateSharedEboPattern(2);
    ASSERT_EQ(ebo.size(), 12u);
    // Quad 1: [4, 5, 6, 4, 6, 7]
    EXPECT_EQ(ebo[6], 4u);
    EXPECT_EQ(ebo[7], 5u);
    EXPECT_EQ(ebo[8], 6u);
    EXPECT_EQ(ebo[9], 4u);
    EXPECT_EQ(ebo[10], 6u);
    EXPECT_EQ(ebo[11], 7u);
}

TEST(SharedEboTest, ValidationOnLargeBuffer65536Quads) {
    constexpr size_t quadCount = 65536;
    const auto ebo = generateSharedEboPattern(quadCount);
    EXPECT_EQ(ebo.size(), quadCount * 6);
    EXPECT_TRUE(validateEboPattern(ebo, quadCount));
}

TEST(SharedEboTest, ValidationRejectsCorruptedIndices) {
    constexpr size_t quadCount = 4;
    auto ebo = generateSharedEboPattern(quadCount);
    EXPECT_TRUE(validateEboPattern(ebo, quadCount));

    // Corrupt one index
    ebo[5] = 999;
    EXPECT_FALSE(validateEboPattern(ebo, quadCount));

    // Wrong size
    ebo.pop_back();
    EXPECT_FALSE(validateEboPattern(ebo, quadCount));
}

TEST(SharedEboTest, ZeroQuadsEmptyBuffer) {
    const auto ebo = generateSharedEboPattern(0);
    EXPECT_TRUE(ebo.empty());
    EXPECT_TRUE(validateEboPattern(ebo, 0));
}
