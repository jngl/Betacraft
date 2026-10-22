// P4.6 — std140-exact packing of the DynamicLightBlock UBO into a byte buffer.
#include "lighting/DynamicLightUbo.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstring>
#include <limits>
#include <random>
#include <vector>

using namespace betacraft::lighting;

namespace {

float readF32(const UboBuffer& b, size_t off) {
    float v;
    std::memcpy(&v, b.data() + off, sizeof v);
    return v;
}
int32_t readI32(const UboBuffer& b, size_t off) {
    int32_t v;
    std::memcpy(&v, b.data() + off, sizeof v);
    return v;
}
uint32_t readU32(const UboBuffer& b, size_t off) {
    uint32_t v;
    std::memcpy(&v, b.data() + off, sizeof v);
    return v;
}

DynamicLight light(glm::vec3 p, float r, glm::vec3 c, float i) {
    DynamicLight l;
    l.position = p;
    l.radius = r;
    l.color = c;
    l.intensity = i;
    return l;
}

std::vector<uint32_t> noMasks() { return std::vector<uint32_t>(kTileCount, 0u); }

} // namespace

// --- Layout constants checked against hand-applied std140 rules ------------------

TEST(DynLightUboLayout, LightStructMembers) {
    EXPECT_EQ(ubo::kLightPositionOffset, 0u);   // vec3 position
    EXPECT_EQ(ubo::kLightRadiusOffset, 12u);    // float packs in the vec3 tail
    EXPECT_EQ(ubo::kLightColorOffset, 16u);     // vec3 color realigned on 16
    EXPECT_EQ(ubo::kLightIntensityOffset, 28u); // float packs in the vec3 tail
    EXPECT_EQ(ubo::kLightStride, 32u);
}

TEST(DynLightUboLayout, BlockMembers) {
    EXPECT_EQ(ubo::kLightsOffset, 0u);
    EXPECT_EQ(ubo::kLightCountOffset, 512u); // 16 * 32
    EXPECT_EQ(ubo::kTileColsOffset, 516u);
    EXPECT_EQ(ubo::kTileRowsOffset, 520u);
    EXPECT_EQ(ubo::kTileSizeOffset, 528u);  // vec2 aligns to 8: 524 -> 528
    EXPECT_EQ(ubo::kTileMasksOffset, 544u); // array aligns to 16: 536 -> 544
    EXPECT_EQ(ubo::kTileMaskStride, 16u);   // uvec4 array stride
    EXPECT_EQ(ubo::kBlockSize, 544u + 36u * 16u);
}

TEST(DynLightUboLayout, TileGridConstants) {
    EXPECT_EQ(kTileCount, kTileCols * kTileRows);
    EXPECT_EQ(kTileMaskVec4Count * 4u, kTileCount); // 4 tile masks per uvec4, no remainder
    EXPECT_LE(kMaxDynamicLights, 32u);              // One bit per light in a uint mask
}

TEST(DynLightUboLayout, FitsMinimumGuaranteedUniformBlockSize) {
    // GL_MAX_UNIFORM_BLOCK_SIZE is at least 16 KiB on every GL 3.3 implementation.
    EXPECT_LE(ubo::kBlockSize, 16384u);
    EXPECT_EQ(ubo::kBlockSize % 16u, 0u);
}

TEST(DynLightUboLayout, OffsetsAreAlignedToTheirBaseAlignment) {
    EXPECT_EQ(ubo::kLightCountOffset % 4u, 0u);
    EXPECT_EQ(ubo::kTileSizeOffset % 8u, 0u);
    EXPECT_EQ(ubo::kTileMasksOffset % 16u, 0u);
    EXPECT_EQ(ubo::lightOffset(1), 32u);
    EXPECT_EQ(ubo::lightOffset(15), 480u);
    EXPECT_EQ(ubo::tileMaskOffset(0), 544u);
    EXPECT_EQ(ubo::tileMaskOffset(1), 548u); // .y component of the first uvec4
    EXPECT_EQ(ubo::tileMaskOffset(4), 560u); // next uvec4
    EXPECT_EQ(ubo::tileMaskOffset(kTileCount - 1), ubo::kBlockSize - 4u);
}

// --- Packing --------------------------------------------------------------------

TEST(DynLightUboPack, EmptyFrameWritesZeroCountAndGridInfo) {
    UboBuffer buf;
    buf.fill(std::byte{0xAB});
    packDynamicLightUbo({}, noMasks(), {120.0f, 80.0f}, buf);
    EXPECT_EQ(readI32(buf, ubo::kLightCountOffset), 0);
    EXPECT_EQ(readI32(buf, ubo::kTileColsOffset), static_cast<int32_t>(kTileCols));
    EXPECT_EQ(readI32(buf, ubo::kTileRowsOffset), static_cast<int32_t>(kTileRows));
    EXPECT_FLOAT_EQ(readF32(buf, ubo::kTileSizeOffset), 120.0f);
    EXPECT_FLOAT_EQ(readF32(buf, ubo::kTileSizeOffset + 4), 80.0f);
    // Every light slot is cleared, never leaking a previous frame.
    for (size_t off = 0; off < ubo::kLightCountOffset; ++off) {
        ASSERT_EQ(buf[off], std::byte{0}) << off;
    }
}

TEST(DynLightUboPack, PaddingBytesAreZeroed) {
    UboBuffer buf;
    buf.fill(std::byte{0xFF});
    packDynamicLightUbo({}, noMasks(), {1.0f, 1.0f}, buf);
    for (size_t off = 524; off < 528; ++off) {
        EXPECT_EQ(buf[off], std::byte{0}) << off;
    }
    for (size_t off = 536; off < 544; ++off) {
        EXPECT_EQ(buf[off], std::byte{0}) << off;
    }
}

TEST(DynLightUboPack, LightFieldsLandAtStd140Offsets) {
    const std::vector<DynamicLight> lights{
        light({1.5f, 2.5f, -3.5f}, 14.0f, {1.0f, 0.94f, 0.82f}, 0.75f),
        light({10.0f, 64.0f, 7.0f}, 15.0f, {0.1f, 0.2f, 0.3f}, 2.0f),
    };
    UboBuffer buf;
    packDynamicLightUbo(lights, noMasks(), {1.0f, 1.0f}, buf);

    EXPECT_EQ(readI32(buf, ubo::kLightCountOffset), 2);
    for (size_t i = 0; i < lights.size(); ++i) {
        const size_t base = ubo::lightOffset(i);
        EXPECT_FLOAT_EQ(readF32(buf, base + 0), lights[i].position.x);
        EXPECT_FLOAT_EQ(readF32(buf, base + 4), lights[i].position.y);
        EXPECT_FLOAT_EQ(readF32(buf, base + 8), lights[i].position.z);
        EXPECT_FLOAT_EQ(readF32(buf, base + 12), lights[i].radius);
        EXPECT_FLOAT_EQ(readF32(buf, base + 16), lights[i].color.r);
        EXPECT_FLOAT_EQ(readF32(buf, base + 20), lights[i].color.g);
        EXPECT_FLOAT_EQ(readF32(buf, base + 24), lights[i].color.b);
        EXPECT_FLOAT_EQ(readF32(buf, base + 28), lights[i].intensity);
    }
    // Slot 2 untouched (zero).
    EXPECT_FLOAT_EQ(readF32(buf, ubo::lightOffset(2) + 12), 0.0f);
}

TEST(DynLightUboPack, ExcessLightsAreTruncatedToCapacity) {
    std::vector<DynamicLight> lights(kMaxDynamicLights + 5, light({0, 0, 0}, 3.0f, {1, 1, 1}, 1.0f));
    lights.back().radius = 99.0f; // must not be written anywhere
    UboBuffer buf;
    packDynamicLightUbo(lights, noMasks(), {1.0f, 1.0f}, buf);
    EXPECT_EQ(readI32(buf, ubo::kLightCountOffset), static_cast<int32_t>(kMaxDynamicLights));
    EXPECT_FLOAT_EQ(readF32(buf, ubo::lightOffset(kMaxDynamicLights - 1) + 12), 3.0f);
}

TEST(DynLightUboPack, TileMasksPackedFourPerUvec4) {
    std::vector<uint32_t> masks(kTileCount, 0u);
    masks[0] = 0x1u;
    masks[1] = 0x2u;
    masks[3] = 0x3u;
    masks[4] = 0x1u;
    masks[kTileCount - 1] = 0x3u;
    const std::vector<DynamicLight> lights(2, light({0, 0, 0}, 3.0f, {1, 1, 1}, 1.0f));
    UboBuffer buf;
    packDynamicLightUbo(lights, masks, {1.0f, 1.0f}, buf);
    EXPECT_EQ(readU32(buf, ubo::kTileMasksOffset + 0), 0x1u);
    EXPECT_EQ(readU32(buf, ubo::kTileMasksOffset + 4), 0x2u);
    EXPECT_EQ(readU32(buf, ubo::kTileMasksOffset + 12), 0x3u);
    EXPECT_EQ(readU32(buf, ubo::kTileMasksOffset + 16), 0x1u);
    EXPECT_EQ(readU32(buf, ubo::kBlockSize - 4), 0x3u);
}

TEST(DynLightUboPack, MaskBitsBeyondLightCountAreCleared) {
    // Invariant: the shader can never index a light slot that was not uploaded this frame.
    std::vector<uint32_t> masks(kTileCount, 0xFFFFFFFFu);
    const std::vector<DynamicLight> lights(3, light({0, 0, 0}, 3.0f, {1, 1, 1}, 1.0f));
    UboBuffer buf;
    packDynamicLightUbo(lights, masks, {1.0f, 1.0f}, buf);
    for (uint32_t t = 0; t < kTileCount; ++t) {
        ASSERT_EQ(readU32(buf, ubo::tileMaskOffset(t)), 0x7u) << t;
    }
}

TEST(DynLightUboPack, ShortMaskSpanLeavesRemainingTilesEmpty) {
    const std::vector<uint32_t> masks(10, 0x1u);
    const std::vector<DynamicLight> lights(1, light({0, 0, 0}, 3.0f, {1, 1, 1}, 1.0f));
    UboBuffer buf;
    buf.fill(std::byte{0xFF});
    packDynamicLightUbo(lights, masks, {1.0f, 1.0f}, buf);
    EXPECT_EQ(readU32(buf, ubo::tileMaskOffset(9)), 0x1u);
    EXPECT_EQ(readU32(buf, ubo::tileMaskOffset(10)), 0u);
}

TEST(DynLightUboPack, PropertyRoundTripRandomFrames) {
    std::mt19937 rng(0xC0FFEEu);
    std::uniform_real_distribution<float> f(-1000.0f, 1000.0f);
    std::uniform_int_distribution<uint32_t> countDist(0, kMaxDynamicLights);
    std::uniform_int_distribution<uint32_t> maskDist;
    for (int iter = 0; iter < 100; ++iter) {
        const uint32_t n = countDist(rng);
        std::vector<DynamicLight> lights;
        for (uint32_t i = 0; i < n; ++i) {
            lights.push_back(light({f(rng), f(rng), f(rng)}, std::abs(f(rng)) + 1.0f, {0.5f, 0.25f, 1.0f},
                                   std::abs(f(rng))));
        }
        std::vector<uint32_t> masks(kTileCount);
        for (auto& m : masks) {
            m = maskDist(rng);
        }
        UboBuffer buf;
        packDynamicLightUbo(lights, masks, {f(rng), f(rng)}, buf);
        ASSERT_EQ(readI32(buf, ubo::kLightCountOffset), static_cast<int32_t>(n));
        const uint32_t allowed = n >= 32 ? 0xFFFFFFFFu : ((1u << n) - 1u);
        for (uint32_t i = 0; i < n; ++i) {
            ASSERT_FLOAT_EQ(readF32(buf, ubo::lightOffset(i) + ubo::kLightPositionOffset + 8), lights[i].position.z);
            ASSERT_FLOAT_EQ(readF32(buf, ubo::lightOffset(i) + ubo::kLightIntensityOffset), lights[i].intensity);
        }
        for (uint32_t t = 0; t < kTileCount; ++t) {
            ASSERT_EQ(readU32(buf, ubo::tileMaskOffset(t)), masks[t] & allowed);
        }
    }
}

TEST(DynLightUboPack, InvalidLightSlotsAreZeroedAndMaskedOut) {
    // The packer is public: a light that bypassed the manager (NaN position, NaN/zero radius,
    // black...) must never reach the GPU, where NaN through clamp() is implementation-defined.
    // Its slot keeps its index (masks stay aligned) but uploads as all-zero (radius 0 makes
    // dynLightAttenuation() take its early-out) and its bit is cleared from every tile.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    std::vector<DynamicLight> lights{light({1, 2, 3}, 4.0f, {1, 1, 1}, 1.0f), light({nan, 0, 0}, 4.0f, {1, 1, 1}, 1.0f),
                                     light({1, 2, 3}, nan, {1, 1, 1}, 1.0f), light({4, 5, 6}, 2.0f, {0, 0, 1}, 0.5f)};
    std::vector<uint32_t> masks(kTileCount, 0xFu);
    UboBuffer buf;
    buf.fill(std::byte{0xAB});
    packDynamicLightUbo(lights, masks, {100.0f, 100.0f}, buf);
    EXPECT_EQ(readI32(buf, ubo::kLightCountOffset), 4);
    for (size_t i : {size_t{1}, size_t{2}}) {
        for (size_t b = 0; b < ubo::kLightStride; ++b) {
            ASSERT_EQ(buf[ubo::lightOffset(i) + b], std::byte{0}) << "slot " << i << " byte " << b;
        }
    }
    EXPECT_FLOAT_EQ(readF32(buf, ubo::lightOffset(3) + ubo::kLightRadiusOffset), 2.0f);
    for (uint32_t t = 0; t < kTileCount; ++t) {
        ASSERT_EQ(readU32(buf, ubo::tileMaskOffset(t)), 0x9u) << t;
    }
}

TEST(DynLightUboPack, TileSizeIsSanitizedToAtLeastOnePixel) {
    // Mirrors TileLightGrid's clamp (and the shader's max(..., 1.0)) so the uploaded value can
    // never be 0, negative or NaN.
    UboBuffer buf;
    packDynamicLightUbo({}, {}, {0.25f, std::numeric_limits<float>::quiet_NaN()}, buf);
    EXPECT_FLOAT_EQ(readF32(buf, ubo::kTileSizeOffset), 1.0f);
    EXPECT_FLOAT_EQ(readF32(buf, ubo::kTileSizeOffset + 4), 1.0f);
    packDynamicLightUbo({}, {}, {120.0f, 80.5f}, buf);
    EXPECT_FLOAT_EQ(readF32(buf, ubo::kTileSizeOffset), 120.0f);
    EXPECT_FLOAT_EQ(readF32(buf, ubo::kTileSizeOffset + 4), 80.5f);
}
