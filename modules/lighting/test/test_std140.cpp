// P4.6 — std140 layout rules (OpenGL 4.6 spec §7.6.2.2, valid for GL 3.3 core).
#include "lighting/Std140.hpp"

#include <gtest/gtest.h>

using namespace betacraft::lighting::std140;

TEST(Std140Rules, AlignUp) {
    EXPECT_EQ(alignUp(0, 16), 0u);
    EXPECT_EQ(alignUp(1, 16), 16u);
    EXPECT_EQ(alignUp(16, 16), 16u);
    EXPECT_EQ(alignUp(17, 8), 24u);
    EXPECT_EQ(alignUp(13, 4), 16u);
}

TEST(Std140Rules, ScalarBaseAlignmentIsFour) {
    EXPECT_EQ(baseAlignment(Type::Float), 4u);
    EXPECT_EQ(baseAlignment(Type::Int), 4u);
    EXPECT_EQ(baseAlignment(Type::UInt), 4u);
    EXPECT_EQ(byteSize(Type::Float), 4u);
}

TEST(Std140Rules, Vec2AlignsToEight) {
    EXPECT_EQ(baseAlignment(Type::Vec2), 8u);
    EXPECT_EQ(byteSize(Type::Vec2), 8u);
}

TEST(Std140Rules, Vec3AlignsLikeVec4ButIsTwelveBytes) {
    EXPECT_EQ(baseAlignment(Type::Vec3), 16u);
    EXPECT_EQ(byteSize(Type::Vec3), 12u);
}

TEST(Std140Rules, Vec4AndIntegerVec4) {
    EXPECT_EQ(baseAlignment(Type::Vec4), 16u);
    EXPECT_EQ(baseAlignment(Type::IVec4), 16u);
    EXPECT_EQ(baseAlignment(Type::UVec4), 16u);
    EXPECT_EQ(byteSize(Type::UVec4), 16u);
}

TEST(Std140Rules, Mat4IsFourVec4Columns) {
    EXPECT_EQ(baseAlignment(Type::Mat4), 16u);
    EXPECT_EQ(byteSize(Type::Mat4), 64u);
}

TEST(Std140Rules, ArrayStrideRoundsUpToVec4) {
    EXPECT_EQ(arrayStride(Type::Float), 16u);
    EXPECT_EQ(arrayStride(Type::UInt), 16u);
    EXPECT_EQ(arrayStride(Type::Vec2), 16u);
    EXPECT_EQ(arrayStride(Type::Vec3), 16u);
    EXPECT_EQ(arrayStride(Type::Vec4), 16u);
    EXPECT_EQ(arrayStride(Type::Mat4), 64u);
}

TEST(Std140Builder, FloatPacksIntoVec3Tail) {
    LayoutBuilder b;
    EXPECT_EQ(b.add(Type::Vec3), 0u);
    EXPECT_EQ(b.add(Type::Float), 12u); // The classic std140 "free" slot after a vec3
    EXPECT_EQ(b.size(), 16u);
}

TEST(Std140Builder, Vec3AfterFloatIsPaddedTo16) {
    LayoutBuilder b;
    EXPECT_EQ(b.add(Type::Float), 0u);
    EXPECT_EQ(b.add(Type::Vec3), 16u);
    EXPECT_EQ(b.size(), 28u);
}

TEST(Std140Builder, Vec2AfterScalarAlignsToEight) {
    LayoutBuilder b;
    EXPECT_EQ(b.add(Type::Int), 0u);
    EXPECT_EQ(b.add(Type::Vec2), 8u);
    EXPECT_EQ(b.add(Type::Float), 16u);
}

TEST(Std140Builder, ArrayStartsOn16AndAdvancesByStride) {
    LayoutBuilder b;
    EXPECT_EQ(b.add(Type::Float), 0u);
    EXPECT_EQ(b.addArray(Type::Float, 3), 16u);
    // Array of 3 floats occupies 3 * 16 bytes, member after it is aligned to 16 again.
    EXPECT_EQ(b.size(), 64u);
    EXPECT_EQ(b.add(Type::Float), 64u);
}

TEST(Std140Builder, StructSizeRoundedTo16) {
    LayoutBuilder s;
    s.add(Type::Vec3);
    s.add(Type::Float);
    s.add(Type::Vec3);
    EXPECT_EQ(s.size(), 28u);
    EXPECT_EQ(s.structSize(), 32u);
}

TEST(Std140Builder, StructArrayAlignedTo16WithStructStride) {
    LayoutBuilder s;
    s.add(Type::Float);
    EXPECT_EQ(s.structSize(), 16u); // Even a single float struct occupies a vec4

    LayoutBuilder b;
    EXPECT_EQ(b.add(Type::Float), 0u);
    EXPECT_EQ(b.addStructArray(s.structSize(), 4), 16u);
    EXPECT_EQ(b.size(), 16u + 4u * 16u);
}

TEST(Std140Builder, EmptyStructArrayIsAlignedButZeroSized) {
    LayoutBuilder b;
    b.add(Type::Float);
    EXPECT_EQ(b.addArray(Type::Vec4, 0), 16u);
    EXPECT_EQ(b.size(), 16u);
}

TEST(Std140Builder, IsConstexpr) {
    constexpr size_t off = [] {
        LayoutBuilder b;
        b.add(Type::Vec3);
        return b.add(Type::Vec2);
    }();
    EXPECT_EQ(off, 16u); // `constexpr` above proves compile-time evaluation
}
