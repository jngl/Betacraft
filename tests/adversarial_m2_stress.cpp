#include <iostream>
#include <vector>
#include <random>
#include <cassert>
#include <cmath>
#include <memory>
#include <chrono>
#include <span>

#include "render/PackedChunkVertex.hpp"
#include "world/DifferentialLightingEngine.hpp"
#include "world/World.hpp"
#include "world/Block.hpp"
#include "tests/test_helpers.hpp"

using namespace betacraft;
using namespace betacraft::render;
using namespace betacraft::world;

static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define STRESS_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "\nFAIL: " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
            g_testsFailed++; \
            return false; \
        } \
    } while(0)

#define RUN_TEST(fn) \
    do { \
        std::cout << "[RUNNING] " << #fn << "..." << std::flush; \
        if (fn()) { \
            std::cout << " PASS" << std::endl; \
            g_testsPassed++; \
        } else { \
            std::cout << " FAILED" << std::endl; \
        } \
    } while(0)

// ============================================================================
// SUITE 1: PackedChunkVertex Boundary Bit Patterns & Fuzzing
// ============================================================================

bool testPackedVertexSizeAndAlignment() {
    STRESS_ASSERT(sizeof(PackedChunkVertex) == 8, "sizeof(PackedChunkVertex) must be 8 bytes");
    STRESS_ASSERT(alignof(PackedChunkVertex) == 8, "alignof(PackedChunkVertex) must be 8 bytes");
    STRESS_ASSERT(std::is_trivially_copyable_v<PackedChunkVertex>, "must be trivially copyable");
    return true;
}

bool testPackedVertexBoundaryBitPatternsWord0() {
    // All zeros
    const uint32_t w0Zero = PackedChunkVertex::packWord0(0, 0, 0, 0, 0, 0, 0);
    STRESS_ASSERT(w0Zero == 0u, "Word 0 all zeros must be 0");
    STRESS_ASSERT(PackedChunkVertex::unpackX(w0Zero) == 0u, "unpackX must be 0");
    STRESS_ASSERT(PackedChunkVertex::unpackY(w0Zero) == 0u, "unpackY must be 0");
    STRESS_ASSERT(PackedChunkVertex::unpackZ(w0Zero) == 0u, "unpackZ must be 0");
    STRESS_ASSERT(PackedChunkVertex::unpackNormal(w0Zero) == 0u, "unpackNormal must be 0");
    STRESS_ASSERT(PackedChunkVertex::unpackAO(w0Zero) == 0u, "unpackAO must be 0");
    STRESS_ASSERT(PackedChunkVertex::unpackUVCorner(w0Zero) == 0u, "unpackUVCorner must be 0");
    STRESS_ASSERT(PackedChunkVertex::unpackMatLayer(w0Zero) == 0u, "unpackMatLayer must be 0");

    // All ones (5+5+5+3+2+2+10 = 32 bits)
    const uint32_t w0Ones = PackedChunkVertex::packWord0(31, 31, 31, 7, 3, 3, 1023);
    STRESS_ASSERT(w0Ones == 0xFFFFFFFFu, "Word 0 all ones must be 0xFFFFFFFF");
    STRESS_ASSERT(PackedChunkVertex::unpackX(w0Ones) == 31u, "unpackX must be 31");
    STRESS_ASSERT(PackedChunkVertex::unpackY(w0Ones) == 31u, "unpackY must be 31");
    STRESS_ASSERT(PackedChunkVertex::unpackZ(w0Ones) == 31u, "unpackZ must be 31");
    STRESS_ASSERT(PackedChunkVertex::unpackNormal(w0Ones) == 7u, "unpackNormal must be 7");
    STRESS_ASSERT(PackedChunkVertex::unpackAO(w0Ones) == 3u, "unpackAO must be 3");
    STRESS_ASSERT(PackedChunkVertex::unpackUVCorner(w0Ones) == 3u, "unpackUVCorner must be 3");
    STRESS_ASSERT(PackedChunkVertex::unpackMatLayer(w0Ones) == 1023u, "unpackMatLayer must be 1023");

    // Bit isolation walking ones
    for (uint32_t b = 0; b < 32; ++b) {
        const uint32_t w0 = (1u << b);
        const uint32_t x    = PackedChunkVertex::unpackX(w0);
        const uint32_t y    = PackedChunkVertex::unpackY(w0);
        const uint32_t z    = PackedChunkVertex::unpackZ(w0);
        const uint32_t norm = PackedChunkVertex::unpackNormal(w0);
        const uint32_t ao   = PackedChunkVertex::unpackAO(w0);
        const uint32_t uv   = PackedChunkVertex::unpackUVCorner(w0);
        const uint32_t mat  = PackedChunkVertex::unpackMatLayer(w0);

        if (b < 5) {
            STRESS_ASSERT(x == (1u << b) && (y + z + norm + ao + uv + mat == 0), "Bit isolation X failed");
        } else if (b < 10) {
            STRESS_ASSERT(y == (1u << (b - 5)) && (x + z + norm + ao + uv + mat == 0), "Bit isolation Y failed");
        } else if (b < 15) {
            STRESS_ASSERT(z == (1u << (b - 10)) && (x + y + norm + ao + uv + mat == 0), "Bit isolation Z failed");
        } else if (b < 18) {
            STRESS_ASSERT(norm == (1u << (b - 15)) && (x + y + z + ao + uv + mat == 0), "Bit isolation Normal failed");
        } else if (b < 20) {
            STRESS_ASSERT(ao == (1u << (b - 18)) && (x + y + z + norm + uv + mat == 0), "Bit isolation AO failed");
        } else if (b < 22) {
            STRESS_ASSERT(uv == (1u << (b - 20)) && (x + y + z + norm + ao + mat == 0), "Bit isolation UV failed");
        } else {
            STRESS_ASSERT(mat == (1u << (b - 22)) && (x + y + z + norm + ao + uv == 0), "Bit isolation Mat failed");
        }
    }

    return true;
}

bool testPackedVertexBoundaryBitPatternsWord1() {
    // All zeros
    const uint32_t w1Zero = PackedChunkVertex::packWord1(0, 0, 0, 0, false, false, 0);
    STRESS_ASSERT(w1Zero == 0u, "Word 1 all zeros must be 0");
    STRESS_ASSERT(PackedChunkVertex::unpackSkyLight(w1Zero) == 0u, "sky must be 0");
    STRESS_ASSERT(PackedChunkVertex::unpackBlockLight(w1Zero) == 0u, "block must be 0");
    STRESS_ASSERT(PackedChunkVertex::unpackBiomeTint(w1Zero) == 0u, "tint must be 0");
    STRESS_ASSERT(PackedChunkVertex::unpackAnimFrame(w1Zero) == 0u, "anim must be 0");
    STRESS_ASSERT(!PackedChunkVertex::unpackSway(w1Zero), "sway must be false");
    STRESS_ASSERT(!PackedChunkVertex::unpackTranslucent(w1Zero), "translucent must be false");
    STRESS_ASSERT(PackedChunkVertex::unpackReserved(w1Zero) == 0u, "reserved must be 0");

    // All ones (4+4+8+5+1+1+9 = 32 bits)
    const uint32_t w1Ones = PackedChunkVertex::packWord1(15, 15, 255, 31, true, true, 511);
    STRESS_ASSERT(w1Ones == 0xFFFFFFFFu, "Word 1 all ones must be 0xFFFFFFFF");
    STRESS_ASSERT(PackedChunkVertex::unpackSkyLight(w1Ones) == 15u, "sky must be 15");
    STRESS_ASSERT(PackedChunkVertex::unpackBlockLight(w1Ones) == 15u, "block must be 15");
    STRESS_ASSERT(PackedChunkVertex::unpackBiomeTint(w1Ones) == 255u, "tint must be 255");
    STRESS_ASSERT(PackedChunkVertex::unpackAnimFrame(w1Ones) == 31u, "anim must be 31");
    STRESS_ASSERT(PackedChunkVertex::unpackSway(w1Ones), "sway must be true");
    STRESS_ASSERT(PackedChunkVertex::unpackTranslucent(w1Ones), "translucent must be true");
    STRESS_ASSERT(PackedChunkVertex::unpackReserved(w1Ones) == 511u, "reserved must be 511");

    // Bit isolation walking ones
    for (uint32_t b = 0; b < 32; ++b) {
        const uint32_t w1 = (1u << b);
        const uint32_t sky   = PackedChunkVertex::unpackSkyLight(w1);
        const uint32_t blk   = PackedChunkVertex::unpackBlockLight(w1);
        const uint32_t tint  = PackedChunkVertex::unpackBiomeTint(w1);
        const uint32_t anim  = PackedChunkVertex::unpackAnimFrame(w1);
        const bool     sway  = PackedChunkVertex::unpackSway(w1);
        const bool     trans = PackedChunkVertex::unpackTranslucent(w1);
        const uint32_t res   = PackedChunkVertex::unpackReserved(w1);

        if (b < 4) {
            STRESS_ASSERT(sky == (1u << b) && (blk + tint + anim + (sway ? 1 : 0) + (trans ? 1 : 0) + res == 0), "Bit isolation sky failed");
        } else if (b < 8) {
            STRESS_ASSERT(blk == (1u << (b - 4)) && (sky + tint + anim + (sway ? 1 : 0) + (trans ? 1 : 0) + res == 0), "Bit isolation block failed");
        } else if (b < 16) {
            STRESS_ASSERT(tint == (1u << (b - 8)) && (sky + blk + anim + (sway ? 1 : 0) + (trans ? 1 : 0) + res == 0), "Bit isolation tint failed");
        } else if (b < 21) {
            STRESS_ASSERT(anim == (1u << (b - 16)) && (sky + blk + tint + (sway ? 1 : 0) + (trans ? 1 : 0) + res == 0), "Bit isolation anim failed");
        } else if (b == 21) {
            STRESS_ASSERT(sway && (sky + blk + tint + anim + (trans ? 1 : 0) + res == 0), "Bit isolation sway failed");
        } else if (b == 22) {
            STRESS_ASSERT(trans && (sky + blk + tint + anim + (sway ? 1 : 0) + res == 0), "Bit isolation trans failed");
        } else {
            STRESS_ASSERT(res == (1u << (b - 23)) && (sky + blk + tint + anim + (sway ? 1 : 0) + (trans ? 1 : 0) == 0), "Bit isolation res failed");
        }
    }

    return true;
}

bool testPackedVertexFuzzing100k() {
    std::mt19937_64 rng(42);
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFFFFFFu);

    for (int i = 0; i < 100000; ++i) {
        const uint32_t r0 = dist(rng);
        const uint32_t r1 = dist(rng);
        const uint32_t r2 = dist(rng);
        const uint32_t r3 = dist(rng);
        const uint32_t r4 = dist(rng);
        const uint32_t r5 = dist(rng);
        const uint32_t r6 = dist(rng);

        const uint32_t w0 = PackedChunkVertex::packWord0(r0, r1, r2, r3, r4, r5, r6);
        STRESS_ASSERT(PackedChunkVertex::unpackX(w0) == (r0 & 0x1Fu), "fuzz unpackX mismatch");
        STRESS_ASSERT(PackedChunkVertex::unpackY(w0) == (r1 & 0x1Fu), "fuzz unpackY mismatch");
        STRESS_ASSERT(PackedChunkVertex::unpackZ(w0) == (r2 & 0x1Fu), "fuzz unpackZ mismatch");
        STRESS_ASSERT(PackedChunkVertex::unpackNormal(w0) == (r3 & 0x07u), "fuzz unpackNorm mismatch");
        STRESS_ASSERT(PackedChunkVertex::unpackAO(w0) == (r4 & 0x03u), "fuzz unpackAO mismatch");
        STRESS_ASSERT(PackedChunkVertex::unpackUVCorner(w0) == (r5 & 0x03u), "fuzz unpackUV mismatch");
        STRESS_ASSERT(PackedChunkVertex::unpackMatLayer(w0) == (r6 & 0x3FFu), "fuzz unpackMat mismatch");

        const bool sway = (dist(rng) & 1) != 0;
        const bool trans = (dist(rng) & 1) != 0;
        const uint32_t w1 = PackedChunkVertex::packWord1(r0, r1, r2, r3, sway, trans, r6);
        STRESS_ASSERT(PackedChunkVertex::unpackSkyLight(w1) == (r0 & 0x0Fu), "fuzz sky mismatch");
        STRESS_ASSERT(PackedChunkVertex::unpackBlockLight(w1) == (r1 & 0x0Fu), "fuzz block mismatch");
        STRESS_ASSERT(PackedChunkVertex::unpackBiomeTint(w1) == (r2 & 0xFFu), "fuzz tint mismatch");
        STRESS_ASSERT(PackedChunkVertex::unpackAnimFrame(w1) == (r3 & 0x1Fu), "fuzz anim mismatch");
        STRESS_ASSERT(PackedChunkVertex::unpackSway(w1) == sway, "fuzz sway mismatch");
        STRESS_ASSERT(PackedChunkVertex::unpackTranslucent(w1) == trans, "fuzz trans mismatch");
        STRESS_ASSERT(PackedChunkVertex::unpackReserved(w1) == (r6 & 0x1FFu), "fuzz res mismatch");
    }

    return true;
}

// ============================================================================
// SUITE 2: Shared EBO Buffer Stress up to 65,536 Quads
// ============================================================================

bool testSharedEboBufferLimits65536() {
    constexpr size_t quadCount = 65536;
    const auto ebo = generateSharedEboPattern(quadCount);

    STRESS_ASSERT(ebo.size() == quadCount * 6, "EBO size must be 393,216");
    STRESS_ASSERT(validateEboPattern(ebo, quadCount), "EBO validation must pass for 65,536 quads");

    // Check first and last quad indices
    STRESS_ASSERT(ebo[0] == 0 && ebo[1] == 1 && ebo[2] == 2 && ebo[3] == 0 && ebo[4] == 2 && ebo[5] == 3, "First quad corrupted");
    const size_t lastOffset = 65535 * 6;
    constexpr uint32_t lastBase = 65535 * 4;
    STRESS_ASSERT(ebo[lastOffset + 5] == lastBase + 3, "Last vertex index corrupted");
    STRESS_ASSERT(ebo[lastOffset + 5] == 262143u, "Last index must be exactly 262,143");
    STRESS_ASSERT(ebo[lastOffset + 5] > 65535u, "Index must exceed 16-bit range proving 32-bit uint requirement");

    return true;
}

bool testSharedEboCorruptionDetection() {
    constexpr size_t quadCount = 65536;
    auto ebo = generateSharedEboPattern(quadCount);

    ebo[32768 * 6 + 1] = 999999;
    STRESS_ASSERT(!validateEboPattern(ebo, quadCount), "Validation must reject corrupted index at midpoint");

    ebo[32768 * 6 + 1] = 32768 * 4 + 1; // restore
    STRESS_ASSERT(validateEboPattern(ebo, quadCount), "Validation must pass after restoration");

    ebo.pop_back();
    STRESS_ASSERT(!validateEboPattern(ebo, quadCount), "Validation must reject truncated buffer");

    return true;
}

// ============================================================================
// SUITE 3: DifferentialLightingEngine Worst-Case & Stress
// ============================================================================

bool testAnalyticalManhattanVolumeFormulas() {
    STRESS_ASSERT(DifferentialLightingEngine::analyticalManhattanVolume(14) == 4089u, "N(14) must be 4,089");
    STRESS_ASSERT(DifferentialLightingEngine::analyticalManhattanVolume(15) == 4991u, "N(15) must be 4,991");

    for (int r = 0; r <= 15; ++r) {
        const size_t discrete = DifferentialLightingEngine::computeDiscreteManhattanVolumeBFS(r);
        const size_t analytical = DifferentialLightingEngine::analyticalManhattanVolume(r);
        STRESS_ASSERT(discrete == analytical, "Discrete BFS must match analytical N(R)");
    }
    return true;
}

bool testWorstCaseOpenAirTorchPlacementAndRemoval() {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(173ULL, tempDir.path());

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

    // 1. Placement (emission 14)
    world.setBlock(center.x, center.y, center.z, BlockId::Torch);
    const size_t placeVisits = world.getLightingEngine().getLastVoxelInspectionCount();
    STRESS_ASSERT(placeVisits <= 4991u, "Torch placement visits must not exceed 4,991");
    STRESS_ASSERT(placeVisits <= 4089u, "Torch placement visits in open air must not exceed N(14)=4089");
    STRESS_ASSERT(world.getBlockLight(center.x, center.y, center.z) == 14u, "Torch center must be 14");
    STRESS_ASSERT(world.getBlockLight(center.x + 13, center.y, center.z) == 1u, "Light at dist 13 must be 1");
    STRESS_ASSERT(world.getBlockLight(center.x + 14, center.y, center.z) == 0u, "Light at dist 14 must be 0");

    // 2. Removal
    world.setBlock(center.x, center.y, center.z, BlockId::Air);
    const size_t remVisits = world.getLightingEngine().getLastVoxelInspectionCount();
    STRESS_ASSERT(remVisits <= 4991u, "Torch removal visits must not exceed 4,991");
    STRESS_ASSERT(remVisits <= 4089u, "Torch removal visits must not exceed N(14)=4089");
    STRESS_ASSERT(world.getBlockLight(center.x, center.y, center.z) == 0u, "Torch center must return to 0");
    STRESS_ASSERT(world.getBlockLight(center.x + 1, center.y, center.z) == 0u, "Neighbor must return to 0");

    return true;
}

bool testDualTorchesIndependentReinjection() {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(173ULL, tempDir.path());

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

    // Place Torch A at (24, 64, 32) and Torch B at (40, 64, 32) (dist = 16)
    world.setBlock(24, 64, 32, BlockId::Torch);
    world.setBlock(40, 64, 32, BlockId::Torch);
    STRESS_ASSERT(world.getBlockLight(24, 64, 32) == 14u, "Torch A must be 14");
    STRESS_ASSERT(world.getBlockLight(40, 64, 32) == 14u, "Torch B must be 14");
    STRESS_ASSERT(world.getBlockLight(32, 64, 32) == 6u, "Midpoint must be 6");

    // Remove Torch A
    world.setBlock(24, 64, 32, BlockId::Air);
    STRESS_ASSERT(world.getBlockLight(40, 64, 32) == 14u, "Torch B must be unaffected at 14");
    STRESS_ASSERT(world.getBlockLight(32, 64, 32) == 6u, "Midpoint must still have 6 from Torch B");
    STRESS_ASSERT(world.getBlockLight(24, 64, 32) == 0u, "Torch A position must extinguish to 0");

    // Remove Torch B
    world.setBlock(40, 64, 32, BlockId::Air);
    STRESS_ASSERT(world.getBlockLight(40, 64, 32) == 0u, "Torch B position must extinguish to 0");
    STRESS_ASSERT(world.getBlockLight(32, 64, 32) == 0u, "Midpoint must extinguish to 0");

    return true;
}

bool testRapidTorchTogglingStress() {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(173ULL, tempDir.path());

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

    for (int cycle = 0; cycle < 500; ++cycle) {
        world.setBlock(pos.x, pos.y, pos.z, BlockId::Torch);
        STRESS_ASSERT(world.getLightingEngine().getLastVoxelInspectionCount() <= 4991u, "Toggle placement <= 4991");
        world.setBlock(pos.x, pos.y, pos.z, BlockId::Air);
        STRESS_ASSERT(world.getLightingEngine().getLastVoxelInspectionCount() <= 4991u, "Toggle removal <= 4991");
    }

    STRESS_ASSERT(world.getBlockLight(pos.x, pos.y, pos.z) == 0u, "Clean 0 after 500 cycles");
    return true;
}

int main() {
    std::cout << "============================================================\n";
    std::cout << " ADVERSARIAL STRESS TEST SUITE: MILESTONE 2 (SPRINT 1.2)    \n";
    std::cout << "============================================================\n\n";

    std::cout << "--- SUITE 1: PackedChunkVertex Boundary Bit Patterns & Fuzzing ---\n";
    RUN_TEST(testPackedVertexSizeAndAlignment);
    RUN_TEST(testPackedVertexBoundaryBitPatternsWord0);
    RUN_TEST(testPackedVertexBoundaryBitPatternsWord1);
    RUN_TEST(testPackedVertexFuzzing100k);

    std::cout << "\n--- SUITE 2: Shared EBO Buffer Stress up to 65,536 Quads ---\n";
    RUN_TEST(testSharedEboBufferLimits65536);
    RUN_TEST(testSharedEboCorruptionDetection);

    std::cout << "\n--- SUITE 3: DifferentialLightingEngine Worst-Case & Stress ---\n";
    RUN_TEST(testAnalyticalManhattanVolumeFormulas);
    RUN_TEST(testWorstCaseOpenAirTorchPlacementAndRemoval);
    RUN_TEST(testDualTorchesIndependentReinjection);
    RUN_TEST(testRapidTorchTogglingStress);

    std::cout << "\n============================================================\n";
    std::cout << "SUMMARY: Passed: " << g_testsPassed << ", Failed: " << g_testsFailed << "\n";
    std::cout << "============================================================\n";

    return (g_testsFailed == 0) ? 0 : 1;
}
