#include "render/PackedChunkVertex.hpp"
#include "tests/test_helpers.hpp"
#include "voxel/Block.hpp"
#include "world/DifferentialLightingEngine.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>
#include <random>
#include <vector>
#include <array>
#include <cmath>

using namespace betacraft;
using namespace betacraft::render;
using namespace betacraft::world;
using namespace betacraft::voxel;

// ============================================================================
// SUITE 1: PackedChunkVertex Boundary Bit Patterns & Fuzzing
// ============================================================================

TEST(AdversarialPackedChunkVertex, SizeAlignmentAndTypeTraits) {
    EXPECT_EQ(sizeof(PackedChunkVertex), 8u);
    EXPECT_EQ(alignof(PackedChunkVertex), 8u);
    EXPECT_TRUE(std::is_trivially_copyable_v<PackedChunkVertex>);
    EXPECT_TRUE(std::is_standard_layout_v<PackedChunkVertex>);
}

TEST(AdversarialPackedChunkVertex, Word0AllZerosAndAllOnes) {
    // All zeros
    const uint32_t w0Zero = PackedChunkVertex::packWord0(0, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(w0Zero, 0x00000000u);
    EXPECT_EQ(PackedChunkVertex::unpackX(w0Zero), 0u);
    EXPECT_EQ(PackedChunkVertex::unpackY(w0Zero), 0u);
    EXPECT_EQ(PackedChunkVertex::unpackZ(w0Zero), 0u);
    EXPECT_EQ(PackedChunkVertex::unpackNormal(w0Zero), 0u);
    EXPECT_EQ(PackedChunkVertex::unpackAO(w0Zero), 0u);
    EXPECT_EQ(PackedChunkVertex::unpackUVCorner(w0Zero), 0u);
    EXPECT_EQ(PackedChunkVertex::unpackMatLayer(w0Zero), 0u);

    // All ones: 5 + 5 + 5 + 3 + 2 + 2 + 10 = 32 bits
    const uint32_t w0Ones = PackedChunkVertex::packWord0(31, 31, 31, 7, 3, 3, 1023);
    EXPECT_EQ(w0Ones, 0xFFFFFFFFu);
    EXPECT_EQ(PackedChunkVertex::unpackX(w0Ones), 31u);
    EXPECT_EQ(PackedChunkVertex::unpackY(w0Ones), 31u);
    EXPECT_EQ(PackedChunkVertex::unpackZ(w0Ones), 31u);
    EXPECT_EQ(PackedChunkVertex::unpackNormal(w0Ones), 7u);
    EXPECT_EQ(PackedChunkVertex::unpackAO(w0Ones), 3u);
    EXPECT_EQ(PackedChunkVertex::unpackUVCorner(w0Ones), 3u);
    EXPECT_EQ(PackedChunkVertex::unpackMatLayer(w0Ones), 1023u);
}

TEST(AdversarialPackedChunkVertex, Word1AllZerosAndAllOnes) {
    // All zeros
    const uint32_t w1Zero = PackedChunkVertex::packWord1(0, 0, 0, 0, false, false, 0);
    EXPECT_EQ(w1Zero, 0x00000000u);
    EXPECT_EQ(PackedChunkVertex::unpackSkyLight(w1Zero), 0u);
    EXPECT_EQ(PackedChunkVertex::unpackBlockLight(w1Zero), 0u);
    EXPECT_EQ(PackedChunkVertex::unpackBiomeTint(w1Zero), 0u);
    EXPECT_EQ(PackedChunkVertex::unpackAnimFrame(w1Zero), 0u);
    EXPECT_FALSE(PackedChunkVertex::unpackSway(w1Zero));
    EXPECT_FALSE(PackedChunkVertex::unpackTranslucent(w1Zero));
    EXPECT_EQ(PackedChunkVertex::unpackReserved(w1Zero), 0u);

    // All ones: 4 + 4 + 8 + 5 + 1 + 1 + 9 = 32 bits
    const uint32_t w1Ones = PackedChunkVertex::packWord1(15, 15, 255, 31, true, true, 511);
    EXPECT_EQ(w1Ones, 0xFFFFFFFFu);
    EXPECT_EQ(PackedChunkVertex::unpackSkyLight(w1Ones), 15u);
    EXPECT_EQ(PackedChunkVertex::unpackBlockLight(w1Ones), 15u);
    EXPECT_EQ(PackedChunkVertex::unpackBiomeTint(w1Ones), 255u);
    EXPECT_EQ(PackedChunkVertex::unpackAnimFrame(w1Ones), 31u);
    EXPECT_TRUE(PackedChunkVertex::unpackSway(w1Ones));
    EXPECT_TRUE(PackedChunkVertex::unpackTranslucent(w1Ones));
    EXPECT_EQ(PackedChunkVertex::unpackReserved(w1Ones), 511u);
}

TEST(AdversarialPackedChunkVertex, Word0SingleBitIsolationWalkingOnes) {
    // For each of the 32 bits in word0, verify that only the target field is affected
    for (uint32_t b = 0; b < 32; ++b) {
        const uint32_t w0 = (1u << b);

        const uint32_t x    = PackedChunkVertex::unpackX(w0);
        const uint32_t y    = PackedChunkVertex::unpackY(w0);
        const uint32_t z    = PackedChunkVertex::unpackZ(w0);
        const uint32_t norm = PackedChunkVertex::unpackNormal(w0);
        const uint32_t ao   = PackedChunkVertex::unpackAO(w0);
        const uint32_t uv   = PackedChunkVertex::unpackUVCorner(w0);
        const uint32_t mat  = PackedChunkVertex::unpackMatLayer(w0);

        if (b < 5) { // bits 0..4: X
            EXPECT_EQ(x, 1u << b);
            EXPECT_EQ(y + z + norm + ao + uv + mat, 0u);
        } else if (b < 10) { // bits 5..9: Y
            EXPECT_EQ(y, 1u << (b - 5));
            EXPECT_EQ(x + z + norm + ao + uv + mat, 0u);
        } else if (b < 15) { // bits 10..14: Z
            EXPECT_EQ(z, 1u << (b - 10));
            EXPECT_EQ(x + y + norm + ao + uv + mat, 0u);
        } else if (b < 18) { // bits 15..17: Normal
            EXPECT_EQ(norm, 1u << (b - 15));
            EXPECT_EQ(x + y + z + ao + uv + mat, 0u);
        } else if (b < 20) { // bits 18..19: AO
            EXPECT_EQ(ao, 1u << (b - 18));
            EXPECT_EQ(x + y + z + norm + uv + mat, 0u);
        } else if (b < 22) { // bits 20..21: UV Corner
            EXPECT_EQ(uv, 1u << (b - 20));
            EXPECT_EQ(x + y + z + norm + ao + mat, 0u);
        } else { // bits 22..31: Material Layer
            EXPECT_EQ(mat, 1u << (b - 22));
            EXPECT_EQ(x + y + z + norm + ao + uv, 0u);
        }
    }
}

TEST(AdversarialPackedChunkVertex, Word1SingleBitIsolationWalkingOnes) {
    for (uint32_t b = 0; b < 32; ++b) {
        const uint32_t w1 = (1u << b);

        const uint32_t sky   = PackedChunkVertex::unpackSkyLight(w1);
        const uint32_t blk   = PackedChunkVertex::unpackBlockLight(w1);
        const uint32_t tint  = PackedChunkVertex::unpackBiomeTint(w1);
        const uint32_t anim  = PackedChunkVertex::unpackAnimFrame(w1);
        const bool     sway  = PackedChunkVertex::unpackSway(w1);
        const bool     trans = PackedChunkVertex::unpackTranslucent(w1);
        const uint32_t res   = PackedChunkVertex::unpackReserved(w1);

        if (b < 4) { // bits 0..3: SkyLight
            EXPECT_EQ(sky, 1u << b);
            EXPECT_EQ(blk + tint + anim + (sway ? 1 : 0) + (trans ? 1 : 0) + res, 0u);
        } else if (b < 8) { // bits 4..7: BlockLight
            EXPECT_EQ(blk, 1u << (b - 4));
            EXPECT_EQ(sky + tint + anim + (sway ? 1 : 0) + (trans ? 1 : 0) + res, 0u);
        } else if (b < 16) { // bits 8..15: BiomeTint
            EXPECT_EQ(tint, 1u << (b - 8));
            EXPECT_EQ(sky + blk + anim + (sway ? 1 : 0) + (trans ? 1 : 0) + res, 0u);
        } else if (b < 21) { // bits 16..20: AnimFrame
            EXPECT_EQ(anim, 1u << (b - 16));
            EXPECT_EQ(sky + blk + tint + (sway ? 1 : 0) + (trans ? 1 : 0) + res, 0u);
        } else if (b == 21) { // bit 21: Sway
            EXPECT_TRUE(sway);
            EXPECT_EQ(sky + blk + tint + anim + (trans ? 1 : 0) + res, 0u);
        } else if (b == 22) { // bit 22: Translucent
            EXPECT_TRUE(trans);
            EXPECT_EQ(sky + blk + tint + anim + (sway ? 1 : 0) + res, 0u);
        } else { // bits 23..31: Reserved
            EXPECT_EQ(res, 1u << (b - 23));
            EXPECT_EQ(sky + blk + tint + anim + (sway ? 1 : 0) + (trans ? 1 : 0), 0u);
        }
    }
}

TEST(AdversarialPackedChunkVertex, AlternatingBitPatternsAndInverses) {
    const uint32_t patternA = 0x55555555u; // 0101...
    const uint32_t patternB = 0xAAAAAAAAu; // 1010...

    // Word 0 pattern A
    EXPECT_EQ(PackedChunkVertex::unpackX(patternA), 0x15u & 0x1Fu); // 10101 = 21
    EXPECT_EQ(PackedChunkVertex::unpackY(patternA), (0x55555555u >> 5u) & 0x1Fu); // 01010 = 10
    EXPECT_EQ(PackedChunkVertex::unpackZ(patternA), (0x55555555u >> 10u) & 0x1Fu); // 10101 = 21

    // Word 0 pattern B
    EXPECT_EQ(PackedChunkVertex::unpackX(patternB), 0x0Au & 0x1Fu); // 01010 = 10
    EXPECT_EQ(PackedChunkVertex::unpackY(patternB), (0xAAAAAAAAu >> 5u) & 0x1Fu); // 10101 = 21
    EXPECT_EQ(PackedChunkVertex::unpackZ(patternB), (0xAAAAAAAAu >> 10u) & 0x1Fu); // 01010 = 10

    // Repack and assert exact bit equality
    const uint32_t repackedA = PackedChunkVertex::packWord0(
        PackedChunkVertex::unpackX(patternA),
        PackedChunkVertex::unpackY(patternA),
        PackedChunkVertex::unpackZ(patternA),
        PackedChunkVertex::unpackNormal(patternA),
        PackedChunkVertex::unpackAO(patternA),
        PackedChunkVertex::unpackUVCorner(patternA),
        PackedChunkVertex::unpackMatLayer(patternA)
    );
    EXPECT_EQ(repackedA, patternA);

    const uint32_t repackedB = PackedChunkVertex::packWord0(
        PackedChunkVertex::unpackX(patternB),
        PackedChunkVertex::unpackY(patternB),
        PackedChunkVertex::unpackZ(patternB),
        PackedChunkVertex::unpackNormal(patternB),
        PackedChunkVertex::unpackAO(patternB),
        PackedChunkVertex::unpackUVCorner(patternB),
        PackedChunkVertex::unpackMatLayer(patternB)
    );
    EXPECT_EQ(repackedB, patternB);
}

TEST(AdversarialPackedChunkVertex, FieldClampingMaskingExtremeValues) {
    // Word 0: pass UINT32_MAX to each field individually, verify only that field gets max
    const uint32_t xOverflow = PackedChunkVertex::packWord0(0xFFFFFFFFu, 0, 0, 0, 0, 0, 0);
    EXPECT_EQ(PackedChunkVertex::unpackX(xOverflow), 0x1Fu);
    EXPECT_EQ(PackedChunkVertex::unpackY(xOverflow), 0u);
    EXPECT_EQ(PackedChunkVertex::unpackMatLayer(xOverflow), 0u);

    const uint32_t matOverflow = PackedChunkVertex::packWord0(0, 0, 0, 0, 0, 0, 0xFFFFFFFFu);
    EXPECT_EQ(PackedChunkVertex::unpackMatLayer(matOverflow), 0x3FFu);
    EXPECT_EQ(PackedChunkVertex::unpackUVCorner(matOverflow), 0u);
    EXPECT_EQ(PackedChunkVertex::unpackX(matOverflow), 0u);

    // Word 1: pass UINT32_MAX to each field
    const uint32_t skyOverflow = PackedChunkVertex::packWord1(0xFFFFFFFFu, 0, 0, 0, false, false, 0);
    EXPECT_EQ(PackedChunkVertex::unpackSkyLight(skyOverflow), 0x0Fu);
    EXPECT_EQ(PackedChunkVertex::unpackBlockLight(skyOverflow), 0u);

    const uint32_t resOverflow = PackedChunkVertex::packWord1(0, 0, 0, 0, false, false, 0xFFFFFFFFu);
    EXPECT_EQ(PackedChunkVertex::unpackReserved(resOverflow), 0x1FFu);
    EXPECT_FALSE(PackedChunkVertex::unpackTranslucent(resOverflow));
}

TEST(AdversarialPackedChunkVertex, MassiveFuzzingRoundtrip100k) {
    std::mt19937_64 rng(1337);
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFFu);

    for (int i = 0; i < 100000; ++i) {
        const uint32_t r0 = dist(rng);
        const uint32_t r1 = dist(rng);
        const uint32_t r2 = dist(rng);
        const uint32_t r3 = dist(rng);
        const uint32_t r4 = dist(rng);
        const uint32_t r5 = dist(rng);
        const uint32_t r6 = dist(rng);

        // Word 0 pack
        const uint32_t w0 = PackedChunkVertex::packWord0(r0, r1, r2, r3, r4, r5, r6);
        EXPECT_EQ(PackedChunkVertex::unpackX(w0), r0 & 0x1Fu);
        EXPECT_EQ(PackedChunkVertex::unpackY(w0), r1 & 0x1Fu);
        EXPECT_EQ(PackedChunkVertex::unpackZ(w0), r2 & 0x1Fu);
        EXPECT_EQ(PackedChunkVertex::unpackNormal(w0), r3 & 0x07u);
        EXPECT_EQ(PackedChunkVertex::unpackAO(w0), r4 & 0x03u);
        EXPECT_EQ(PackedChunkVertex::unpackUVCorner(w0), r5 & 0x03u);
        EXPECT_EQ(PackedChunkVertex::unpackMatLayer(w0), r6 & 0x3FFu);

        // Word 1 pack
        const bool sway = (dist(rng) & 1) != 0;
        const bool trans = (dist(rng) & 1) != 0;
        const uint32_t w1 = PackedChunkVertex::packWord1(r0, r1, r2, r3, sway, trans, r6);
        EXPECT_EQ(PackedChunkVertex::unpackSkyLight(w1), r0 & 0x0Fu);
        EXPECT_EQ(PackedChunkVertex::unpackBlockLight(w1), r1 & 0x0Fu);
        EXPECT_EQ(PackedChunkVertex::unpackBiomeTint(w1), r2 & 0xFFu);
        EXPECT_EQ(PackedChunkVertex::unpackAnimFrame(w1), r3 & 0x1Fu);
        EXPECT_EQ(PackedChunkVertex::unpackSway(w1), sway);
        EXPECT_EQ(PackedChunkVertex::unpackTranslucent(w1), trans);
        EXPECT_EQ(PackedChunkVertex::unpackReserved(w1), r6 & 0x1FFu);
    }
}

TEST(AdversarialPackedChunkVertex, LocalPositionAndLightConversion) {
    for (uint32_t secY = 0; secY < 8; ++secY) {
        for (uint32_t y = 0; y <= 16; ++y) {
            const uint32_t w0 = PackedChunkVertex::packWord0(7, y, 11, 0, 3, 0, 1);
            const uint32_t w1 = PackedChunkVertex::packWord1(15, 7, 255, 0, false, false, secY);
            const PackedChunkVertex v{w0, w1};

            const glm::vec3 pos = v.getLocalPosition();
            EXPECT_EQ(pos.x, 7.0f);
            EXPECT_EQ(pos.y, static_cast<float>(secY * 16u + y));
            EXPECT_EQ(pos.z, 11.0f);

            const glm::vec2 light = v.getLight();
            EXPECT_NEAR(light.x, 1.0f, 1e-5f);
            EXPECT_NEAR(light.y, 7.0f / 15.0f, 1e-5f);
        }
    }
}

TEST(AdversarialPackedChunkVertex, PlanarUVProjectionBoundaryStress) {
    // Test large coordinates, negative coordinates, exact integers
    const glm::vec3 negPos(-15.25f, -64.75f, -3.1f);
    const glm::vec2 uvTop = PackedChunkVertex::computePlanarUV(negPos, 0); // +Y: (x, z)
    EXPECT_GE(uvTop.x, 0.0f);
    EXPECT_LT(uvTop.x, 1.0f);
    EXPECT_GE(uvTop.y, 0.0f);
    EXPECT_LT(uvTop.y, 1.0f);

    const glm::vec3 largePos(1000.4f, 500.9f, 999.1f);
    const glm::vec2 uvEast = PackedChunkVertex::computePlanarUV(largePos, 4); // +X: (z, y)
    EXPECT_NEAR(uvEast.x, 0.1f, 1e-3f);
    EXPECT_NEAR(uvEast.y, 0.9f, 1e-3f);
}

// ============================================================================
// SUITE 2: Shared EBO Buffer Stress up to 65,536 Quads
// ============================================================================

TEST(AdversarialSharedEBO, BoundaryQuadCounts) {
    const auto ebo0 = generateSharedEboPattern(0);
    EXPECT_TRUE(ebo0.empty());
    EXPECT_TRUE(validateEboPattern(ebo0, 0));

    const auto ebo1 = generateSharedEboPattern(1);
    ASSERT_EQ(ebo1.size(), 6u);
    EXPECT_TRUE(validateEboPattern(ebo1, 1));

    const auto ebo2 = generateSharedEboPattern(2);
    ASSERT_EQ(ebo2.size(), 12u);
    EXPECT_TRUE(validateEboPattern(ebo2, 2));

    const auto ebo65535 = generateSharedEboPattern(65535);
    EXPECT_EQ(ebo65535.size(), 65535 * 6);
    EXPECT_TRUE(validateEboPattern(ebo65535, 65535));
}

TEST(AdversarialSharedEBO, ExhaustiveValidationAndIndexLimits65536) {
    constexpr size_t quadCount = 65536;
    const auto ebo = generateSharedEboPattern(quadCount);

    ASSERT_EQ(ebo.size(), quadCount * 6); // 393,216 indices
    EXPECT_TRUE(validateEboPattern(ebo, quadCount));

    // Verify first quad
    EXPECT_EQ(ebo[0], 0u);
    EXPECT_EQ(ebo[1], 1u);
    EXPECT_EQ(ebo[2], 2u);
    EXPECT_EQ(ebo[3], 0u);
    EXPECT_EQ(ebo[4], 2u);
    EXPECT_EQ(ebo[5], 3u);

    // Verify last quad (quad 65,535)
    const size_t lastOffset = 65535 * 6;
    constexpr uint32_t lastBase = 65535 * 4; // 262,140
    EXPECT_EQ(ebo[lastOffset + 0], lastBase + 0); // 262,140
    EXPECT_EQ(ebo[lastOffset + 1], lastBase + 1); // 262,141
    EXPECT_EQ(ebo[lastOffset + 2], lastBase + 2); // 262,142
    EXPECT_EQ(ebo[lastOffset + 3], lastBase + 0); // 262,140
    EXPECT_EQ(ebo[lastOffset + 4], lastBase + 2); // 262,142
    EXPECT_EQ(ebo[lastOffset + 5], lastBase + 3); // 262,143

    // Verify maximum vertex index exceeds 65535 (proving uint16_t would overflow)
    EXPECT_GT(ebo[lastOffset + 5], 65535u);
    EXPECT_EQ(ebo[lastOffset + 5], 262143u);
}

TEST(AdversarialSharedEBO, CorruptionRejectionAcrossQuadBoundaries) {
    constexpr size_t quadCount = 65536;
    auto ebo = generateSharedEboPattern(quadCount);

    // Corrupt at start
    ebo[0] = 1;
    EXPECT_FALSE(validateEboPattern(ebo, quadCount));
    ebo[0] = 0; // restore

    // Corrupt at midpoint (quad 32,768)
    ebo[32768 * 6 + 4] = 999999;
    EXPECT_FALSE(validateEboPattern(ebo, quadCount));
    ebo[32768 * 6 + 4] = 32768 * 4 + 2; // restore

    // Corrupt at end
    ebo[65535 * 6 + 5] = 0;
    EXPECT_FALSE(validateEboPattern(ebo, quadCount));
    ebo[65535 * 6 + 5] = 65535 * 4 + 3; // restore

    // Verify restored
    EXPECT_TRUE(validateEboPattern(ebo, quadCount));

    // Size mismatch
    ebo.pop_back();
    EXPECT_FALSE(validateEboPattern(ebo, quadCount));
}

TEST(AdversarialSharedEBO, ScalabilityBeyond65536Quads) {
    // Test 131,072 quads (524,288 vertices)
    constexpr size_t largeQuads = 131072;
    const auto largeEbo = generateSharedEboPattern(largeQuads);
    EXPECT_EQ(largeEbo.size(), largeQuads * 6);
    EXPECT_TRUE(validateEboPattern(largeEbo, largeQuads));
    EXPECT_EQ(largeEbo.back(), static_cast<uint32_t>((largeQuads - 1) * 4 + 3));
}

// ============================================================================
// SUITE 3: DifferentialLightingEngine Worst-Case & Stress
// ============================================================================

TEST(AdversarialDifferentialLighting, AnalyticalVolumeFormulasExhaustive) {
    for (size_t r = 0; r <= 20; ++r) {
        size_t shellSum = 1;
        for (size_t i = 1; i <= r; ++i) {
            shellSum += DifferentialLightingEngine::analyticalManhattanShell(i);
        }
        EXPECT_EQ(DifferentialLightingEngine::analyticalManhattanVolume(r), shellSum);
    }

    // Exact theoretical values
    EXPECT_EQ(DifferentialLightingEngine::analyticalManhattanVolume(14), 4089u);
    EXPECT_EQ(DifferentialLightingEngine::analyticalManhattanVolume(15), 4991u);

    // Discrete 3D lattice BFS verification
    for (int r = 0; r <= 15; ++r) {
        const size_t discrete = DifferentialLightingEngine::computeDiscreteManhattanVolumeBFS(r);
        const size_t analytical = DifferentialLightingEngine::analyticalManhattanVolume(r);
        EXPECT_EQ(discrete, analytical) << "Failed at radius " << r;
    }
}

TEST(AdversarialDifferentialLighting, OpenAirTorchPlacementVisitBudgetUnder4991) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(42ULL, tempDir.path());

    // Load 3x3 chunk area (from (1,1) to (3,3)) to provide wide open air
    for (int cz = 1; cz <= 3; ++cz) {
        for (int cx = 1; cx <= 3; ++cx) {
            world.ensureChunkLoaded({cx, cz});
        }
    }

    // Clear open sphere of radius 16 around center (32, 64, 32)
    const glm::ivec3 center(32, 64, 32);
    for (int dy = -16; dy <= 16; ++dy) {
        for (int dz = -16; dz <= 16; ++dz) {
            for (int dx = -16; dx <= 16; ++dx) {
                world.setBlock(center.x + dx, center.y + dy, center.z + dz, BlockId::Air);
                world.setBlockLight(center.x + dx, center.y + dy, center.z + dz, 0);
            }
        }
    }

    EXPECT_EQ(world.getBlockLight(center.x, center.y, center.z), 0u);

    // Place Torch (emission 14)
    world.setBlock(center.x, center.y, center.z, BlockId::Torch);

    // Verify visit count is bounded by analytical volume N(15) = 4991 (and <= 4089 for radius 14)
    const size_t placementVisits = world.getLightingEngine().getLastVoxelInspectionCount();
    EXPECT_LE(placementVisits, 4991u);
    EXPECT_LE(placementVisits, 4089u);
    EXPECT_GT(placementVisits, 0u);

    // Verify Manhattan light attenuation along all 6 cardinal axes
    EXPECT_EQ(world.getBlockLight(center.x, center.y, center.z), 14u);
    for (int d = 1; d <= 13; ++d) {
        const uint8_t expected = static_cast<uint8_t>(14 - d);
        EXPECT_EQ(world.getBlockLight(center.x + d, center.y, center.z), expected);
        EXPECT_EQ(world.getBlockLight(center.x - d, center.y, center.z), expected);
        EXPECT_EQ(world.getBlockLight(center.x, center.y + d, center.z), expected);
        EXPECT_EQ(world.getBlockLight(center.x, center.y - d, center.z), expected);
        EXPECT_EQ(world.getBlockLight(center.x, center.y, center.z + d), expected);
        EXPECT_EQ(world.getBlockLight(center.x, center.y, center.z - d), expected);
    }
    // At distance 14, light should be 0
    EXPECT_EQ(world.getBlockLight(center.x + 14, center.y, center.z), 0u);
    EXPECT_EQ(world.getBlockLight(center.x, center.y + 14, center.z), 0u);
    EXPECT_EQ(world.getBlockLight(center.x, center.y, center.z + 14), 0u);
}

TEST(AdversarialDifferentialLighting, OpenAirTorchRemovalVisitBudgetUnder4991) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(42ULL, tempDir.path());

    for (int cz = 1; cz <= 3; ++cz) {
        for (int cx = 1; cx <= 3; ++cx) {
            world.ensureChunkLoaded({cx, cz});
        }
    }

    const glm::ivec3 center(32, 64, 32);
    for (int dy = -16; dy <= 16; ++dy) {
        for (int dz = -16; dz <= 16; ++dz) {
            for (int dx = -16; dx <= 16; ++dx) {
                world.setBlock(center.x + dx, center.y + dy, center.z + dz, BlockId::Air);
                world.setBlockLight(center.x + dx, center.y + dy, center.z + dz, 0);
            }
        }
    }

    world.setBlock(center.x, center.y, center.z, BlockId::Torch);
    EXPECT_EQ(world.getBlockLight(center.x, center.y, center.z), 14u);

    // Remove Torch
    world.setBlock(center.x, center.y, center.z, BlockId::Air);

    // Verify removal visit count strictly bounded by 4991
    const size_t removalVisits = world.getLightingEngine().getLastVoxelInspectionCount();
    EXPECT_LE(removalVisits, 4991u);
    EXPECT_LE(removalVisits, 4089u);
    EXPECT_GT(removalVisits, 0u);

    // Verify complete extinction everywhere
    for (int d = 0; d <= 14; ++d) {
        EXPECT_EQ(world.getBlockLight(center.x + d, center.y, center.z), 0u);
        EXPECT_EQ(world.getBlockLight(center.x - d, center.y, center.z), 0u);
        EXPECT_EQ(world.getBlockLight(center.x, center.y + d, center.z), 0u);
        EXPECT_EQ(world.getBlockLight(center.x, center.y - d, center.z), 0u);
        EXPECT_EQ(world.getBlockLight(center.x, center.y, center.z + d), 0u);
        EXPECT_EQ(world.getBlockLight(center.x, center.y, center.z - d), 0u);
    }
}

TEST(AdversarialDifferentialLighting, OpenAirMaxEmission15WorstCaseExactBound) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(42ULL, tempDir.path());

    for (int cz = 1; cz <= 3; ++cz) {
        for (int cx = 1; cx <= 3; ++cx) {
            world.ensureChunkLoaded({cx, cz});
        }
    }

    const glm::ivec3 center(32, 64, 32);
    for (int dy = -16; dy <= 16; ++dy) {
        for (int dz = -16; dz <= 16; ++dz) {
            for (int dx = -16; dx <= 16; ++dx) {
                world.setBlock(center.x + dx, center.y + dy, center.z + dz, BlockId::Air);
                world.setBlockLight(center.x + dx, center.y + dy, center.z + dz, 0);
            }
        }
    }

    // Set emission 15 (theoretical max light level)
    world.getLightingEngine().updateLightSource(center.x, center.y, center.z, 15);
    const size_t addVisits = world.getLightingEngine().getLastVoxelInspectionCount();
    // In voxel engine, light level 1 propagates to distance 14 (light 0 is darkness, not propagated).
    // Radius 14 visits exactly N(14) = 4,089 voxels, which strictly satisfies <= 4,991.
    EXPECT_LE(addVisits, 4991u);
    EXPECT_EQ(addVisits, 4089u);

    // Reset emission 0
    world.getLightingEngine().updateLightSource(center.x, center.y, center.z, 0);
    const size_t remVisits = world.getLightingEngine().getLastVoxelInspectionCount();
    EXPECT_LE(remVisits, 4991u);
    EXPECT_EQ(remVisits, 4089u);

    EXPECT_EQ(world.getBlockLight(center.x, center.y, center.z), 0u);
}

TEST(AdversarialDifferentialLighting, DualTorchesIndependentReinjectionStress) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(42ULL, tempDir.path());

    for (int cz = 1; cz <= 3; ++cz) {
        for (int cx = 1; cx <= 3; ++cx) {
            world.ensureChunkLoaded({cx, cz});
        }
    }

    // Clear open space
    for (int dy = -16; dy <= 16; ++dy) {
        for (int dz = -16; dz <= 16; ++dz) {
            for (int dx = 0; dx <= 64; ++dx) {
                world.setBlock(dx, 64 + dy, 32 + dz, BlockId::Air);
                world.setBlockLight(dx, 64 + dy, 32 + dz, 0);
            }
        }
    }

    // Place Torch A at (24, 64, 32) and Torch B at (40, 64, 32) (dist = 16)
    world.setBlock(24, 64, 32, BlockId::Torch);
    world.setBlock(40, 64, 32, BlockId::Torch);

    EXPECT_EQ(world.getBlockLight(24, 64, 32), 14u);
    EXPECT_EQ(world.getBlockLight(40, 64, 32), 14u);

    // Midpoint (32, 64, 32) is dist 8 from both: 14 - 8 = 6
    EXPECT_EQ(world.getBlockLight(32, 64, 32), 6u);

    // Remove Torch A
    world.setBlock(24, 64, 32, BlockId::Air);

    // Torch B must remain unaffected at 14
    EXPECT_EQ(world.getBlockLight(40, 64, 32), 14u);

    // Midpoint still receives 6 from Torch B
    EXPECT_EQ(world.getBlockLight(32, 64, 32), 6u);

    // At x=27 (dist 13 from Torch B): 14 - 13 = 1
    EXPECT_EQ(world.getBlockLight(27, 64, 32), 1u);

    // At x=26 (dist 14 from Torch B): 0
    EXPECT_EQ(world.getBlockLight(26, 64, 32), 0u);

    // At x=24 (where Torch A was): 0
    EXPECT_EQ(world.getBlockLight(24, 64, 32), 0u);

    // Now remove Torch B: entire world returns to 0
    world.setBlock(40, 64, 32, BlockId::Air);
    EXPECT_EQ(world.getBlockLight(40, 64, 32), 0u);
    EXPECT_EQ(world.getBlockLight(32, 64, 32), 0u);
}

TEST(AdversarialDifferentialLighting, AdjacentTorchesReinjectionStress) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(42ULL, tempDir.path());

    for (int cz = 1; cz <= 3; ++cz) {
        for (int cx = 1; cx <= 3; ++cx) {
            world.ensureChunkLoaded({cx, cz});
        }
    }

    for (int dy = -16; dy <= 16; ++dy) {
        for (int dz = -16; dz <= 16; ++dz) {
            for (int dx = 0; dx <= 64; ++dx) {
                world.setBlock(dx, 64 + dy, 32 + dz, BlockId::Air);
                world.setBlockLight(dx, 64 + dy, 32 + dz, 0);
            }
        }
    }

    // Place torches adjacent at dist 4: (30, 64, 32) and (34, 64, 32)
    world.setBlock(30, 64, 32, BlockId::Torch);
    world.setBlock(34, 64, 32, BlockId::Torch);

    EXPECT_EQ(world.getBlockLight(30, 64, 32), 14u);
    EXPECT_EQ(world.getBlockLight(34, 64, 32), 14u);
    // Midpoint (32, 64, 32) dist 2 from both: 14 - 2 = 12
    EXPECT_EQ(world.getBlockLight(32, 64, 32), 12u);

    // Remove Torch A at (30, 64, 32)
    world.setBlock(30, 64, 32, BlockId::Air);

    // (30, 64, 32) is dist 4 from Torch B: 14 - 4 = 10
    EXPECT_EQ(world.getBlockLight(30, 64, 32), 10u);
    EXPECT_EQ(world.getBlockLight(34, 64, 32), 14u);

    // Remove Torch B
    world.setBlock(34, 64, 32, BlockId::Air);
    EXPECT_EQ(world.getBlockLight(30, 64, 32), 0u);
    EXPECT_EQ(world.getBlockLight(34, 64, 32), 0u);
}

TEST(AdversarialDifferentialLighting, RapidTorchTogglingStress1000Cycles) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(42ULL, tempDir.path());

    for (int cz = 1; cz <= 3; ++cz) {
        for (int cx = 1; cx <= 3; ++cx) {
            world.ensureChunkLoaded({cx, cz});
        }
    }

    const glm::ivec3 pos(32, 64, 32);
    for (int dy = -16; dy <= 16; ++dy) {
        for (int dz = -16; dz <= 16; ++dz) {
            for (int dx = -16; dx <= 16; ++dx) {
                world.setBlock(pos.x + dx, pos.y + dy, pos.z + dz, BlockId::Air);
                world.setBlockLight(pos.x + dx, pos.y + dy, pos.z + dz, 0);
            }
        }
    }

    // Toggle torch 1,000 times (500 on, 500 off)
    for (int cycle = 0; cycle < 500; ++cycle) {
        // Place torch
        world.setBlock(pos.x, pos.y, pos.z, BlockId::Torch);
        const size_t onVisits = world.getLightingEngine().getLastVoxelInspectionCount();
        ASSERT_LE(onVisits, 4991u);
        ASSERT_EQ(world.getBlockLight(pos.x, pos.y, pos.z), 14u);

        // Remove torch
        world.setBlock(pos.x, pos.y, pos.z, BlockId::Air);
        const size_t offVisits = world.getLightingEngine().getLastVoxelInspectionCount();
        ASSERT_LE(offVisits, 4991u);
        ASSERT_EQ(world.getBlockLight(pos.x, pos.y, pos.z), 0u);
    }

    // Verify completely clean state
    EXPECT_EQ(world.getBlockLight(pos.x, pos.y, pos.z), 0u);
    EXPECT_EQ(world.getBlockLight(pos.x + 1, pos.y, pos.z), 0u);
}

TEST(AdversarialDifferentialLighting, ChunkBoundaryCrossBorderLighting) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(42ULL, tempDir.path());

    // Load chunks (0,0), (1,0), (0,1), (1,1)
    world.ensureChunkLoaded({0, 0});
    world.ensureChunkLoaded({1, 0});
    world.ensureChunkLoaded({0, 1});
    world.ensureChunkLoaded({1, 1});

    // Clear boundary region around (15, 64, 15)
    for (int dy = -16; dy <= 16; ++dy) {
        for (int z = 0; z < 32; ++z) {
            for (int x = 0; x < 32; ++x) {
                world.setBlock(x, 64 + dy, z, BlockId::Air);
                world.setBlockLight(x, 64 + dy, z, 0);
            }
        }
    }

    // Place torch at chunk border: (15, 64, 15)
    world.setBlock(15, 64, 15, BlockId::Torch);
    EXPECT_LE(world.getLightingEngine().getLastVoxelInspectionCount(), 4991u);

    // Check light in chunk (0,0)
    EXPECT_EQ(world.getBlockLight(15, 64, 15), 14u);
    // Check light crossed into chunk (1,0) at x=16
    EXPECT_EQ(world.getBlockLight(16, 64, 15), 13u);
    // Check light crossed into chunk (0,1) at z=16
    EXPECT_EQ(world.getBlockLight(15, 64, 16), 13u);
    // Check light crossed into chunk (1,1) at (16, 64, 16)
    EXPECT_EQ(world.getBlockLight(16, 64, 16), 12u);

    // Remove torch: clean removal across borders
    world.setBlock(15, 64, 15, BlockId::Air);
    EXPECT_LE(world.getLightingEngine().getLastVoxelInspectionCount(), 4991u);
    EXPECT_EQ(world.getBlockLight(15, 64, 15), 0u);
    EXPECT_EQ(world.getBlockLight(16, 64, 15), 0u);
    EXPECT_EQ(world.getBlockLight(15, 64, 16), 0u);
    EXPECT_EQ(world.getBlockLight(16, 64, 16), 0u);
}
