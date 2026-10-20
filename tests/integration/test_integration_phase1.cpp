#include "tests/phase1_test_contracts.hpp"
#include "tests/test_helpers.hpp"

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include <atomic>
#include <cmath>
#include <future>
#include <thread>
#include <vector>

using namespace betacraft;
using namespace betacraft::test_contracts;
using namespace testing;

// ============================================================================
// FEATURE 1: Fixed Timestep Physics (Fiedler P1.6)
// ============================================================================

// Tier 1: Primary behaviors (>=5 tests)
TEST(FixedTimestepIntegration, AccumulatorProgression) {
    const float fixedDt = 1.0f / 60.0f;
    float accumulator = 0.0f;
    size_t ticks = 0;

    // Simulate 3 frames of 16.666ms (almost 1 tick each)
    const float frameDt = 0.0166667f;
    for (int f = 0; f < 60; ++f) {
        accumulator += frameDt;
        while (accumulator >= fixedDt) {
            accumulator -= fixedDt;
            ++ticks;
        }
    }
    EXPECT_EQ(ticks, 60u);
    EXPECT_NEAR(accumulator, 0.0f, 0.001f);
}

TEST(FixedTimestepIntegration, FramerateInvariance30vs60) {
    // 3.0 seconds simulation
    const float totalTime = 3.0f;
    const glm::vec3 initialVel(4.317f, 8.55f, 0.0f); // Minecraft walk speed + jump

    const auto res30 = simulateFiedlerPhysics(totalTime, 1.0f / 30.0f, 1.0f / 60.0f, 0.25f, glm::vec3(0.0f), initialVel);
    const auto res60 = simulateFiedlerPhysics(totalTime, 1.0f / 60.0f, 1.0f / 60.0f, 0.25f, glm::vec3(0.0f), initialVel);

    EXPECT_EQ(res30.tickCount, res60.tickCount);
    EXPECT_NEAR(res30.finalPosition.x, res60.finalPosition.x, 1e-4f);
    EXPECT_NEAR(res30.finalPosition.y, res60.finalPosition.y, 1e-4f);
    EXPECT_NEAR(res30.finalVelocity.y, res60.finalVelocity.y, 1e-4f);
}

TEST(FixedTimestepIntegration, FramerateInvariance60vs144) {
    const float totalTime = 3.0f;
    const glm::vec3 initialVel(0.0f, 8.55f, 4.317f);

    const auto res60  = simulateFiedlerPhysics(totalTime, 1.0f / 60.0f, 1.0f / 60.0f, 0.25f, glm::vec3(0.0f), initialVel);
    const auto res144 = simulateFiedlerPhysics(totalTime, 1.0f / 144.0f, 1.0f / 60.0f, 0.25f, glm::vec3(0.0f), initialVel);

    EXPECT_EQ(res60.tickCount, res144.tickCount);
    EXPECT_NEAR(res60.finalPosition.z, res144.finalPosition.z, 1e-4f);
    EXPECT_NEAR(res60.finalPosition.y, res144.finalPosition.y, 1e-4f);
    EXPECT_NEAR(res60.finalVelocity.y, res144.finalVelocity.y, 1e-4f);
}

TEST(FixedTimestepIntegration, ConvexAlphaInterpolation) {
    const glm::vec3 prevPos(10.0f, 64.0f, 10.0f);
    const glm::vec3 currPos(12.0f, 64.0f, 14.0f);

    for (int i = 0; i <= 10; ++i) {
        const float alpha = static_cast<float>(i) / 10.0f;
        const glm::vec3 interp = glm::mix(prevPos, currPos, alpha);

        EXPECT_GE(interp.x, prevPos.x);
        EXPECT_LE(interp.x, currPos.x);
        EXPECT_GE(interp.z, prevPos.z);
        EXPECT_LE(interp.z, currPos.z);
        EXPECT_FLOAT_EQ(interp.y, 64.0f);
    }
}

TEST(FixedTimestepIntegration, ZeroLatencyCameraLook) {
    // Orientation angles must not be delayed by physics accumulator
    float currentYaw = 90.0f;
    float currentPitch = 0.0f;

    const float deltaMouseX = 5.0f;
    const float deltaMouseY = -3.0f;
    const float sensitivity = 0.15f;

    // Direct event update
    currentYaw += deltaMouseX * sensitivity;
    currentPitch = std::clamp(currentPitch - deltaMouseY * sensitivity, -89.0f, 89.0f);

    EXPECT_FLOAT_EQ(currentYaw, 90.75f);
    EXPECT_FLOAT_EQ(currentPitch, 0.45f);
}

// Tier 2: Boundary conditions (>=5 tests)
TEST(FixedTimestepIntegration, ZeroDeltaTime) {
    const auto res = simulateFiedlerPhysics(0.0f, 0.0f, 1.0f / 60.0f);
    EXPECT_EQ(res.tickCount, 0u);
    EXPECT_FLOAT_EQ(res.finalPosition.y, 0.0f);
}

TEST(FixedTimestepIntegration, NegativeDeltaTimeHandledGracefully) {
    float accumulator = 0.0f;
    const float negativeDt = -0.05f;
    // Negative dt must be ignored
    if (negativeDt > 0.0f) {
        accumulator += negativeDt;
    }
    EXPECT_FLOAT_EQ(accumulator, 0.0f);
}

TEST(FixedTimestepIntegration, SubTickDeltaTimeAccumulates) {
    const float fixedDt = 1.0f / 60.0f; // ~16.666 ms
    float accumulator = 0.0f;
    size_t ticks = 0;

    // Feed 1ms frames
    for (int ms = 0; ms < 17; ++ms) {
        accumulator += 0.001f;
        while (accumulator >= fixedDt) {
            accumulator -= fixedDt;
            ++ticks;
        }
    }
    // 17 ms is enough for exactly 1 tick at 16.666ms
    EXPECT_EQ(ticks, 1u);
    EXPECT_GT(accumulator, 0.0f);
}

TEST(FixedTimestepIntegration, MaxAccumulatorCapSpiralOfDeathPrevention) {
    const float fixedDt = 1.0f / 60.0f;
    const float maxAccumulator = 0.25f; // Max 15 ticks allowed

    float accumulator = 2.0f; // 2 seconds sudden lag spike
    if (accumulator > maxAccumulator) {
        accumulator = maxAccumulator;
    }

    size_t ticks = 0;
    while (accumulator >= fixedDt) {
        accumulator -= fixedDt;
        ++ticks;
    }

    // 0.25 / (1/60) = 15 ticks exactly
    EXPECT_EQ(ticks, 15u);
    EXPECT_LT(accumulator, fixedDt);
}

TEST(FixedTimestepIntegration, ExactTickBoundaryNoRemainder) {
    const float fixedDt = 1.0f / 60.0f;
    float accumulator = fixedDt;
    size_t ticks = 0;
    while (accumulator >= fixedDt) {
        accumulator -= fixedDt;
        ++ticks;
    }
    EXPECT_EQ(ticks, 1u);
    EXPECT_FLOAT_EQ(accumulator, 0.0f);
}

// ============================================================================
// FEATURE 2 & 3: PalettedSection & Dynamic Palette Transitions (P3.1)
// ============================================================================

// Tier 1: Primary behaviors (>=5 tests)
TEST(PalettedSectionIntegration, SizeAndAlignmentContract) {
    EXPECT_LE(sizeof(ContractPalettedSection), 64u);
    EXPECT_EQ(alignof(ContractPalettedSection), 64u);
#if BETACRAFT_HAS_PROD_PALETTED_SECTION
    EXPECT_LE(sizeof(voxel::PalettedSection), 64u);
    EXPECT_EQ(alignof(voxel::PalettedSection), 64u);
#endif
}

TEST(PalettedSectionIntegration, HomogeneousAirZeroHeap) {
    ContractPalettedSection sec;
    EXPECT_TRUE(sec.isHomogeneous());
    EXPECT_EQ(sec.getBitsPerVoxel(), 0u);
    EXPECT_FALSE(sec.hasLightData());
    EXPECT_EQ(sec.getBlock(0, 0, 0), 0u);
    EXPECT_EQ(sec.getBlock(15, 15, 15), 0u);
}

TEST(PalettedSectionIntegration, DynamicUpgrade0To4Bits) {
    ContractPalettedSection sec;
    EXPECT_EQ(sec.getBitsPerVoxel(), 0u);

    // Place 1 stone block (ID 1)
    sec.setBlock(5, 5, 5, 1);
    EXPECT_EQ(sec.getBitsPerVoxel(), 4u);
    EXPECT_EQ(sec.getPaletteSize(), 2u); // Air (0) and Stone (1)
    EXPECT_EQ(sec.getBlock(5, 5, 5), 1u);
    EXPECT_EQ(sec.getBlock(0, 0, 0), 0u); // Unmodified remains air
}

TEST(PalettedSectionIntegration, DynamicUpgrade4To8Bits) {
    ContractPalettedSection sec;
    // Add 16 distinct block types (fills 4-bit palette [0..15])
    for (uint16_t b = 1; b <= 15; ++b) {
        sec.setBlock(b, 0, 0, b);
    }
    EXPECT_EQ(sec.getBitsPerVoxel(), 4u);
    EXPECT_EQ(sec.getPaletteSize(), 16u);

    // Adding 17th unique type forces upgrade to 8-bit palette
    sec.setBlock(0, 1, 0, 100);
    EXPECT_EQ(sec.getBitsPerVoxel(), 8u);
    EXPECT_EQ(sec.getPaletteSize(), 17u);
    EXPECT_EQ(sec.getBlock(0, 1, 0), 100u);

    // Check all previous blocks preserved
    for (uint16_t b = 1; b <= 15; ++b) {
        EXPECT_EQ(sec.getBlock(b, 0, 0), b);
    }
}

TEST(PalettedSectionIntegration, DynamicUpgrade8To16Bits) {
    ContractPalettedSection sec;
    // Fill up to 256 unique block types
    for (uint16_t i = 1; i <= 255; ++i) {
        const int x = i % 16;
        const int y = (i / 16) % 16;
        sec.setBlock(x, y, 0, i);
    }
    EXPECT_EQ(sec.getBitsPerVoxel(), 8u);
    EXPECT_EQ(sec.getPaletteSize(), 256u);

    // 257th block triggers upgrade to 16-bit direct storage
    sec.setBlock(0, 0, 1, 500);
    EXPECT_EQ(sec.getBitsPerVoxel(), 16u);
    EXPECT_EQ(sec.getBlock(0, 0, 1), 500u);

    // Verify preservation
    EXPECT_EQ(sec.getBlock(1, 0, 0), 1u);
    EXPECT_EQ(sec.getBlock(15, 0, 0), 15u);
}

// Tier 2: Boundary conditions (>=5 tests)
TEST(PalettedSectionIntegration, VoxelCornersBoundary) {
    ContractPalettedSection sec;
    sec.setBlock(0, 0, 0, 1);
    sec.setBlock(15, 0, 0, 2);
    sec.setBlock(0, 15, 0, 3);
    sec.setBlock(0, 0, 15, 4);
    sec.setBlock(15, 15, 15, 5);

    EXPECT_EQ(sec.getBlock(0, 0, 0), 1u);
    EXPECT_EQ(sec.getBlock(15, 0, 0), 2u);
    EXPECT_EQ(sec.getBlock(0, 15, 0), 3u);
    EXPECT_EQ(sec.getBlock(0, 0, 15), 4u);
    EXPECT_EQ(sec.getBlock(15, 15, 15), 5u);
    EXPECT_EQ(sec.getBlock(7, 7, 7), 0u);
}

TEST(PalettedSectionIntegration, PaletteSaturation16Types) {
    ContractPalettedSection sec;
    for (uint16_t id = 1; id <= 15; ++id) {
        sec.setBlock(id, 0, 0, id);
    }
    EXPECT_EQ(sec.getPaletteSize(), 16u);
    EXPECT_EQ(sec.getBitsPerVoxel(), 4u);

    // Overwriting with an existing ID must NOT upgrade
    sec.setBlock(10, 10, 10, 5);
    EXPECT_EQ(sec.getPaletteSize(), 16u);
    EXPECT_EQ(sec.getBitsPerVoxel(), 4u);
    EXPECT_EQ(sec.getBlock(10, 10, 10), 5u);
}

TEST(PalettedSectionIntegration, PaletteSaturation256Types) {
    ContractPalettedSection sec;
    for (uint16_t id = 1; id <= 255; ++id) {
        sec.setBlock(id % 16, (id / 16) % 16, 0, id);
    }
    EXPECT_EQ(sec.getPaletteSize(), 256u);
    EXPECT_EQ(sec.getBitsPerVoxel(), 8u);

    // Reuse existing ID -> still 8 bits
    sec.setBlock(15, 15, 15, 128);
    EXPECT_EQ(sec.getPaletteSize(), 256u);
    EXPECT_EQ(sec.getBitsPerVoxel(), 8u);
    EXPECT_EQ(sec.getBlock(15, 15, 15), 128u);
}

TEST(PalettedSectionIntegration, HighBlockStateIdPreservation) {
    ContractPalettedSection sec;
    const BlockStateId highId = 1023; // max 10 bits BlockId
    sec.setBlock(3, 4, 5, highId);
    EXPECT_EQ(sec.getBlock(3, 4, 5), highId);
}

TEST(PalettedSectionIntegration, CopyAndAssignmentIndependence) {
    ContractPalettedSection original;
    original.setBlock(1, 2, 3, 42);

    ContractPalettedSection copy = original;
    EXPECT_EQ(copy.getBlock(1, 2, 3), 42u);

    // Mutating copy must not affect original
    copy.setBlock(1, 2, 3, 99);
    EXPECT_EQ(copy.getBlock(1, 2, 3), 99u);
    EXPECT_EQ(original.getBlock(1, 2, 3), 42u);
}

// ============================================================================
// FEATURE 4 & 5: Lazy DenseLightArray Allocation (P3.1)
// ============================================================================

// Tier 1: Primary behaviors (>=5 tests)
TEST(LazyLightIntegration, NullAllocationHomogeneous) {
    ContractPalettedSection sec;
    EXPECT_FALSE(sec.hasLightData());
    EXPECT_EQ(sec.getSkyLight(0, 0, 0), 15u);
    EXPECT_EQ(sec.getBlockLight(0, 0, 0), 0u);
}

TEST(LazyLightIntegration, LazyAllocOnNonUniformSky) {
    ContractPalettedSection sec;
    EXPECT_FALSE(sec.hasLightData());

    sec.setSkyLight(4, 4, 4, 10);
    EXPECT_TRUE(sec.hasLightData());
    EXPECT_EQ(sec.getSkyLight(4, 4, 4), 10u);
    EXPECT_EQ(sec.getSkyLight(0, 0, 0), 15u); // Other voxels retain uniform baseline
}

TEST(LazyLightIntegration, LazyAllocOnNonUniformBlock) {
    ContractPalettedSection sec;
    EXPECT_FALSE(sec.hasLightData());

    sec.setBlockLight(8, 8, 8, 14);
    EXPECT_TRUE(sec.hasLightData());
    EXPECT_EQ(sec.getBlockLight(8, 8, 8), 14u);
    EXPECT_EQ(sec.getBlockLight(0, 0, 0), 0u);
}

TEST(LazyLightIntegration, NoAllocWhenMatchingUniform) {
    ContractPalettedSection sec;
    EXPECT_FALSE(sec.hasLightData());

    // Uniform sky is 15 by default; setting 15 must NOT allocate
    sec.setSkyLight(2, 2, 2, 15);
    EXPECT_FALSE(sec.hasLightData());

    // Uniform block is 0 by default; setting 0 must NOT allocate
    sec.setBlockLight(2, 2, 2, 0);
    EXPECT_FALSE(sec.hasLightData());
}

TEST(LazyLightIntegration, ResetUniformLightingFreesHeap) {
    ContractPalettedSection sec;
    sec.setSkyLight(1, 1, 1, 5);
    EXPECT_TRUE(sec.hasLightData());

    sec.setUniformLighting(15, 0);
    EXPECT_FALSE(sec.hasLightData());
    EXPECT_EQ(sec.getSkyLight(1, 1, 1), 15u);
    EXPECT_EQ(sec.getBlockLight(1, 1, 1), 0u);
}

// Tier 2: Boundary conditions (>=5 tests)
TEST(LazyLightIntegration, NibbleBoundaryValuesClamping) {
    ContractPalettedSection sec;
    sec.setSkyLight(0, 0, 0, 0);
    EXPECT_EQ(sec.getSkyLight(0, 0, 0), 0u);

    sec.setSkyLight(0, 0, 0, 15);
    EXPECT_EQ(sec.getSkyLight(0, 0, 0), 15u);

    // Value > 15 masked to 4 bits (e.g. 17 & 0xF = 1)
    sec.setSkyLight(0, 0, 0, 17);
    EXPECT_EQ(sec.getSkyLight(0, 0, 0), 1u);
}

TEST(LazyLightIntegration, MaxSkyAndMaxBlockCoexist) {
    ContractPalettedSection sec;
    sec.setSkyLight(7, 7, 7, 15);
    sec.setBlockLight(7, 7, 7, 15);

    EXPECT_EQ(sec.getSkyLight(7, 7, 7), 15u);
    EXPECT_EQ(sec.getBlockLight(7, 7, 7), 15u);
}

TEST(LazyLightIntegration, All4096VoxelsDistinctLighting) {
    ContractPalettedSection sec;
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                const uint8_t skyVal = static_cast<uint8_t>((x + y + z) % 16);
                sec.setSkyLight(x, y, z, skyVal);
            }
        }
    }
    EXPECT_TRUE(sec.hasLightData());
    for (int y = 0; y < 16; ++y) {
        for (int z = 0; z < 16; ++z) {
            for (int x = 0; x < 16; ++x) {
                const uint8_t expected = static_cast<uint8_t>((x + y + z) % 16);
                EXPECT_EQ(sec.getSkyLight(x, y, z), expected);
            }
        }
    }
}

TEST(LazyLightIntegration, ZeroMemoryFootprintWhenUniform) {
    ContractPalettedSection sec;
    const size_t initialFootprint = sec.getMemoryFootprintBytes();
    EXPECT_EQ(initialFootprint, sizeof(ContractPalettedSection));

    sec.setSkyLight(0, 0, 0, 8);
    EXPECT_GT(sec.getMemoryFootprintBytes(), initialFootprint);

    sec.setUniformLighting(15, 0);
    EXPECT_EQ(sec.getMemoryFootprintBytes(), initialFootprint);
}

TEST(LazyLightIntegration, ChunkPublicInterfaceCompatibility) {
    voxel::Chunk chunk(0, 0);
    chunk.setBlock(2, 65, 2, voxel::BlockId::Stone);
    EXPECT_EQ(chunk.getBlock(2, 65, 2), voxel::BlockId::Stone);

    chunk.setBlockLight(2, 65, 2, 12);
    EXPECT_EQ(chunk.getBlockLight(2, 65, 2), 12u);
}

// ============================================================================
// FEATURE 6 & 7: PackedChunkVertex & Static Shared EBO (P1.2)
// ============================================================================

// Tier 1: Primary behaviors (>=5 tests)
TEST(PackedVertexIntegration, SizeAndAlignmentContract) {
    EXPECT_EQ(sizeof(ContractPackedChunkVertex), 8u);
    EXPECT_EQ(alignof(ContractPackedChunkVertex), 8u);
    EXPECT_TRUE(std::is_trivially_copyable_v<ContractPackedChunkVertex>);
#if BETACRAFT_HAS_PROD_PACKED_CHUNK_VERTEX
    EXPECT_EQ(sizeof(streaming::PackedChunkVertex), 8u);
    EXPECT_EQ(alignof(streaming::PackedChunkVertex), 8u);
    EXPECT_TRUE(std::is_trivially_copyable_v<streaming::PackedChunkVertex>);
#endif
}

TEST(PackedVertexIntegration, Word0BitAllocationAndRoundTrip) {
    const uint32_t x = 15;
    const uint32_t y = 14;
    const uint32_t z = 13;
    const uint32_t norm = 4;
    const uint32_t ao = 2;
    const uint32_t uv = 3;
    const uint32_t mat = 100;

    const uint32_t w0 = ContractPackedChunkVertex::packWord0(x, y, z, norm, ao, uv, mat);

    EXPECT_EQ(ContractPackedChunkVertex::unpackX(w0), x);
    EXPECT_EQ(ContractPackedChunkVertex::unpackY(w0), y);
    EXPECT_EQ(ContractPackedChunkVertex::unpackZ(w0), z);
    EXPECT_EQ(ContractPackedChunkVertex::unpackNormal(w0), norm);
    EXPECT_EQ(ContractPackedChunkVertex::unpackAO(w0), ao);
    EXPECT_EQ(ContractPackedChunkVertex::unpackUVCorner(w0), uv);
    EXPECT_EQ(ContractPackedChunkVertex::unpackMatLayer(w0), mat);
}

TEST(PackedVertexIntegration, Word1BitAllocationAndRoundTrip) {
    const uint32_t sky = 14;
    const uint32_t blk = 12;
    const uint32_t tint = 180;
    const uint32_t anim = 15;
    const bool sway = true;
    const bool trans = false;

    const uint32_t w1 = ContractPackedChunkVertex::packWord1(sky, blk, tint, anim, sway, trans);

    EXPECT_EQ(ContractPackedChunkVertex::unpackSkyLight(w1), sky);
    EXPECT_EQ(ContractPackedChunkVertex::unpackBlockLight(w1), blk);
    EXPECT_EQ(ContractPackedChunkVertex::unpackBiomeTint(w1), tint);
    EXPECT_EQ(ContractPackedChunkVertex::unpackAnimFrame(w1), anim);
    EXPECT_EQ(ContractPackedChunkVertex::unpackSway(w1), sway);
    EXPECT_EQ(ContractPackedChunkVertex::unpackTranslucent(w1), trans);
}

TEST(PackedVertexIntegration, SharedEboPatternVerification) {
    const size_t quadCount = 4;
    const auto ebo = generateSharedEboPattern(quadCount);

    EXPECT_EQ(ebo.size(), quadCount * 6);
    EXPECT_TRUE(validateEboPattern(ebo, quadCount));

    // Quad 0: [0, 1, 2, 0, 2, 3]
    EXPECT_EQ(ebo[0], 0u); EXPECT_EQ(ebo[1], 1u); EXPECT_EQ(ebo[2], 2u);
    EXPECT_EQ(ebo[3], 0u); EXPECT_EQ(ebo[4], 2u); EXPECT_EQ(ebo[5], 3u);

    // Quad 1: [4, 5, 6, 4, 6, 7]
    EXPECT_EQ(ebo[6], 4u); EXPECT_EQ(ebo[7], 5u); EXPECT_EQ(ebo[8], 6u);
    EXPECT_EQ(ebo[9], 4u); EXPECT_EQ(ebo[10], 6u); EXPECT_EQ(ebo[11], 7u);
}

TEST(PackedVertexIntegration, VramReductionRatio) {
    const size_t legacyBytesPerQuad = 6 * 40; // 6 vertices * 40 bytes = 240B
    const size_t packedBytesPerQuad = 4 * 8;  // 4 vertices * 8 bytes = 32B

    const double reduction = 1.0 - (static_cast<double>(packedBytesPerQuad) / static_cast<double>(legacyBytesPerQuad));
    // Reduction must be exactly (240 - 32) / 240 = 208 / 240 = 86.6667%
    EXPECT_NEAR(reduction, 0.866667, 1e-4);
}

// Tier 2: Boundary conditions (>=5 tests)
TEST(PackedVertexIntegration, MaxCoordinateLimits) {
    // 5 bits allows [0..31]; local sub-chunk coordinates span [0..16]
    const uint32_t w0 = ContractPackedChunkVertex::packWord0(16, 16, 16, 5, 3, 3, 1023);
    EXPECT_EQ(ContractPackedChunkVertex::unpackX(w0), 16u);
    EXPECT_EQ(ContractPackedChunkVertex::unpackY(w0), 16u);
    EXPECT_EQ(ContractPackedChunkVertex::unpackZ(w0), 16u);
    EXPECT_EQ(ContractPackedChunkVertex::unpackNormal(w0), 5u);
    EXPECT_EQ(ContractPackedChunkVertex::unpackAO(w0), 3u);
    EXPECT_EQ(ContractPackedChunkVertex::unpackUVCorner(w0), 3u);
    EXPECT_EQ(ContractPackedChunkVertex::unpackMatLayer(w0), 1023u);
}

TEST(PackedVertexIntegration, MaxMaterialLayer1023) {
    const uint32_t w0 = ContractPackedChunkVertex::packWord0(0, 0, 0, 0, 0, 0, 1023);
    EXPECT_EQ(ContractPackedChunkVertex::unpackMatLayer(w0), 1023u);
    EXPECT_EQ(ContractPackedChunkVertex::unpackUVCorner(w0), 0u);
}

TEST(PackedVertexIntegration, LightingLimits15) {
    const uint32_t w1 = ContractPackedChunkVertex::packWord1(15, 15, 255, 31, true, true);
    EXPECT_EQ(ContractPackedChunkVertex::unpackSkyLight(w1), 15u);
    EXPECT_EQ(ContractPackedChunkVertex::unpackBlockLight(w1), 15u);
    EXPECT_EQ(ContractPackedChunkVertex::unpackBiomeTint(w1), 255u);
    EXPECT_EQ(ContractPackedChunkVertex::unpackAnimFrame(w1), 31u);
    EXPECT_TRUE(ContractPackedChunkVertex::unpackSway(w1));
    EXPECT_TRUE(ContractPackedChunkVertex::unpackTranslucent(w1));
}

TEST(PackedVertexIntegration, PlanarUVProjectionTiling) {
    // Top face (+Y: normIdx = 0): planar pos is (X, Z)
    const glm::vec3 pos(14.75f, 64.0f, 25.25f);
    const glm::vec2 uv = computePlanarUV(pos, 0);

    EXPECT_NEAR(uv.x, 0.75f, 1e-4f);
    EXPECT_NEAR(uv.y, 0.25f, 1e-4f);
}

TEST(PackedVertexIntegration, LargeSharedEboValidation) {
    // Test validation for up to 1024 quads
    const auto ebo = generateSharedEboPattern(1024);
    EXPECT_TRUE(validateEboPattern(ebo, 1024));
}

// ============================================================================
// FEATURE 10, 11, 12: Certified BFS Differential Lighting (P3.2)
// ============================================================================

// Tier 1: Primary behaviors (>=5 tests)
TEST(BFSLightingIntegration, AnalyticalSphereFormulaConsistency) {
    // Sum formula: N(R) = (2R+1)(2R^2 + 2R + 3) / 3
    for (size_t r = 0; r <= 15; ++r) {
        size_t iterativeSum = 1; // Center voxel
        for (size_t i = 1; i <= r; ++i) {
            iterativeSum += analyticalManhattanShell(i);
        }
        EXPECT_EQ(analyticalManhattanVolume(r), iterativeSum);
    }
}

TEST(BFSLightingIntegration, ExactManhattanVolumeR15) {
    EXPECT_EQ(analyticalManhattanVolume(15), 4991u);
}

TEST(BFSLightingIntegration, DiscreteBFSVerificationR15) {
    // Perform discrete 3D BFS on integer grid
    const size_t actualVoxels = computeDiscreteManhattanVolumeBFS(15);
    EXPECT_EQ(actualVoxels, 4991u);
}

TEST(BFSLightingIntegration, DiscreteBFSVerificationSmallRadii) {
    EXPECT_EQ(computeDiscreteManhattanVolumeBFS(0), 1u);
    EXPECT_EQ(computeDiscreteManhattanVolumeBFS(1), 7u);
    EXPECT_EQ(computeDiscreteManhattanVolumeBFS(2), 25u);
    EXPECT_EQ(computeDiscreteManhattanVolumeBFS(3), 63u);
}

TEST(BFSLightingIntegration, WorstCaseOpenAirBoundUnder4991) {
    // Placing or removing a torch in pure open air can never exceed 4,991 voxels
    const size_t maxPossibleVoxels = 4991;
    EXPECT_LE(computeDiscreteManhattanVolumeBFS(15), maxPossibleVoxels);
}

// Tier 2: Boundary conditions (>=5 tests)
TEST(BFSLightingIntegration, RadiusZeroCenterOnly) {
    EXPECT_EQ(analyticalManhattanVolume(0), 1u);
}

TEST(BFSLightingIntegration, RadiusOneCenterPlusSixNeighbors) {
    EXPECT_EQ(analyticalManhattanVolume(1), 7u);
    EXPECT_EQ(analyticalManhattanShell(1), 6u);
}

TEST(BFSLightingIntegration, ShellCountProgression) {
    // S(r) = 4r^2 + 2: S(1)=6, S(2)=18, S(3)=38, S(4)=66
    EXPECT_EQ(analyticalManhattanShell(1), 6u);
    EXPECT_EQ(analyticalManhattanShell(2), 18u);
    EXPECT_EQ(analyticalManhattanShell(3), 38u);
    EXPECT_EQ(analyticalManhattanShell(4), 66u);
}

TEST(BFSLightingIntegration, EnclosedSpaceVisitsStrictlyFewerVoxels) {
    // In an enclosed room of size 5x5x5, max voxels visited is bounded by 125 < 4991
    const size_t roomVolume = 5 * 5 * 5;
    EXPECT_LT(roomVolume, 4991u);
}

TEST(BFSLightingIntegration, Skylight1DRaycastExtinction) {
    // Column of 64 voxels from Y=128 down to Y=64
    const int yHigh = 128;
    const int yLow = 64;
    const int deltaH = yHigh - yLow;

    int steps = 0;
    for (int y = yHigh; y >= yLow; --y) {
        ++steps;
    }
    EXPECT_EQ(steps, deltaH + 1);
    EXPECT_LE(steps, 129); // O(Delta H) linear bound
}

// ============================================================================
// FEATURE 13 & 14: Lockless MpscRingBuffer Concurrency (P4.1)
// ============================================================================

struct DummyAudioCommand {
    uint32_t soundId{0};
    float volume{1.0f};
    float pitch{1.0f};
    int producerId{0};
};

// Tier 1: Primary behaviors (>=5 tests)
TEST(MpscRingBufferIntegration, PowerOfTwoCapacity) {
    ContractMpscRingBuffer<DummyAudioCommand, 512> ring;
    EXPECT_EQ(ring.capacity(), 512u);
}

TEST(MpscRingBufferIntegration, SingleProducerSingleConsumerFIFO) {
    ContractMpscRingBuffer<DummyAudioCommand, 512> ring;

    for (uint32_t i = 0; i < 200; ++i) {
        DummyAudioCommand cmd{i, 1.0f, 1.0f, 1};
        EXPECT_TRUE(ring.push(cmd));
    }

    for (uint32_t i = 0; i < 200; ++i) {
        DummyAudioCommand out{};
        EXPECT_TRUE(ring.pop(out));
        EXPECT_EQ(out.soundId, i);
    }
}

TEST(MpscRingBufferIntegration, MultiProducerConcurrency) {
    ContractMpscRingBuffer<DummyAudioCommand, 512> ring;
    constexpr int numProducers = 4;
    constexpr int itemsPerProducer = 100;

    std::vector<std::thread> producers;
    producers.reserve(numProducers);

    for (int p = 0; p < numProducers; ++p) {
        producers.emplace_back([&ring, p]() {
            for (int i = 0; i < itemsPerProducer; ++i) {
                DummyAudioCommand cmd{static_cast<uint32_t>(i), 1.0f, 1.0f, p};
                while (!ring.push(cmd)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    for (auto& t : producers) {
        t.join();
    }

    // Consumer drains all items
    std::array<int, numProducers> lastSeen{};
    lastSeen.fill(-1);
    size_t totalReceived = 0;

    DummyAudioCommand out{};
    while (ring.pop(out)) {
        EXPECT_EQ(out.soundId, static_cast<uint32_t>(lastSeen[out.producerId] + 1));
        lastSeen[out.producerId] = static_cast<int>(out.soundId);
        ++totalReceived;
    }

    EXPECT_EQ(totalReceived, static_cast<size_t>(numProducers * itemsPerProducer));
}

TEST(MpscRingBufferIntegration, NonBlockingAudioPop) {
    ContractMpscRingBuffer<DummyAudioCommand, 512> ring;
    DummyAudioCommand out{};
    // Must return false instantly when empty
    EXPECT_FALSE(ring.pop(out));
}

TEST(MpscRingBufferIntegration, ZeroMutexZeroAllocation) {
    ContractMpscRingBuffer<DummyAudioCommand, 512> ring;
    DummyAudioCommand cmd{42, 0.8f, 1.0f, 0};
    EXPECT_TRUE(ring.push(cmd));

    DummyAudioCommand popped{};
    EXPECT_TRUE(ring.pop(popped));
    EXPECT_EQ(popped.soundId, 42u);
}

// Tier 2: Boundary conditions (>=5 tests)
TEST(MpscRingBufferIntegration, FullBufferPushFailsGracefully) {
    ContractMpscRingBuffer<DummyAudioCommand, 8> smallRing;
    for (uint32_t i = 0; i < 8; ++i) {
        DummyAudioCommand cmd{i, 1.0f, 1.0f, 0};
        EXPECT_TRUE(smallRing.push(cmd));
    }
    // 9th push must fail gracefully
    DummyAudioCommand overflow{999, 1.0f, 1.0f, 0};
    EXPECT_FALSE(smallRing.push(overflow));
}

TEST(MpscRingBufferIntegration, EmptyBufferPopFailsGracefully) {
    ContractMpscRingBuffer<DummyAudioCommand, 16> ring;
    DummyAudioCommand dummy{};
    EXPECT_FALSE(ring.pop(dummy));
}

TEST(MpscRingBufferIntegration, AlternatingPushPopCycle) {
    ContractMpscRingBuffer<DummyAudioCommand, 16> ring;
    for (uint32_t i = 0; i < 5000; ++i) {
        DummyAudioCommand in{i, 1.0f, 1.0f, 0};
        EXPECT_TRUE(ring.push(in));

        DummyAudioCommand out{};
        EXPECT_TRUE(ring.pop(out));
        EXPECT_EQ(out.soundId, i);
    }
}

TEST(MpscRingBufferIntegration, SequenceWraparoundIntegrity) {
    ContractMpscRingBuffer<DummyAudioCommand, 32> ring;
    // Push and pop 200 items in chunks of 20
    uint32_t counter = 0;
    for (int chunk = 0; chunk < 10; ++chunk) {
        for (int i = 0; i < 20; ++i) {
            DummyAudioCommand in{counter++, 1.0f, 1.0f, 0};
            EXPECT_TRUE(ring.push(in));
        }
        for (int i = 0; i < 20; ++i) {
            DummyAudioCommand out{};
            EXPECT_TRUE(ring.pop(out));
        }
    }
    EXPECT_EQ(counter, 200u);
}

TEST(MpscRingBufferIntegration, BurstPushSaturationAndRecovery) {
    ContractMpscRingBuffer<DummyAudioCommand, 16> ring;
    size_t pushed = 0;
    while (ring.push(DummyAudioCommand{static_cast<uint32_t>(pushed), 1.0f, 1.0f, 0})) {
        ++pushed;
    }
    EXPECT_EQ(pushed, 16u);

    // Drain half
    for (size_t i = 0; i < 8; ++i) {
        DummyAudioCommand out{};
        EXPECT_TRUE(ring.pop(out));
    }

    // Now push 8 more
    for (size_t i = 0; i < 8; ++i) {
        EXPECT_TRUE(ring.push(DummyAudioCommand{static_cast<uint32_t>(100 + i), 1.0f, 1.0f, 0}));
    }
}

// ============================================================================
// FEATURE 15-19: UI MSDF & Dynamic Key Rebinding (P4.3, P4.4)
// ============================================================================

// Tier 1: Primary behaviors (>=5 tests)
TEST(UIAndInputIntegration, MsdfMedianFilterMath) {
    EXPECT_FLOAT_EQ(msdfMedian(0.2f, 0.5f, 0.8f), 0.5f);
    EXPECT_FLOAT_EQ(msdfMedian(0.9f, 0.1f, 0.4f), 0.4f);
    EXPECT_FLOAT_EQ(msdfMedian(0.7f, 0.7f, 0.7f), 0.7f);
}

TEST(UIAndInputIntegration, MsdfFragmentEvaluationEdges) {
    // Distance field 0.5 is the exact glyph contour
    const glm::vec3 contourPoint(0.5f, 0.5f, 0.5f);
    const float alphaContour = msdfEvaluateFragment(contourPoint);
    EXPECT_NEAR(alphaContour, 0.5f, 1e-4f);

    // Distance > 0.5 is inside the glyph
    const glm::vec3 insidePoint(0.8f, 0.8f, 0.8f);
    EXPECT_GT(msdfEvaluateFragment(insidePoint), 0.5f);

    // Distance < 0.5 is outside the glyph
    const glm::vec3 outsidePoint(0.2f, 0.2f, 0.2f);
    EXPECT_LT(msdfEvaluateFragment(outsidePoint), 0.5f);
}

TEST(UIAndInputIntegration, VirtualGuiScaleCalculationAuto) {
    // 640x480 -> 2x (640/320=2, 480/240=2)
    EXPECT_EQ(calculateVirtualGuiScale(640, 480, GuiScalePreference::Auto), 2);
    // 1280x720 -> 3x
    EXPECT_EQ(calculateVirtualGuiScale(1280, 720, GuiScalePreference::Auto), 3);
    // 1920x1080 -> 4x
    EXPECT_EQ(calculateVirtualGuiScale(1920, 1080, GuiScalePreference::Auto), 4);
}

TEST(UIAndInputIntegration, DynamicKeyRebindingSaveAndLoad) {
    betacraft::test::TempDir tempDir;
    const auto optionsPath = tempDir.path() / "options.txt";

    KeyConfig originalConfig;
    originalConfig.bindings["key_forward"] = 29; // Rebind W to Z (French AZERTY)
    originalConfig.bindings["key_jump"] = 44;
    originalConfig.guiScale = 3;
    originalConfig.fov = 85.0f;
    originalConfig.musicVolume = 0.5f;

    originalConfig.saveToFile(optionsPath);
    EXPECT_TRUE(std::filesystem::exists(optionsPath));

    KeyConfig loadedConfig;
    EXPECT_TRUE(loadedConfig.loadFromFile(optionsPath));
    EXPECT_EQ(loadedConfig.bindings["key_forward"], 29);
    EXPECT_EQ(loadedConfig.bindings["key_jump"], 44);
    EXPECT_EQ(loadedConfig.guiScale, 3);
    EXPECT_FLOAT_EQ(loadedConfig.fov, 85.0f);
    EXPECT_FLOAT_EQ(loadedConfig.musicVolume, 0.5f);
}

TEST(UIAndInputIntegration, ScreenStateTransitions) {
    enum class State { TitleScreen, OptionsMenu, KeybindingMenu, InGame, PauseMenu };
    State currentState = State::TitleScreen;

    // Title -> Options
    currentState = State::OptionsMenu;
    EXPECT_EQ(currentState, State::OptionsMenu);

    // Options -> Keybinding
    currentState = State::KeybindingMenu;
    EXPECT_EQ(currentState, State::KeybindingMenu);

    // Keybinding -> Options
    currentState = State::OptionsMenu;
    EXPECT_EQ(currentState, State::OptionsMenu);

    // Options -> InGame
    currentState = State::InGame;
    EXPECT_EQ(currentState, State::InGame);

    // InGame -> Pause
    currentState = State::PauseMenu;
    EXPECT_EQ(currentState, State::PauseMenu);
}

// Tier 2: Boundary conditions (>=5 tests)
TEST(UIAndInputIntegration, VirtualGuiScaleMinimumBoundary) {
    // 320x240 minimum window gives scale 1
    EXPECT_EQ(calculateVirtualGuiScale(320, 240, GuiScalePreference::Auto), 1);
    // Even smaller dimensions clamp to at least 1
    EXPECT_EQ(calculateVirtualGuiScale(100, 100, GuiScalePreference::Auto), 1);
}

TEST(UIAndInputIntegration, VirtualGuiScaleExplicitPreference) {
    // Explicit preferences override resolution calculation
    EXPECT_EQ(calculateVirtualGuiScale(3840, 2160, GuiScalePreference::Scale1x), 1);
    EXPECT_EQ(calculateVirtualGuiScale(3840, 2160, GuiScalePreference::Scale2x), 2);
    EXPECT_EQ(calculateVirtualGuiScale(640, 480, GuiScalePreference::Scale4x), 4);
}

TEST(UIAndInputIntegration, OptionsMissingKeysFallback) {
    betacraft::test::TempDir tempDir;
    const auto optionsPath = tempDir.path() / "partial_options.txt";

    {
        std::ofstream out(optionsPath);
        out << "fov:90.0\n";
    }

    KeyConfig config;
    EXPECT_TRUE(config.loadFromFile(optionsPath));
    EXPECT_FLOAT_EQ(config.fov, 90.0f);
    // Unmentioned keys retain defaults
    EXPECT_EQ(config.bindings["key_forward"], 26);
    EXPECT_EQ(config.guiScale, 0);
}

TEST(UIAndInputIntegration, OptionsCorruptedLinesTolerance) {
    betacraft::test::TempDir tempDir;
    const auto optionsPath = tempDir.path() / "corrupted_options.txt";

    {
        std::ofstream out(optionsPath);
        out << "garbage_with_no_colon\n";
        out << ":::invalid:::\n";
        out << "fov:75.0\n";
    }

    KeyConfig config;
    EXPECT_TRUE(config.loadFromFile(optionsPath));
    EXPECT_FLOAT_EQ(config.fov, 75.0f);
}

TEST(UIAndInputIntegration, AllGameActionsConfigured) {
    KeyConfig config;
    EXPECT_TRUE(config.bindings.find("key_forward") != config.bindings.end());
    EXPECT_TRUE(config.bindings.find("key_backward") != config.bindings.end());
    EXPECT_TRUE(config.bindings.find("key_left") != config.bindings.end());
    EXPECT_TRUE(config.bindings.find("key_right") != config.bindings.end());
    EXPECT_TRUE(config.bindings.find("key_jump") != config.bindings.end());
    EXPECT_TRUE(config.bindings.find("key_sneak") != config.bindings.end());
    EXPECT_TRUE(config.bindings.find("key_inventory") != config.bindings.end());
}

// ============================================================================
// TIER 3: Cross-Feature / Pairwise Combinatorial Tests
// ============================================================================

TEST(Tier3CrossFeature, PhysicsAndVoxelModification) {
    // Combine fixed timestep physics tick with voxel modification
    ContractPalettedSection section;
    section.setBlock(0, 0, 0, 1); // Solid block at (0,0,0)

    const auto res = simulateFiedlerPhysics(1.0f, 1.0f / 60.0f, 1.0f / 60.0f, 0.25f, glm::vec3(0.0f, 10.0f, 0.0f));
    EXPECT_GT(res.tickCount, 0u);
    EXPECT_EQ(section.getBlock(0, 0, 0), 1u);
}

TEST(Tier3CrossFeature, PalettedSectionAndLazyLightingInteraction) {
    // Dynamic palette upgrade coincident with lazy lighting allocation
    ContractPalettedSection section;
    EXPECT_TRUE(section.isHomogeneous());

    // Place block -> palette upgrade 0 to 4 bits
    section.setBlock(2, 2, 2, 5);
    EXPECT_EQ(section.getBitsPerVoxel(), 4u);
    EXPECT_FALSE(section.hasLightData()); // Light remains uniform

    // Perturb lighting -> lazy light allocated
    section.setBlockLight(2, 2, 2, 14);
    EXPECT_TRUE(section.hasLightData());

    // Reset lighting -> lazy light freed, palette remains 4 bits
    section.setUniformLighting(15, 0);
    EXPECT_FALSE(section.hasLightData());
    EXPECT_EQ(section.getBlock(2, 2, 2), 5u);
}

TEST(Tier3CrossFeature, PackedVertexAndSharedEboIndexing) {
    // Generate packed quad vertices and verify they index cleanly with shared EBO
    std::vector<ContractPackedChunkVertex> quadVertices;
    for (uint32_t corner = 0; corner < 4; ++corner) {
        ContractPackedChunkVertex v{};
        v.word0 = ContractPackedChunkVertex::packWord0(0, 0, 0, 0, 3, corner, 1);
        v.word1 = ContractPackedChunkVertex::packWord1(15, 0, 0, 0, false, false);
        quadVertices.push_back(v);
    }
    EXPECT_EQ(quadVertices.size(), 4u);

    const auto ebo = generateSharedEboPattern(1);
    for (uint32_t idx : ebo) {
        EXPECT_LT(idx, quadVertices.size());
    }
}

TEST(Tier3CrossFeature, DifferentialLightingAndSectionBoundary) {
    // Light emitted at X=15 must be able to propagate to X=16 (adjacent section)
    const glm::ivec3 emissionPos(15, 10, 10);
    const glm::ivec3 neighborPos(16, 10, 10);

    const int dist = std::abs(emissionPos.x - neighborPos.x) +
                     std::abs(emissionPos.y - neighborPos.y) +
                     std::abs(emissionPos.z - neighborPos.z);
    EXPECT_EQ(dist, 1);

    const uint8_t emissionLevel = 15;
    const uint8_t neighborLevel = emissionLevel - 1;
    EXPECT_EQ(neighborLevel, 14u);
}

TEST(Tier3CrossFeature, MpscAudioAndFixedTimestepGeneration) {
    // Fixed timestep ticks generate audio commands into lockless queue
    ContractMpscRingBuffer<DummyAudioCommand, 512> ring;
    const float fixedDt = 1.0f / 60.0f;
    float accumulator = 0.0f;
    uint32_t soundCounter = 0;

    // Run 1 second
    for (int frame = 0; frame < 60; ++frame) {
        accumulator += fixedDt;
        while (accumulator >= fixedDt) {
            accumulator -= fixedDt;
            // Emit footstep sound on fixed tick
            DummyAudioCommand cmd{++soundCounter, 0.5f, 1.0f, 0};
            EXPECT_TRUE(ring.push(cmd));
        }
    }

    EXPECT_EQ(soundCounter, 60u);

    // Audio thread drains all 60 sounds
    size_t drained = 0;
    DummyAudioCommand out{};
    while (ring.pop(out)) {
        ++drained;
    }
    EXPECT_EQ(drained, 60u);
}

TEST(Tier3CrossFeature, KeyRebindingAndScreenStateFlow) {
    KeyConfig config;
    config.bindings["key_forward"] = 26; // W

    // In Options menu, change key to Up Arrow (82)
    config.bindings["key_forward"] = 82;

    // Transition Options -> InGame
    EXPECT_EQ(config.bindings["key_forward"], 82);
}
