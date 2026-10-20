#include <iostream>
#include <vector>
#include <thread>
#include <chrono>
#include <cassert>
#include <cmath>

#include "render/ChunkMesher.hpp"
#include "streaming/PackedChunkVertex.hpp"
#include "tests/test_helpers.hpp"
#include "voxel/Block.hpp"
#include "world/DifferentialLightingEngine.hpp"
#include "world/World.hpp"

using namespace betacraft;
using namespace betacraft::render;
using namespace betacraft::streaming;
using namespace betacraft::world;
using namespace betacraft::voxel;

static int g_testsPassed = 0;
static int g_testsFailed = 0;

#define STRESS_ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            std::cerr << "FAIL: " << msg << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
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

// 1. Bitfield roundtrip & masking stress
bool testPackedChunkVertexBitfields() {
    STRESS_ASSERT(sizeof(PackedChunkVertex) == 8, "sizeof must be 8");
    STRESS_ASSERT(alignof(PackedChunkVertex) == 8, "alignof must be 8");
    STRESS_ASSERT(std::is_trivially_copyable_v<PackedChunkVertex>, "must be trivially copyable");

    // Test extreme and individual bit toggling
    for (uint32_t x = 0; x <= 31; x += 7) {
        for (uint32_t y = 0; y <= 31; y += 7) {
            for (uint32_t z = 0; z <= 31; z += 7) {
                for (uint32_t n = 0; n <= 7; ++n) {
                    for (uint32_t ao = 0; ao <= 3; ++ao) {
                        for (uint32_t uv = 0; uv <= 3; ++uv) {
                            for (uint32_t mat = 0; mat <= 1023; mat += 255) {
                                const uint32_t w0 = PackedChunkVertex::packWord0(x, y, z, n, ao, uv, mat);
                                STRESS_ASSERT(PackedChunkVertex::unpackX(w0) == (x & 0x1Fu), "X mismatch");
                                STRESS_ASSERT(PackedChunkVertex::unpackY(w0) == (y & 0x1Fu), "Y mismatch");
                                STRESS_ASSERT(PackedChunkVertex::unpackZ(w0) == (z & 0x1Fu), "Z mismatch");
                                STRESS_ASSERT(PackedChunkVertex::unpackNormal(w0) == (n & 0x07u), "Normal mismatch");
                                STRESS_ASSERT(PackedChunkVertex::unpackAO(w0) == (ao & 0x03u), "AO mismatch");
                                STRESS_ASSERT(PackedChunkVertex::unpackUVCorner(w0) == (uv & 0x03u), "UV mismatch");
                                STRESS_ASSERT(PackedChunkVertex::unpackMatLayer(w0) == (mat & 0x3FFu), "Mat mismatch");
                            }
                        }
                    }
                }
            }
        }
    }

    // Word 1 stress
    for (uint32_t sky = 0; sky <= 15; ++sky) {
        for (uint32_t blk = 0; blk <= 15; ++blk) {
            for (uint32_t tint = 0; tint <= 255; tint += 85) {
                for (uint32_t anim = 0; anim <= 31; anim += 15) {
                    for (int sway = 0; sway <= 1; ++sway) {
                        for (int trans = 0; trans <= 1; ++trans) {
                            for (uint32_t res = 0; res <= 511; res += 127) {
                                const uint32_t w1 = PackedChunkVertex::packWord1(
                                    sky, blk, tint, anim, sway != 0, trans != 0, res
                                );
                                STRESS_ASSERT(PackedChunkVertex::unpackSkyLight(w1) == sky, "Sky mismatch");
                                STRESS_ASSERT(PackedChunkVertex::unpackBlockLight(w1) == blk, "Blk mismatch");
                                STRESS_ASSERT(PackedChunkVertex::unpackBiomeTint(w1) == tint, "Tint mismatch");
                                STRESS_ASSERT(PackedChunkVertex::unpackAnimFrame(w1) == anim, "Anim mismatch");
                                STRESS_ASSERT(PackedChunkVertex::unpackSway(w1) == (sway != 0), "Sway mismatch");
                                STRESS_ASSERT(PackedChunkVertex::unpackTranslucent(w1) == (trans != 0), "Trans mismatch");
                                STRESS_ASSERT(PackedChunkVertex::unpackReserved(w1) == res, "Res mismatch");
                            }
                        }
                    }
                }
            }
        }
    }

    return true;
}

// 2. Planar UV projection across negative and fractional world coordinates
bool testPlanarUVProjection() {
    // Normal 0 (+Y)
    glm::vec2 uvTop = PackedChunkVertex::computePlanarUV({-10.25f, 64.0f, -5.75f}, 0);
    STRESS_ASSERT(std::abs(uvTop.x - 0.75f) < 1e-4f, "Top X fract mismatch");
    STRESS_ASSERT(std::abs(uvTop.y - 0.25f) < 1e-4f, "Top Y fract mismatch");

    // Normal 4 (+X)
    glm::vec2 uvEast = PackedChunkVertex::computePlanarUV({100.0f, 70.33f, 50.80f}, 4);
    STRESS_ASSERT(std::abs(uvEast.x - 0.80f) < 1e-4f, "East X fract mismatch");
    STRESS_ASSERT(std::abs(uvEast.y - 0.33f) < 1e-4f, "East Y fract mismatch");

    // Normal 2 (+Z)
    glm::vec2 uvSouth = PackedChunkVertex::computePlanarUV({-0.1f, -0.4f, 10.0f}, 2);
    STRESS_ASSERT(std::abs(uvSouth.x - 0.9f) < 1e-4f, "South X fract mismatch");
    STRESS_ASSERT(std::abs(uvSouth.y - 0.6f) < 1e-4f, "South Y fract mismatch");

    return true;
}

// 3. Shared EBO pattern validation
bool testSharedEBOPattern() {
    const auto ebo = generateSharedEboPattern(65536);
    STRESS_ASSERT(ebo.size() == 65536 * 6, "EBO size mismatch");
    STRESS_ASSERT(validateEboPattern(ebo, 65536), "EBO validation failed");
    return true;
}

// 4. Cross-chunk boundary lighting propagation in all cardinal directions
bool testCrossChunkLightingPropagation() {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(173ULL, tempDir.path());

    // Load 4 chunks: (0, 0), (1, 0), (0, 1), (1, 1)
    world.ensureChunkLoaded({0, 0});
    world.ensureChunkLoaded({1, 0});
    world.ensureChunkLoaded({0, 1});
    world.ensureChunkLoaded({1, 1});

    // Clear dark room across 4 chunk junction (x=10..22, z=10..22, y=60..70)
    for (int y = 60; y <= 70; ++y) {
        for (int z = 10; z <= 22; ++z) {
            for (int x = 10; x <= 22; ++x) {
                const bool isWall = (x == 10 || x == 22 || z == 10 || z == 22 || y == 60 || y == 70);
                world.setBlock(x, y, z, isWall ? BlockId::Cobblestone : BlockId::Air);
            }
        }
    }

    // Place torch in chunk (0, 0) at (15, 65, 15)
    world.setBlock(15, 65, 15, BlockId::Torch);
    STRESS_ASSERT(world.getBlockLight(15, 65, 15) == 14, "Torch center must be 14");

    // +X border into chunk (1, 0) at (16, 65, 15)
    STRESS_ASSERT(world.getBlockLight(16, 65, 15) == 13, "Cross +X must be 13");
    STRESS_ASSERT(world.getBlockLight(17, 65, 15) == 12, "Cross +X+1 must be 12");

    // +Z border into chunk (0, 1) at (15, 65, 16)
    STRESS_ASSERT(world.getBlockLight(15, 65, 16) == 13, "Cross +Z must be 13");
    STRESS_ASSERT(world.getBlockLight(15, 65, 17) == 12, "Cross +Z+1 must be 12");

    // Diagonal corner into chunk (1, 1) at (16, 65, 16) (Manhattan dist = 2 -> 14 - 2 = 12)
    STRESS_ASSERT(world.getBlockLight(16, 65, 16) == 12, "Cross diagonal corner must be 12");

    // Remove torch
    world.setBlock(15, 65, 15, BlockId::Air);
    STRESS_ASSERT(world.getBlockLight(15, 65, 15) == 0, "Torch center removed must be 0");
    STRESS_ASSERT(world.getBlockLight(16, 65, 15) == 0, "Cross +X must be 0");
    STRESS_ASSERT(world.getBlockLight(15, 65, 16) == 0, "Cross +Z must be 0");
    STRESS_ASSERT(world.getBlockLight(16, 65, 16) == 0, "Cross corner must be 0");

    return true;
}

// 5. World boundaries (Y=0, Y=127) and negative coordinates
bool testWorldBoundaryLighting() {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(173ULL, tempDir.path());
    world.ensureChunkLoaded({-1, -1});

    // Negative chunk coord test: (-5, 64, -5)
    world.setBlock(-5, 64, -5, BlockId::Torch);
    STRESS_ASSERT(world.getBlockLight(-5, 64, -5) == 14, "Negative coord torch must be 14");
    STRESS_ASSERT(world.getBlockLight(-4, 64, -5) == 13, "Negative coord neighbor must be 13");
    world.setBlock(-5, 64, -5, BlockId::Air);
    STRESS_ASSERT(world.getBlockLight(-5, 64, -5) == 0, "Negative coord removed must be 0");

    // Vertical boundary test: Y=0
    world.setBlock(5, 0, 5, BlockId::Torch);
    STRESS_ASSERT(world.getBlockLight(5, 0, 5) == 14, "Y=0 torch must be 14");
    STRESS_ASSERT(world.getBlockLight(5, 1, 5) == 13, "Y=1 neighbor must be 13");
    world.setBlock(5, 0, 5, BlockId::Air);
    STRESS_ASSERT(world.getBlockLight(5, 0, 5) == 0, "Y=0 removed must be 0");

    // Vertical boundary test: Y=127
    world.setBlock(5, 127, 5, BlockId::Torch);
    STRESS_ASSERT(world.getBlockLight(5, 127, 5) == 14, "Y=127 torch must be 14");
    STRESS_ASSERT(world.getBlockLight(5, 126, 5) == 13, "Y=126 neighbor must be 13");
    world.setBlock(5, 127, 5, BlockId::Air);
    STRESS_ASSERT(world.getBlockLight(5, 127, 5) == 0, "Y=127 removed must be 0");

    return true;
}

// 6. Check internal voxel skylight of placed opaque block
bool testOpaqueBlockVoxelLightInspection() {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(173ULL, tempDir.path());
    world.ensureChunkLoaded({0, 0});

    // Clear column at (5, 5)
    for (int y = 0; y < 128; ++y) {
        world.setBlock(5, y, 5, BlockId::Air);
    }
    STRESS_ASSERT(world.getSkyLight(5, 100, 5) == 15, "Air column must have 15 skylight");

    // Place stone at (5, 100, 5)
    world.setBlock(5, 100, 5, BlockId::Stone);

    uint8_t stoneSky = world.getSkyLight(5, 100, 5);
    std::cout << "\n    [OBSERVATION] Placed stone internal skyLight = " << (int)stoneSky << std::endl;

    // Now place stone above at (5, 101, 5)
    world.setBlock(5, 101, 5, BlockId::Stone);

    // Destroy stone at (5, 100, 5)
    world.setBlock(5, 100, 5, BlockId::Air);

    uint8_t airBelowSky = world.getSkyLight(5, 100, 5);
    std::cout << "    [OBSERVATION] Air under stone at Y=101 skyLight = " << (int)airBelowSky << std::endl;

    return true;
}

// 7. Concurrent meshing thread safety
bool testConcurrentMeshing() {
    BlockRegistry::initialize();
    test::TempDir tempDir;
    World world(173ULL, tempDir.path());

    // Populate several chunks
    for (int cz = -1; cz <= 1; ++cz) {
        for (int cx = -1; cx <= 1; ++cx) {
            world.ensureChunkLoaded({cx, cz});
        }
    }

    std::vector<std::thread> workers;
    for (int t = 0; t < 4; ++t) {
        workers.emplace_back([&world, t]() {
            const Chunk* chunk = world.getChunk({(t % 2), (t / 2)});
            if (chunk) {
                for (int i = 0; i < 5; ++i) {
                    const auto mesh = ChunkMesher::buildMesh(world, *chunk, (i % 2 == 0));
                    STRESS_ASSERT(!mesh.opaqueVertices.empty(), "Mesh should not be empty");
                }
            }
            return true;
        });
    }

    for (auto& w : workers) {
        w.join();
    }

    return true;
}

int main() {
    std::cout << "=== RUNNING MILESTONE 2 ADVERSARIAL STRESS SUITE ===" << std::endl;

    RUN_TEST(testPackedChunkVertexBitfields);
    RUN_TEST(testPlanarUVProjection);
    RUN_TEST(testSharedEBOPattern);
    RUN_TEST(testCrossChunkLightingPropagation);
    RUN_TEST(testWorldBoundaryLighting);
    RUN_TEST(testOpaqueBlockVoxelLightInspection);
    RUN_TEST(testConcurrentMeshing);

    std::cout << "\n=== RESULTS: " << g_testsPassed << " passed, " << g_testsFailed << " failed ===" << std::endl;
    return (g_testsFailed == 0) ? 0 : 1;
}
