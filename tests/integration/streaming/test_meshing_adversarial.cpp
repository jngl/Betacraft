// Adversarial checks on chunk meshing data: PackedChunkVertex bit fields under walking ones,
// alternating patterns, overflowing fields and 100k random round trips, planar UVs at negative
// and large coordinates, the shared EBO up to and past 65,536 quads, and ChunkMesher on four
// threads at once.
#include "render/ChunkMesher.hpp"
#include "streaming/PackedChunkVertex.hpp"
#include "test_support/TempDir.hpp"
#include "voxel/Block.hpp"
#include "world/World.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <random>
#include <thread>
#include <vector>

using namespace betacraft;
using namespace betacraft::streaming;
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

TEST(AdversarialPackedChunkVertex, PlanarUVIsInUnitRangeForEveryNormal) {
    // Planar UV projection must always lie in [0.0, 1.0) regardless of negative coords
    const glm::vec3 testCoords[] = {
        {-15.75f, 64.25f, -3.5f},
        {-0.001f, -0.999f, -100.33f},
        {0.0f, 0.0f, 0.0f},
        {16.0f, 128.0f, 16.0f},
        {1234.56f, 789.12f, -567.89f}
    };

    for (const auto& pos : testCoords) {
        for (uint32_t norm = 0; norm < 6; ++norm) {
            const glm::vec2 uv = PackedChunkVertex::computePlanarUV(pos, norm);
            EXPECT_GE(uv.x, 0.0f);
            EXPECT_LT(uv.x, 1.0f);
            EXPECT_GE(uv.y, 0.0f);
            EXPECT_LT(uv.y, 1.0f);
        }
    }
}

TEST(AdversarialPackedChunkVertex, PlanarUVIsTheFractionalPartOfNegativeCoordinates) {
    // Normal 0 (+Y): (x, z)
    const glm::vec2 uvTop = PackedChunkVertex::computePlanarUV({-10.25f, 64.0f, -5.75f}, 0);
    EXPECT_NEAR(uvTop.x, 0.75f, 1e-4f);
    EXPECT_NEAR(uvTop.y, 0.25f, 1e-4f);

    // Normal 4 (+X): (z, y)
    const glm::vec2 uvEast = PackedChunkVertex::computePlanarUV({100.0f, 70.33f, 50.80f}, 4);
    EXPECT_NEAR(uvEast.x, 0.80f, 1e-4f);
    EXPECT_NEAR(uvEast.y, 0.33f, 1e-4f);

    // Normal 2 (+Z): (x, y)
    const glm::vec2 uvSouth = PackedChunkVertex::computePlanarUV({-0.1f, -0.4f, 10.0f}, 2);
    EXPECT_NEAR(uvSouth.x, 0.9f, 1e-4f);
    EXPECT_NEAR(uvSouth.y, 0.6f, 1e-4f);
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
// SUITE 3: ChunkMesher on several threads
// ============================================================================

TEST(AdversarialChunkMesher, ConcurrentMeshingOfLoadedChunks) {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(173ULL, tempDir.path());
    for (int cz = -1; cz <= 1; ++cz) {
        for (int cx = -1; cx <= 1; ++cx) {
            world.ensureChunkLoaded({cx, cz});
        }
    }

    // Four threads mesh four chunks of a world nobody writes to; every mesh must come out whole.
    std::atomic<int> emptyMeshes{0};
    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&world, &emptyMeshes, t]() {
            const Chunk* chunk = world.getChunk({t % 2, t / 2});
            ASSERT_NE(chunk, nullptr);
            for (int i = 0; i < 5; ++i) {
                const auto mesh = render::ChunkMesher::buildMesh(world, *chunk, i % 2 == 0);
                if (mesh.opaqueVertices.empty()) {
                    ++emptyMeshes;
                }
            }
        });
    }
    for (auto& w : workers) {
        w.join();
    }
    EXPECT_EQ(emptyMeshes.load(), 0);
}
