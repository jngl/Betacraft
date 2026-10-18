#include "render/PackedChunkVertex.hpp"
#include <gtest/gtest.h>
#include <type_traits>
#include <vector>

using namespace betacraft::render;

TEST(PackedChunkVertexTest, SizeAndAlignmentContract) {
    EXPECT_EQ(sizeof(PackedChunkVertex), 8u);
    EXPECT_EQ(alignof(PackedChunkVertex), 8u);
    EXPECT_TRUE(std::is_trivially_copyable_v<PackedChunkVertex>);
    static_assert(sizeof(PackedChunkVertex) == 8, "PackedChunkVertex must be exactly 8 bytes");
    static_assert(alignof(PackedChunkVertex) == 8, "PackedChunkVertex must be 8-byte aligned");
}

TEST(PackedChunkVertexTest, BitpackingRoundtripWord0) {
    const uint32_t x = 16;
    const uint32_t y = 16;
    const uint32_t z = 16;
    const uint32_t norm = 5;
    const uint32_t ao = 3;
    const uint32_t uv = 3;
    const uint32_t mat = 1023;

    const uint32_t w0 = PackedChunkVertex::packWord0(x, y, z, norm, ao, uv, mat);

    EXPECT_EQ(PackedChunkVertex::unpackX(w0), x);
    EXPECT_EQ(PackedChunkVertex::unpackY(w0), y);
    EXPECT_EQ(PackedChunkVertex::unpackZ(w0), z);
    EXPECT_EQ(PackedChunkVertex::unpackNormal(w0), norm);
    EXPECT_EQ(PackedChunkVertex::unpackAO(w0), ao);
    EXPECT_EQ(PackedChunkVertex::unpackUVCorner(w0), uv);
    EXPECT_EQ(PackedChunkVertex::unpackMatLayer(w0), mat);
}

TEST(PackedChunkVertexTest, BitpackingZeroWord0) {
    const uint32_t w0 = PackedChunkVertex::packWord0(0, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(w0, 0u);
    EXPECT_EQ(PackedChunkVertex::unpackX(w0), 0u);
    EXPECT_EQ(PackedChunkVertex::unpackY(w0), 0u);
    EXPECT_EQ(PackedChunkVertex::unpackZ(w0), 0u);
    EXPECT_EQ(PackedChunkVertex::unpackNormal(w0), 0u);
    EXPECT_EQ(PackedChunkVertex::unpackAO(w0), 0u);
    EXPECT_EQ(PackedChunkVertex::unpackUVCorner(w0), 0u);
    EXPECT_EQ(PackedChunkVertex::unpackMatLayer(w0), 0u);
}

TEST(PackedChunkVertexTest, BitpackingWord0ClampingAndMasking) {
    // Test values exceeding field widths: masks must prevent corrupting neighbor fields
    const uint32_t w0 = PackedChunkVertex::packWord0(0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFFFF);
    EXPECT_EQ(PackedChunkVertex::unpackX(w0), 0x1Fu);
    EXPECT_EQ(PackedChunkVertex::unpackY(w0), 0x1Fu);
    EXPECT_EQ(PackedChunkVertex::unpackZ(w0), 0x1Fu);
    EXPECT_EQ(PackedChunkVertex::unpackNormal(w0), 0x07u);
    EXPECT_EQ(PackedChunkVertex::unpackAO(w0), 0x03u);
    EXPECT_EQ(PackedChunkVertex::unpackUVCorner(w0), 0x03u);
    EXPECT_EQ(PackedChunkVertex::unpackMatLayer(w0), 0x3FFu);
}

TEST(PackedChunkVertexTest, BitpackingRoundtripWord1) {
    const uint32_t sky = 15;
    const uint32_t blk = 14;
    const uint32_t tint = 210;
    const uint32_t anim = 31;
    const bool sway = true;
    const bool trans = true;
    const uint32_t res = 511;

    const uint32_t w1 = PackedChunkVertex::packWord1(sky, blk, tint, anim, sway, trans, res);

    EXPECT_EQ(PackedChunkVertex::unpackSkyLight(w1), sky);
    EXPECT_EQ(PackedChunkVertex::unpackBlockLight(w1), blk);
    EXPECT_EQ(PackedChunkVertex::unpackBiomeTint(w1), tint);
    EXPECT_EQ(PackedChunkVertex::unpackAnimFrame(w1), anim);
    EXPECT_EQ(PackedChunkVertex::unpackSway(w1), sway);
    EXPECT_EQ(PackedChunkVertex::unpackTranslucent(w1), trans);
    EXPECT_EQ(PackedChunkVertex::unpackReserved(w1), res);
}

TEST(PackedChunkVertexTest, BitpackingWord1ClampingAndMasking) {
    const uint32_t w1 = PackedChunkVertex::packWord1(0xFF, 0xFF, 0xFFFF, 0xFF, true, true, 0xFFFF);
    EXPECT_EQ(PackedChunkVertex::unpackSkyLight(w1), 0x0Fu);
    EXPECT_EQ(PackedChunkVertex::unpackBlockLight(w1), 0x0Fu);
    EXPECT_EQ(PackedChunkVertex::unpackBiomeTint(w1), 0xFFu);
    EXPECT_EQ(PackedChunkVertex::unpackAnimFrame(w1), 0x1Fu);
    EXPECT_TRUE(PackedChunkVertex::unpackSway(w1));
    EXPECT_TRUE(PackedChunkVertex::unpackTranslucent(w1));
    EXPECT_EQ(PackedChunkVertex::unpackReserved(w1), 0x1FFu);
}

TEST(PackedChunkVertexTest, QuadMemoryFootprintAndReduction) {
    const size_t bytesPerQuad = 4 * sizeof(PackedChunkVertex);
    EXPECT_EQ(bytesPerQuad, 32u);

    constexpr size_t legacyBytesPerQuad = 6 * 40; // 240 bytes
    const double reduction = 1.0 - (static_cast<double>(bytesPerQuad) / static_cast<double>(legacyBytesPerQuad));
    EXPECT_NEAR(reduction, 0.866667, 1e-4);
}

TEST(PackedChunkVertexTest, PlanarUVProjectionFractalTiling) {
    // +Y face (normalIdx = 0): planar is (worldPos.x, worldPos.z)
    const glm::vec3 topPos(12.75f, 64.0f, 33.25f);
    const glm::vec2 topUv = PackedChunkVertex::computePlanarUV(topPos, 0);
    EXPECT_NEAR(topUv.x, 0.75f, 1e-4f);
    EXPECT_NEAR(topUv.y, 0.25f, 1e-4f);

    // +X face (normalIdx = 4): planar is (worldPos.z, worldPos.y)
    const glm::vec3 eastPos(10.0f, 70.33f, 15.66f);
    const glm::vec2 eastUv = PackedChunkVertex::computePlanarUV(eastPos, 4);
    EXPECT_NEAR(eastUv.x, 0.66f, 1e-4f);
    EXPECT_NEAR(eastUv.y, 0.33f, 1e-4f);

    // +Z face (normalIdx = 2): planar is (worldPos.x, worldPos.y)
    const glm::vec3 southPos(5.12f, 80.88f, 20.0f);
    const glm::vec2 southUv = PackedChunkVertex::computePlanarUV(southPos, 2);
    EXPECT_NEAR(southUv.x, 0.12f, 1e-4f);
    EXPECT_NEAR(southUv.y, 0.88f, 1e-4f);
}
